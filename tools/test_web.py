#!/usr/bin/env python3
"""Tests the WebAssembly build against the native program.

Usage: python tools/test_web.py [--build] [--meth PATH] [--browser PATH]

Every expression of web/tests/cases.txt is evaluated by the module in a
headless browser (web/test.html) and by the native `meth eval --format json`;
the two results must agree. Exact results (numbers, text, errors, the exit
code) must be identical. An approximate result ("≈ 0.841470985") may differ by
a rounding of its last digit, because the browser's Math functions are not the
C library's. Needs web/meth.wasm (--build runs build_web.bat first), the
native meth program and Edge or Chrome; nothing is installed or fetched.
Exits 1 when a case differs.
"""

import argparse
import functools
import html
import http.server
import json
import os
import re
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")

NATIVE = [
    "build/direct/release/meth.exe",
    "build/direct/debug/meth.exe",
    "build/bin/Release/meth.exe",
    "build/bin/Debug/meth.exe",
    "build/make/bin/meth",
    "build/cmake/meth",
]

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    "/usr/bin/microsoft-edge",
]

# A rounding of the ninth digit: the shown value has 9 significant digits.
APPROX_TOLERANCE = 2e-8


def first_existing(paths):
    for path in paths:
        full = path if os.path.isabs(path) else os.path.join(ROOT, path)
        if os.path.exists(full):
            return full
    return None


def read_cases():
    with open(os.path.join(WEB, "tests", "cases.txt"), encoding="utf-8") as f:
        lines = [line.strip() for line in f.read().split("\n")]
    return [line for line in lines if line and not line.startswith("#")]


def run_native(meth, expression):
    done = subprocess.run(
        [meth, "eval", "--format", "json", expression],
        capture_output=True,
        timeout=60,
    )
    out = done.stdout.decode("utf-8", errors="replace")
    return done.returncode, out.split("\n")[0].strip()


def run_browser(browser, port, page):
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as profile:
        done = subprocess.run(
            [
                browser,
                "--headless=new",
                "--disable-gpu",
                "--user-data-dir=" + profile,
                "--virtual-time-budget=60000",
                "--dump-dom",
                "http://127.0.0.1:%d/%s" % (port, page),
            ],
            capture_output=True,
            timeout=180,
        )
    dom = done.stdout.decode("utf-8", errors="replace")
    found = re.search(r'<pre id="results"[^>]*>(.*?)</pre>', dom, re.S)
    if found is None or not found.group(1).strip():
        return None
    return json.loads(html.unescape(found.group(1)))


def run_browser_retrying(browser, port, page):
    """The dump can be taken before the module has loaded (the network is
    not part of the browser's virtual time), so an empty page is tried again."""
    outcome = None
    for _ in range(10):
        if outcome is None:
            outcome = run_browser(browser, port, page)
    if outcome is None:
        raise RuntimeError("the page never gave results")
    return outcome


def approx_value(text):
    found = re.fullmatch(r"≈\s*(\S+)", text)
    return float(found.group(1)) if found else None


def same_value(a, b):
    ok = a == b
    x, y = approx_value(a), approx_value(b)
    if not ok and x is not None and y is not None:
        ok = abs(x - y) <= APPROX_TOLERANCE * max(abs(x), abs(y))
    return ok


def same_result(native, wasm):
    """None when the two JSON lines agree, else what differs."""
    try:
        a, b = json.loads(native), json.loads(wasm)
    except ValueError:
        return None if native == wasm else "not JSON"
    for key in sorted(set(a) | set(b)):
        va, vb = a.get(key), b.get(key)
        if key == "value" and isinstance(va, str) and isinstance(vb, str):
            if not same_value(va, vb):
                return "value: %r != %r" % (va, vb)
        elif va != vb:
            return "%s: %r != %r" % (key, va, vb)
    return None


def check_api(api):
    """The page API (web/js/meth.js) against the module; returns the number
    of checks that failed."""
    failed = [c for c in api if not c["ok"]]
    for c in failed:
        print("FAIL api: %s %s" % (c["name"], c["detail"]))
    print("%d api checks, %d failed" % (len(api), len(failed)))
    return len(failed)


def check_groups(groups):
    """The C test groups run inside the module (web/tests.wasm); returns the
    number of failures."""
    failed = 0
    if groups is None:
        print("web/tests.wasm is missing: the C test groups were not run")
    elif "error" in groups:
        print("the groups page failed: " + groups["error"])
        failed = 1
    else:
        checks = 0
        for group in groups["groups"]:
            checks += group["checks"]
            if "FAIL" in group["output"]:
                failed += 1
                print("group %s:" % group["name"])
                print(group["output"])
        print("%d groups, %d checks in the module" % (len(groups["groups"]), checks))
        if groups["failures"]:
            failed += 1
            print(groups["output"])
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--build", action="store_true", help="run build_web.bat first")
    parser.add_argument("--meth", help="the native meth program")
    parser.add_argument("--browser", help="Edge or Chrome")
    args = parser.parse_args()

    if args.build:
        if subprocess.run([os.path.join(ROOT, "build_web.bat")], cwd=ROOT).returncode:
            return 2
    if not os.path.exists(os.path.join(WEB, "meth.wasm")):
        print("web/meth.wasm is missing: run build_web.bat or --build")
        return 2
    meth = args.meth or first_existing(NATIVE)
    browser = args.browser or first_existing(BROWSERS)
    if meth is None or browser is None:
        print("need the native meth program (--meth) and Edge or Chrome (--browser)")
        return 2

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass

    handler = functools.partial(Quiet, directory=WEB)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        port = server.server_address[1]
        wasm = run_browser_retrying(browser, port, "test.html")
        api = run_browser_retrying(browser, port, "apitest.html")
        groups = None
        if os.path.exists(os.path.join(WEB, "tests.wasm")):
            groups = run_browser_retrying(browser, port, "groups.html")
    finally:
        server.shutdown()

    if isinstance(wasm, dict):
        print("the page failed: " + wasm.get("error", "?"))
        return 1
    cases = read_cases()
    if [w["input"] for w in wasm] != cases:
        print("the page evaluated other expressions than the file lists")
        return 1

    failed = 0
    for case, got in zip(cases, wasm):
        code, line = run_native(meth, case)
        why = None
        if code != got["code"]:
            why = "exit code %d != %d" % (code, got["code"])
        else:
            why = same_result(line, got["text"].split("\n")[0].strip())
        if why:
            failed += 1
            print("FAIL %s\n     %s" % (case, why))
    print("%d expressions, %d differ" % (len(cases), failed))
    failed += check_api(api)
    failed += check_groups(groups)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
