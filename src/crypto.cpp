#include "crypto.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace crypto {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

constexpr std::array<std::uint8_t, 256> kSbox = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

constexpr std::array<std::uint8_t, 11> kRcon = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32U - n));
}

inline std::uint32_t load_be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24U) |
           (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) |
            static_cast<std::uint32_t>(p[3]);
}

inline void store_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24U);
    p[1] = static_cast<std::uint8_t>(v >> 16U);
    p[2] = static_cast<std::uint8_t>(v >> 8U);
    p[3] = static_cast<std::uint8_t>(v);
}

inline void store_be64(std::uint8_t* p, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<std::uint8_t>(v & 0xffU);
        v >>= 8U;
    }
}

struct Sha256Ctx {
    std::array<std::uint32_t, 8> h = {
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U
    };
    std::array<std::uint8_t, 64> buffer{};
    std::size_t buffered = 0;
    std::uint64_t total_bytes = 0;
};

void sha256_transform(Sha256Ctx& ctx, const std::uint8_t block[64]) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = load_be32(block + i * 4);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3U);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10U);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = ctx.h[0];
    std::uint32_t b = ctx.h[1];
    std::uint32_t c = ctx.h[2];
    std::uint32_t d = ctx.h[3];
    std::uint32_t e = ctx.h[4];
    std::uint32_t f = ctx.h[5];
    std::uint32_t g = ctx.h[6];
    std::uint32_t h = ctx.h[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t t1 = h + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d;
    ctx.h[4] += e; ctx.h[5] += f; ctx.h[6] += g; ctx.h[7] += h;
}

void sha256_update(Sha256Ctx& ctx, const std::uint8_t* data, std::size_t size) {
    ctx.total_bytes += static_cast<std::uint64_t>(size);
    while (size > 0) {
        const std::size_t take = std::min<std::size_t>(64 - ctx.buffered, size);
        std::memcpy(ctx.buffer.data() + ctx.buffered, data, take);
        ctx.buffered += take;
        data += take;
        size -= take;
        if (ctx.buffered == 64) {
            sha256_transform(ctx, ctx.buffer.data());
            ctx.buffered = 0;
        }
    }
}

Digest32 sha256_finish(Sha256Ctx& ctx) {
    const std::uint64_t bit_length = ctx.total_bytes * 8U;
    ctx.buffer[ctx.buffered++] = 0x80;
    if (ctx.buffered > 56) {
        std::fill(ctx.buffer.begin() + static_cast<std::ptrdiff_t>(ctx.buffered), ctx.buffer.end(), 0);
        sha256_transform(ctx, ctx.buffer.data());
        ctx.buffered = 0;
    }
    std::fill(ctx.buffer.begin() + static_cast<std::ptrdiff_t>(ctx.buffered), ctx.buffer.begin() + 56, 0);
    store_be64(ctx.buffer.data() + 56, bit_length);
    sha256_transform(ctx, ctx.buffer.data());

    Digest32 out{};
    for (std::size_t i = 0; i < 8; ++i) {
        store_be32(out.data() + i * 4, ctx.h[i]);
    }
    return out;
}

std::array<std::uint8_t, 176> aes_expand_key(const Block16& key) {
    std::array<std::uint8_t, 176> expanded{};
    std::copy(key.begin(), key.end(), expanded.begin());
    std::size_t generated = 16;
    std::size_t rcon_index = 1;
    std::array<std::uint8_t, 4> temp{};

    while (generated < expanded.size()) {
        for (std::size_t i = 0; i < 4; ++i) {
            temp[i] = expanded[generated - 4 + i];
        }
        if ((generated % 16) == 0) {
            const std::uint8_t first = temp[0];
            temp[0] = kSbox[temp[1]];
            temp[1] = kSbox[temp[2]];
            temp[2] = kSbox[temp[3]];
            temp[3] = kSbox[first];
            temp[0] ^= kRcon[rcon_index++];
        }
        for (std::size_t i = 0; i < 4; ++i) {
            expanded[generated] = expanded[generated - 16] ^ temp[i];
            ++generated;
        }
    }
    return expanded;
}

