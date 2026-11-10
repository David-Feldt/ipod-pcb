#!/usr/bin/env python3
"""Serve the pairing bounce page locally (development path for milestone 6).

Until pairing/index.html is hosted on HTTPS, the firmware uses the loopback
redirect URI already registered for tools/spotify_token.py:

    http://127.0.0.1:8888/callback

Run this, then on the SAME computer open the remote's setup page
(http://<device-ip>/, shown on the e-paper; `pair start` in the console if it
isn't up) and press "Sign in with Spotify". Spotify returns to this server,
which serves pairing/index.html, and that page forwards the code to the board.

    python3 tools/pairing_bounce.py
"""
import http.server
import pathlib
import sys

PORT = 8888
PAGE = pathlib.Path(__file__).resolve().parent.parent / "pairing" / "index.html"


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] != "/callback":
            self.send_error(404)
            return
        body = PAGE.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)
        print("bounced a sign-in to the remote", file=sys.stderr)

    def log_message(self, *args):
        pass


def main():
    server = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
    print(f"Serving {PAGE.relative_to(PAGE.parent.parent)} on http://127.0.0.1:{PORT}/callback (Ctrl-C to stop)",
          file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
