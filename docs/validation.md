# 成品验证记录

日期：2026-10-14。环境：用户本机 Windows + WSL Ubuntu，g++ 15，MySQL 8.4。
测试使用随机临时数据库、临时运行目录及随机本地端口，未修改用户 qgydb 数据。

## 功能与资源检查

| 验证 | 结果 |
| --- | --- |
| C++17、Wall、Wextra 编译 | 通过 |
| 既有功能回归 `tests/current_progress.py` | 41 项通过 |
| 完整模式矩阵和连接压力 `tests/full_regression.py --stress` | 261 项通过 |
| AddressSanitizer + UndefinedBehaviorSanitizer 模式矩阵 | 259 项通过（不含两项大连接压力检查） |
| 日志专项 | 4 个线程共写 200 条，16 条分卷，13 个文件共 200 条，退出无丢失 |

模式矩阵覆盖 `-a 0/1` × `-m 0/1/2/3`，交替开启同步/异步日志及 SO_LINGER。
覆盖半条请求、慢读写、流水线、重复 Content-Length、目录越界、软链接、空文件、
图片/视频 SHA-256 一致性、账号注册登录、个人页权限、退出、并发请求、对端半关闭和信号退出。
另验证禁用日志不写文件、不完整请求被超时回收。

## 连接数量与吞吐量分开记录

- 连接测试：同时打开并保留 **10500 条 TCP 连接**，每条请求一次 index.html，全部得到 200。
  所有响应收到之后才关闭连接。不是串行连接 10500 次；也不是 10500 个用户同时做密码哈希。
- Webbench 示例：模拟 Proactor、ET/ET、关闭日志、100 个客户端、5 秒，
  本次输出为 **144080 次成功、0 次失败，1728960 pages/min**。
  测试路径为小型 `/index.html`，数据不能代表大视频或登录的吞吐量，也不能直接与上游机器比较。
- Webbench 的读取成功并不校验 HTTP 状态码和正文；完整性由单独的回归脚本验证。

## 复现命令

在 Ubuntu 的项目目录运行：

```sh
make
sudo python3 tests/current_progress.py
sudo python3 tests/full_regression.py --stress
```

压力脚本将测试进程和它的服务进程的文件描述符上限提高到至少 40000（受系统硬上限约束）。
普通手动运行如需上万连接，也必须检查 `ulimit -n`。

内存检查：

```sh
make TARGET=server-sanitize CXXFLAGS='-std=c++17 -O1 -g -Wall -Wextra -pthread -fsanitize=address,undefined -fno-omit-frame-pointer'
sudo env WEBSERVER_TEST_BINARY="$PWD/server-sanitize" ASAN_OPTIONS=detect_leaks=1 python3 tests/full_regression.py
```

日志专项（使用临时目录，不动现有 logs）：

```sh
g++ -std=c++17 -Wall -Wextra -pthread -I. tests/logger_regression.cpp log/logger.cpp -o /tmp/webserver-logger-test
test_dir=$(mktemp -d)
(cd "$test_dir" && /tmp/webserver-logger-test)
```

Webbench（先在另一个终端启动自己的服务器）：

```sh
make -C test_pressure/webbench-1.5 webbench
test_pressure/webbench-1.5/webbench -c 100 -t 5 http://127.0.0.1:8080/index.html
```

本轮没有进行长期压力运行、跨公网环境、TLS、磁盘故障或 ThreadSanitizer 检查。
未把用户的真实账号密码用于自动测试。视频验证了完整传输和页面资源路径，未自动操控浏览器验证所有编解码器。
