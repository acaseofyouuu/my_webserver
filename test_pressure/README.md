# 压力测试

此目录保留 TinyWebServer 配套 Webbench 1.5 工具的源码。
许可证与改动说明见 `webbench-1.5/COPYING`、`debian/copyright` 和项目 `NOTICE`。

```sh
make -C test_pressure/webbench-1.5 webbench
test_pressure/webbench-1.5/webbench -c 100 -t 5 http://127.0.0.1:8080/index.html
```

先另开一个终端启动你自己的服务器，例如 `./server -c 1 -a 0 -m 3`。
只对自己授权的服务压测。Webbench 用多个进程反复建立连接，和“同时保持一万条长连接”是不同测试。
后一项使用 `sudo python3 tests/full_regression.py --stress`，测试自己创建的临时服务。

工具的 pages/min 是每分钟折算值，不是每秒 QPS。HTTP 内容正确性由回归测试另行检查。
