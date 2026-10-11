#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The remote touch screen's server (port/stream), through tools/tests/stream-host.cpp: ports, routes,
WebSockets, the video's back-pressure, touches and their edges, validation, expiry, takeover and
restart. From dolphin78's prototype (#38). Python's standard library only."""
import hashlib
import base64
import os
import statistics
import struct
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import urllib.parse

binary = sys.argv[1]
def dimensions(jpeg):
    assert jpeg[:2] == b'\xff\xd8' and jpeg[-2:] == b'\xff\xd9'
    pos = 2
    while pos < len(jpeg):
        assert jpeg[pos] == 255
        marker = jpeg[pos + 1]
        length = int.from_bytes(jpeg[pos + 2:pos + 4], 'big')
        if marker in (0xc0, 0xc1, 0xc2):
            h, w = struct.unpack('>HH', jpeg[pos + 5:pos + 9])
            return w, h
        pos += 2 + length
    raise AssertionError('No JPEG dimensions')

class WebSocket:
    def __init__(self, port, route):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=2)
        self.socket.settimeout(2)
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall((f'GET {route} HTTP/1.1\r\nHost: localhost:{port}\r\n'
                             'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                             f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        reply = b''
        while not reply.endswith(b'\r\n\r\n'):
            reply += self.exact(1)
        assert reply.startswith(b'HTTP/1.1 101'), reply
        expected = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        assert b'Sec-WebSocket-Accept: ' + expected in reply
    def exact(self, size):
        result = b''
        while len(result) < size:
            chunk = self.socket.recv(size - len(result))
            if not chunk:
                raise EOFError('WebSocket closed')
            result += chunk
        return result
    def send(self, text, opcode=1, fin=True):
        data = text.encode() if isinstance(text, str) else text
        mask = os.urandom(4)
        assert len(data) < 126
        self.socket.sendall(bytes([(128 if fin else 0) | opcode, 128 | len(data)]) + mask +
                            bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
    def receive(self):
        header = self.exact(2)
        assert not header[1] & 128
        size = header[1] & 127
        if size == 126: size = struct.unpack('>H', self.exact(2))[0]
        if size == 127: size = struct.unpack('>Q', self.exact(8))[0]
        assert size <= 2_000_000
        return header[0] & 15, self.exact(size)
    def close(self):
        self.socket.close()

# Occupy 8080 and verify the advertised fallback port.
blocker = socket.socket()
blocker.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    blocker.bind(('0.0.0.0', 8080)); blocker.listen(1)
except OSError:
    blocker.close()

process = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
connections = []
try:
    url = process.stdout.readline().strip()
    assert url.startswith('http://'), url
    port = urllib.parse.urlsplit(url).port
    assert 8081 <= port <= 65535, url
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def get(path):
        return opener.open(f'http://127.0.0.1:{port}/' + path, timeout=2)
    def command(text):
        process.stdin.write(text + '\n'); process.stdin.flush()
        return process.stdout.readline().strip()
    def state():
        down, x, y = command('touch').split()
        return int(down), float(x), float(y)
    def wait_state(expected):
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            if state() == expected: return
            time.sleep(.002)
        raise AssertionError(('Touch state not received', expected))
    def rejected(path, code):
        try:
            get(path)
            raise AssertionError('Request was accepted: ' + path)
        except urllib.error.HTTPError as error:
            assert error.code == code
    assert b'/video' in get('').read()
    rejected('missing', 404)
    rejected('input', 400)  # Upgrade is required, not authentication.
    assert get('frame').status == 204
    time.sleep(.2)
    response = get('frame')
    assert response.headers['Content-Type'] == 'image/jpeg'
    assert dimensions(response.read()) == (854, 480)
    touch = 'touch?down='
    assert get(touch + '1&x=0.25&y=0.75').status == 204
    assert state() == (1, .25, .75)
    for values in ['1&x=nan&y=0', '1&x=2&y=0', '2&x=0&y=0', '1&x=0&y=0junk']:
        rejected(touch + values, 400)
    get(touch + '0&x=0.25&y=0.75'); assert state() == (0, .25, .75)

    input_ws = WebSocket(port, '/input'); connections.append(input_ws)
    video = WebSocket(port, '/video'); connections.append(video)
    video.send('ready')
    opcode, image = video.receive()
    assert opcode == 2 and dimensions(image) == (854, 480)
    # The viewer withholds decode credit: no additional images may queue.
    video.socket.settimeout(.15)
    try:
        video.socket.recv(1)
        raise AssertionError('Video queued without decode credit')
    except TimeoutError:
        pass
    video.socket.settimeout(2)
    # Slow/incomplete HTTP clients must not delay the input connection.
    for _ in range(3):
        slow = socket.create_connection(('127.0.0.1', port), timeout=2)
        slow.sendall(b'GET /frame HTTP/1.1\r\n')
        connections.append(slow)
    timings = []
    for i in range(1, 11):
        x = i / 10
        started = time.monotonic()
        input_ws.send(f'1,{x},0.5')
        wait_state((1, x, .5))
        timings.append(time.monotonic() - started)
    assert max(timings) < .2, timings
    print(f'Touch delivery under active encoding/stalled clients: median {statistics.median(timings)*1000:.1f} ms, max {max(timings)*1000:.1f} ms (host)')
    # Measure continuous delivery without the old 100 ms polling floor.
    started = time.monotonic()
    for _ in range(20):
        video.send('ready')
        opcode, image = video.receive()
        assert opcode == 2 and dimensions(image) == (854, 480)
    rate = 20 / (time.monotonic() - started)
    assert rate > 15, rate
    print(f'Video delivery: {rate:.1f} FPS, latest JPEG {len(image):,} bytes (host synthetic source)')
    input_ws.send('0,1,0.5'); wait_state((0, 1, .5))
    # A press/release arriving between emulator polls must preserve a tap.
    input_ws.send('1,0.25,0.75'); input_ws.send('0,0.25,0.75')
    time.sleep(.03)
    assert state() == (1, .25, .75)
    assert state() == (0, .25, .75)
    # Fragmented commands with interleaved control frames.
    input_ws.send('1,0.', fin=False)
    input_ws.send('ping', opcode=9)
    assert input_ws.receive() == (10, b'ping')
    input_ws.send('25,0.75', opcode=0)
    wait_state((1, .25, .75))
    time.sleep(1.1)
    assert state()[0] == 0, 'Held touch must expire without heartbeats'
    input_ws.send('1,0,1'); wait_state((1, 0, 1))
    # A new input session takes over; closing an old session must not release it.
    replacement = WebSocket(port, '/input'); connections.append(replacement)
    replacement.send('1,0.5,0.5'); wait_state((1, .5, .5))
    input_ws.close(); time.sleep(.02)
    assert state() == (1, .5, .5)
    replacement.close(); wait_state((0, .5, .5))
    malformed = WebSocket(port, '/input'); connections.append(malformed)
    malformed.send('1,nan,0')
    assert malformed.socket.recv(1) == b'', 'Invalid input must close its session'
    unmasked = WebSocket(port, '/input'); connections.append(unmasked)
    unmasked.socket.sendall(b'\x81\x05hello')
    assert unmasked.socket.recv(1) == b'', 'Unmasked WebSocket frames must be rejected'
    invalid_close = WebSocket(port, '/input'); connections.append(invalid_close)
    invalid_close.send(b'x', opcode=8)
    assert invalid_close.socket.recv(1) == b'', 'One-byte close payload is invalid'
    assert command('state') == 'A browser is showing it'
    # Stop with clients still connected, then start without stale frames or input.
    started = time.monotonic()
    assert command('disable') == 'Off'
    assert time.monotonic() - started < 1
    assert command('enable') == url
    assert state()[0] == 0
    assert get('frame').status == 204
    print('PASS: port fallback, independent WebSockets, video backpressure, touch latency, tap edges, fragmentation, ping, validation, expiry, takeover and restart')
finally:
    for connection in connections: connection.close()
    process.stdin.write('quit\n'); process.stdin.flush()
    process.wait(timeout=5)
    blocker.close()
