#include "account_repository.h"
#include "database_pool.h"
#include "password_hash.h"
#include "../log/logger.h"

#include <mysql/mysql.h>

#include <string>

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