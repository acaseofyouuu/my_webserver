#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

class SessionManager
{
public:
    static SessionManager &instance();

    static constexpr int lifetime_seconds = 30 * 60;

    bool create(const std::string &username,
                std::string &session_id);

    bool find_user(const std::string &session_id,
                   std::string &username);

    void remove(const std::string &session_id);

private:
    SessionManager() = default;

    struct Session
    {
        std::string username;
        std::chrono::steady_clock::time_point expires_at;
    };

    void remove_expired_locked();

    std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;
};