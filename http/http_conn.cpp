#include "http_conn.h"
#include "../log/logger.h"
#include "../database/account_repository.h"
#include "../session/session_manager.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <filesystem>
#include <cerrno>
#include <sys/socket.h>
#include <sys/time.h>
#include <cctype>
#include <charconv>

namespace
{
    std::string get_session_id(const std::string &cookie_header)
    {
        std::istringstream stream(cookie_header);
        std::string part;
        std::string session_id;

        while (std::getline(stream, part, ';'))
        {
            const auto start = part.find_first_not_of(" \t");

            if (start == std::string::npos ||
                part.compare(start, 11, "session_id=") != 0)
            {
                continue;
            }

            if (!session_id.empty())
            {
                return {};
            }

            std::string value = part.substr(start + 11);
            const auto end = value.find_last_not_of(" \t");

            if (end != std::string::npos)
            {
                value.erase(end + 1);
            }

            if (value.size() != 64 ||
                value.find_first_not_of("0123456789abcdef") !=
                    std::string::npos)
            {
                return {};
            }

            session_id = value;
        }

        return session_id;
    }

    bool decode_form_component(const std::string &encoded,
                               std::string &decoded)
    {
        decoded.clear();

        for (std::size_t i = 0; i < encoded.size(); ++i)
        {
            if (encoded[i] == '+')
            {
                decoded.push_back(' ');
            }
            else if (encoded[i] == '%')
            {
                if (i + 2 >= encoded.size())
                {
                    return false;
                }

                unsigned int byte = 0;
                const char *begin = encoded.data() + i + 1;
                const char *end = begin + 2;

                auto result = std::from_chars(begin, end, byte, 16);

                if (result.ec != std::errc{} || result.ptr != end)
                {
                    return false;
                }

                decoded.push_back(static_cast<char>(byte));
                i += 2;
            }
            else
            {
                decoded.push_back(encoded[i]);
            }
        }

        return true;
    }

    bool parse_form_fields(
        const std::string &body,
        std::unordered_map<std::string, std::string> &fields)
    {
        if (body.empty())
        {
            return false;
        }

        std::size_t start = 0;

        while (start < body.size())
        {
            std::size_t end = body.find('&', start);

            if (end == std::string::npos)
            {
                end = body.size();
            }

            std::size_t equals = body.find('=', start);

            if (equals == std::string::npos ||
                equals >= end ||
                equals == start)
            {
                return false;
            }

            std::string name;
            std::string value;

            if (!decode_form_component(
                    body.substr(start, equals - start), name) ||
                !decode_form_component(
                    body.substr(equals + 1, end - equals - 1), value) ||
                name.empty() ||
                fields.find(name) != fields.end())
            {
                return false;
            }

            fields.emplace(name, value);

            if (end == body.size())
            {
                return true;
            }

            start = end + 1;
        }

        return false;
    }

    std::string get_content_type(const std::string &file_path)
    {
        std::string extension =
            std::filesystem::path(file_path).extension().string();

        if (extension == ".html")
        {
            return "text/html; charset=UTF-8";
        }

        if (extension == ".css")
        {
            return "text/css; charset=UTF-8";
        }

        if (extension == ".js")
        {
            return "application/javascript; charset=UTF-8";
        }

        if (extension == ".png")
        {
            return "image/png";
        }

        if (extension == ".jpg" || extension == ".jpeg")
        {
            return "image/jpeg";
        }

        if (extension == ".gif")
        {
            return "image/gif";
        }

        if (extension == ".svg")
        {
            return "image/svg+xml";
        }

        if (extension == ".ico")
        {
            return "image/x-icon";
        }

        return "application/octet-stream";
    }
}

HttpConnection::HttpConnection(int client_fd)
    : client_fd_(client_fd)
{
}

bool HttpConnection::read_request(std::string &request)
{
    constexpr std::size_t max_request_size = 16 * 1024;
    char buffer[4096];

    while (request.find("\r\n\r\n") == std::string::npos)
    {
        ssize_t bytes_read = recv(client_fd_, buffer, sizeof(buffer), 0);

        if (bytes_read == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                std::cerr << "Client request timed out\n";
                return false;
            }

            std::cerr << "Failed to read client request\n";
            return false;
        }

        if (bytes_read == 0)
        {
            std::cout << "Client closed the connection before sending a complete request\n";
            return false;
        }

        request.append(buffer, static_cast<std::size_t>(bytes_read));

        if (request.size() > max_request_size)
        {
            std::cerr << "HTTP request is too large\n";
            return false;
        }
    }
    return true;
}

