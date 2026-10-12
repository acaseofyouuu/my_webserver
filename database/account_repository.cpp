#include "account_repository.h"
#include "database_pool.h"
#include "password_hash.h"
#include "../log/logger.h"

#include <mysql/mysql.h>

#include <string>
#include <array>

AccountRepository::RegisterResult
AccountRepository::register_user(const std::string &username,
                                 const std::string &password)
{
    std::string stored_hash;

    if (!PasswordHash::generate(password, stored_hash))
    {
        Logger::instance().error("Failed to hash password");
        return RegisterResult::Error;
    }

    DatabaseConnectionGuard guard;
    MYSQL *connection = guard.get();
    MYSQL_STMT *statement = mysql_stmt_init(connection);

    if (statement == nullptr)
    {
        Logger::instance().error("Failed to create MySQL statement");
        return RegisterResult::Error;
    }

    constexpr char insert_sql[] =
        "INSERT INTO accounts (username, password_hash) VALUES (?, ?)";

    if (mysql_stmt_prepare(
            statement, insert_sql, sizeof(insert_sql) - 1) != 0)
    {
        Logger::instance().error("Failed to prepare account insert");
        mysql_stmt_close(statement);
        return RegisterResult::Error;
    }

    std::string writable_username = username;
    unsigned long username_length =
        static_cast<unsigned long>(writable_username.size());
    unsigned long hash_length =
        static_cast<unsigned long>(stored_hash.size());

    MYSQL_BIND parameters[2]{};

    parameters[0].buffer_type = MYSQL_TYPE_STRING;
    parameters[0].buffer = writable_username.data();
    parameters[0].buffer_length = username_length;
    parameters[0].length = &username_length;

    parameters[1].buffer_type = MYSQL_TYPE_STRING;
    parameters[1].buffer = stored_hash.data();
    parameters[1].buffer_length = hash_length;
    parameters[1].length = &hash_length;

    if (mysql_stmt_bind_param(statement, parameters) != 0)
    {
        Logger::instance().error("Failed to bind account data");
        mysql_stmt_close(statement);
        return RegisterResult::Error;
    }

    int execute_result = mysql_stmt_execute(statement);
    unsigned int error_code = mysql_stmt_errno(statement);
    mysql_stmt_close(statement);

    if (execute_result == 0)
    {
        return RegisterResult::Created;
    }

    if (error_code == 1062)
    {
        return RegisterResult::UsernameTaken;
    }

    Logger::instance().error(
        "Failed to insert account, MySQL error " +
        std::to_string(error_code));

    return RegisterResult::Error;
}

AccountRepository::LoginResult
AccountRepository::verify_user(const std::string &username,
                               const std::string &password)
{
    std::string stored_hash;

    {
        DatabaseConnectionGuard guard;
        MYSQL *connection = guard.get();
        MYSQL_STMT *statement = mysql_stmt_init(connection);

        if (statement == nullptr)
        {
            Logger::instance().error("Failed to create login query");
            return LoginResult::Error;
        }

        constexpr char select_sql[] =
            "SELECT password_hash FROM accounts WHERE username = ?";

        if (mysql_stmt_prepare(
                statement, select_sql, sizeof(select_sql) - 1) != 0)
        {
            Logger::instance().error("Failed to prepare login query");
            mysql_stmt_close(statement);
            return LoginResult::Error;
        }

        std::string writable_username = username;
        unsigned long username_length =
            static_cast<unsigned long>(writable_username.size());

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_STRING;
        parameter.buffer = writable_username.data();
        parameter.buffer_length = username_length;
        parameter.length = &username_length;

        if (mysql_stmt_bind_param(statement, &parameter) != 0 ||
            mysql_stmt_execute(statement) != 0)
        {
            Logger::instance().error("Failed to run login query");
            mysql_stmt_close(statement);
            return LoginResult::Error;
        }

        std::array<char, 256> hash_buffer{};
        unsigned long hash_length = 0;

        MYSQL_BIND result{};
        result.buffer_type = MYSQL_TYPE_STRING;
        result.buffer = hash_buffer.data();
        result.buffer_length =
            static_cast<unsigned long>(hash_buffer.size());
        result.length = &hash_length;

        if (mysql_stmt_bind_result(statement, &result) != 0 ||
            mysql_stmt_store_result(statement) != 0)
        {
            Logger::instance().error("Failed to read login result");
            mysql_stmt_close(statement);
            return LoginResult::Error;
        }

        int fetch_result = mysql_stmt_fetch(statement);

        if (fetch_result == MYSQL_NO_DATA)
        {
            mysql_stmt_close(statement);
            return LoginResult::InvalidCredentials;
        }

        if (fetch_result != 0 ||
            hash_length > hash_buffer.size())
        {
            Logger::instance().error("Invalid login query result");
            mysql_stmt_close(statement);
            return LoginResult::Error;
        }

        stored_hash.assign(hash_buffer.data(), hash_length);
        mysql_stmt_close(statement);
    }

    if (PasswordHash::verify(password, stored_hash))
    {
        return LoginResult::Verified;
    }

    return LoginResult::InvalidCredentials;
}