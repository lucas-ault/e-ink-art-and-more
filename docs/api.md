# HTTP API

Two servers are involved. The studio on the computer is what the UI and any later automation call. The board has a smaller API that only the studio (or a careful curl) should hit.

The studio listens on `127.0.0.1:8765` and does not serve a page. Errors are JSON: `{"error":"..."}`.

## Studio

### Sources

An upload is an in-memory source. It is not on the glass and it is not in the library until `POST /api/library`.

`POST /api/sources`

- JSON `{"url":"https://..."}`. An image response is stored as kind `image`. Anything else is screenshotted in headless Chrome at 1200×1600 and stored as kind `page`. The title defaults to the hostname.
- Multipart field `file`. Kind `image`. The title defaults to the filename without its extension.

Response: `{"id","kind","title","width","height"}`. `id` is 32 hex characters.

`GET /api/sources/{id}` returns the original PNG. Unknown after a restart.

### Preview and one-shot push

`POST /api/preview` with `{"id","crop"}`.

`crop` is source pixels: `x`, `y`, `width`, `height`. The studio crops that rectangle and resizes it to 1200×1600 before dithering. The response is a six-color PNG. Add `?format=frame` to get the raw 960,000 bytes instead.

`POST /api/push` with `{"id","crop","host"}` dithers again and posts the frame. It does not write the library or `now.json`. Prefer `/api/library` and `/api/show`.

### Library

Pieces are stored under `data/library/{id}/` as `image.png`, `frame.bin`, and `meta.json`. `data/library/index.json` is the ordered list.

`POST /api/library` with `{"id","crop","title"}` copies a source into the library and packs the frame. `title` is optional and is capped at 120 characters. The saved piece gets a new id.

`GET /api/library` returns `{"pieces":[...]}` without image bodies. Each piece has `id`, `title`, `kind`, `width`, `height`, `added`, and `url`.

`GET /api/library/{id}` returns the original PNG.

`GET /api/library/{id}/preview` returns the six-color PNG decoded from `frame.bin`.

`DELETE /api/library/{id}` removes the folder and the index entry. If that piece was the one in `now.json`, Now stays as a record of what was pushed.

### Show, Next, Now, Clear

`POST /api/show` with `{"id","host"}` pushes the stored frame and writes `data/library/now.json`. Response: `{"ok":true,"id","title","host"}`.

`POST /api/next` with `{"host"}` shows the piece after the current id and wraps to the start. One piece shows that piece. An empty library returns `400` and `library is empty`.

`GET /api/now` returns `{"id","title","host","busy","reachable"}`. `busy` and `reachable` come from a live `GET /health` on the panel. `host` falls back to the address in `tools/.cache/studio.json`.

`POST /api/clear` with `{"host"}` posts `/clear` to the board. The glass goes white. `now.json` is left as it was.

Show, Next, push, and clear can take about half a minute. Call them with a timeout above that. `409` means the panel is already refreshing.

### Panel address and Wi-Fi

`GET /api/settings` returns `{"host"}`.

`GET /api/device?host=10.0.0.194` proxies `GET /health` and remembers the host.

`GET /api/wifi?host=...` proxies the board's `GET /wifi`. The password is not in that response.

`POST /api/wifi` with `{"host","ssid","password"}` forwards the change and does not store the password. The computer loses the board until it is on the new network too. See [Firmware](firmware.md).

`host` may be `10.0.0.194` or `10.0.0.194:80`. Any other port is kept. A missing or unparseable host is `invalid host`.

## Board

The board listens on port 80 once it has an address. These routes are also what the phone setup page uses.

| Method | Path | Body | Result |
| --- | --- | --- | --- |
| `GET` | `/health` | | `{"ip","busy"}` |
| `POST` | `/frame` | 960,000 raw bytes, `application/octet-stream` | `ok` after the refresh, or `409` `busy` |
| `POST` | `/clear` | empty | white refresh, or `409` |
| `GET` | `/wifi` | | `{"ssid","ip","switching","state"}` |
| `POST` | `/wifi` | `{"ssid","password"}` | `{"ok":true}` immediately, then the board joins |
| `GET` | `/scan` | | `{"networks":[{"ssid","rssi","auth"}]}` |
| `GET` | `/` | | the setup HTML page |

`state` is `connecting`, `connected`, `setup`, or `failed`. `GET /wifi` never returns the password.

`POST /frame` with the wrong length is `400` and `expected 960000 bytes`.

```bash
curl -s http://10.0.0.194/health
curl -s -X POST http://127.0.0.1:8765/api/next \
  -H 'Content-Type: application/json' \
  -d '{"host":"10.0.0.194"}'
```

Give the second call up to three minutes. A short client timeout does not cancel the push. The studio keeps writing the frame, and the glass still updates.
