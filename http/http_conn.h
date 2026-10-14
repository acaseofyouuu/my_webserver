#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

class HttpConnection
{
public:
    explicit HttpConnection(int client_fd);

    enum class Next { Read, Process, Write, Close };
    ~HttpConnection();
    HttpConnection(const HttpConnection &) = delete;
    HttpConnection &operator=(const HttpConnection &) = delete;
    Next read_ready(bool edge_triggered);
    Next process();
    Next write_ready();
    int fd() const { return client_fd_; }

private:
    int client_fd_;
    std::string request_buffer_;
    enum class ParseState { RequestLine, Headers, Body };
    bool advance_parser();
    bool reject_request();
    void reset_parser();
    void build_response(const std::string &request);
    void release_file();
    bool map_file(const std::string &path);
    ParseState parse_state_ = ParseState::RequestLine;
    std::size_t parse_cursor_ = 0, frame_size_ = 0, body_size_ = 0;
    std::unordered_map<std::string, std::string> frame_headers_;
    bool invalid_frame_ = false, peer_closed_ = false, keep_alive_ = false;
    std::string response_header_, fallback_body_;
    void *mapped_file_ = nullptr;
    std::size_t file_size_ = 0, sent_ = 0;
    bool parse_request_line(const std::string &request,
                            std::string &method,
                            std::string &request_path,
                            std::string &http_version);

    bool parse_headers(
        const std::string &request,
        std::unordered_map<std::string, std::string> &headers);

    bool should_keep_alive(
        const std::string &http_version,
        const std::unordered_map<std::string, std::string> &headers);
        
};
