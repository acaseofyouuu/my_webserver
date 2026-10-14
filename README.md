# my_webserver

基于 qinguoyi/TinyWebServer 学习复刻的 Linux C++17 Web 服务器。
在之前亲手实现的注册登录、密码哈希、会话和个人页面基础上，完成原版的主要功能。
代码沿用自己的类和模块，不是原版逐行拷贝。

**先运行，再跟着请求读代码：** [启动指南](docs/run-guide.md) · [学习路线](docs/learning-guide.md) ·
[原版功能对照](docs/feature-parity.md) · [验证记录](docs/validation.md)。

## 已完成

- 非阻塞 socket、epoll、EPOLLONESHOT，监听/连接 socket 的四种 LT/ET 组合。
- 线程池；Reactor 和模拟 Proactor 两种事件处理模式。
- HTTP 请求行/请求头/正文状态机，GET/POST、错误响应、keep-alive、连续请求。
- 静态网页、CSS、图片、视频；mmap 映射文件，iovec 分散发送及部分发送续传。
- MySQL 连接池、参数化 SQL、注册、登录、重复用户名处理。
- 同步/异步日志、按日期/行数分卷、关闭日志开关。
- 有序超时队列回收空闲及不完整请求连接，SIGINT/SIGTERM 收尾退出。
- 保留自己的扩展：PBKDF2-SHA256 密码哈希、Cookie 会话、个人页面、退出登录。
- 原版 Webbench 工具，以及隔离数据库的自动回归和 10500 连接测试。

## 现在怎么运行

在 **Ubuntu / WSL** 中进入项目目录：

```sh
cd /mnt/c/Users/17856/Documents/my_webserver
make
read -s -p 'MySQL webserver 用户密码: ' WEBSERVER_DB_PASSWORD
echo
export WEBSERVER_DB_PASSWORD
./server
```

现有数据库 `qgydb` 和 `accounts` 表继续使用，不需要删除或重新建账号。
浏览器打开 `http://127.0.0.1:8080/`，选择新用户或已有账号。
登录后可以查看图片、播放视频、进入个人页面。按 Ctrl+C 停止。

## 模式选择

```sh
./server -p 8080 -t 4 -s 4 -i 10 -l 1 -m 3 -a 1
./server -h
```

| 参数 | 含义 | 默认 |
| --- | --- | --- |
| `-p` | 监听端口 | 8080 |
| `-t` | 工作线程数 | 4 |
| `-s` | 数据库连接数 | 4 |
| `-i` | 连接空闲超时（秒） | 10 |
| `-l` | 0 同步日志，1 异步日志 | 0 |
| `-m` | 0 LT/LT，1 LT/ET，2 ET/LT，3 ET/ET；前者为监听 socket | 0 |
| `-a` | 0 模拟 Proactor，1 Reactor | 0 |
| `-o` | 0 关闭 SO_LINGER，1 开启并设置 1 秒 | 0 |
| `-c` | 0 打开日志，1 关闭日志 | 0 |

数据库还可以通过 `WEBSERVER_DB_HOST`、`WEBSERVER_DB_USER`、`WEBSERVER_DB_NAME`、
`WEBSERVER_DB_PORT` 配置。默认 localhost / webserver / qgydb / 3306。
密码只通过 `WEBSERVER_DB_PASSWORD` 提供。

## 验证

```sh
sudo python3 tests/current_progress.py
sudo python3 tests/full_regression.py --stress
```

测试使用本机 MySQL root 的 Unix socket 登录，新建随机命名的临时数据库、随机端口和临时资源目录；
不访问已有账号，不占用 8080，结束后清理。完整的测试范围及限制见验证记录。

## 项目来源与保留进度

参考项目：[qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer)。
本项目仍采用 Apache-2.0，独立 Webbench 工具保留 GPL-2.0 许可证，示例媒体来源见 [NOTICE](NOTICE)。
自动完善前的学习进度提交为 `dc8ff6c`；已有 Git 历史保留，完成前另有源码 ZIP 备份。
历史上的阶段对照保存在 `docs/current-progress-alignment.md`。
