#!/usr/bin/env python3
"""A small web front end for oXitipLen.

Serves one page and one endpoint. The prover is the C++ binary; nothing
else is needed at run time, and no shell is ever involved.

    python3 web/server.py [--port 8080] [--binary ./oXitipLen] [--host 0.0.0.0]
"""

import argparse
import json
import os
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))

# A request from a browser is untrusted, so it is bounded on every axis that
# costs something: how much text, how many lines, and how long the prover may
# run. The prover itself is given the lines as arguments, never as a command
# line for a shell to interpret.
MAX_BYTES = 4000
MAX_LINES = 40
TIMEOUT_SECONDS = 20


class Prover:
    def __init__(self, binary):
        self.binary = binary

    def run(self, lines, proof):
        flag = "--proof" if proof else "--prove"
        try:
            done = subprocess.run(
                # "--" so that a line beginning with "-" is input and
                # never mistaken for an option
                [self.binary, flag, "--"] + lines,
                capture_output=True, text=True,
                timeout=TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired:
            return {"ok": False, "error": "timed out"}
        except OSError as e:
            return {"ok": False, "error": "cannot run the prover: %s" % e}

        out = done.stdout
        if done.returncode != 0:
            message = done.stderr.strip() or "the prover failed"
            return {"ok": False, "error": message}

        verdict = None
        if "is TRUE." in out:
            verdict = True
        elif "is FALSE." in out:
            verdict = False
        body = out.split("Proof of", 1)
        proof_text = ("Proof of" + body[1]) if len(body) > 1 else ""
        # the verdict line is printed last; keep it out of the proof block
        proof_text = proof_text.split("The information expression")[0].rstrip()
        return {"ok": True, "verdict": verdict, "proof": proof_text,
                "raw": out}

    def count(self, lines):
        try:
            done = subprocess.run([self.binary, "--"] + lines,
                                  capture_output=True,
                                  text=True, timeout=TIMEOUT_SECONDS)
            return int(done.stdout.strip()) if done.returncode == 0 else None
        except Exception:
            return None


def clean(payload):
    """Validate the request, returning (lines, show_proof) or raising."""
    text = payload.get("expression", "")
    if not isinstance(text, str):
        raise ValueError("expression must be text")
    if len(text.encode("utf-8")) > MAX_BYTES:
        raise ValueError("expression is too long")
    lines = [l.strip() for l in text.splitlines()]
    lines = [l for l in lines if l and not l.startswith("#")]
    if not lines:
        raise ValueError("nothing to prove")
    if len(lines) > MAX_LINES:
        raise ValueError("too many lines")
    for l in lines:
        if any(ord(c) < 32 for c in l):
            raise ValueError("control characters are not allowed")
    return lines, bool(payload.get("proof", True))


class Handler(BaseHTTPRequestHandler):
    prover = None
    server_version = "oXitipWeb/1.0"

    def log_message(self, fmt, *args):            # quieter than the default
        pass

    def _send(self, code, body, kind="application/json"):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                self._send(200, f.read(), "text/html; charset=utf-8")
        else:
            self._send(404, json.dumps({"error": "not found"}))

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/api/prove":
            self._send(404, json.dumps({"error": "not found"}))
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BYTES * 2:
            self._send(413, json.dumps({"error": "request too large"}))
            return
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
            lines, want_proof = clean(payload)
        except ValueError as e:
            self._send(400, json.dumps({"ok": False, "error": str(e)}))
            return
        except Exception:
            self._send(400, json.dumps({"ok": False, "error": "bad request"}))
            return

        result = self.prover.run(lines, want_proof)
        if result.get("ok"):
            result["variables"] = self.prover.count(lines)
        self._send(200, json.dumps(result))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--host", default="127.0.0.1",
                    help="use 0.0.0.0 to accept connections from elsewhere")
    ap.add_argument("--binary", default=os.path.join(HERE, "..", "oXitipLen"))
    args = ap.parse_args()

    binary = os.path.abspath(args.binary)
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
        raise SystemExit("no prover at %s -- run make first" % binary)

    Handler.prover = Prover(binary)
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print("oXitip web on http://%s:%d  (prover: %s)" %
          (args.host, args.port, binary))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
