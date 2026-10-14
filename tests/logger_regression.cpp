#include "log/logger.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <string>

// 在临时目录运行：并发异步日志写满多个分卷，退出时必须完整保留。
int main()
{
    if (!Logger::instance().initialize("rotation/server.log", true, true, 16)) return 1;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([i] { for (int j = 0; j < 50; ++j)
            Logger::instance().info("entry-" + std::to_string(i) + "-" + std::to_string(j)); });
    for (auto &thread : threads) thread.join();
    Logger::instance().shutdown();
    std::size_t count = 0, files = 0;
    for (const auto &entry : std::filesystem::directory_iterator("rotation"))
    {
        ++files;
        std::ifstream stream(entry.path());
        std::string line;
        while (std::getline(stream, line)) ++count;
    }
    if (count != 200 || files != 13) return 2;
    std::cout << "PASS: 200 concurrent log entries, 13 files, complete drain\n";
}
