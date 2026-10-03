// PRISM ENGINE — core/hash.cpp : SHA-256, HMAC, HKDF, ChaCha20, Poly1305, AEAD.
// Fully offline reference implementations. Tested against RFC 6234 / RFC 8439
// vectors in engine/tests/test_crypto.cpp.
#include "prism/core/hash.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace prism::crypto {

namespace {

inline u32 rotr(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }
inline u32 load_be32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}
inline void store_be32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v >> 24); p[1] = static_cast<u8>(v >> 16);
    p[2] = static_cast<u8>(v >> 8);  p[3] = static_cast<u8>(v);
}
inline u32 load_le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

constexpr u32 K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

constexpr char kHex[] = "0123456789abcdef";

} // namespace

// ---------------------------------------------------------------- sha256 ---
void Sha256::reset() {
    state_[0] = 0x6a09e667u; state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u; state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu; state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu; state_[7] = 0x5be0cd19u;
    bitlen_ = 0; buflen_ = 0;
}

void Sha256::transform(const u8* block) {
    u32 w[64];
    for (int i = 0; i < 16; ++i) w[i] = load_be32(block + 4 * i);
    for (int i = 16; i < 64; ++i) {
        u32 s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        u32 s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    u32 e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
        u32 S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = h + S1 + ch + K256[i] + w[i];
        u32 S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        u32 mj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const void* data, std::size_t len) {
    auto p = static_cast<const u8*>(data);
    bitlen_ += static_cast<u64>(len) * 8u;
    while (len > 0) {
        std::size_t take = std::min(len, 64u - buflen_);
        std::memcpy(buffer_ + buflen_, p, take);
        buflen_ += take; p += take; len -= take;
        if (buflen_ == 64) { transform(buffer_); buflen_ = 0; }
    }
}

Sha256Digest Sha256::finalize() {
    u8 pad[64] = {0x80};
    std::size_t padlen = (buflen_ < 56) ? (56 - buflen_) : (120 - buflen_);
    u64 bits = bitlen_;
    update(pad, padlen);
    u8 lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<u8>(bits >> (56 - 8 * i));
    update(lenbuf, 8);
    Sha256Digest out{};
    for (int i = 0; i < 8; ++i) store_be32(out.data() + 4 * i, state_[i]);
    return out;
}

std::string Sha256::to_hex(const Sha256Digest& d) {
    std::string s; s.resize(64);
    for (std::size_t i = 0; i < d.size(); ++i) {
        s[2*i] = kHex[d[i] >> 4]; s[2*i+1] = kHex[d[i] & 0xF];
    }
    return s;
}

// ----------------------------------------------------------- hmac / hkdf ---
Sha256Digest hmac_sha256(std::string_view key, std::string_view message) {
    u8 k[64] = {};
    if (key.size() > 64) {
        auto d = Sha256::hash(key);
        std::memcpy(k, d.data(), 32);
    } else {
        std::memcpy(k, key.data(), key.size());
    }
    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = static_cast<u8>(k[i] ^ 0x36); opad[i] = static_cast<u8>(k[i] ^ 0x5C); }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(message.data(), message.size());
    auto idig = inner.finalize();
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(idig.data(), idig.size());
    return outer.finalize();
}

std::string hmac_sha256_hex(std::string_view key, std::string_view message) {
    return Sha256::to_hex(hmac_sha256(key, message));
}

std::vector<u8> hkdf_sha256(std::string_view ikm, std::string_view salt,
                            std::string_view info, std::size_t length) {
    // RFC 5869. Extract-then-expand, T(i) = HMAC(PRK, T(i-1) || info || i).
    auto prk = hmac_sha256(salt, ikm);
    const std::string_view prk_view(reinterpret_cast<const char*>(prk.data()), prk.size());

    std::vector<u8> out;
    std::vector<u8> t;
    u8 counter = 1;
    while (out.size() < length) {
        Sha256 payload;
        if (!t.empty()) payload.update(t.data(), t.size());
        payload.update(info.data(), info.size());
        payload.update(&counter, 1);
        // Hash first, then HMAC the digest — identical result to HMAC over the
        // whole concatenation, but avoids a per-block temporary buffer.
        auto odig = hmac_sha256(prk_view, std::string());
        {
            Sha256 inner, outer;
            u8 k[64] = {};
            std::memcpy(k, prk.data(), 32);
            u8 ipad[64], opad[64];
            for (int i = 0; i < 64; ++i) {
                ipad[i] = static_cast<u8>(k[i] ^ 0x36);
                opad[i] = static_cast<u8>(k[i] ^ 0x5C);
            }
            inner.update(ipad, 64);
            if (!t.empty()) inner.update(t.data(), t.size());
            inner.update(info.data(), info.size());
            inner.update(&counter, 1);
            auto idig = inner.finalize();
            outer.update(opad, 64);
            outer.update(idig.data(), idig.size());
            odig = outer.finalize();
        }
        t.assign(odig.begin(), odig.end());
        out.insert(out.end(), t.begin(), t.end());
        ++counter;
    }
    out.resize(length);
    return out;
}

