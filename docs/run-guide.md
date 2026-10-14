# 从启动到看到网页

## 1. 终端和目录

编译、启动、查看日志均在 Ubuntu 终端操作。
Windows PowerShell 仍可用于 Git 上传；两者访问的是同一个项目目录。

```sh
cd /mnt/c/Users/17856/Documents/my_webserver
```

程序目前从当前目录读取 `root/`、`private/`，所以必须先进入这里再运行。
先前运行的旧程序需要在它的终端按 Ctrl+C 停止，再启动新版。

## 2. 环境

已有环境无需重复安装。新电脑需要 Ubuntu/WSL、g++、make、MySQL、OpenSSL 开发库：

```sh
sudo apt update
sudo apt install build-essential mysql-server libmysqlclient-dev libssl-dev python3
sudo systemctl start mysql
```

编译：`make` 或 `sh build.sh`。修改了 C++ 文件后重新运行 `make`，再重启程序。

## 3. 数据库

当前学习环境已经有 `qgydb`、`webserver` 数据库用户和 `accounts` 表，继续使用即可。
服务器连接数据库的密码，与网页上某个账号的密码是两回事。

新环境需要先创建数据库和数据库用户，再导入 `database/schema.sql`。
已配置数据库用户时，可以这样创建缺失的账号表（不会清空已有表）：

```sh
mysql -u webserver -p qgydb < database/schema.sql
```

默认数据库参数写在 `config.h`；可以通过环境变量覆盖而不改代码。
不要将真实密码写入源码或提交进 Git。

## 4. 启动

```sh
read -s -p 'MySQL webserver 用户密码: ' WEBSERVER_DB_PASSWORD
echo
export WEBSERVER_DB_PASSWORD
./server
```

密码输入时不回显；新开 Ubuntu 终端后通常需要重新设置。
打开浏览器访问 `http://127.0.0.1:8080/`，同一次会话统一使用这个地址，
不要在 `localhost` 和 `127.0.0.1` 之间切换，否则浏览器使用不同的 Cookie 域。

## 5. 检查功能

1. 首页“新用户”进入注册页，注册后显示登录表单。密码 8～128 字节。
2. 同名注册应被拒绝；错误密码显示登录失败；正确密码进入选择页面。
3. 选择页可查看图片、播放视频、进入个人页面。
4. 退出登录后重新访问 `/profile` 应要求登录。`/welcome.html` 本身是公开导航页。
5. `/index.html`、`/about.html`、`/contact.html` 保留以前的学习网页。
6. 不存在的文件返回 404；不支持的方法返回 405；不合法的请求返回 400。

日志：`tail -f logs/server.log`；这是另一个终端里的查看命令，按 Ctrl+C 只停止查看。
日志分卷留在 logs 下，不会自动删除旧卷。关闭日志 `-c 1` 后没有日志输出是预期行为。

## 6. 停止和常见问题

- 启动终端按 Ctrl+C：停止接受新连接，尝试完成正在发送的响应，最多给网络收尾 5 秒，
  等工作线程任务结束后关闭连接；数据库读写设置了 5 秒超时，异常任务可能让总退出耗时更长。
- 空闲连接超时不会关掉浏览器页面。浏览器下一次请求可以重新连接。
- `WEBSERVER_DB_PASSWORD is not set`：在当前 Ubuntu 终端重新设置密码变量。
- `Listen failed: Address already in use`：旧服务仍占用端口。先停止自己的旧进程，或改用 `-p 8081`。
- MySQL `Access denied`：先用 `mysql -u webserver -p qgydb` 验证数据库密码。
- 注册返回 500：查看日志并检查 `accounts` 表是否存在。不要删除数据库来尝试修复。

会话保存在内存中，重启服务器后需要重新登录；账号和密码哈希保存在 MySQL，重启不会丢失。
