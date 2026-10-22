#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The remote touch screen's files page (port/stream: files.h, SetUploadFolders), through
tools/tests/stream-host.cpp: the page and its folders, a file sent whole, names and folders refused,
nothing replaced, and a file cut short leaving nothing behind. Python's standard library only.

    test-upload.py build/tests/stream-host
"""
import json
import os
import socket
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

binary = sys.argv[1]
failures = 0


def check(ok, what):
    global failures
    print(("PASS  " if ok else "FAIL  ") + what)
    failures += not ok


with tempfile.TemporaryDirectory() as games:
    process = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                               env=dict(os.environ, STREAM_UPLOADS=games))
    try:
        url = process.stdout.readline().strip()
        port = urllib.parse.urlsplit(url).port
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

        def request(method, path, body=None):
            req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=body, method=method)
            try:
                with opener.open(req, timeout=10) as answer:
                    return answer.status, answer.read()
            except urllib.error.HTTPError as error:
                return error.code, error.read()

        def upload(name, body, folder="games"):
            return request("PUT", "/upload?to=" + folder + "&name=" + urllib.parse.quote(name, safe=""), body)

        code, page = request("GET", "/files")
        check(code == 200 and b"/upload" in page and b"/folders" in page, "the files page")
        code, touch = request("GET", "/")
        check(code == 200 and b'href="/files"' in touch, "the touch screen's page leads to it")
        code, folders = request("GET", "/folders")
        listed = json.loads(folders)
        check(code == 200 and [f["id"] for f in listed] == ["games", "nowhere"] and listed[0]["label"] == "Games", "the folders, as JSON")

        data = os.urandom(5 * 1024 * 1024 + 123)
        code, _ = upload("Mario Kart 8 [0100152000022000].nsp", data)
        path = os.path.join(games, "Mario Kart 8 [0100152000022000].nsp")
        check(code == 201 and open(path, "rb").read() == data, "a 5 MB file arrives whole")
        check(stat.S_IMODE(os.stat(path).st_mode) == 0o666, "open to every user, as the app's files are")
        code, _ = upload("ポケモン Y.3ds", b"3ds")
        check(code == 201 and os.path.exists(os.path.join(games, "ポケモン Y.3ds")), "a name with Japanese and a space")
        code, _ = upload("empty.txt", b"")
        check(code == 201 and os.path.getsize(os.path.join(games, "empty.txt")) == 0, "an empty file")

        code, _ = upload("Mario Kart 8 [0100152000022000].nsp", b"other")
        check(code == 409 and open(path, "rb").read() == data, "a file of that name is never replaced")
        for name in ["../escape.nsp", "a/b.nsp", ".hidden", "..", "", "tab\there"]:
            code, _ = upload(name, b"x")
            check(code == 400, f"refused name {name!r}")
        check(not os.path.exists(os.path.join(os.path.dirname(games), "escape.nsp")), "nothing written outside the folder")
        code, _ = upload("x.nds", b"x", folder="elsewhere")
        check(code == 400, "a folder not listed is refused")
        code, _ = upload("x.nds", b"x", folder="nowhere")
        check(code == 500, "a folder that cannot be made says so")

        # no Content-Length
        with socket.create_connection(("127.0.0.1", port), timeout=5) as raw:
            raw.sendall(b"PUT /upload?to=games&name=nolength.nds HTTP/1.1\r\nHost: x\r\n\r\n")
            answer = raw.recv(4096)
        check(b" 411 " in answer.split(b"\r\n")[0] and not os.path.exists(os.path.join(games, "nolength.nds")), "no length: refused")

        # cut short: the browser goes away halfway
        with socket.create_connection(("127.0.0.1", port), timeout=5) as raw:
            raw.sendall(b"PUT /upload?to=games&name=cut.nds HTTP/1.1\r\nHost: x\r\nContent-Length: 100000\r\n\r\n" + b"y" * 40000)
            time.sleep(0.3)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and any(name.endswith(".part") for name in os.listdir(games)):
            time.sleep(0.05)
        check(not os.path.exists(os.path.join(games, "cut.nds")) and not any(n.endswith(".part") for n in os.listdir(games)),
              "a file cut short leaves nothing behind")

        process.stdin.write("disable\n")
        process.stdin.flush()
        process.stdout.readline()
    finally:
        process.stdin.close()
        process.wait(timeout=10)

print("all passed" if not failures else f"{failures} failed")
sys.exit(1 if failures else 0)
