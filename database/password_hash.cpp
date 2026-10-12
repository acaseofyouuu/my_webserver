#include "password_hash.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstddef>
#include <string>

namespace
{
    constexpr int iterations = 600000;

    std::string to_hex(const unsigned char *bytes,
                       std::size_t length)
    {
        const char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(length * 2);

        for (std::size_t i = 0; i < length; ++i)
        {
            result.push_back(digits[bytes[i] >> 4]);
            result.push_back(digits[bytes[i] & 0x0f]);
        }

        return result;
    }
}

bool PasswordHash::generate(const std::string &password,
                            std::string &stored_hash)
{
    std::array<unsigned char, 16> salt{};
    std::array<unsigned char, 32> hash{};

    if (RAND_bytes(salt.data(),
                   static_cast<int>(salt.size())) != 1)
    {
        return false;
    }

    if (PKCS5_PBKDF2_HMAC(
            password.data(),
            static_cast<int>(password.size()),
            salt.data(),
            static_cast<int>(salt.size()),
            iterations,
            EVP_sha256(),
            static_cast<int>(hash.size()),
            hash.data()) != 1)
    {
        return false;
    }

    stored_hash =
        "pbkdf2-sha256$" +
        std::to_string(iterations) + "$" +
        to_hex(salt.data(), salt.size()) + "$" +
        to_hex(hash.data(), hash.size());

    return true;
}