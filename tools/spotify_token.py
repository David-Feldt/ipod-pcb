#!/usr/bin/env python3
"""Get a Spotify refresh token for the E-ink Remote (development path).

Runs the PKCE authorization-code flow against your Spotify developer app:
opens the Spotify login page in your browser, catches the redirect on
http://127.0.0.1:8888/callback, exchanges the code and gets a refresh token.

    python3 tools/spotify_token.py                          # print the token
    python3 tools/spotify_token.py --device /dev/cu.usbmodem101   # send it to the board

With --device the token is written straight to the board's console
(`token set ...`) and never printed. That needs pyserial (the ESP-IDF Python
environment has it). Close any open serial monitor first.

The app must list http://127.0.0.1:8888/callback as a redirect URI, and your
Spotify account must be in the app's User Management list.
"""
import argparse
import base64
import hashlib
import http.server
import json
import secrets
import sys
import threading
import time
import urllib.parse
import urllib.request
import webbrowser

DEFAULT_CLIENT_ID = "fb6818b0834e4e418b6ad5ef191383ee"   # not secret (PKCE has no client secret)
SCOPES = "user-read-playback-state user-modify-playback-state"
PORT = 8888
REDIRECT_URI = f"http://127.0.0.1:{PORT}/callback"


def pkce_pair():
    verifier = secrets.token_urlsafe(64)[:96]
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    return verifier, challenge


def wait_for_code(auth_url, state, timeout_s=300):
    result = {}
    done = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            if url.path != "/callback":
                self.send_error(404)
                return
            q = urllib.parse.parse_qs(url.query)
            if q.get("state", [""])[0] != state:
                result["error"] = "state mismatch (stale browser tab?)"
            elif "error" in q:
                result["error"] = q["error"][0]
            else:
                result["code"] = q.get("code", [""])[0]
            ok = "code" in result
            body = ("<h2>E-ink Remote: signed in.</h2><p>You can close this tab.</p>" if ok
                    else f"<h2>Sign-in failed: {result.get('error')}</h2>")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(body.encode())
            done.set()

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print("Opening the Spotify login page. If no browser opens, visit:\n" + auth_url + "\n", file=sys.stderr)
    webbrowser.open(auth_url)
    finished = done.wait(timeout_s)
    server.shutdown()
    if not finished:
        sys.exit("Timed out waiting for the browser login.")
    if "error" in result:
        sys.exit("Spotify login failed: " + result["error"])
    return result["code"]


def exchange(client_id, code, verifier):
    data = urllib.parse.urlencode({
        "grant_type": "authorization_code", "code": code, "redirect_uri": REDIRECT_URI,
        "client_id": client_id, "code_verifier": verifier,
    }).encode()
    req = urllib.request.Request("https://accounts.spotify.com/api/token", data=data,
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        sys.exit(f"Token exchange failed: HTTP {e.code} {e.read().decode(errors='replace')}")


def send_to_device(port, token):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial missing: run this with the ESP-IDF Python (. firmware/remote/env.sh) or pip install pyserial")
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.write(b"\r\n")
        time.sleep(0.5)
        s.reset_input_buffer()
        s.write(f"token set {token}\r\n".encode())
        out, deadline = b"", time.time() + 5
        while time.time() < deadline and b"saved" not in out and b"failed" not in out:
            out += s.read(256)
    if b"saved" not in out:
        sys.exit("The board did not confirm the token. Is the remote> console running on " + port + "?")
    print("Token saved on the board. Check it with `spotify refresh` and `spotify now` in the console.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--client-id", default=DEFAULT_CLIENT_ID)
    ap.add_argument("--device", help="serial port of the board; sends `token set` instead of printing")
    args = ap.parse_args()

    verifier, challenge = pkce_pair()
    state = secrets.token_urlsafe(16)
    auth_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "client_id": args.client_id, "response_type": "code", "redirect_uri": REDIRECT_URI,
        "code_challenge_method": "S256", "code_challenge": challenge, "scope": SCOPES, "state": state,
    })
    code = wait_for_code(auth_url, state)
    tokens = exchange(args.client_id, code, verifier)
    refresh = tokens.get("refresh_token")
    if not refresh:
        sys.exit("Spotify returned no refresh token: " + json.dumps(tokens)[:300])
    print(f"Signed in; scopes: {tokens.get('scope')}", file=sys.stderr)
    if args.device:
        send_to_device(args.device, refresh)
    else:
        print("\nPaste this into the board's console:\n")
        print(f"token set {refresh}")


if __name__ == "__main__":
    main()