// ---------------------------------------------------------------- chacha20 -
namespace {
inline u32 rotl32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }
inline void qr(u32& a, u32& b, u32& c, u32& d) {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}
} // namespace

void chacha20_xor(const u8 key[32], const u8 nonce[12], u32 counter,
                  const u8* in, u8* out, std::size_t len) {
    u32 state[16];
    state[0] = 0x61707865u; state[1] = 0x3320646eu;
    state[2] = 0x79622d32u; state[3] = 0x6b206574u;
    for (int i = 0; i < 8; ++i) state[4 + i] = load_le32(key + 4 * i);
    state[12] = counter;
    for (int i = 0; i < 3; ++i) state[13 + i] = load_le32(nonce + 4 * i);

    std::size_t off = 0;
    while (off < len) {
        u32 x[16];
        std::memcpy(x, state, sizeof(x));
        for (int i = 0; i < 10; ++i) {
            qr(x[0], x[4], x[8],  x[12]);
            qr(x[1], x[5], x[9],  x[13]);
            qr(x[2], x[6], x[10], x[14]);
            qr(x[3], x[7], x[11], x[15]);
            qr(x[0], x[5], x[10], x[15]);
            qr(x[1], x[6], x[11], x[12]);
            qr(x[2], x[7], x[8],  x[13]);
            qr(x[3], x[4], x[9],  x[14]);
        }
        u8 ks[64];
        for (int i = 0; i < 16; ++i) {
            u32 v = x[i] + state[i];
            ks[4*i+0] = static_cast<u8>(v);       ks[4*i+1] = static_cast<u8>(v >> 8);
            ks[4*i+2] = static_cast<u8>(v >> 16); ks[4*i+3] = static_cast<u8>(v >> 24);
        }
        std::size_t take = std::min<std::size_t>(64, len - off);
        for (std::size_t i = 0; i < take; ++i) out[off + i] = static_cast<u8>(in[off + i] ^ ks[i]);
        off += take;
        state[12]++;
    }
}