inline std::uint8_t xtime(std::uint8_t x) {
    return static_cast<std::uint8_t>((x << 1U) ^ ((x & 0x80U) ? 0x1bU : 0x00U));
}

void aes_add_round_key(Block16& state, const std::array<std::uint8_t, 176>& keys, unsigned round) {
    const std::size_t offset = static_cast<std::size_t>(round) * 16;
    for (std::size_t i = 0; i < 16; ++i) {
        state[i] ^= keys[offset + i];
    }
}

void aes_sub_bytes(Block16& state) {
    for (auto& b : state) b = kSbox[b];
}

void aes_shift_rows(Block16& s) {
    const Block16 t = s;
    s[1]  = t[5];  s[5]  = t[9];  s[9]  = t[13]; s[13] = t[1];
    s[2]  = t[10]; s[6]  = t[14]; s[10] = t[2];  s[14] = t[6];
    s[3]  = t[15]; s[7]  = t[3];  s[11] = t[7];  s[15] = t[11];
}

void aes_mix_columns(Block16& s) {
    for (std::size_t c = 0; c < 4; ++c) {
        const std::size_t i = c * 4;
        const std::uint8_t a0 = s[i], a1 = s[i+1], a2 = s[i+2], a3 = s[i+3];
        const std::uint8_t x = a0 ^ a1 ^ a2 ^ a3;
        s[i]   = static_cast<std::uint8_t>(s[i]   ^ x ^ xtime(static_cast<std::uint8_t>(a0 ^ a1)));
        s[i+1] = static_cast<std::uint8_t>(s[i+1] ^ x ^ xtime(static_cast<std::uint8_t>(a1 ^ a2)));
        s[i+2] = static_cast<std::uint8_t>(s[i+2] ^ x ^ xtime(static_cast<std::uint8_t>(a2 ^ a3)));
        s[i+3] = static_cast<std::uint8_t>(s[i+3] ^ x ^ xtime(static_cast<std::uint8_t>(a3 ^ a0)));
    }
}

Block16 aes_encrypt_with_expanded(const std::array<std::uint8_t, 176>& keys, const Block16& input) {
    Block16 state = input;
    aes_add_round_key(state, keys, 0);
    for (unsigned round = 1; round < 10; ++round) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, keys, round);
    }
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, keys, 10);
    return state;
}

void xor_block(Block16& dst, const Block16& src) {
    for (std::size_t i = 0; i < 16; ++i) dst[i] ^= src[i];
}

void shift_right_one(Block16& v) {
    std::uint8_t carry = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint8_t next_carry = static_cast<std::uint8_t>(v[i] & 1U);
        v[i] = static_cast<std::uint8_t>((v[i] >> 1U) | (carry << 7U));
        carry = next_carry;
    }
}

Block16 galois_multiply(const Block16& x, const Block16& y) {
    Block16 z{};
    Block16 v = y;
    for (std::size_t bit = 0; bit < 128; ++bit) {
        const bool set = ((x[bit / 8] >> (7U - static_cast<unsigned>(bit % 8))) & 1U) != 0;
        if (set) xor_block(z, v);
        const bool lsb = (v[15] & 1U) != 0;
        shift_right_one(v);
        if (lsb) v[0] ^= 0xe1U;
    }
    return z;
}

void ghash_block(Block16& y, const Block16& h, const std::uint8_t* block, std::size_t size) {
    Block16 x{};
    if (size > 0) std::memcpy(x.data(), block, size);
    xor_block(y, x);
    y = galois_multiply(y, h);
}

