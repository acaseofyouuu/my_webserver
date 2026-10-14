# 用一个请求学会整个项目

先读一条最简单的路线：浏览器请求 `/index.html`，服务器返回文件。
暂时不要把所有 C++ 文件从头到尾背下来。每一节都可以拿来和助手逐步讨论、运行、观察。

## 1. 程序怎样开始运行

文件：`main.cpp`，函数 `main → run_application`。

先解析参数，配置停止信号，再初始化日志和数据库连接池，最后构造、启动 WebServer。
`server.run()` 是长期运行的事件循环。它返回后，局部对象析构，释放资源。

亲手验证：`./server -h` 不需要数据库就能显示帮助；`./server -p 8080x` 会拒绝非法数字。
需要弄懂：main 的 argc/argv 是什么，为什么要检查返回值，对象析构发生在什么时候。

## 2. 浏览器怎样连进来

文件：`webserver.cpp`，函数 `start`、`accept_clients`。

`socket` 创建通信入口，`bind` 绑定本机端口，`listen` 等待连接。
监听 socket 可读时，`accept4` 拿到一个代表具体客户端的新 fd。
这两个 fd 分工不同：监听 fd 继续接人，连接 fd 专门和这个客户端收发数据。

每个客户端对应一个 HttpConnection 对象。对象跨事件保留，避免“收到半条请求就丢了”的问题。
主线程给连接一个递增 ID；即使系统复用 fd，旧事件也不会冒充新的连接。

## 3. 为什么不一直卡在 recv

文件：`HttpConnection::read_ready`、`WebServer::run`。

socket 为非阻塞。recv 暂时无数据时返回 EAGAIN，代码保存缓冲区并返回 Next::Read。
WebServer 重新注册 EPOLLIN，等 epoll 以后报告数据到了，再继续读取。
这不是“接收失败”，只是“还没轮到下一批数据”。

LT：数据仍可读时可以继续获得通知。ET：就绪状态变化时通知，通常读到 EAGAIN。
本项目同时使用 ONESHOT，每次交接后重新注册关注事件，避免多个线程同时处理同一连接。
可以对照测试里的 slow reader：12 个客户端只发半条请求，其他完整请求仍可成功。

## 4. 收到一堆字符，怎么知道它是什么

文件：`http/http_conn.cpp`，函数 `advance_parser`。

HTTP 请求分为请求行、请求头、正文。状态机记录“目前读到哪一部分”。
`parse_cursor_` 是已经检查的位置，`body_size_` 来自 Content-Length，`frame_size_` 是完整请求长度。
数据不够就保留状态，下一次追加后继续检查；够了返回 Next::Process。

请求行示例：`GET /index.html HTTP/1.1`。方法是 GET，路径是 /index.html，版本是 HTTP/1.1。
头部的 Host 和 Connection 不属于网页内容。POST 的账号字段在正文里。

## 5. 谁负责真正处理请求

文件：`WebServer::dispatch`、`ThreadPool::worker_loop`、`HttpConnection::process`。

任务放进线程池的队列，一个工作线程取出任务执行。
模拟 Proactor 模式下，主线程先收齐请求，工作线程做解析后的业务和准备响应。
Reactor 模式下，工作线程也执行就绪连接的收发。

工作线程完成后，把结果放入完成队列，通过 eventfd 叫醒主线程。
主线程据此决定继续等读、等写还是关闭连接。连接表、定时器、epoll 注册只由主线程维护。
需要弄懂：为什么需要 mutex，为什么完成队列与客户端 socket 不是同一个东西。

## 6. 网页是怎样返回的

文件：`build_response`、`map_file`、`write_ready`。

普通路径对应 root 下的资源；不存在时选择 404 页面。
响应先有 `HTTP/1.1 200 OK` 等状态行，再有 Content-Type 和 Content-Length，然后才是网页内容。
map_file 将文件映射到内存；iovec 把响应头与文件内容一起交给内核发送。

一次发送不保证发完。`sent_` 记录已发字节，EAGAIN 时返回 Next::Write，之后从原位置续发。
发送完成后，长连接继续等下一个请求；短连接则关闭。
亲手验证：`curl -i http://127.0.0.1:8080/index.html`，区分响应头和 HTML 正文。

## 7. 注册、登录怎样接入这条路线

文件：`build_response`、`database/account_repository.cpp`、`database/password_hash.cpp`、`session/session_manager.cpp`。

注册：解析表单 → 校验字段 → 密码加盐哈希 → 从池中借数据库连接 → 参数化 INSERT → 归还连接 → 返回页面。
登录：按用户名 SELECT 哈希 → 归还连接 → 验证密码 → 创建随机 session_id → 响应 Set-Cookie。
个人页：浏览器带 Cookie → 服务器查会话 → 有效才读 private/profile.html。
退出：服务器删除会话，同时告诉浏览器清除 Cookie。

这里数据库保存账号，会话表保存“目前谁已经登录”；不是把数据库密码交给浏览器。
DatabaseConnectionGuard 的析构自动归还连接，是 RAII 的实际应用。

## 8. 服务器怎样长期运行和退出

日志线程负责异步写文件；定时器队列负责找出超时连接；线程池处理任务。
空闲超时与会话有效期完全不同：前者回收 TCP 连接，后者决定是否还算登录。
SIGINT 来自 Ctrl+C，signalfd 把信号转换成 epoll 能处理的事件。
退出时不再接收新连接，收尾响应、等待任务、释放连接，最后排空日志。

建议每次只选一节：先画调用顺序，再用一条请求验证，最后解释涉及的一个系统调用。
能解释“为什么这样分工、失败了怎么办”，比背下来某一行更重要。
