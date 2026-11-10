# Pairing bounce page

`index.html` is the Spotify redirect URI for the E-ink Remote's sign-in. Spotify
only redirects to HTTPS (or loopback) addresses, while the remote serves plain
HTTP on the local network, so this static page forwards `?code&state` to
`http://<remote-ip>/callback`. It has no server code and stores nothing. It only
forwards to private IPv4 addresses (10/8, 172.16/12, 192.168/16, 169.254/16).

## Hosting (Cloudflare Pages)

The repo is private, so host only this folder:

```sh
npx wrangler pages deploy pairing --project-name eink-remote-pair
```

Then:

1. In the Spotify developer dashboard, add the deployed page as a redirect URI,
   for example `https://eink-remote-pair.pages.dev/` (keep
   `http://127.0.0.1:8888/callback` for the development tools).
2. Set the same URL as **Spotify redirect URI** under **E-ink Remote** in
   `idf.py menuconfig` (`CONFIG_REMOTE_PAIRING_REDIRECT_URI`), rebuild and flash.
   The board and the dashboard must match exactly, including the trailing slash.

## Local testing

`python3 tools/pairing_bounce.py` serves this page at
`http://127.0.0.1:8888/callback`, which the firmware uses while
`CONFIG_REMOTE_PAIRING_REDIRECT_URI` is empty. Open the remote's setup page in a
browser on the same computer.