bool HttpConnection::read_body(std::string &request,
                               std::size_t body_length)
{
    constexpr std::size_t max_request_size = 16 * 1024;
    const std::size_t header_end = request.find("\r\n\r\n");

    if (header_end == std::string::npos)
    {
        return false;
    }

    const std::size_t body_start = header_end + 4;

    if (body_start > max_request_size ||
        body_length > max_request_size - body_start)
    {
        return false;
    }

    char buffer[4096];

    while (request.size() < body_start + body_length)
    {
        std::size_t remaining =
            body_start + body_length - request.size();

        std::size_t to_read =
            remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        ssize_t bytes_read = recv(client_fd_, buffer, to_read, 0);

        if (bytes_read == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            std::cerr << "Failed to read request body\n";
            return false;
        }

        if (bytes_read == 0)
        {
            std::cerr << "Client closed before request body was complete\n";
            return false;
        }

        request.append(buffer, static_cast<std::size_t>(bytes_read));
    }

    return true;
}

bool HttpConnection::parse_request_line(const std::string &request,
                                        std::string &method,
                                        std::string &request_path,
                                        std::string &http_version)
{
    std::size_t line_end = request.find("\r\n");

    if (line_end == std::string::npos)
    {
        return false;
    }

    std::istringstream line_stream(request.substr(0, line_end));
    std::string extra_part;

    if (!(line_stream >> method >> request_path >> http_version))
    {
        return false;
    }

    if (line_stream >> extra_part)
    {
        return false;
    }

    return true;
}

bool HttpConnection::parse_headers(
    const std::string &request,
    std::unordered_map<std::string, std::string> &headers)
{
    std::size_t line_start = request.find("\r\n");

    if (line_start == std::string::npos)
    {
        return false;
    }

    line_start += 2;

    while (true)
    {
        std::size_t line_end = request.find("\r\n", line_start);

        if (line_end == std::string::npos)
        {
            return false;
        }

        if (line_end == line_start)
        {
            return true;
        }

        std::string header_line =
            request.substr(line_start, line_end - line_start);

        std::size_t colon_position = header_line.find(':');

        if (colon_position == std::string::npos ||
            colon_position == 0)
        {
            return false;
        }

        std::string header_name =
            header_line.substr(0, colon_position);

        std::string header_value =
            header_line.substr(colon_position + 1);

        for (char &character : header_name)
        {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }

        while (!header_value.empty() &&
               (header_value.front() == ' ' ||
                header_value.front() == '\t'))
        {
            header_value.erase(header_value.begin());
        }

        headers[header_name] = header_value;
        line_start = line_end + 2;
    }
}

bool HttpConnection::should_keep_alive(
    const std::string &http_version,
    const std::unordered_map<std::string, std::string> &headers)
{
    std::string connection_value;

    auto connection_header = headers.find("connection");

    if (connection_header != headers.end())
    {
        connection_value = connection_header->second;

        for (char &character : connection_value)
        {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }

        while (!connection_value.empty() &&
               (connection_value.back() == ' ' ||
                connection_value.back() == '\t'))
        {
            connection_value.pop_back();
        }
    }

    if (http_version == "HTTP/1.1")
    {
        return connection_value != "close";
    }

    return connection_value == "keep-alive";
}

bool HttpConnection::send_all(const std::string &data)
{
    std::size_t total_sent = 0;

    while (total_sent < data.size())
    {
        ssize_t bytes_sent = send(client_fd_,
                                  data.data() + total_sent,
                                  data.size() - total_sent,
                                  MSG_NOSIGNAL);

        if (bytes_sent == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            std::cerr << "Failed to send response\n";
            return false;
        }

        if (bytes_sent == 0)
        {
            std::cerr << "Connection closed while sending response\n";
            return false;
        }

        total_sent += static_cast<std::size_t>(bytes_sent);
    }

    return true;
}