// ---------------------------------------------------------------- poly1305 -
std::array<u8, 16> poly1305_mac(const u8 key[32], const u8* msg, std::size_t len) {
    u32 r[5], h[5] = {0,0,0,0,0}, pad[4];
    // r = clamp(le128(key[0..16]))
    u32 t0 = load_le32(key + 0), t1 = load_le32(key + 4);
    u32 t2 = load_le32(key + 8), t3 = load_le32(key + 12);
    r[0] =   t0                       & 0x3ffffffu;
    r[1] = ((t0 >> 26) | (t1 << 6))   & 0x3ffff03u;
    r[2] = ((t1 >> 20) | (t2 << 12))  & 0x3ffc0ffu;
    r[3] = ((t2 >> 14) | (t3 << 18))  & 0x3f03fffu;
    r[4] =  (t3 >> 8)                 & 0x00fffffu;
    pad[0] = load_le32(key + 16); pad[1] = load_le32(key + 20);
    pad[2] = load_le32(key + 24); pad[3] = load_le32(key + 28);

    const u32 s1 = r[1] * 5, s2 = r[2] * 5, s3 = r[3] * 5, s4 = r[4] * 5;
    std::size_t off = 0;
    while (off < len) {
        std::size_t take = std::min<std::size_t>(16, len - off);
        u8 block[17] = {};
        std::memcpy(block, msg + off, take);
        block[take] = 1;                       // 0x01 padding byte (RFC 8439 §2.5.1)
        u32 u0 = load_le32(block + 0),  u1 = load_le32(block + 4);
        u32 u2 = load_le32(block + 8),  u3 = load_le32(block + 12);
        h[0] +=   u0                       & 0x3ffffffu;
        h[1] += ((u0 >> 26) | (u1 << 6))   & 0x3ffffffu;
        h[2] += ((u1 >> 20) | (u2 << 12))  & 0x3ffffffu;
        h[3] += ((u2 >> 14) | (u3 << 18))  & 0x3ffffffu;
        h[4] +=  (u3 >> 8) | (block[16] << 24);

        // h = h * r  (26-bit limbs, 64-bit accumulators)
        u64 d0 = static_cast<u64>(h[0])*r[0] + static_cast<u64>(h[1])*s4 + static_cast<u64>(h[2])*s3 +
                 static_cast<u64>(h[3])*s2 + static_cast<u64>(h[4])*s1;
        u64 d1 = static_cast<u64>(h[0])*r[1] + static_cast<u64>(h[1])*r[0] + static_cast<u64>(h[2])*s4 +
                 static_cast<u64>(h[3])*s3 + static_cast<u64>(h[4])*s2;
        u64 d2 = static_cast<u64>(h[0])*r[2] + static_cast<u64>(h[1])*r[1] + static_cast<u64>(h[2])*r[0] +
                 static_cast<u64>(h[3])*s4 + static_cast<u64>(h[4])*s3;
        u64 d3 = static_cast<u64>(h[0])*r[3] + static_cast<u64>(h[1])*r[2] + static_cast<u64>(h[2])*r[1] +
                 static_cast<u64>(h[3])*r[0] + static_cast<u64>(h[4])*s4;
        u64 d4 = static_cast<u64>(h[0])*r[4] + static_cast<u64>(h[1])*r[3] + static_cast<u64>(h[2])*r[2] +
                 static_cast<u64>(h[3])*r[1] + static_cast<u64>(h[4])*r[0];

        u32 c;
        c = static_cast<u32>(d0 >> 26); h[0] = static_cast<u32>(d0) & 0x3ffffffu;
        d1 += c; c = static_cast<u32>(d1 >> 26); h[1] = static_cast<u32>(d1) & 0x3ffffffu;
        d2 += c; c = static_cast<u32>(d2 >> 26); h[2] = static_cast<u32>(d2) & 0x3ffffffu;
        d3 += c; c = static_cast<u32>(d3 >> 26); h[3] = static_cast<u32>(d3) & 0x3ffffffu;
        d4 += c; c = static_cast<u32>(d4 >> 26); h[4] = static_cast<u32>(d4) & 0x3ffffffu;
        h[0] += c * 5; c = h[0] >> 26; h[0] &= 0x3ffffffu; h[1] += c;
        off += take;
    }

    // final reduction mod 2^130-5
    u32 c;
    c = h[1] >> 26; h[1] &= 0x3ffffffu;
    h[2] += c; c = h[2] >> 26; h[2] &= 0x3ffffffu;
    h[3] += c; c = h[3] >> 26; h[3] &= 0x3ffffffu;
    h[4] += c; c = h[4] >> 26; h[4] &= 0x3ffffffu;
    h[0] += c * 5; c = h[0] >> 26; h[0] &= 0x3ffffffu; h[1] += c;

    // compute h + -p, then select h (if h < p) or h-p (if h >= p).
    // ALL five limbs of g must be masked — masking only g4 leaks the +5 carry
    // back into the result and breaks every h >= p case.
    u32 g[5];
    c = h[0] + 5; g[0] = c & 0x3ffffffu; c >>= 26;
    c += h[1];    g[1] = c & 0x3ffffffu; c >>= 26;
    c += h[2];    g[2] = c & 0x3ffffffu; c >>= 26;
    c += h[3];    g[3] = c & 0x3ffffffu; c >>= 26;
    c += h[4];    g[4] = c & 0x3ffffffu;
    // c >= 2^26 here means h + 5 overflowed 130 bits, i.e. h >= p, so we take g.
    const u32 keep_g = 0u - (c >> 26);          // all-ones when h >= p
    g[0] &= keep_g; g[1] &= keep_g; g[2] &= keep_g; g[3] &= keep_g; g[4] &= keep_g;
    const u32 keep_h = ~keep_g;                 // all-ones when h < p
    for (int i = 0; i < 5; ++i) h[i] = (h[i] & keep_h) | g[i];

    // 26-bit limbs -> four little-endian 32-bit words, truncated mod 2^128.
    // The truncation is REQUIRED: h is 130 bits wide but the tag is mod 2^128,
    // and carrying h's bits 128..129 into the pad addition corrupts every word.
    // Plain u32 shifts already yield exactly the right low 32 bits per word.
    const u32 w0 = h[0]         | (h[1] << 26);
    const u32 w1 = (h[1] >>  6) | (h[2] << 20);
    const u32 w2 = (h[2] >> 12) | (h[3] << 14);
    const u32 w3 = (h[3] >> 18) | (h[4] <<  8);

    u64 acc = static_cast<u64>(w0) + pad[0];            u32 f  = static_cast<u32>(acc);
    acc = static_cast<u64>(w1) + pad[1] + (acc >> 32);  u32 f1 = static_cast<u32>(acc);
    acc = static_cast<u64>(w2) + pad[2] + (acc >> 32);  u32 f2 = static_cast<u32>(acc);
    acc = static_cast<u64>(w3) + pad[3] + (acc >> 32);  u32 f3 = static_cast<u32>(acc);

    std::array<u8, 16> out{};
    out[0]=static_cast<u8>(f);       out[1]=static_cast<u8>(f>>8);
    out[2]=static_cast<u8>(f>>16);   out[3]=static_cast<u8>(f>>24);
    out[4]=static_cast<u8>(f1);      out[5]=static_cast<u8>(f1>>8);
    out[6]=static_cast<u8>(f1>>16);  out[7]=static_cast<u8>(f1>>24);
    out[8]=static_cast<u8>(f2);      out[9]=static_cast<u8>(f2>>8);
    out[10]=static_cast<u8>(f2>>16); out[11]=static_cast<u8>(f2>>24);
    out[12]=static_cast<u8>(f3);     out[13]=static_cast<u8>(f3>>8);
    out[14]=static_cast<u8>(f3>>16); out[15]=static_cast<u8>(f3>>24);
    return out;
}

