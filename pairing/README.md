# Pairing bounce page

`index.html` is the Spotify redirect URI for the E-ink Remote's sign-in. Spotify
only redirects to HTTPS (or loopback) addresses, while the remote serves plain
HTTP on the local network, so this static page forwards `?code&state` to
`http://<remote-ip>/callback`. It has no server code and stores nothing. It only
forwards to private IPv4 addresses (10/8, 172.16/12, 192.168/16, 169.254/16).

## Hosting (GitHub Pages)

This repo is private, so the page is published from a separate public repo,
[David-Feldt/eink-remote-pair](https://github.com/David-Feldt/eink-remote-pair),
at **https://david-feldt.github.io/eink-remote-pair/**. That URL is the firmware default
(`CONFIG_REMOTE_PAIRING_REDIRECT_URI`) and must be listed under Redirect URIs in
the Spotify developer dashboard, exactly, including the trailing slash.

To publish a change, copy `index.html` into that repo and push:

```sh
git clone https://github.com/David-Feldt/eink-remote-pair /tmp/pair
cp pairing/index.html /tmp/pair/ && git -C /tmp/pair commit -am "Update bounce page" && git -C /tmp/pair push
```

## Local testing

With `CONFIG_REMOTE_PAIRING_REDIRECT_URI` set to empty in menuconfig, the board
uses `http://127.0.0.1:8888/callback`; `python3 tools/pairing_bounce.py` serves
this page there. Open the remote's setup page in a browser on the same computer.
