#pragma once

#include <string>

namespace PasswordHash
{
    bool generate(const std::string &password,
                  std::string &stored_hash);

    bool verify(const std::string &password,
            const std::string &stored_hash);
}