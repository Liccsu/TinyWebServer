#!/usr/bin/env python3
"""并发冒烟测试：并发 GET 请求验证服务器在高并发下的正确性。

用法: python3 scripts/smoke_concurrent.py [host] [port] [concurrency] [requests]
退出码: 0 = 全部成功；1 = 存在失败。
"""
import socket
import sys
import threading
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6666
CONCURRENCY = int(sys.argv[3]) if len(sys.argv) > 3 else 32
REQUESTS = int(sys.argv[4]) if len(sys.argv) > 4 else 200

results = []
errors = []
lock = threading.Lock()
start = time.time()


def worker(wid: int):
    ok = 0
    for i in range(REQUESTS):
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            s.settimeout(5)
            req = f"GET /index.html HTTP/1.1\r\nHost: {HOST}\r\nConnection: close\r\n\r\n".encode()
            s.sendall(req)
            buf = b""
            while True:
                chunk = s.recv(4096)
                if not chunk:
                    break
                buf += chunk
            s.close()
            head, _, body = buf.partition(b"\r\n\r\n")
            status = head.split(b" ", 2)[1] if len(head.split(b" ", 2)) > 1 else b""
            if status == b"200" and len(body) > 0:
                ok += 1
            else:
                with lock:
                    errors.append(f"worker{wid} req{i}: status={status!r} len={len(body)}")
        except Exception as e:  # noqa: BLE001
            with lock:
                errors.append(f"worker{wid} req{i}: {e}")
    with lock:
        results.append(ok)


threads = [threading.Thread(target=worker, args=(w,)) for w in range(CONCURRENCY)]
for t in threads:
    t.start()
for t in threads:
    t.join()

total = CONCURRENCY * REQUESTS
ok = sum(results)
elapsed = time.time() - start
print(f"concurrency={CONCURRENCY} requests={total} ok={ok} errors={len(errors)} "
      f"elapsed={elapsed:.2f}s qps={total / elapsed:.0f}")
if errors:
    for e in errors[:10]:
        print("  ERROR:", e)
    sys.exit(1)
sys.exit(0)
