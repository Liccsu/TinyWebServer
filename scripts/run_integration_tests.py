#!/usr/bin/env python3
"""集成测试：启动真实服务器进程，用 raw socket 客户端验证 HTTP 行为。

用法: python3 run_integration_tests.py <server_binary> <project_root>
退出码: 0 = 全部通过；1 = 存在失败。
"""
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import shutil

PASS = 0
FAIL = 0
FAILURES = []


def check(name: str, condition: bool, detail: str = ""):
    global PASS, FAIL
    if condition:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        FAILURES.append(f"{name}: {detail}")
        print(f"  [FAIL] {name} - {detail}")


def recv_until_close(sock: socket.socket, timeout: float = 5.0) -> bytes:
    sock.settimeout(timeout)
    buf = b""
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    return buf


def recv_response(sock: socket.socket, timeout: float = 5.0):
    """读取一个完整 HTTP 响应（基于 Content-Length），返回 (head_bytes, body_bytes)"""
    sock.settimeout(timeout)
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("connection closed before headers")
        buf += chunk
    head, _, body = buf.partition(b"\r\n\r\n")
    clen = 0
    for line in head.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            clen = int(line.split(b":", 1)[1].strip())
    while len(body) < clen:
        chunk = sock.recv(65536)
        if not chunk:
            break
        body += chunk
    return head, body


def raw_request(port: int, payload: bytes, timeout: float = 5.0) -> bytes:
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.sendall(payload)
    data = recv_until_close(s, timeout)
    s.close()
    return data


def first_status_line(data: bytes) -> str:
    return data.split(b"\r\n", 1)[0].decode(errors="replace")


