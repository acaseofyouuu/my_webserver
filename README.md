# my_webserver

Learning and Replicating a C++ Web Server Based on TinyWebServer

## 学习记录

- 已完成 Git 安装、仓库克隆和本地项目设置。

## 当前学习进度

这是一个正在学习和复刻的项目，尚未完成 TinyWebServer 的全部功能。
对照源码为 qinguoyi/TinyWebServer 的本地 `TinyWebServer-master` 版本。

已经实现：HTTP 请求与响应、静态文件及 CSS、错误页面、长连接、
epoll 事件监听、线程池、空闲连接回收、启动参数、同步日志、
MySQL 连接池、注册登录、密码哈希、会话与个人页面、退出登录、停止时收尾。

2026-10-14 按**当前进度**对齐原版的注册登录入口与页面流程。
未提前实现图片视频演示、异步日志、非阻塞 I/O、LT/ET 切换、
Reactor/模拟 Proactor 切换或完整 HTTP 状态机。
已有 epoll 和线程池不表示已经实现了这些进阶功能。

详细对应关系、修改范围和后续学习边界见 [当前进度对照](docs/current-progress-alignment.md)。

## 本地运行（Ubuntu / WSL）

在项目目录编译：

```sh
make
```

沿用已有 MySQL 配置：本机 `qgydb` 数据库、`webserver` 用户。
账号表结构在 `database/schema.sql`，数据库连接密码仍由环境变量传入。
若当前 Ubuntu 终端还没有设置密码：

```sh
read -s -p 'MySQL webserver 用户的密码: ' WEBSERVER_DB_PASSWORD
echo
export WEBSERVER_DB_PASSWORD
./server
```

浏览器打开 `http://127.0.0.1:8080/`，可选择“新用户”或“已有账号”。
这仍然使用自己已经创建的账号；无须重新初始化数据库。
最初的 Hello 页面保留在 `/index.html`，以前的 `/login.html` 仍可使用。

已有启动参数仍为 `-p` 端口、`-t` 线程数、`-i` 空闲秒数、`-s` 数据库连接数。
默认值保持 8080、4、10、4，以保留当前的使用习惯。
日志在 `logs/server.log`，按 Ctrl+C 停止。

## 当前阶段回归验证

```sh
sudo python3 tests/current_progress.py
```

此测试要求本机 MySQL root 可以通过 Unix socket 登录。它编译一个测试入口，
复用真实的 HTTP、WebServer、线程池、日志、账号和会话代码，
在临时目录和随机端口运行；创建名称以 `webserver_alignment_` 开头的独立数据库，
结束时清理。不会修改 `qgydb`、现有账号或 8080 上的服务。
测试入口只将数据库换为测试库、空闲超时设为 1 秒；正式程序仍由 `make` 单独验证编译。
