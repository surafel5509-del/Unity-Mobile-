// PRISM ENGINE — offline crypto tests. RFC 6234 / RFC 4231 / RFC 8439 vectors,
// so the implementations are provably correct rather than "probably fine".
#include "prism_test.h"
#include "prism/core/hash.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

using namespace prism;
using namespace prism::crypto;

namespace {
std::string hex_of(const u8* p, std::size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s; s.resize(n * 2);
    for (std::size_t i = 0; i < n; ++i) { s[2*i] = h[p[i] >> 4]; s[2*i+1] = h[p[i] & 0xF]; }
    return s;
}
} // namespace

PRISM_TEST(crypto_sha256_rfc6234_vectors) {
    // RFC 6234 §8.5
    PRISM_CHECK_STR(Sha256::hex("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    PRISM_CHECK_STR(Sha256::hex(""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    PRISM_CHECK_STR(Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // multi-block (spans > 64 bytes, exercises the padding/length path)
    std::string million(1000, 'a');
    Sha256 h; h.update(million); h.update(million);
    PRISM_CHECK_STR(Sha256::to_hex(h.finalize()), Sha256::hex(million + million));
}

PRISM_TEST(crypto_hmac_sha256_rfc4231) {
    // RFC 4231 test case 1: key = 0x0b x20, data = "Hi There"
    std::string key(20, '\x0b');
    PRISM_CHECK_STR(hmac_sha256_hex(key, "Hi There"),
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    // test case 2: key = "Jefe", data = "what do ya want for nothing?"
    PRISM_CHECK_STR(hmac_sha256_hex("Jefe", "what do ya want for nothing?"),
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

PRISM_TEST(crypto_chacha20_rfc8439) {
    u8 key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<u8>(i);
    u8 nonce[12] = {0,0,0,0, 0,0,0,0x4a, 0,0,0,0};
    const std::string plain =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one "
        "tip for the future, sunscreen would be.";
    std::vector<u8> ct(plain.size());
    chacha20_xor(key, nonce, 1, reinterpret_cast<const u8*>(plain.data()), ct.data(), ct.size());
    const std::string got = hex_of(ct.data(), ct.size());
    // RFC 8439 §2.4.2 expected ciphertext (first 32 bytes shown)
    PRISM_CHECK(got.rfind("6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b", 0) == 0);
    // decrypt must round-trip
    std::vector<u8> back(plain.size());
    chacha20_xor(key, nonce, 1, ct.data(), back.data(), back.size());
    PRISM_CHECK_STR(std::string(back.begin(), back.end()), plain);
}

PRISM_TEST(crypto_poly1305_rfc8439) {
    const u8 key[32] = {
        0x85,0xd6,0xbe,0x78,0x57,0x55,0x6d,0x33,0x7f,0x44,0x52,0xfe,0x42,0xd5,0x06,0xa8,
        0x01,0x03,0x80,0x8a,0xfb,0x0d,0xb2,0xfd,0x4a,0xbf,0xf6,0xaf,0x41,0x49,0xf5,0x1b};
    const std::string msg = "Cryptographic Forum Research Group";
    auto tag = poly1305_mac(key, reinterpret_cast<const u8*>(msg.data()), msg.size());
    PRISM_CHECK_STR(hex_of(tag.data(), tag.size()), "a8061dc1305136c6c22b8baf0c0127a9");

    // RFC 8439 Appendix A.3 vector #1: all-zero key over 64 zero bytes.
    u8 zkey[32] = {}, zmsg[64] = {};
    auto t1 = poly1305_mac(zkey, zmsg, sizeof(zmsg));
    PRISM_CHECK_STR(hex_of(t1.data(), t1.size()), "00000000000000000000000000000000");
    // empty message
    auto t2 = poly1305_mac(key, nullptr, 0);
    PRISM_CHECK_EQ(static_cast<int>(t2.size()), 16);
}

// Independent 256-bit big-integer Poly1305 used only to validate the shipped
// limb implementation. Host-only (needs 128-bit ints); never linked into the APK.
#if defined(__SIZEOF_INT128__)
namespace {
struct Big { u64 w[4] = {}; };   // little-endian 64-bit limbs, 256 bits

Big big_bytes(const u8* p, std::size_t n) {
    Big b;
    for (std::size_t i = 0; i < n && i < 32; ++i) b.w[i/8] |= static_cast<u64>(p[i]) << (8*(i%8));
    return b;
}
void big_add(Big& a, const Big& b) {
    unsigned __int128 carry = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned __int128 s = static_cast<unsigned __int128>(a.w[i]) + b.w[i] + carry;
        a.w[i] = static_cast<u64>(s); carry = s >> 64;
    }
}
void big_add_small(Big& a, u64 v) { Big b; b.w[0] = v; big_add(a, b); }
void big_mul_small(Big& a, u64 v) {
    unsigned __int128 carry = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned __int128 s = static_cast<unsigned __int128>(a.w[i]) * v + carry;
        a.w[i] = static_cast<u64>(s); carry = s >> 64;
    }
}
/// x < 2^130 (3 limbs) times y < 2^124 (2 limbs) -> product < 2^254, fits 4 limbs.
Big big_mul(const Big& x, const Big& y) {
    unsigned __int128 acc[4] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            acc[i+j] += static_cast<unsigned __int128>(x.w[i]) * y.w[j];
    Big r; unsigned __int128 carry = 0;
    for (int i = 0; i < 4; ++i) { acc[i] += carry; r.w[i] = static_cast<u64>(acc[i]); carry = acc[i] >> 64; }
    return r;
}
void big_shr(Big& a, int shift) {
    const int ws = shift / 64, bs = shift % 64;
    u64 out[4] = {};
    for (int i = 0; i + ws < 4; ++i) {
        u64 lo = a.w[i + ws];
        u64 hi = (i + ws + 1 < 4) ? a.w[i + ws + 1] : 0;
        out[i] = bs == 0 ? lo : static_cast<u64>((lo >> bs) | (hi << (64 - bs)));
    }
    std::memcpy(a.w, out, sizeof(out));
}
void big_mask130(Big& a) { a.w[2] &= 0x3u; a.w[3] = 0; }   // 130 = 2*64 + 2
bool big_ge_130(const Big& a) { return (a.w[2] >> 2) != 0 || a.w[3] != 0; }

/// v mod (2^130 - 5): fold hi*2^130 -> hi*5, then one conditional subtract of p.
void big_reduce(Big& v) {
    for (int i = 0; i < 6 && big_ge_130(v); ++i) {
        Big hi = v; big_shr(hi, 130);
        Big lo = v; big_mask130(lo);
        big_mul_small(hi, 5);
        v = lo; big_add(v, hi);
    }
    Big tmp = v;
    big_add_small(tmp, 5);
    if (big_ge_130(tmp)) { v = tmp; big_mask130(v); }
}

std::array<u8,16> poly1305_reference(const u8 key[32], const u8* msg, std::size_t len) {
    Big r = big_bytes(key, 16);
    r.w[0] &= 0x0FFFFFFC0FFFFFFFull;   // RFC 8439 §2.5 clamp
    r.w[1] &= 0x0FFFFFFC0FFFFFFCull;
    Big s = big_bytes(key + 16, 16);
    Big acc;
    std::size_t off = 0;
    while (off < len) {
        std::size_t take = std::min<std::size_t>(16, len - off);
        u8 block[17] = {};
        std::memcpy(block, msg + off, take);
        block[take] = 1;
        Big n = big_bytes(block, 17);
        big_add(acc, n); big_reduce(acc);
        acc = big_mul(acc, r); big_reduce(acc);
        off += take;
    }
    big_add(acc, s);
    std::array<u8,16> out{};
    for (int i = 0; i < 16; ++i) out[i] = static_cast<u8>(acc.w[i/8] >> (8*(i%8)));
    return out;
}

u64 splitmix(u64& s) {
    s += 0x9E3779B97F4A7C15ull;
    u64 z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
} // namespace

PRISM_TEST(crypto_poly1305_matches_bigint_reference) {
    u64 seed = 0x5EED1234C0FFEEull;
    int compared = 0;
    for (int trial = 0; trial < 500; ++trial) {
        u8 k[32];
        for (int i = 0; i < 4; ++i) {
            u64 v = splitmix(seed);
            for (int b = 0; b < 8; ++b) k[i*8+b] = static_cast<u8>(v >> (8*b));
        }
        const std::size_t n = static_cast<std::size_t>(splitmix(seed) % 100);
        std::vector<u8> m(n);
        for (std::size_t i = 0; i < n; ++i) m[i] = static_cast<u8>(splitmix(seed));
        auto a = poly1305_reference(k, m.data(), n);
        auto b = poly1305_mac(k, m.data(), n);
        ++compared;
        if (a != b) {
            PRISM_CHECK_STR(("len=" + std::to_string(n) + " got " + hex_of(b.data(), 16)),
                            ("len=" + std::to_string(n) + " ref " + hex_of(a.data(), 16)));
            break;   // one diagnostic is enough
        }
    }
    PRISM_CHECK_EQ(compared, 500);
}
#endif

PRISM_TEST(crypto_aead_seal_roundtrip_and_tamper) {
    const std::string key = derive_key("correct horse battery staple", "dev.prismengine.demo");
    PRISM_CHECK_EQ(static_cast<int>(key.size()), 32);
    const std::string save = R"({"slot":"hero","coins":1450,"hp":37})";
    auto blob = seal(key, save, "prism-save-v1");
    PRISM_CHECK(blob.size() >= save.size() + 33);
    auto plain = open(key, blob.data(), blob.size(), "prism-save-v1");
    PRISM_CHECK_STR(std::string(plain.begin(), plain.end()), save);

    // wrong associated data -> authentication failure, not garbage plaintext
    PRISM_CHECK(open(key, blob.data(), blob.size(), "other-aad").empty());
    // wrong key -> authentication failure
    const std::string other = derive_key("hunter2", "dev.prismengine.demo");
    PRISM_CHECK(open(other, blob.data(), blob.size(), "prism-save-v1").empty());
    // flipped ciphertext byte -> authentication failure
    auto tampered = blob; tampered.back() ^= 0x01;
    PRISM_CHECK(open(key, tampered.data(), tampered.size(), "prism-save-v1").empty());
    // truncated / bad magic -> rejected
    PRISM_CHECK(open(key, blob.data(), 10, "prism-save-v1").empty());
    auto badmagic = blob; badmagic[0] = 'X';
    PRISM_CHECK(open(key, badmagic.data(), badmagic.size(), "prism-save-v1").empty());
    // 32-byte key is mandatory
    PRISM_CHECK(seal("short", save).empty());
}

PRISM_TEST(crypto_hkdf_length_and_determinism) {
    auto a = hkdf_sha256("ikm", "salt", "prism/save", 42);
    auto b = hkdf_sha256("ikm", "salt", "prism/save", 42);
    auto c = hkdf_sha256("ikm", "salt", "prism/other", 42);
    PRISM_CHECK_EQ(static_cast<int>(a.size()), 42);
    PRISM_CHECK(a == b);
    PRISM_CHECK(!(a == c));
}

PRISM_TEST(crypto_offline_unlock_codes) {
    UnlockCode u{"com.ray.spectral.pack", "device-0001", "merchant-secret-offline"};
    const std::string code = u.generate();
    PRISM_CHECK(code.rfind("PRISM-", 0) == 0);
    PRISM_CHECK(u.verify(code));
    PRISM_CHECK(u.verify(UnlockCode::normalise(code).substr(5)));       // bare form
    UnlockCode other_device{"com.ray.spectral.pack", "device-0002", "merchant-secret-offline"};
    PRISM_CHECK(!other_device.verify(code));
    UnlockCode other_sku{"com.ray.ads.remover", "device-0001", "merchant-secret-offline"};
    PRISM_CHECK(!other_sku.verify(code));
    PRISM_CHECK(!u.verify("PRISM-AAAA-AAAA-AAAA"));
    PRISM_CHECK(!u.verify(""));
    // codes are case-insensitive and ignore separators
    std::string lower; for (char ch : code) lower.push_back(static_cast<char>(std::tolower(ch)));
    PRISM_CHECK(u.verify(lower));
}

PRISM_TEST(crypto_constant_time_and_crc) {
    const u8 a[4] = {1,2,3,4}, b[4] = {1,2,3,4}, c[4] = {1,2,3,5};
    PRISM_CHECK(constant_time_equal(a, b, 4));
    PRISM_CHECK(!constant_time_equal(a, c, 4));
    // CRC-32/ISO-HDLC of "123456789" is 0xCBF43926
    PRISM_CHECK_EQ(crc32("123456789"), 0xCBF43926u);
    PRISM_CHECK_NE(fnv1a("prism/a.png"), fnv1a("prism/b.png"));
    PRISM_CHECK_EQ(combine_unordered(1, 2), combine_unordered(2, 1));
    PRISM_CHECK_NE(combine_ordered(1, 2), combine_ordered(2, 1));
}