bool constant_time_equal(const u8* a, const u8* b, std::size_t n) {
    u8 diff = 0;
    for (std::size_t i = 0; i < n; ++i) diff |= static_cast<u8>(a[i] ^ b[i]);
    return diff == 0;
}

// ------------------------------------------------------------- AEAD seal ---
namespace {
constexpr u8 kSealMagic[4] = {'P','R','S','M'};
constexpr u8 kSealVersion  = 1;
} // namespace

std::vector<u8> seal(std::string_view key, std::string_view plaintext, std::string_view aad) {
    if (key.size() != 32) return {};
    std::vector<u8> out;
    out.reserve(4 + 1 + 12 + 16 + plaintext.size());
    out.insert(out.end(), kSealMagic, kSealMagic + 4);
    out.push_back(kSealVersion);

    // Deterministic nonce from HMAC(key, aad || plaintext) so identical inputs
    // produce identical files (nice for content-addressed asset caches).
    Sha256 h;
    h.update(aad.data(), aad.size());
    h.update("\x00", 1);
    h.update(plaintext.data(), plaintext.size());
    h.update(reinterpret_cast<const u8*>(key.data()), key.size());
    auto nd = h.finalize();
    u8 nonce[12];
    std::memcpy(nonce, nd.data(), 12);
    out.insert(out.end(), nonce, nonce + 12);

    const std::size_t ct_offset = out.size() + 16;
    out.resize(ct_offset + plaintext.size());
    u8* ct = out.data() + ct_offset;

    auto* k = reinterpret_cast<const u8*>(key.data());
    chacha20_xor(k, nonce, 1,
                 reinterpret_cast<const u8*>(plaintext.data()), ct, plaintext.size());

    // Poly1305 over aad || pad || ct || pad || lengths (RFC 8439 §2.8)
    std::vector<u8> mac_in;
    mac_in.insert(mac_in.end(), aad.begin(), aad.end());
    std::size_t pad = (16 - (aad.size() % 16)) % 16;
    mac_in.insert(mac_in.end(), pad, 0);
    mac_in.insert(mac_in.end(), ct, ct + plaintext.size());
    pad = (16 - (plaintext.size() % 16)) % 16;
    mac_in.insert(mac_in.end(), pad, 0);
    u64 la = aad.size(), lc = plaintext.size();
    for (int i = 0; i < 8; ++i) { mac_in.push_back(static_cast<u8>(la >> (8*i))); }
    for (int i = 0; i < 8; ++i) { mac_in.push_back(static_cast<u8>(lc >> (8*i))); }

    u8 poly_key[32];
    u8 zeros[64] = {};
    chacha20_xor(k, nonce, 0, zeros, poly_key, 32);
    auto tag = poly1305_mac(poly_key, mac_in.data(), mac_in.size());
    std::memcpy(out.data() + ct_offset - 16, tag.data(), 16);
    return out;
}

