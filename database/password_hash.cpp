#include "password_hash.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstddef>
#include <string>
#include <charconv>
#include <openssl/crypto.h>

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

    bool from_hex(const std::string &text,
                  unsigned char *output,
                  std::size_t output_length)
    {
        if (text.size() != output_length * 2)
        {
            return false;
        }

        for (std::size_t i = 0; i < output_length; ++i)
        {
            unsigned int value = 0;
            const char *begin = text.data() + i * 2;
            const char *end = begin + 2;

            auto result = std::from_chars(begin, end, value, 16);

            if (result.ec != std::errc{} || result.ptr != end)
            {
                return false;
            }

            output[i] = static_cast<unsigned char>(value);
        }

        return true;
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

bool PasswordHash::verify(const std::string &password,
                          const std::string &stored_hash)
{
    const std::string prefix = "pbkdf2-sha256$";

    if (stored_hash.compare(0, prefix.size(), prefix) != 0)
    {
        return false;
    }

    std::size_t iterations_end =
        stored_hash.find('$', prefix.size());

    if (iterations_end == std::string::npos)
    {
        return false;
    }

    std::size_t salt_end =
        stored_hash.find('$', iterations_end + 1);

    if (salt_end == std::string::npos)
    {
        return false;
    }

    int saved_iterations = 0;
    const char *begin = stored_hash.data() + prefix.size();
    const char *end = stored_hash.data() + iterations_end;

    auto parsed = std::from_chars(
        begin, end, saved_iterations);

    if (parsed.ec != std::errc{} ||
        parsed.ptr != end ||
        saved_iterations < 1 ||
        saved_iterations > 1000000)
    {
        return false;
    }

    std::array<unsigned char, 16> salt{};
    std::array<unsigned char, 32> expected_hash{};
    std::array<unsigned char, 32> calculated_hash{};

    if (!from_hex(
            stored_hash.substr(
                iterations_end + 1,
                salt_end - iterations_end - 1),
            salt.data(),
            salt.size()) ||
        !from_hex(
            stored_hash.substr(salt_end + 1),
            expected_hash.data(),
            expected_hash.size()))
    {
        return false;
    }

    if (PKCS5_PBKDF2_HMAC(
            password.data(),
            static_cast<int>(password.size()),
            salt.data(),
            static_cast<int>(salt.size()),
            saved_iterations,
            EVP_sha256(),
            static_cast<int>(calculated_hash.size()),
            calculated_hash.data()) != 1)
    {
        return false;
    }

    return CRYPTO_memcmp(
               calculated_hash.data(),
               expected_hash.data(),
               expected_hash.size()) == 0;
}