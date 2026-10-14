"""WSL/Ubuntu: sudo python3 tests/current_progress.py

使用本机 root 的 MySQL socket 登录，临时数据库和运行目录在 finally 中清理。
不读取应用密码，不修改 qgydb，也不连接已有的 8080 服务。
"""
import concurrent.futures
from contextlib import closing
import hashlib
import http.client
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import urllib.parse
import uuid

REPO = Path(__file__).resolve().parents[1]
DB = "webserver_alignment_" + uuid.uuid4().hex
PASSWORD = "alignment-test-password"
checks = []


def check(condition, name):
    if not condition:
        raise AssertionError(name)
    checks.append(name)
    print("PASS:", name, flush=True)


def sql(statement):
    return subprocess.check_output(
        ["mysql", "--protocol=socket", "-u", "root", "-Nse", statement],
        text=True,
    ).strip()


def request(port, path, method="GET", fields=None, headers=None, connection=None):
    own = connection is None
    conn = connection or http.client.HTTPConnection("127.0.0.1", port, timeout=8)
    hdr = dict(headers or {})
    body = None
    if fields is not None:
        body = urllib.parse.urlencode(fields)
        hdr["Content-Type"] = "application/x-www-form-urlencoded"
    elif method == "POST":
        body = ""
    if own:
        hdr["Connection"] = "close"
    try:
        conn.request(method, path, body=body, headers=hdr)
        response = conn.getresponse()
        data = response.read()
        return response.status, dict(response.getheaders()), data
    finally:
        if own:
            conn.close()


def raw_request(port, pieces):
    with socket.create_connection(("127.0.0.1", port), timeout=8) as sock:
        for piece in pieces:
            sock.sendall(piece)
            time.sleep(0.04)
        data = b""
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                return data
            data += chunk


