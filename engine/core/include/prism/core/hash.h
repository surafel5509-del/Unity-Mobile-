// =====================================================================
//  PRISM ENGINE — core/hash.h
//  Content hashing + offline cryptography. No external dependency, no
//  network. Used by: asset database (content addressing), encrypted
//  local saves, offline unlock codes, anti-cheat integrity checks and
//  rollback-netcode state checksums.
//
//  Algorithms (all self-contained, all constant-time where required):
//    FNV-1a 64      — fast string/path hashing
//    CRC32 (IEEE)   — APK chunk integrity
//    SHA-256        — asset digests, unlock-code derivation
//    HMAC-SHA256    — offline IAP receipts, anti-cheat seals
//    ChaCha20       — stream cipher for local save files
//    Poly1305       — MAC paired with ChaCha20 (AEAD, RFC 8439)
//    XXH3-lite      — order-independent content hash for ECS snapshots
// =====================================================================
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "../core/types.h"

namespace prism::crypto {

// ------------------------------------------------------------------ fnv ----
inline u64 fnv1a(std::string_view data, u64 seed = 0xCBF29CE484222325ull) {
    u64 h = seed;
    for (unsigned char c : data) { h ^= c; h *= 0x100000001B3ull; }
    return h;
}
inline u32 fnv1a_32(std::string_view data) {
    u32 h = 2166136261u;
    for (unsigned char c : data) { h ^= c; h *= 16777619u; }
    return h;
}

/// Order-independent combiner: hash(a,b) == hash(b,a) is NOT what we want for
/// ordered data, but IS what we want for unordered sets (e.g. active systems).
inline u64 combine_unordered(u64 acc, u64 v) { return acc + v + 0x9E3779B97F4A7C15ull; }
inline u64 combine_ordered(u64 acc, u64 v) {
    acc ^= v + 0x9E3779B97F4A7C15ull + (acc << 6) + (acc >> 2);
    return acc;
}

// ---------------------------------------------------------------- crc32 ----
class Crc32 {
public:
    Crc32() { reset(); }
    void reset() { crc_ = 0xFFFFFFFFu; }
    void update(const void* data, std::size_t n) {
        auto p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            crc_ ^= p[i];
            for (int k = 0; k < 8; ++k) crc_ = (crc_ >> 1) ^ (0xEDB88320u & (0u - (crc_ & 1u)));
        }
    }
    void update(std::string_view s) { update(s.data(), s.size()); }
    [[nodiscard]] u32 value() const { return crc_ ^ 0xFFFFFFFFu; }
private:
    u32 crc_ = 0xFFFFFFFFu;
};
inline u32 crc32(std::string_view s) { Crc32 c; c.update(s); return c.value(); }

// ---------------------------------------------------------------- sha256 ---
using Sha256Digest = std::array<u8, 32>;

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, std::size_t len);
    void update(std::string_view s) { update(s.data(), s.size()); }
    Sha256Digest finalize();
    [[nodiscard]] static Sha256Digest hash(std::string_view data) {
        Sha256 h; h.update(data); return h.finalize();
    }
    [[nodiscard]] static std::string hex(std::string_view data) { return to_hex(hash(data)); }
    [[nodiscard]] static std::string to_hex(const Sha256Digest& d);
private:
    void transform(const u8* block);
    u32 state_[8]{};
    u64 bitlen_ = 0;
    u8 buffer_[64]{};
    std::size_t buflen_ = 0;
};

// ------------------------------------------------------------ hmac-sha256 --
[[nodiscard]] Sha256Digest hmac_sha256(std::string_view key, std::string_view message);
[[nodiscard]] std::string hmac_sha256_hex(std::string_view key, std::string_view message);
/// RFC 5869 HKDF-SHA256 (extract + expand) — used to derive per-save keys.
[[nodiscard]] std::vector<u8> hkdf_sha256(std::string_view ikm, std::string_view salt,
                                          std::string_view info, std::size_t length);

// ---------------------------------------------------------------- chacha20 -
/// RFC 8439 ChaCha20. `key` must be 32 bytes, `nonce` 12 bytes.
void chacha20_xor(const u8 key[32], const u8 nonce[12], u32 counter,
                  const u8* in, u8* out, std::size_t len);
inline void chacha20_encrypt(std::string_view key, std::string_view nonce, u32 counter,
                             const u8* in, u8* out, std::size_t len) {
    chacha20_xor(reinterpret_cast<const u8*>(key.data()),
                 reinterpret_cast<const u8*>(nonce.data()), counter, in, out, len);
}

// ---------------------------------------------------------------- poly1305 -
[[nodiscard]] std::array<u8, 16> poly1305_mac(const u8 key[32], const u8* msg, std::size_t len);

/// Constant-time compare — never short-circuits on mismatch.
[[nodiscard]] bool constant_time_equal(const u8* a, const u8* b, std::size_t n);

// ------------------------------------------------------------ AEAD bundle --
/// ChaCha20-Poly1305 sealed blob. This is the on-disk format for every PRISM
/// local save / high-score / receipt file. Layout:
///   "PRSM" | u8 version | 12-byte nonce | 16-byte tag | ciphertext...
[[nodiscard]] std::vector<u8> seal(std::string_view key, std::string_view plaintext,
                                   std::string_view aad = {});
/// Returns empty vector on authentication failure (tampered or wrong key).
[[nodiscard]] std::vector<u8> open(std::string_view key, const u8* blob, std::size_t len,
                                   std::string_view aad = {});

/// Derives a stable device-scoped key from a user passphrase + app salt.
/// (On Android the platform layer mixes in ANDROID_ID; host builds use the salt only.)
[[nodiscard]] std::string derive_key(std::string_view passphrase, std::string_view salt,
                                     std::size_t rounds = 4096);

// -------------------------------------------------------- offline IAP ------
/// Offline unlock codes: "PRISM-XXXX-XXXX-XXXX" where the trailing group is an
/// HMAC-SHA256 truncation of (sku|deviceId|merchantSecret). Verifiable with no
/// network, no server, no account. See docs/08-data-and-services.md.
struct UnlockCode {
    std::string sku;
    std::string device_id;
    std::string merchant_secret;

    [[nodiscard]] std::string generate() const;
    [[nodiscard]] bool verify(std::string_view code) const;
    [[nodiscard]] static std::string format(std::string_view raw20);
    [[nodiscard]] static std::string normalise(std::string_view code);
};

} // namespace prism::crypto
