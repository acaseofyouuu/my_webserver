#pragma once

#include <string>

namespace PasswordHash
{
    bool generate(const std::string &password,
                  std::string &stored_hash);
}