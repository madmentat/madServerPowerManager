#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crypto {

using Block16 = std::array<std::uint8_t, 16>;
using Tag16 = std::array<std::uint8_t, 16>;
using Iv12 = std::array<std::uint8_t, 12>;
using Digest32 = std::array<std::uint8_t, 32>;

Digest32 sha256(const std::uint8_t* data, std::size_t size);
Digest32 sha256(const std::vector<std::uint8_t>& data);
Digest32 hmac_sha256(const std::uint8_t* key, std::size_t key_size,
                     const std::uint8_t* data, std::size_t data_size);

Block16 aes128_encrypt_block(const Block16& key, const Block16& input);

struct GcmResult {
    std::vector<std::uint8_t> ciphertext;
    Tag16 tag{};
};

GcmResult aes128_gcm_encrypt(const Block16& key,
                             const Iv12& iv,
                             const std::uint8_t* aad,
                             std::size_t aad_size,
                             const std::uint8_t* plaintext,
                             std::size_t plaintext_size);

bool aes128_gcm_decrypt(const Block16& key,
                        const Iv12& iv,
                        const std::uint8_t* aad,
                        std::size_t aad_size,
                        const std::uint8_t* ciphertext,
                        std::size_t ciphertext_size,
                        const Tag16& expected_tag,
                        std::vector<std::uint8_t>& plaintext);

bool constant_time_equal(const std::uint8_t* a, const std::uint8_t* b, std::size_t size);
std::string hex_encode(const std::uint8_t* data, std::size_t size);
bool run_self_tests(std::string& error);

} // namespace crypto
