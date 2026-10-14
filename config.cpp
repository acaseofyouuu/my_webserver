#include "config.h"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {
int number(const char *text, int low, int high)
{
    std::string value(text);
    int result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result < low || result > high)
        throw std::invalid_argument("Invalid numeric option: " + value);
    return result;
}
}
bool Config::parse(int argc, char *argv[])
{
    try
    {
        if (const char *s = std::getenv("WEBSERVER_DB_HOST")) database_host = s;
        if (const char *s = std::getenv("WEBSERVER_DB_USER")) database_user = s;
        if (const char *s = std::getenv("WEBSERVER_DB_NAME")) database_name = s;
        if (const char *s = std::getenv("WEBSERVER_DB_PORT")) database_port = number(s, 1, 65535);
        int option;
        while ((option = getopt(argc, argv, "hp:t:i:s:l:m:o:c:a:")) != -1)
        {
            switch (option)
            {
            case 'p': port = number(optarg, 1, 65535); break;
            case 't': thread_count = number(optarg, 1, 256); break;
            case 'i': idle_timeout_seconds = number(optarg, 1, 86400); break;
            case 's': database_pool_size = number(optarg, 1, 256); break;
            case 'l': log_mode = number(optarg, 0, 1); break;
            case 'm': trigger_mode = number(optarg, 0, 3); break;
            case 'o': linger_mode = number(optarg, 0, 1); break;
            case 'c': close_log = number(optarg, 0, 1); break;
            case 'a': actor_model = number(optarg, 0, 1); break;
            case 'h': help = true; break;
            default: return false;
            }
        }
        if (optind != argc) throw std::invalid_argument("Unexpected positional argument");
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return false; }
    if (help)
        std::cout << "Usage: " << argv[0] << " [-p port] [-t threads] [-s db_connections] [-i idle_seconds]\n"
            "  -l 0|1   synchronous/asynchronous log\n"
            "  -m 0..3  LT/LT, LT/ET, ET/LT, ET/ET\n"
            "  -o 0|1   SO_LINGER disabled/enabled (1 second)\n"
            "  -c 0|1   log enabled/disabled\n"
            "  -a 0|1   simulated Proactor/Reactor\n";
    return true;
}
