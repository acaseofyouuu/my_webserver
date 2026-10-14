#include "session_manager.h"

#include <array>
#include <openssl/rand.h>

SessionManager &SessionManager::instance()
{
    static SessionManager manager;
    return manager;
}

bool SessionManager::create(const std::string &username,
                            std::string &session_id)
{
    session_id.clear();

    std::array<unsigned char, 32> random_bytes{};

    if (RAND_bytes(random_bytes.data(),
                   static_cast<int>(random_bytes.size())) != 1)
    {
        return false;
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);

    for (unsigned char byte : random_bytes)
    {
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 0x0f]);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    remove_expired_locked();

    if (sessions_.size() >= 10000)
    {
        return false;
    }

    Session session{
        username,
        std::chrono::steady_clock::now() +
            std::chrono::seconds(lifetime_seconds)};

    auto result = sessions_.emplace(token, session);

    if (!result.second)
    {
        return false;
    }

    session_id = token;
    return true;
}

void SessionManager::remove_expired_locked()
{
    const auto now = std::chrono::steady_clock::now();

    for (auto it = sessions_.begin(); it != sessions_.end();)
    {
        if (it->second.expires_at <= now)
        {
            it = sessions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool SessionManager::find_user(const std::string &session_id,
                               std::string &username)
{
    username.clear();

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);

    if (it == sessions_.end())
    {
        return false;
    }

    if (it->second.expires_at <= std::chrono::steady_clock::now())
    {
        sessions_.erase(it);
        return false;
    }

    username = it->second.username;
    return true;
}

void SessionManager::remove(const std::string &session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    sessions_.erase(session_id);
}