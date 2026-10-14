#include "http_conn.h"
#include "../log/logger.h"
#include "../database/account_repository.h"
#include "../session/session_manager.h"

#include <sstream>
#include <string>
#include <unistd.h>
#include <filesystem>
#include <cerrno>
#include <sys/socket.h>
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

        if (extension == ".mp4") return "video/mp4";
        if (extension == ".avi") return "video/x-msvideo";
        return "application/octet-stream";
    }
}

#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
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

    for (const std::string prefix : {"http://", "https://"})
    {
        if (request_path.compare(0, prefix.size(), prefix) == 0)
        {
            auto slash = request_path.find('/', prefix.size());
            request_path = slash == std::string::npos ? "/" : request_path.substr(slash);
        }
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

        while (!header_value.empty() && (header_value.back() == ' ' || header_value.back() == '\t'))
            header_value.pop_back();
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

    std::istringstream tokens(connection_value);
    std::string token;
    bool explicit_keep_alive = false;
    while (std::getline(tokens, token, ','))
    {
        const auto first = token.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        token = token.substr(first, token.find_last_not_of(" \t") - first + 1);
        if (token == "close") return false;
        if (token == "keep-alive") explicit_keep_alive = true;
    }
    return http_version == "HTTP/1.1" || explicit_keep_alive;
}

HttpConnection::HttpConnection(int client_fd) : client_fd_(client_fd) {}
HttpConnection::~HttpConnection()
{
    release_file();
    if (client_fd_ >= 0) close(client_fd_);
}
void HttpConnection::release_file()
{
    if (mapped_file_) munmap(mapped_file_, file_size_);
    mapped_file_ = nullptr;
    file_size_ = 0;
}
bool HttpConnection::map_file(const std::string &path)
{
    release_file();
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0)
    { close(fd); return false; }
    file_size_ = static_cast<std::size_t>(st.st_size);
    if (file_size_ != 0)
    {
        mapped_file_ = mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped_file_ == MAP_FAILED)
        { mapped_file_ = nullptr; file_size_ = 0; close(fd); return false; }
    }
    close(fd);
    return true;
}
void HttpConnection::reset_parser()
{
    parse_state_ = ParseState::RequestLine;
    parse_cursor_ = frame_size_ = body_size_ = 0;
    frame_headers_.clear();
    invalid_frame_ = false;
}
bool HttpConnection::reject_request()
{
    invalid_frame_ = true;
    frame_size_ = request_buffer_.size();
    return true;
}
bool HttpConnection::advance_parser()
{
    constexpr std::size_t limit = 16 * 1024;
    if (invalid_frame_ || frame_size_) return true;
    while (parse_state_ != ParseState::Body)
    {
        auto end = request_buffer_.find("\r\n", parse_cursor_);
        if (end == std::string::npos)
        {
            auto lf = request_buffer_.find('\n', parse_cursor_);
            if (lf != std::string::npos || request_buffer_.size() >= limit) return reject_request();
            return false;
        }
        const std::string line = request_buffer_.substr(parse_cursor_, end - parse_cursor_);
        parse_cursor_ = end + 2;
        if (parse_state_ == ParseState::RequestLine)
        {
            std::string method, path, version;
            if (!parse_request_line(line + "\r\n", method, path, version) ||
                (version != "HTTP/1.0" && version != "HTTP/1.1")) return reject_request();
            parse_state_ = ParseState::Headers;
        }
        else if (line.empty())
        {
            if (frame_headers_.count("transfer-encoding") || frame_headers_.count("expect"))
                return reject_request();
            auto length = frame_headers_.find("content-length");
            if (length != frame_headers_.end())
            {
                const auto &value = length->second;
                auto result = std::from_chars(value.data(), value.data() + value.size(), body_size_);
                if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                    body_size_ > 8192) return reject_request();
            }
            if (parse_cursor_ + body_size_ > limit) return reject_request();
            parse_state_ = ParseState::Body;
        }
        else
        {
            auto colon = line.find(':');
            if (colon == std::string::npos || colon == 0) return reject_request();
            std::string key = line.substr(0, colon);
            for (char &ch : key)
            {
                if (!std::isalnum(static_cast<unsigned char>(ch)) &&
                    std::string("!#$%&'*+-.^_`|~").find(ch) == std::string::npos) return reject_request();
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            std::string value = line.substr(colon + 1);
            for (unsigned char ch : value)
                if ((ch < 32 && ch != '\t') || ch == 127) return reject_request();
            const auto begin = value.find_first_not_of(" \t");
            value = begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
            if (!frame_headers_.emplace(key, value).second) return reject_request();
        }
    }
    if (request_buffer_.size() < parse_cursor_ + body_size_) return false;
    frame_size_ = parse_cursor_ + body_size_;
    return true;
}
HttpConnection::Next HttpConnection::read_ready(bool edge_triggered)
{
    if (advance_parser()) return Next::Process;
    char data[4096];
    do
    {
        auto space = 16 * 1024 - request_buffer_.size();
        if (space == 0) { reject_request(); return Next::Process; }
        auto size = recv(client_fd_, data, std::min(space, sizeof(data)), 0);
        if (size > 0)
        {
            request_buffer_.append(data, static_cast<std::size_t>(size));
            if (advance_parser()) return Next::Process;
            if (!edge_triggered) return Next::Read;
        }
        else if (size == 0)
        {
            peer_closed_ = true;
            if (request_buffer_.empty()) return Next::Close;
            reject_request();
            return Next::Process;
        }
        else if (errno == EINTR) continue;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) return Next::Read;
        else return Next::Close;
    } while (true);
}
HttpConnection::Next HttpConnection::process()
{
    if (!advance_parser()) return Next::Read;
    std::string request = invalid_frame_ ? "INVALID\r\n\r\n" : request_buffer_.substr(0, frame_size_);
    request_buffer_.erase(0, frame_size_);
    build_response(request);
    reset_parser();
    return Next::Write;
}
HttpConnection::Next HttpConnection::write_ready()
{
    const std::size_t body_size = mapped_file_ ? file_size_ : fallback_body_.size();
    const char *body = mapped_file_ ? static_cast<const char *>(mapped_file_) : fallback_body_.data();
    const std::size_t total = response_header_.size() + body_size;
    std::size_t budget = 256 * 1024;
    while (sent_ < total && budget)
    {
        iovec vectors[2]{};
        int count = 0;
        if (sent_ < response_header_.size())
        {
            vectors[count++] = {response_header_.data() + sent_, std::min(response_header_.size() - sent_, budget)};
            auto remaining = budget - vectors[0].iov_len;
            if (remaining && body_size) vectors[count++] = {const_cast<char *>(body), std::min(body_size, remaining)};
        }
        else
        {
            auto offset = sent_ - response_header_.size();
            vectors[count++] = {const_cast<char *>(body + offset), std::min(body_size - offset, budget)};
        }
        // iovec 分散写；MSG_NOSIGNAL 使对端断连不会杀死整个进程。
        msghdr message{};
        message.msg_iov = vectors;
        message.msg_iovlen = count;
        auto size = sendmsg(client_fd_, &message, MSG_NOSIGNAL);
        if (size > 0) { sent_ += size; budget -= size; }
        else if (size < 0 && errno == EINTR) continue;
        else if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return Next::Write;
        else return Next::Close;
    }
    if (sent_ < total) return Next::Write;
    release_file();
    response_header_.clear();
    fallback_body_.clear();
    sent_ = 0;
    if (!keep_alive_) return Next::Close;
    return advance_parser() ? Next::Process : Next::Read;
}

