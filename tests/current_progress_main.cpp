// 测试入口：只把数据库换成临时测试库，复用项目的真实服务实现。
#include "webserver.h"
#include "database/database_pool.h"
#include "log/logger.h"
#include <csignal>
#include <pthread.h>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) return 1;
    if (!Logger::instance().initialize("logs/server.log")) return 1;
    if (!DatabasePool::instance().initialize(
            "localhost", "root", "", argv[1], 3306, 3)) return 1;
    WebServer server(std::stoi(argv[2]), 4, 1);
    if (!server.start()) return 1;
    server.run();
}