def run_tests(binary: str, port: int, tmp: str, dist: str):
    print(f"== 集成测试: server={binary} port={port} ==")

    # --- 基础 GET ---
    print("-- 静态文件服务 --")
    resp = raw_request(port, b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    check("GET / 返回 200", b" 200 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))
    check("GET / 返回 index 内容", b"<html>integration-index</html>" in resp, "body 内容不符")

    resp = raw_request(port, b"GET /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    check("GET /index.html 200", b" 200 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    resp = raw_request(port, b"GET /sub/page.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    check("GET 子目录 200", b" 200 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    resp = raw_request(port, b"GET /nope.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    check("GET 不存在 -> 404", b" 404 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    # --- HEAD ---
    print("-- HEAD --")
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"HEAD /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    data = recv_until_close(s, timeout=5)
    s.close()
    head, _, body = data.partition(b"\r\n\r\n")
    check("HEAD 200", b" 200 " in head.split(b"\r\n", 1)[0], first_status_line(data))
    check("HEAD 无 body", len(body) == 0, f"body={len(body)} bytes")
    check("HEAD 有 Content-Length", b"content-length:" in head.lower(), "缺少 Content-Length")

    # --- 健康检查 ---
    print("-- 健康检查 --")
    resp = raw_request(port, b"GET /healthz HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    status = resp.split(b" ", 2)[1] if len(resp.split(b" ", 2)) > 1 else b""
    check("GET /healthz -> 200", status == b"200", f"status={status!r}")
    check("GET /healthz 返回 ok 文本", b"ok" in resp, "body 缺少 ok")

    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"HEAD /healthz HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    data = recv_until_close(s, timeout=5)
    s.close()
    check("HEAD /healthz 200 无 body", b" 200 " in data.split(b"\r\n", 1)[0] and
          data.count(b"\r\n\r\n") == 1, "HEAD healthz 行为错误")

    # --- 安全 ---
    print("-- 安全防御 --")
    for label, path in [
        ("原始路径穿越", b"GET /../../etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"),
        ("编码点穿越", b"GET /%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"),
        ("编码斜杠穿越", b"GET /..%2f..%2fetc%2fpasswd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"),
        ("绝对路径", b"GET //etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"),
    ]:
        resp = raw_request(port, path)
        status = resp.split(b" ", 2)[1] if len(resp.split(b" ", 2)) > 1 else b""
        check(f"{label} -> 拒绝(403/404)", status in (b"403", b"404"), f"status={status!r}")
        check(f"{label} 不泄露文件内容", b"root:" not in resp and b"TOP" not in resp, "泄露系统文件内容")

    # --- 协议错误 ---
    print("-- 协议错误 --")
    resp = raw_request(port, b"PUT /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    check("PUT -> 405", b" 405 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    resp = raw_request(port, b"POST /login.html HTTP/1.1\r\nHost: x\r\nContent-Length: 99999999\r\n\r\nx")
    check("超大 Content-Length -> 413", b" 413 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    resp = raw_request(port, b"POST /login.html HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")
    check("chunked -> 501", b" 501 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    resp = raw_request(port, b"GET / HTTP/1.1\r\nHost x\r\n\r\n")
    check("缺冒号 header -> 400", b" 400 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    resp = raw_request(port, b"GET /%00 HTTP/1.1\r\nHost: x\r\n\r\n")
    check("NUL 注入 -> 400", b" 400 " in resp.split(b"\r\n", 1)[0], first_status_line(resp))

    # --- Keep-Alive ---
    print("-- Keep-Alive --")
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    ok = True
    for _ in range(3):
        s.sendall(b"GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n")
        try:
            head, body = recv_response(s, timeout=5)
            if b" 200 " not in head.split(b"\r\n", 1)[0] or len(body) == 0:
                ok = False
        except Exception as e:
            ok = False
            break
    s.close()
    check("同一连接 3 次请求均成功", ok, "keep-alive 复用失败")

    # --- 大文件（部分写路径）---
    print("-- 大文件传输 --")
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"GET /big.bin HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    data = recv_until_close(s, timeout=10)
    s.close()
    head, _, body = data.partition(b"\r\n\r\n")
    clen = 0
    for line in head.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            clen = int(line.split(b":", 1)[1].strip())
    check("100KB 文件完整传输", clen == 100000 and len(body) == 100000,
          f"clen={clen} body={len(body)}")

    # --- 并发 ---
    print("-- 并发 --")
    errors = []
    results = []
    lock = threading.Lock()

    def worker(wid):
        ok_n = 0
        for _ in range(20):
            try:
                resp = raw_request(port,
                                   b"GET /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n",
                                   timeout=10)
                if b" 200 " in resp.split(b"\r\n", 1)[0]:
                    ok_n += 1
            except Exception as e:
                with lock:
                    errors.append(str(e))
        with lock:
            results.append(ok_n)

    threads = [threading.Thread(target=worker, args=(w,)) for w in range(8)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    check("8 线程 x 20 并发全部 200", sum(results) == 160 and not errors,
          f"ok={sum(results)}/160 errors={errors[:3]}")

    # --- 慢客户端/空闲超时 ---
    print("-- 慢客户端与空闲超时 --")
    # 场景 1：空闲连接（不发数据）被服务器超时关闭
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.settimeout(5)
    data = recv_until_close(s, timeout=5)
    s.close()
    check("空闲连接被服务器关闭", data == b"", f"收到 {len(data)} 字节（应无数据仅关闭）")

    # 场景 2：慢客户端（只发送半个请求后挂起）——不完整报文不应触发 400，
    # 而应等待更多数据直至超时关闭（绝不错杀/崩溃）
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"GET /index.ht")  # 半个请求行
    s.settimeout(5)
    data = recv_until_close(s, timeout=5)
    s.close()
    check("慢客户端（半请求）被超时关闭且无 400 响应", data == b"",
          f"收到 {len(data)} 字节（服务器不应回复 400 而是超时关闭）")

    # 场景 3：Header 不完整
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"GET /index.html HTTP/1.1\r\nHost: x\r\nX-Part")
    s.settimeout(5)
    data = recv_until_close(s, timeout=5)
    s.close()
    check("慢客户端（半 Header）被超时关闭", data == b"",
          f"收到 {len(data)} 字节（应超时关闭而非错误响应）")

    # --- pipeline 拒绝 ---
    print("-- Pipeline --")
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"GET /index.html HTTP/1.1\r\nHost: x\r\n\r\nGET / HTTP/1.1\r\nHost: x\r\n\r\n")
    data = recv_until_close(s, timeout=5)
    s.close()
    check("pipeline 后连接被关闭（安全）", data.count(b"HTTP/1.1 200") == 1,
          f"200 响应数={data.count(b'HTTP/1.1 200')}")

    # --- 优雅退出 ---
    print("-- 优雅退出 --")


def find_free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    if len(sys.argv) < 3:
        print("usage: run_integration_tests.py <server_binary> <project_root>")
        return 1
    binary = os.path.abspath(sys.argv[1])
    root = os.path.abspath(sys.argv[2])
    if not os.path.exists(binary):
        print(f"server binary not found: {binary}")
        return 1

    port = find_free_port()
    tmp = tempfile.mkdtemp(prefix="tws_it_")
    dist = os.path.join(tmp, "dist")
    os.makedirs(os.path.join(dist, "sub"))
    with open(os.path.join(dist, "index.html"), "w") as f:
        f.write("<html>integration-index</html>")
    with open(os.path.join(dist, "sub", "page.html"), "w") as f:
        f.write("<html>sub-page</html>")
    with open(os.path.join(dist, "big.bin"), "wb") as f:
        f.write(b"x" * 100000)

    os.makedirs(os.path.join(tmp, "config"))
    with open(os.path.join(tmp, "config", "config.yml"), "w") as f:
        f.write(f"""\
server:
    port: {port}
    timeout: 1000

mysql:
    host: 127.0.0.1
    port: 3306
    user: root
    password: secret
    db: test_db
    pool_size: 1
    pool_min_size: 1
    pool_max_size: 2

log:
    directory: {tmp}/log
    level: 5
    size: 64
    basename: it_test
    colorful: false
    output_to_file: false

site:
    path: {dist}
""")

    proc = subprocess.Popen([binary], cwd=tmp,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        # 等待端口就绪
        deadline = time.time() + 10
        ready = False
        while time.time() < deadline:
            if proc.poll() is not None:
                break
            try:
                s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                s.close()
                ready = True
                break
            except OSError:
                time.sleep(0.1)
        if not ready:
            print(f"[FAIL] 服务器未在 {10}s 内就绪（退出码 {proc.poll()}）")
            return 1

        run_tests(binary, port, tmp, dist)

        # 优雅退出验证
        proc.send_signal(signal.SIGTERM)
        try:
            rc = proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = -1
        check("SIGTERM 优雅退出（退出码 0）", rc == 0, f"exit code={rc}")
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"\n结果: {PASS} 通过, {FAIL} 失败")
    if FAILURES:
        print("失败明细:")
        for f in FAILURES:
            print(f"  - {f}")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