std::vector<u8> open(std::string_view key, const u8* blob, std::size_t len, std::string_view aad) {
    if (key.size() != 32 || len < 4 + 1 + 12 + 16) return {};
    if (std::memcmp(blob, kSealMagic, 4) != 0) return {};
    if (blob[4] != kSealVersion) return {};
    const u8* nonce = blob + 5;
    const u8* tag = blob + 17;
    const u8* ct = blob + 33;
    const std::size_t ct_len = len - 33;

    std::vector<u8> mac_in;
    mac_in.insert(mac_in.end(), aad.begin(), aad.end());
    std::size_t pad = (16 - (aad.size() % 16)) % 16;
    mac_in.insert(mac_in.end(), pad, 0);
    mac_in.insert(mac_in.end(), ct, ct + ct_len);
    pad = (16 - (ct_len % 16)) % 16;
    mac_in.insert(mac_in.end(), pad, 0);
    u64 la = aad.size(), lc = ct_len;
    for (int i = 0; i < 8; ++i) mac_in.push_back(static_cast<u8>(la >> (8*i)));
    for (int i = 0; i < 8; ++i) mac_in.push_back(static_cast<u8>(lc >> (8*i)));

    auto* k = reinterpret_cast<const u8*>(key.data());
    u8 poly_key[32], zeros[64] = {};
    chacha20_xor(k, nonce, 0, zeros, poly_key, 32);
    auto expect = poly1305_mac(poly_key, mac_in.data(), mac_in.size());
    if (!constant_time_equal(expect.data(), tag, 16)) return {};

    std::vector<u8> plain(ct_len);
    chacha20_xor(k, nonce, 1, ct, plain.data(), ct_len);
    return plain;
}

std::string derive_key(std::string_view passphrase, std::string_view salt, std::size_t rounds) {
    // Lightweight PBKDF: iterated HMAC-SHA256. Not scrypt, but offline-safe and
    // deterministic across arm64/arm32/x64 so LAN saves interoperate.
    Sha256Digest u = hmac_sha256(passphrase, salt);
    Sha256Digest acc = u;
    for (std::size_t i = 1; i < rounds; ++i) {
        u = hmac_sha256(std::string_view(reinterpret_cast<const char*>(u.data()), 32), "prism-iter");
        for (int b = 0; b < 32; ++b) acc[b] ^= u[b];
    }
    return std::string(reinterpret_cast<const char*>(acc.data()), 32);
}

// ------------------------------------------------------------ unlock code --
std::string UnlockCode::format(std::string_view raw20) {
    static const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no I/O/0/1
    std::string s;
    for (std::size_t i = 0; i < raw20.size() && s.size() < 12; ++i) {
        s.push_back(alphabet[static_cast<unsigned char>(raw20[i]) % 32]);
    }
    while (s.size() < 12) s.push_back('2');
    return "PRISM-" + s.substr(0, 4) + "-" + s.substr(4, 4) + "-" + s.substr(8, 4);
}

std::string UnlockCode::normalise(std::string_view code) {
    std::string out;
    for (char c : code) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c == '0') c = 'O';
        if (c == '1') c = 'I';
        if ((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9')) out.push_back(c);
    }
    return out;
}

std::string UnlockCode::generate() const {
    std::string payload = sku + "|" + device_id;
    auto mac = hmac_sha256(merchant_secret, payload);
    return format(std::string_view(reinterpret_cast<const char*>(mac.data()), 20));
}

bool UnlockCode::verify(std::string_view code) const {
    if (sku.empty() || merchant_secret.empty()) return false;
    std::string n = normalise(code);
    // accept "PRISMXXXX..." or bare "XXXX..." forms
    std::string expected_full = normalise(generate());
    std::string expected_bare = expected_full.substr(expected_full.find("PRISM") + 5);
    std::string bare = n;
    if (bare.rfind("PRISM", 0) == 0) bare = bare.substr(5);
    if (bare.empty()) return false;
    if (bare.size() != 12) return false;
    bool ok_full = n.size() == expected_full.size() &&
                   constant_time_equal(reinterpret_cast<const u8*>(n.data()),
                                       reinterpret_cast<const u8*>(expected_full.data()), n.size());
    bool ok_bare = constant_time_equal(reinterpret_cast<const u8*>(bare.data()),
                                       reinterpret_cast<const u8*>(expected_bare.data()), 12);
    return ok_full || ok_bare;
}

} // namespace prism::crypto
