# 成品与 TinyWebServer 的对应关系

基准为学习者下载的 `TinyWebServer-master`。目标是覆盖它的核心功能，保留自己的代码组织和已完成的扩展。
这不是逐行复制，也不声称实现任意 HTTP 功能。

| 原版功能/机制 | 成品对应实现 | 差异 |
| --- | --- | --- |
| 线程池、任务队列 | `threadpool/thread_pool.*` | std::thread / mutex / condition_variable；有界队列，线程可 join |
| 非阻塞 socket 与 epoll | `WebServer::start/accept_clients/run` | accept4、EPOLLONESHOT，单调连接 ID 防止 fd 复用误处理 |
| LT/ET 四种组合 | `-m 0..3` | 与原版参数含义一致 |
| Reactor | `-a 1`，工作线程 read/process/write | 用 eventfd 通知主线程完成，不忙等共享整数 |
| 模拟 Proactor | `-a 0`，主线程 read/write，工作线程 process | 与原版的线程职责对应；不是真正内核异步 I/O |
| HTTP 状态机 | `HttpConnection::advance_parser` | RequestLine/Headers/Body，保留跨事件缓冲区；业务层再次解析字段 |
| GET/POST、长连接 | `build_response`、`write_ready` | 支持 HTTP/1.0/1.1，保留流水线缓冲；不支持 chunked、Expect:100-continue |
| mmap 与分散写 | `map_file`、`write_ready` | sendmsg+iovec 代替 writev，使用 MSG_NOSIGNAL；记录部分发送位置 |
| 注册、登录、数据库连接池 | `DatabasePool`、`AccountRepository` | 保留 accounts 表、参数化 SQL、密码哈希，未换回原版明文 user 表 |
| 图片、视频、关注页面 | `/5`、`/6`、`/7`，picture/video/fans.html | 使用原版示例 jpg/mp4；资料页链接原版作者项目 |
| 同步/异步日志 | `Logger`，`-l 0/1` | 有界消息队列、专用日志线程，退出排空队列 |
| 日志分卷、开关 | `Logger::sink`、`-c` | 日期变化或单进程累计 500 万行分卷；保留 logs/server.log 当前文件 |
| 定时器 | `WebServer::timers_`、`expire_clients` | multimap 有序队列取代排序链表，steady_clock 取代墙上时间 |
| 信号和退出 | signalfd、SIGINT/SIGTERM | 沿用先前有序退出功能，不复制原版 self-pipe/alarm 实现 |
| 启动参数 | `Config::parse` | 原版 -p/-l/-m/-o/-s/-t/-c/-a 全部可用，保留自己的 -i |
| SO_LINGER | `-o 0/1` | 与原版一样可选；它与应用整体收尾不是同一件事 |
| Webbench 压测 | `test_pressure/webbench-1.5` | 独立保留原工具；兼容现代 Ubuntu，初始化信号掩码并修正退出码，改动见 NOTICE |

## 保留的自己的功能

- 默认 8080、4 个线程、4 条数据库连接、10 秒空闲超时。
- 注册密码长度规则，PBKDF2-SHA256 随机盐与 600000 次迭代。
- session_id Cookie，HttpOnly、SameSite=Lax、30 分钟会话；`/profile` 与 `/logout`。
- 旧 `/register`、`/login` 接口接受 username/password；原版 `/3CGISQL.cgi`、`/2CGISQL.cgi` 接受 user/password。
- 原版入口注册成功后显示登录表单，登录成功后显示选择页；旧入口保留原来的成功页。
- 更明确的状态码：201 注册成功、409 重名、403 登录失败，而非原版统一用 200 返回业务页面。

## 范围与边界

这是学习用途的 HTTP/1.x 服务器。和原版一样没有 TLS/HTTP2，也没有完整实现 HTTP 标准。
视频返回完整文件，不提供 Range/206 断点拖动；不支持 chunked 请求、WebSocket、文件上传。
单次接收缓冲区上限 16 KiB、请求正文上限 8 KiB；重复请求头保守地按错误处理。
会话重启失效；静态文件在发送期间应保持不变，避免 mmap 的映射文件被外部截断。
目录检查防止常见越界路径与软链接访问，但不针对恶意并发改动本机资源目录设计。

测试数据是当前 WSL 环境的结果，不能把上游 README 中的 QPS 当作本项目的性能。
