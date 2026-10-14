"""sudo python3 tests/full_regression.py [--stress]
对真实 server 二进制执行模式矩阵、协议、账号、慢连接、日志和退出测试。
只使用随机临时数据库；--stress 另测 10500 条同时存活的连接。
"""
from contextlib import closing
import concurrent.futures
import hashlib
import http.client
import os
from pathlib import Path
import resource
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid

from current_progress import request, raw_request, sql

REPO = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('WEBSERVER_TEST_BINARY', str(REPO / 'server'))).resolve()
DB = 'webserver_complete_' + uuid.uuid4().hex
COUNT = 0


def check(ok, name):
    global COUNT
    if not ok:
        raise AssertionError(name)
    COUNT += 1
    print('PASS:', name, flush=True)


def start(folder, actor, mode, async_log, idle=3, disabled=False, linger=0):
    with socket.socket() as pick:
        pick.bind(('127.0.0.1', 0))
        port = pick.getsockname()[1]
    env = dict(os.environ, WEBSERVER_DB_HOST='localhost', WEBSERVER_DB_USER='root',
               WEBSERVER_DB_NAME=DB, WEBSERVER_DB_PASSWORD='')
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    output = open(folder / 'console.txt', 'a')
    proc = subprocess.Popen([str(BINARY), '-p', str(port), '-t', '4', '-s', '3',
                             '-i', str(idle), '-a', str(actor), '-m', str(mode),
                             '-l', str(async_log), '-c', str(int(disabled)), '-o', str(linger)],
                            cwd=folder, env=env, stdout=output, stderr=subprocess.STDOUT)
    output.close()
    for _ in range(100):
        if proc.poll() is not None:
            raise RuntimeError((folder / 'console.txt').read_text())
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=.1):
                return proc, port
        except OSError:
            time.sleep(.05)
    proc.kill()
    proc.wait()
    raise RuntimeError('startup timeout')


def stop(proc, sig=signal.SIGTERM):
    if proc.poll() is None:
        proc.send_signal(sig)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            raise AssertionError('shutdown timeout')


def read_response(stream):
    status = stream.readline()
    if not status:
        raise AssertionError('connection closed before response')
    headers = {}
    while True:
        line = stream.readline()
        if line == b'\r\n':
            break
        if not line:
            raise AssertionError('truncated header')
        key, value = line.split(b':', 1)
        headers[key.lower()] = value.strip()
    body = stream.read(int(headers[b'content-length']))
    return int(status.split()[1]), body


def matrix(folder):
    for actor in (0, 1):
        for mode in range(4):
            proc, port = start(folder, actor, mode, (actor + mode) % 2, linger=mode % 2)
            label = f'actor={actor} mode={mode}'
            slow = []
            try:
                for _ in range(12):
                    sock = socket.create_connection(('127.0.0.1', port), timeout=8)
                    sock.sendall(b'GET / HTTP/1.1\r\nHost:')
                    slow.append(sock)
                began = time.monotonic()
                check(request(port, '/')[0] == 200 and time.monotonic() - began < 2,
                      label + ' slow readers do not occupy workers')
                for sock in slow:
                    sock.sendall(b' localhost\r\nConnection: close\r\n\r\n')
                    with sock.makefile('rb') as stream:
                        check(read_response(stream)[0] == 200, label + ' fragmented request')
                for sock in slow:
                    sock.close()
                slow.clear()
                with socket.create_connection(('127.0.0.1', port), timeout=8) as sock:
                    sock.sendall(b'GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n'
                                 b'GET /about.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n')
                    with sock.makefile('rb') as stream:
                        check(read_response(stream)[0] == 200 and read_response(stream)[0] == 200,
                              label + ' pipelined requests retained')
                check(raw_request(port, [b'GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n']).startswith(b'HTTP/1.1 400'),
                      label + ' duplicate framing rejected')
                check(raw_request(port, [b'POST /login HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n']).startswith(b'HTTP/1.1 400'),
                      label + ' unsupported transfer framing rejected')
                for path in ('/../private/profile.html', '/%2e%2e/private/profile.html', '/escape.html'):
                    check(request(port, path)[0] == 404, label + ' directory boundary ' + path)
                check(request(port, '/index.html?x=1')[0] == 200, label + ' query separated from path')
                check(request(port, '/empty.bin')[2] == b'', label + ' empty file')
                check(request(port, '/6', 'POST')[0] == 200, label + ' original media navigation')
                for path, mime in (('/xxx.jpg', 'image/jpeg'), ('/xxx.mp4', 'video/mp4')):
                    status, headers, body = request(port, path)
                    check(status == 200 and headers['Content-Type'] == mime and
                          hashlib.sha256(body).digest() == hashlib.sha256((folder / 'root' / path[1:]).read_bytes()).digest(),
                          label + ' binary integrity ' + path)
                user = f'user_{actor}_{mode}'
                check(request(port, '/3CGISQL.cgi', 'POST', {'user': user, 'password': 'test-password-123'})[0] == 201,
                      label + ' original registration')
                status, headers, body = request(port, '/2CGISQL.cgi', 'POST', {'user': user, 'password': 'test-password-123'})
                cookie = headers.get('Set-Cookie', '').split(';')[0]
                check(status == 200 and request(port, '/profile', headers={'Cookie': cookie})[0] == 200,
                      label + ' original login and protected profile')
                request(port, '/logout', 'POST', headers={'Cookie': cookie})
                check(request(port, '/profile', headers={'Cookie': cookie})[0] == 403, label + ' logout revokes session')
                with concurrent.futures.ThreadPoolExecutor(max_workers=24) as pool:
                    results = list(pool.map(lambda _: request(port, '/style.css')[0], range(96)))
                check(all(s == 200 for s in results), label + ' concurrent requests')
                # 对端不读取大文件时，其他连接仍能得到响应。
                with socket.create_connection(('127.0.0.1', port), timeout=8) as blocked:
                    blocked.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
                    blocked.sendall(b'GET /xxx.mp4 HTTP/1.1\r\nHost: x\r\n\r\n')
                    time.sleep(.1)
                    check(request(port, '/')[0] == 200, label + ' slow writer does not block server')
                with socket.create_connection(('127.0.0.1', port), timeout=8) as half:
                    half.sendall(b'GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n')
                    half.shutdown(socket.SHUT_WR)
                    with half.makefile('rb') as stream:
                        check(read_response(stream)[0] == 200, label + ' half-close still receives response')
            finally:
                for sock in slow:
                    sock.close()
                stop(proc, signal.SIGINT)
            check(proc.returncode == 0, label + ' clean shutdown')


