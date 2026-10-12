#pragma once

#include <string>

namespace AccountRepository
{
    enum class RegisterResult
    {
        Created,
        UsernameTaken,
        Error
    };

    RegisterResult register_user(const std::string &username,
                                 const std::string &password);

    enum class LoginResult
    {
        Verified,
        InvalidCredentials,
        Error
    };

    LoginResult verify_user(const std::string &username,
                            const std::string &password);
}