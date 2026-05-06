#!/usr/bin/env python3

import argparse
import hmac
import json
import os
import platform
import shlex
import socket
import subprocess
import time

from base64 import b64decode
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def parse_args():
    parser = argparse.ArgumentParser(description="T113Claw LAN remote execution agent")
    parser.add_argument("--bind", default="0.0.0.0", help="Bind address")
    parser.add_argument("--port", type=int, default=8765, help="Listen port")
    parser.add_argument("--username", required=True, help="Basic auth username")
    parser.add_argument("--password", required=True, help="Basic auth password")
    parser.add_argument("--max-timeout", type=int, default=120,
                        help="Maximum command timeout in seconds")
    parser.add_argument("--max-output", type=int, default=4096,
                        help="Maximum characters returned for stdout/stderr each")
    parser.add_argument("--allow-prefix", action="append", default=[],
                        help="Optional command prefix allowlist; can be repeated")
    return parser.parse_args()


def truncate_text(text, limit):
    if len(text) <= limit:
        return text, False
    suffix = "\n... (truncated)"
    keep = max(0, limit - len(suffix))
    return text[:keep] + suffix, True


def make_handler(args):
    class RemoteHandler(BaseHTTPRequestHandler):
        server_version = "T113ClawRemote/0.1"

        def log_message(self, fmt, *fmt_args):
            message = "%s - - [%s] %s" % (
                self.client_address[0],
                self.log_date_time_string(),
                fmt % fmt_args,
            )
            print(message, flush=True)

        def _send_json(self, status_code, payload):
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status_code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _unauthorized(self):
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="T113Claw Remote"')
            self.send_header("Content-Length", "0")
            self.end_headers()

        def _check_auth(self):
            header = self.headers.get("Authorization", "")
            if not header.startswith("Basic "):
                return False
            try:
                decoded = b64decode(header[6:], validate=True).decode("utf-8")
            except Exception:
                return False
            username, sep, password = decoded.partition(":")
            if not sep:
                return False
            return (
                hmac.compare_digest(username, args.username)
                and hmac.compare_digest(password, args.password)
            )

        def _read_json_body(self):
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            try:
                return json.loads(raw.decode("utf-8"))
            except Exception:
                return None

        def do_GET(self):
            if not self._check_auth():
                self._unauthorized()
                return

            if self.path != "/status":
                self._send_json(404, {"error": "not found"})
                return

            payload = {
                "hostname": socket.gethostname(),
                "platform": platform.platform(),
                "cwd": os.getcwd(),
                "user": os.getenv("USER") or os.getenv("USERNAME") or "unknown",
            }
            self._send_json(200, payload)

        def do_POST(self):
            if not self._check_auth():
                self._unauthorized()
                return

            if self.path != "/exec":
                self._send_json(404, {"error": "not found"})
                return

            payload = self._read_json_body()
            if not isinstance(payload, dict):
                self._send_json(400, {"error": "invalid json body"})
                return

            command = payload.get("command")
            timeout = payload.get("timeout", 30)
            working_directory = payload.get("working_directory")

            if not isinstance(command, str) or not command.strip():
                self._send_json(400, {"error": "missing command"})
                return

            if not isinstance(timeout, int):
                self._send_json(400, {"error": "timeout must be an integer"})
                return

            timeout = max(1, min(timeout, args.max_timeout))

            if working_directory is not None:
                if not isinstance(working_directory, str) or not working_directory.strip():
                    self._send_json(400, {"error": "working_directory must be a non-empty string"})
                    return
                if not os.path.isdir(working_directory):
                    self._send_json(400, {"error": "working_directory does not exist"})
                    return

            if args.allow_prefix:
                stripped = command.lstrip()
                if not any(stripped.startswith(prefix) for prefix in args.allow_prefix):
                    self._send_json(403, {
                        "error": "command rejected by allowlist",
                        "allowed_prefixes": args.allow_prefix,
                    })
                    return

            start = time.perf_counter()
            timed_out = False
            try:
                proc = subprocess.run(
                    command,
                    shell=True,
                    capture_output=True,
                    text=True,
                    timeout=timeout,
                    cwd=working_directory,
                )
                stdout = proc.stdout or ""
                stderr = proc.stderr or ""
                exit_code = proc.returncode
            except subprocess.TimeoutExpired as exc:
                stdout = exc.stdout or ""
                stderr = exc.stderr or ""
                exit_code = 124
                timed_out = True

            duration_ms = int((time.perf_counter() - start) * 1000)
            stdout, stdout_truncated = truncate_text(stdout, args.max_output)
            stderr, stderr_truncated = truncate_text(stderr, args.max_output)

            self._send_json(200, {
                "command": command,
                "argv_hint": shlex.split(command)[:8] if command.strip() else [],
                "exit_code": exit_code,
                "timed_out": timed_out,
                "duration_ms": duration_ms,
                "stdout": stdout,
                "stderr": stderr,
                "stdout_truncated": stdout_truncated,
                "stderr_truncated": stderr_truncated,
            })

    return RemoteHandler


def main():
    args = parse_args()
    handler = make_handler(args)
    server = ThreadingHTTPServer((args.bind, args.port), handler)
    print(
        f"T113Claw remote agent listening on http://{args.bind}:{args.port} "
        f"(allow_prefix={args.allow_prefix or 'ANY'})",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()