def stress(folder):
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    resource.setrlimit(resource.RLIMIT_NOFILE, (min(hard, max(soft, 40000)), hard))
    proc, port = start(folder, 0, 3, 0, idle=120, disabled=True)
    sockets = []
    try:
        for _ in range(10500):
            sockets.append(socket.create_connection(('127.0.0.1', port), timeout=10))
        request_bytes = b'GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n'
        for sock in sockets:
            sock.sendall(request_bytes)
        successes = 0
        for sock in sockets:
            with sock.makefile('rb') as stream:
                if read_response(stream)[0] == 200:
                    successes += 1
        check(successes == 10500, '10500 simultaneously open connections each return 200')
        # 保持连接直到所有响应都收完，不能用串行反复开关冒充并发连接数。
        print('STRESS: 10500 connections, 10500 successful responses; no upstream QPS claim.', flush=True)
    finally:
        for sock in sockets:
            sock.close()
        stop(proc)
    check(proc.returncode == 0, 'stress shutdown')


def main():
    if os.geteuid() != 0:
        raise SystemExit('Run in Ubuntu with sudo for the isolated MySQL root socket test.')
    if 'WEBSERVER_TEST_BINARY' not in os.environ:
        subprocess.run(['make'], cwd=REPO, check=True)
    check(subprocess.run([str(BINARY), '-h'], stdout=subprocess.DEVNULL).returncode == 0, 'help does not need database password')
    for args in (['-p', '8080junk'], ['-a', '2'], ['-m', '4'], ['-s', '0'], ['-l', '-1']):
        check(subprocess.run([str(BINARY), *args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0,
              'invalid option ' + ' '.join(args))
    sql(f'CREATE DATABASE `{DB}` CHARACTER SET utf8mb4')
    try:
        sql(f'USE `{DB}`; ' + (REPO / 'database/schema.sql').read_text())
        with tempfile.TemporaryDirectory(prefix='webserver-complete-') as name:
            folder = Path(name)
            shutil.copytree(REPO / 'root', folder / 'root')
            shutil.copytree(REPO / 'private', folder / 'private')
            (folder / 'root/empty.bin').write_bytes(b'')
            (folder / 'root/escape.html').symlink_to(folder / 'private/profile.html')
            matrix(folder)
            log = (folder / 'logs/server.log').read_text()
            check('Server stopped; workers joined' in log, 'async log drained at shutdown')
            check('test-password-123' not in log, 'password not logged')
            before = (folder / 'logs/server.log').read_bytes()
            proc, port = start(folder, 1, 3, 1, idle=1, disabled=True)
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=5) as sock:
                    sock.sendall(b'GET / HTTP/1.1\r\nHost:')
                    time.sleep(1.5)
                    check(sock.recv(1) == b'', 'incomplete request reclaimed by idle timer')
                check(request(port, '/')[0] == 200, 'server still works with log disabled')
            finally:
                stop(proc)
            check((folder / 'logs/server.log').read_bytes() == before, 'disabled logger writes nothing')
            if '--stress' in sys.argv:
                stress(folder)
            console = (folder / 'console.txt').read_text()
            if any(marker in console for marker in ('runtime error:', 'ERROR: AddressSanitizer', 'ERROR: LeakSanitizer')):
                raise AssertionError('Sanitizer diagnostic in server console:\n' + console[-5000:])
    finally:
        sql(f'DROP DATABASE `{DB}`')
    print(f'ALL {COUNT} full-regression checks passed. Temporary database removed.')


if __name__ == '__main__':
    main()