void HttpConnection::build_response(const std::string &request)
{
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
                        (headers.find("host") == headers.end() || headers["host"].empty());

    bool malformed_request = invalid_frame_ || !request_line_valid ||
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
                const std::size_t body_start =
                    request.find("\r\n\r\n") + 4;

                request_body = request.substr(body_start, body_length);
            }
        }
    }

    // 查询参数不是文件路径；路径解码后再校验。
    request_path = request_path.substr(0, request_path.find('?'));
    std::string decoded_path, encoded_path;
    for (char ch : request_path) encoded_path += ch == '+' ? "%2B" : std::string(1, ch);
    if (!decode_form_component(encoded_path, decoded_path)) malformed_request = true;
    else request_path = decoded_path;
    if (request_path.empty() || request_path.find_first_of("\r\n") != std::string::npos ||
        request_path.find('\0') != std::string::npos) malformed_request = true;
    if (!malformed_request)
        Logger::instance().info("HTTP request: " + method + " " + request_path);

    // 原版的表单地址复用已有账号逻辑，不另建一套账号或会话。
    const bool original_account_action =
        request_path == "/2" || request_path == "/2CGISQL.cgi" ||
        request_path == "/3" || request_path == "/3CGISQL.cgi";
    const bool page_navigation = request_path == "/0" || request_path == "/1" ||
        request_path == "/5" || request_path == "/6" || request_path == "/7";

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
        !malformed_request && !peer_closed_ &&
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
        const std::unordered_map<std::string, std::string> pages{
            {"/0", "root/register.html"}, {"/1", "root/log.html"},
            {"/5", "root/picture.html"}, {"/6", "root/video.html"}, {"/7", "root/fans.html"}};
        file_path = pages.at(request_path);
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

    if (file_path.compare(0, 5, "root/") == 0)
    {
        std::error_code error;
        auto root = std::filesystem::canonical("root", error);
        auto target = std::filesystem::weakly_canonical(file_path, error);
        auto relative = target.lexically_relative(root);
        if (error || relative.empty() || *relative.begin() == "..")
        { file_path = "root/404.html"; status_line = "HTTP/1.1 404 Not Found\r\n"; }
    }
    fallback_body_.clear();
    bool opened = map_file(file_path);
    if (!opened && status_line == "HTTP/1.1 200 OK\r\n")
    {
        file_path = "root/404.html";
        status_line = "HTTP/1.1 404 Not Found\r\n";
        opened = map_file(file_path);
    }
    if (!opened)
    {
        status_line = "HTTP/1.1 500 Internal Server Error\r\n";
        fallback_body_ = "Internal Server Error\n";
        keep_alive = false;
    }
    auto body_size = opened ? file_size_ : fallback_body_.size();
    response_header_ = status_line;
    response_header_ += "Content-Type: " + get_content_type(file_path) + "\r\n";
    response_header_ += "Content-Length: " + std::to_string(body_size) + "\r\n";
    response_header_ += cookie_header;
    if (request_path == "/profile" || post_action) response_header_ += "Cache-Control: no-store\r\n";
    if (method_not_allowed)
        response_header_ += post_action ? "Allow: POST\r\n" :
            (page_navigation ? "Allow: GET, POST\r\n" : "Allow: GET\r\n");
    response_header_ += keep_alive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    keep_alive_ = keep_alive;
    sent_ = 0;
    Logger::instance().info("HTTP response prepared: " + status_line.substr(0, status_line.size() - 2));
}