def run():
    if os.geteuid() != 0:
        raise SystemExit("请在 Ubuntu 使用 sudo python3 tests/current_progress.py；测试使用 MySQL root socket。")
    process = None
    sql("SELECT 1")
    with tempfile.TemporaryDirectory(prefix="webserver-current-progress-") as name:
        temp = Path(name)
        for folder in ("root", "private"):
            shutil.copytree(REPO / folder, temp / folder)
        sources = ["config.cpp", "webserver.cpp", "http/http_conn.cpp",
                   "threadpool/thread_pool.cpp", "log/logger.cpp",
                   "database/database_pool.cpp", "database/password_hash.cpp",
                   "database/account_repository.cpp", "session/session_manager.cpp",
                   "tests/current_progress_main.cpp"]
        binary = temp / "test-server"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                        "-I", str(REPO), *[str(REPO / s) for s in sources],
                        "-lmysqlclient", "-lcrypto", "-o", str(binary)], check=True)
        sql(f"CREATE DATABASE `{DB}` CHARACTER SET utf8mb4")
        try:
            sql(f"USE `{DB}`; " + (REPO / "database/schema.sql").read_text())
            with socket.socket() as pick:
                pick.bind(("127.0.0.1", 0))
                port = pick.getsockname()[1]
            with (temp / "console.txt").open("w") as output:
                process = subprocess.Popen([str(binary), DB, str(port)], cwd=temp,
                                           stdout=output, stderr=subprocess.STDOUT)
                for _ in range(100):
                    if process.poll() is not None:
                        raise RuntimeError((temp / "console.txt").read_text())
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                            break
                    except OSError:
                        time.sleep(0.05)
                else:
                    raise RuntimeError("test server did not start")

                status, _, body = request(port, "/")
                check(status == 200 and b'action="/0"' in body and b'action="/1"' in body,
                      "original home: register/login choices")
                check(request(port, "/index.html")[2] == (REPO / "root/index.html").read_bytes(),
                      "original learning index preserved")
                for path, action in [("/0", b'/3CGISQL.cgi'), ("/1", b'/2CGISQL.cgi')]:
                    for method in ("GET", "POST"):
                        result = request(port, path, method)
                        check(result[0] == 200 and action in result[2], f"{method} {path} form navigation")
                result = request(port, "/3CGISQL.cgi", "POST", {"user": "original_user", "password": PASSWORD})
                check(result[0] == 201 and b'/2CGISQL.cgi' in result[2],
                      "original registration returns login form")
                check(request(port, "/3CGISQL.cgi", "POST", {"user": "original_user", "password": PASSWORD})[0] == 409,
                      "duplicate account rejected")
                check(request(port, "/2CGISQL.cgi", "POST", {"user": "original_user", "password": "x"})[0] == 403,
                      "short wrong login password remains a login failure")
                status, headers, body = request(port, "/2CGISQL.cgi", "POST", {"user": "original_user", "password": PASSWORD})
                cookie = headers.get("Set-Cookie", "")
                check(status == 200 and b'/profile' in body and "HttpOnly" in cookie,
                      "original login returns welcome page and session")
                cookie = cookie.split(";", 1)[0]
                check(request(port, "/profile")[0] == 403, "private profile requires session")
                check(request(port, "/profile", headers={"Cookie": cookie})[0] == 200,
                      "session opens private profile")
                check(request(port, "/private/profile.html")[0] == 404,
                      "private file cannot bypass session route")
                status, hdr, _ = request(port, "/logout", "POST", headers={"Cookie": cookie})
                check(status == 200 and "Max-Age=0" in hdr.get("Set-Cookie", "") and
                      request(port, "/profile", headers={"Cookie": cookie})[0] == 403,
                      "logout revokes cookie and server session")
                status, _, body = request(port, "/register", "POST", {"username": "old_user", "password": PASSWORD})
                check(status == 201 and body == (REPO / "root/register_success.html").read_bytes(),
                      "existing register endpoint unchanged")
                status, hdr, body = request(port, "/login", "POST", {"username": "old_user", "password": PASSWORD})
                check(status == 200 and "Set-Cookie" in hdr and
                      body == (REPO / "root/login_success.html").read_bytes(),
                      "existing login endpoint unchanged")
                for path in ("/2", "/2CGISQL.cgi"):
                    check(request(port, path, "GET")[0] == 405, f"{path} requires POST")
                for path in ("/login.html", "/about.html", "/contact.html", "/log.html", "/welcome.html",
                             "/logError.html", "/registerError.html"):
                    check(request(port, path)[0] == 200, f"page {path}")
                check(request(port, "/style.css")[1].get("Content-Type") == "text/css; charset=UTF-8",
                      "CSS content type preserved")
                check(request(port, "/missing.html")[0] == 404, "missing file remains 404")
                check(request(port, "/index.html", "POST")[0] == 405, "unsupported method remains 405")
                check(raw_request(port, [b"GET / HTTP/1.1\r\nConnection: close\r\n\r\n"]).startswith(b"HTTP/1.1 400"),
                      "malformed request remains 400")
                form = urllib.parse.urlencode({"user": "original_user", "password": PASSWORD}).encode()
                head = (f"POST /2CGISQL.cgi HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n"
                        f"Content-Type: application/x-www-form-urlencoded\r\nContent-Length: {len(form)}\r\n\r\n").encode()
                check(raw_request(port, [head[:20], head[20:], form[:5], form[5:]]).startswith(b"HTTP/1.1 200"),
                      "fragmented POST still works")
                with closing(http.client.HTTPConnection("127.0.0.1", port, timeout=8)) as conn:
                    check(request(port, "/", connection=conn)[0] == 200, "keep-alive first response")
                    source_port = conn.sock.getsockname()[1]
                    check(request(port, "/about.html", connection=conn)[0] == 200 and
                          conn.sock.getsockname()[1] == source_port, "keep-alive reuses connection")
                payload = os.urandom(1024 * 1024)
                (temp / "root/regression.bin").write_bytes(payload)
                received = request(port, "/regression.bin")[2]
                check(hashlib.sha256(received).digest() == hashlib.sha256(payload).digest(),
                      "large binary response unchanged")
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                    statuses = list(pool.map(lambda _: request(port, "/index.html")[0], range(24)))
                check(all(s == 200 for s in statuses), "existing worker pool serves concurrent requests")
                with socket.create_connection(("127.0.0.1", port), timeout=8) as idle:
                    time.sleep(2.3)
                    check(idle.recv(1) == b"", "idle connection timeout preserved")
                for path in ("/5", "/6", "/7"):
                    check(request(port, path)[0] == 404, f"future route {path} intentionally not implemented")
                stored = sql(f"SELECT password_hash FROM `{DB}`.accounts WHERE username='original_user'")
                check(stored.startswith("pbkdf2-sha256$") and len(stored) == 118,
                      "original form still stores password hash")
                check(PASSWORD not in (temp / "logs/server.log").read_text(), "password absent from log")
                process.send_signal(signal.SIGINT)
                check(process.wait(timeout=10) == 0, "Ctrl+C orderly shutdown preserved")
                print(f"\n{len(checks)} checks passed; temporary database removed on exit.")
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            sql(f"DROP DATABASE `{DB}`")


if __name__ == "__main__":
    run()
