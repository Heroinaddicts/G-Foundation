import selectors
import socket
import time

HOST = "127.0.0.1"
PORT = 8888
TARGET = 1

selector = selectors.DefaultSelector()
connections = set()
total_bytes = 0
disconnected = 0
last_report = time.monotonic()

try:
    for i in range(TARGET):
        try:
            sock = socket.create_connection((HOST, PORT), timeout=5)
            sock.setblocking(False)
            selector.register(sock, selectors.EVENT_READ)
            connections.add(sock)
        except OSError as err:
            print(f"第 {i + 1} 次连接失败：{err}")

        if (i + 1) % 100 == 0:
            print(f"已尝试 {i + 1}/{TARGET}，当前连接 {len(connections)} 条")

    print(f"连接建立完成：{len(connections)} 条。按 Ctrl+C 断开。")

    while connections:
        for key, _ in selector.select(timeout=1):
            sock = key.fileobj

            try:
                data = sock.recv(65536)
            except BlockingIOError:
                continue
            except OSError:
                data = b""

            if not data:
                selector.unregister(sock)
                sock.close()
                connections.discard(sock)
                disconnected += 1
            else:
                total_bytes += len(data)

        now = time.monotonic()
        if now - last_report >= 5:
            print(
                f"连接数 {len(connections)}，"
                f"已读取 {total_bytes} 字节，"
                f"断开 {disconnected} 条"
            )
            last_report = now

except KeyboardInterrupt:
    print("\n收到 Ctrl+C，准备关闭连接。")
finally:
    for sock in list(connections):
        try:
            selector.unregister(sock)
        except Exception:
            pass
        sock.close()

    selector.close()
    print("连接已关闭。")