bool HttpConnection::handle()
{
    std::cout << "Client connected , fd = " << client_fd_ << '\n';

    timeval receive_timeout{};
    receive_timeout.tv_sec = 5;
    receive_timeout.tv_usec = 0;

    if (setsockopt(client_fd_, SOL_SOCKET,
                   SO_RCVTIMEO,
                   &receive_timeout,
                   sizeof(receive_timeout)) == -1)
    {
        std::cerr << "Failed to set client receive timeout\n";
        return false;
    }

    std::string request;

    if (!read_request(request))
    {
        return false;
    }

    std::string method;
    std::string request_path;
    std::string http_version;

    bool request_line_valid =
        parse_request_line(request, method, request_path, http_version);

    std::unordered_map<std::string, std::string> headers;

    bool headers_valid = parse_headers(request, headers);

    bool missing_host = request_line_valid &&
                        headers_valid &&
                        http_version == "HTTP/1.1" &&
                        headers.find("host") == headers.end();

    std::cout << "Method: " << method << '\n';
    std::cout << "Path: " << request_path << '\n';
    std::cout << "HTTP version: " << http_version << '\n';

    Logger::instance().info(
        "HTTP request: " +
        method + " " +
        request_path + " " +
        http_version);

    bool malformed_request = !request_line_valid ||
                             !headers_valid ||
                             missing_host ||
                             (http_version != "HTTP/1.0" && http_version != "HTTP/1.1");

    std::string request_body;

    if (!malformed_request && method == "POST")
    {
        auto length_header = headers.find("content-length");

        if (length_header == headers.end())
        {
            malformed_request = true;
        }
        else
        {
            const std::string &value = length_header->second;
            std::size_t body_length = 0;

            auto result = std::from_chars(
                value.data(),
                value.data() + value.size(),
                body_length);

            if (result.ec != std::errc{} ||
                result.ptr != value.data() + value.size() ||
                body_length > 8 * 1024)
            {
                malformed_request = true;
            }
            else
            {
                if (!read_body(request, body_length))
                {
                    return false;
                }

                const std::size_t body_start =
                    request.find("\r\n\r\n") + 4;

                request_body = request.substr(body_start, body_length);
            }
        }
    }

    // 原版的表单地址复用已有账号逻辑，不另建一套账号或会话。
    const bool original_account_action =
        request_path == "/2" || request_path == "/2CGISQL.cgi" ||
        request_path == "/3" || request_path == "/3CGISQL.cgi";
    const bool page_navigation = request_path == "/0" || request_path == "/1";

    if (request_path == "/2" || request_path == "/2CGISQL.cgi")
    {
        request_path = "/login";
    }
    else if (request_path == "/3" || request_path == "/3CGISQL.cgi")
    {
        request_path = "/register";
    }

    bool account_action =
        request_path == "/register" ||
        request_path == "/login";

    bool post_action =
        account_action || request_path == "/logout";

    std::string username;
    std::string password;

    if (!malformed_request &&
        method == "POST" &&
        account_action)
    {
        auto content_type = headers.find("content-type");
        std::unordered_map<std::string, std::string> fields;
        const std::string username_field = original_account_action ? "user" : "username";

        if (content_type == headers.end() ||
            content_type->second != "application/x-www-form-urlencoded" ||
            !parse_form_fields(request_body, fields) ||
            fields.size() != 2 ||
            fields.find(username_field) == fields.end() ||
            fields.find("password") == fields.end())
        {
            malformed_request = true;
        }
        else
        {
            username = fields[username_field];
            password = fields["password"];

            if (username.empty() ||
                username.size() > 50 ||
                (request_path == "/register" && password.size() < 8) ||
                password.size() > 128)
            {
                malformed_request = true;
            }
        }
    }

    bool keep_alive =
        !malformed_request &&
        should_keep_alive(http_version, headers);

    bool method_not_allowed =
        !malformed_request &&
        ((post_action && method != "POST") ||
         (page_navigation && method != "GET" && method != "POST") ||
         (!post_action && !page_navigation && method != "GET"));

    if (!malformed_request && request_path == "/")
    {
        request_path = "/judge.html";
    }

    bool invalid_path =
        !malformed_request &&
        (request_path.front() != '/' ||
         request_path.find("..") != std::string::npos);

    std::string current_session_id;
    std::string logged_in_username;
    bool logged_in = false;

    auto cookie = headers.find("cookie");

    if (!malformed_request && cookie != headers.end())
    {
        current_session_id = get_session_id(cookie->second);

        if (!current_session_id.empty())
        {
            logged_in = SessionManager::instance().find_user(
                current_session_id, logged_in_username);
        }
    }

    std::string file_path;
    std::string status_line;
    std::string cookie_header;

    if (malformed_request)
    {
        file_path = "root/400.html";
        status_line = "HTTP/1.1 400 Bad Request\r\n";
    }
    else if (method_not_allowed)
    {
        file_path = "root/405.html";
        status_line = "HTTP/1.1 405 Method Not Allowed\r\n";
    }
    else if (invalid_path)
    {
        file_path = "root/404.html";
        status_line = "HTTP/1.1 404 Not Found\r\n";
    }
    else if (page_navigation)
    {
        file_path = request_path == "/0" ? "root/register.html" : "root/log.html";
        status_line = "HTTP/1.1 200 OK\r\n";
    }
    else if (request_path == "/logout")
    {
        SessionManager::instance().remove(current_session_id);

        cookie_header =
            "Set-Cookie: session_id=; Path=/; HttpOnly; "
            "SameSite=Lax; Max-Age=0\r\n";

        file_path = "root/login.html";
        status_line = "HTTP/1.1 200 OK\r\n";
    }
    else if (request_path == "/profile")
    {
        if (logged_in)
        {
            file_path = "private/profile.html";
            status_line = "HTTP/1.1 200 OK\r\n";
        }
        else
        {
            file_path = "root/login.html";
            status_line = "HTTP/1.1 403 Forbidden\r\n";
        }
    }
    else if (method == "POST" && request_path == "/register")
    {
        auto result =
            AccountRepository::register_user(username, password);

        if (result == AccountRepository::RegisterResult::Created)
        {
            // 原版注册成功后显示登录表单；旧 /register 接口保留已有成功页。
            file_path = original_account_action ? "root/log.html" : "root/register_success.html";
            status_line = "HTTP/1.1 201 Created\r\n";
        }
        else if (result ==
                 AccountRepository::RegisterResult::UsernameTaken)
        {
            file_path = "root/register_exists.html";
            status_line = "HTTP/1.1 409 Conflict\r\n";
        }
        else
        {
            file_path = "root/500.html";
            status_line = "HTTP/1.1 500 Internal Server Error\r\n";
        }
    }
    else if (method == "POST" && request_path == "/login")
    {
        auto result =
            AccountRepository::verify_user(username, password);

        if (result == AccountRepository::LoginResult::Verified)
        {
            std::string session_id;

            if (SessionManager::instance().create(username, session_id))
            {
                cookie_header =
                    "Set-Cookie: session_id=" + session_id +
                    "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" +
                    std::to_string(SessionManager::lifetime_seconds) +
                    "\r\n";

                file_path = original_account_action ? "root/welcome.html" : "root/login_success.html";
                status_line = "HTTP/1.1 200 OK\r\n";
            }
            else
            {
                file_path = "root/500.html";
                status_line = "HTTP/1.1 500 Internal Server Error\r\n";
            }
        }
        else if (result ==
                 AccountRepository::LoginResult::InvalidCredentials)
        {
            file_path = "root/login_failed.html";
            status_line = "HTTP/1.1 403 Forbidden\r\n";
        }
        else
        {
            file_path = "root/500.html";
            status_line = "HTTP/1.1 500 Internal Server Error\r\n";
        }
    }
    else if (request_path == "/logError.html" || request_path == "/registerError.html")
    {
        file_path = request_path == "/logError.html" ?
            "root/login_failed.html" : "root/register_exists.html";
        status_line = "HTTP/1.1 200 OK\r\n";
    }
    else
    {
        file_path = "root" + request_path;
        status_line = "HTTP/1.1 200 OK\r\n";
    }

    std::ifstream file(file_path, std::ios::binary);

    if (!file.is_open() && status_line == "HTTP/1.1 200 OK\r\n")
    {
        file_path = "root/404.html";
        status_line = "HTTP/1.1 404 Not Found\r\n";

        file.clear();
        file.open(file_path, std::ios::binary);
    }

    if (!file.is_open())
    {
        std::cerr << "Failed to open " << file_path << '\n';
        return false;
    }

    std::ostringstream body_stream;
    body_stream << file.rdbuf();

    std::string body = body_stream.str();
    std::string content_type = get_content_type(file_path);

    std::string response = status_line;
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += cookie_header;

    if (request_path == "/profile" || post_action)
    {
        response += "Cache-Control: no-store\r\n";
    }

    if (method_not_allowed)
    {
        if (post_action)
        {
            response += "Allow: POST\r\n";
        }
        else if (page_navigation)
        {
            response += "Allow: GET, POST\r\n";
        }
        else
        {
            response += "Allow: GET\r\n";
        }
    }

    if (keep_alive)
    {
        response += "Connection: keep-alive\r\n";
    }
    else
    {
        response += "Connection: close\r\n";
    }
    response += "\r\n";
    response += body;

    if (!send_all(response))
    {
        return false;
    }

    Logger::instance().info(
        "HTTP response sent: " +
        status_line.substr(0, status_line.size() - 2));

    return keep_alive;
}
