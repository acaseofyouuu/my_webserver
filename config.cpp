#include "config.h"

#include <exception>
#include <iostream>
#include <string>
#include <unistd.h>

bool Config::parse(int argc, char *argv[])
{
    int option;

    while ((option = getopt(argc, argv, "p:t:i:")) != -1)
    {
        try
        {
            switch (option)
            {
            case 'p':
                port = std::stoi(optarg);

                if (port < 1 || port > 65535)
                {
                    std::cerr << "Port must be between 1 and 65535\n";
                    return false;
                }

                break;

            case 't':
            {
                int value = std::stoi(optarg);

                if (value < 1)
                {
                    std::cerr << "Thread count must be positive\n";
                    return false;
                }

                thread_count = static_cast<std::size_t>(value);
                break;
            }

            case 'i':
                idle_timeout_seconds = std::stoi(optarg);

                if (idle_timeout_seconds < 1)
                {
                    std::cerr << "Idle timeout must be positive\n";
                    return false;
                }

                break;

            default:
                std::cerr
                    << "Usage: " << argv[0]
                    << " [-p port] [-t threads] [-i idle_seconds]\n";
                return false;
            }
        }
        catch (const std::exception &)
        {
            std::cerr << "Configuration value must be a valid number\n";
            return false;
        }
    }

    if (optind < argc)
    {
        std::cerr << "Unexpected argument: " << argv[optind] << '\n';
        return false;
    }

    return true;
}