Block16 ghash(const Block16& h,
              const std::uint8_t* aad, std::size_t aad_size,
              const std::uint8_t* ciphertext, std::size_t ciphertext_size) {
    Block16 y{};
    std::size_t pos = 0;
    while (pos < aad_size) {
        const std::size_t n = std::min<std::size_t>(16, aad_size - pos);
        ghash_block(y, h, aad + pos, n);
        pos += n;
    }
    pos = 0;
    while (pos < ciphertext_size) {
        const std::size_t n = std::min<std::size_t>(16, ciphertext_size - pos);
        ghash_block(y, h, ciphertext + pos, n);
        pos += n;
    }
    Block16 length_block{};
    store_be64(length_block.data(), static_cast<std::uint64_t>(aad_size) * 8U);
    store_be64(length_block.data() + 8, static_cast<std::uint64_t>(ciphertext_size) * 8U);
    ghash_block(y, h, length_block.data(), 16);
    return y;
}

void increment_counter32(Block16& counter) {
    for (int i = 15; i >= 12; --i) {
        counter[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(counter[static_cast<std::size_t>(i)] + 1U);
        if (counter[static_cast<std::size_t>(i)] != 0) break;
    }
}

std::vector<std::uint8_t> gctr(const std::array<std::uint8_t, 176>& keys,
                               Block16 counter,
                               const std::uint8_t* input,
                               std::size_t input_size) {
    std::vector<std::uint8_t> output(input_size);
    std::size_t pos = 0;
    while (pos < input_size) {
        increment_counter32(counter);
        const Block16 stream = aes_encrypt_with_expanded(keys, counter);
        const std::size_t n = std::min<std::size_t>(16, input_size - pos);
        for (std::size_t i = 0; i < n; ++i) output[pos + i] = input[pos + i] ^ stream[i];
        pos += n;
    }
    return output;
}

bool decode_hex(const std::string& text, std::vector<std::uint8_t>& out) {
    if ((text.size() % 2) != 0) return false;
    out.clear();
    out.reserve(text.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

} // namespace

Digest32 sha256(const std::uint8_t* data, std::size_t size) {
    Sha256Ctx ctx;
    sha256_update(ctx, data, size);
    return sha256_finish(ctx);
}

Digest32 sha256(const std::vector<std::uint8_t>& data) {
    return sha256(data.data(), data.size());
}

Digest32 hmac_sha256(const std::uint8_t* key, std::size_t key_size,
                     const std::uint8_t* data, std::size_t data_size) {
    std::array<std::uint8_t, 64> key_block{};
    if (key_size > key_block.size()) {
        const Digest32 digest = sha256(key, key_size);
        std::copy(digest.begin(), digest.end(), key_block.begin());
    } else if (key_size > 0) {
        std::memcpy(key_block.data(), key, key_size);
    }

    std::array<std::uint8_t, 64> ipad{};
    std::array<std::uint8_t, 64> opad{};
    for (std::size_t i = 0; i < 64; ++i) {
        ipad[i] = key_block[i] ^ 0x36U;
        opad[i] = key_block[i] ^ 0x5cU;
    }

    Sha256Ctx inner;
    sha256_update(inner, ipad.data(), ipad.size());
    sha256_update(inner, data, data_size);
    const Digest32 inner_digest = sha256_finish(inner);

    Sha256Ctx outer;
    sha256_update(outer, opad.data(), opad.size());
    sha256_update(outer, inner_digest.data(), inner_digest.size());
    return sha256_finish(outer);
}

Block16 aes128_encrypt_block(const Block16& key, const Block16& input) {
    return aes_encrypt_with_expanded(aes_expand_key(key), input);
}

GcmResult aes128_gcm_encrypt(const Block16& key,
                             const Iv12& iv,
                             const std::uint8_t* aad,
                             std::size_t aad_size,
                             const std::uint8_t* plaintext,
                             std::size_t plaintext_size) {
    const auto keys = aes_expand_key(key);
    const Block16 zero{};
    const Block16 h = aes_encrypt_with_expanded(keys, zero);
    Block16 j0{};
    std::copy(iv.begin(), iv.end(), j0.begin());
    j0[15] = 1;

    GcmResult result;
    result.ciphertext = gctr(keys, j0, plaintext, plaintext_size);
    Block16 s = ghash(h, aad, aad_size, result.ciphertext.data(), result.ciphertext.size());
    const Block16 e_j0 = aes_encrypt_with_expanded(keys, j0);
    for (std::size_t i = 0; i < 16; ++i) result.tag[i] = s[i] ^ e_j0[i];
    return result;
}

bool aes128_gcm_decrypt(const Block16& key,
                        const Iv12& iv,
                        const std::uint8_t* aad,
                        std::size_t aad_size,
                        const std::uint8_t* ciphertext,
                        std::size_t ciphertext_size,
                        const Tag16& expected_tag,
                        std::vector<std::uint8_t>& plaintext) {
    const auto keys = aes_expand_key(key);
    const Block16 zero{};
    const Block16 h = aes_encrypt_with_expanded(keys, zero);
    Block16 j0{};
    std::copy(iv.begin(), iv.end(), j0.begin());
    j0[15] = 1;

    Block16 s = ghash(h, aad, aad_size, ciphertext, ciphertext_size);
    const Block16 e_j0 = aes_encrypt_with_expanded(keys, j0);
    Tag16 calculated{};
    for (std::size_t i = 0; i < 16; ++i) calculated[i] = s[i] ^ e_j0[i];
    if (!constant_time_equal(calculated.data(), expected_tag.data(), calculated.size())) {
        plaintext.clear();
        return false;
    }
    plaintext = gctr(keys, j0, ciphertext, ciphertext_size);
    return true;
}

bool constant_time_equal(const std::uint8_t* a, const std::uint8_t* b, std::size_t size) {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < size; ++i) diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

std::string hex_encode(const std::uint8_t* data, std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned>(data[i]);
    return out.str();
}

bool run_self_tests(std::string& error) {
    {
        const Digest32 digest = sha256(nullptr, 0);
        if (hex_encode(digest.data(), digest.size()) != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") {
            error = "SHA-256 self-test failed";
            return false;
        }
    }
    {
        std::array<std::uint8_t, 20> key{};
        key.fill(0x0b);
        const char* text = "Hi There";
        const Digest32 digest = hmac_sha256(key.data(), key.size(),
                                             reinterpret_cast<const std::uint8_t*>(text), 8);
        if (hex_encode(digest.data(), digest.size()) != "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") {
            error = "HMAC-SHA256 self-test failed";
            return false;
        }
    }
    {
        std::vector<std::uint8_t> key_v, input_v;
        decode_hex("000102030405060708090a0b0c0d0e0f", key_v);
        decode_hex("00112233445566778899aabbccddeeff", input_v);
        Block16 key{}, input{};
        std::copy(key_v.begin(), key_v.end(), key.begin());
        std::copy(input_v.begin(), input_v.end(), input.begin());
        const Block16 output = aes128_encrypt_block(key, input);
        if (hex_encode(output.data(), output.size()) != "69c4e0d86a7b0430d8cdb78070b4c55a") {
            error = "AES-128 self-test failed";
            return false;
        }
    }
    {
        Block16 key{};
        Iv12 iv{};
        Block16 plaintext{};
        const GcmResult encrypted = aes128_gcm_encrypt(key, iv, nullptr, 0, plaintext.data(), plaintext.size());
        if (hex_encode(encrypted.ciphertext.data(), encrypted.ciphertext.size()) != "0388dace60b6a392f328c2b971b2fe78" ||
            hex_encode(encrypted.tag.data(), encrypted.tag.size()) != "ab6e47d42cec13bdf53a67b21257bddf") {
            error = "AES-GCM self-test failed";
            return false;
        }
        std::vector<std::uint8_t> decrypted;
        if (!aes128_gcm_decrypt(key, iv, nullptr, 0,
                                encrypted.ciphertext.data(), encrypted.ciphertext.size(),
                                encrypted.tag, decrypted) ||
            decrypted.size() != plaintext.size() ||
            !constant_time_equal(decrypted.data(), plaintext.data(), plaintext.size())) {
            error = "AES-GCM decrypt self-test failed";
            return false;
        }
    }
    error.clear();
    return true;
}

} // namespace crypto
