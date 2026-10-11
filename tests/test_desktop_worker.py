"""只用 loopback 和临时密钥验证主机凭据切换、拒绝其他设备、EOF 清理。"""
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time

HEADER = struct.Struct('!4sB3xII')

def receive(sock, count):
    result = b''
    while len(result) < count:
        chunk = sock.recv(count - len(result))
        if not chunk:
            break
        result += chunk
    return result

def connect(port):
    deadline = time.monotonic() + 5
    while True:
        try:
            sock = socket.create_connection(('127.0.0.1', port), timeout=1)
            sock.settimeout(3)
            return sock
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(.05)

def authenticate(sock, key, accepted):
    sock.sendall(HEADER.pack(b'CB01', 1, 0, 64) + key)
    reply = receive(sock, 16)
    if accepted:
        assert reply == HEADER.pack(b'CB01', 1, 0, 64), reply
        assert receive(sock, 64) == key
    else:
        assert not reply, 'unselected device authenticated'

with tempfile.TemporaryDirectory(prefix='cb-desktop-test-') as folder:
    files = [pathlib.Path(folder) / f'{name}.key' for name in ('A', 'B')]
    keys = [b'a' * 64, b'b' * 64]
    for file, key in zip(files, keys):
        file.write_bytes(key + b'\n')
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    for index in (0, 1):
        worker = subprocess.Popen([sys.argv[1], '--listen', '127.0.0.1', '--port', str(port),
            '--key-file', str(files[index]), '--desktop-control'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            with connect(port) as wrong:
                authenticate(wrong, keys[1 - index], False)
            with connect(port) as selected:
                authenticate(selected, keys[index], True)
                if index == 1:
                    # 已进入部分帧接收时也要在既有 5 秒超时后正常释放，不重放输入。
                    selected.sendall(b'C')
                    time.sleep(.2)
                # 关闭父界面控制管道，必须正常取消已连接会话并释放资源。
                worker.stdin.close()
                worker.wait(timeout=8)
                output = worker.stdout.read()
                assert worker.returncode == 0, output
                assert output.count(b'TEST_BASELINE_RESET') == 1, output
                assert b'TEST_PLATFORM_CLOSED' in output and b'TEST_KEYBOARD_CLOSED' in output, output
                assert b'TEST_KEYBOARD_CONNECTED 0' in output, output
                try:
                    assert not receive(selected, 16), 'old connection remained active'
                except ConnectionResetError:
                    pass  # 未完成帧被丢弃并关闭套接字，部分系统报告 reset。

        finally:
            if worker.poll() is None:
                worker.kill()
                worker.wait()
            worker.stdout.close()
print('Exclusive pairing selection, baseline and graceful worker shutdown tests passed')
