# Studio

The studio is two processes. [tools/studio.py](../tools/studio.py) is the API on `127.0.0.1:8765`. [web/](../web/) is a Next.js app on port 3000. `npm run dev` from `web/` starts both, and kills both when you stop it. `npm run dev:web` starts Next only, which is what you want when the API is already running.

Next must be the system Node. The API must be a Python with Pillow and `requests`. Point the `api` script in [web/package.json](../web/package.json) at that interpreter. The ESP-IDF virtualenv is the wrong one: it can build firmware and it cannot dither.

The studio binds `127.0.0.1` without a reverse DNS lookup. The stock `TCPServer` calls `getfqdn` during `bind`, and that call can sit forever on mDNS for `127.0.0.1`. `StudioServer.server_bind` skips it. The same hang hits `python -m http.server` on this machine, so a throwaway fixture server needs the same override.

## Pages

Side navigation from tablet width up, bottom navigation on a phone. The artwork is the color on the page. The chrome is one light shell.

**Currently showing** (`/`) reads `GET /api/now` and the library. It shows the six-color preview of the last piece sent, the panel address, and `idle` or `refreshing`. Next calls `POST /api/next`. Clear calls `POST /api/clear`. An empty library points at Upload. After a clear, the caption says the glass is white and still names the piece that was last sent.

**Upload art** (`/upload`) takes a file or a URL. The crop is a 3:4 window: drag to pan, scroll to zoom. The minimum zoom covers the frame, so the crop does not letterbox. Preview posts `/api/preview`. Show saves with `POST /api/library` and then pushes with `POST /api/show`. A file wins over a URL when both are filled in. If the save works and the push fails, the piece is already in the library.

**Select art** (`/library`) is a grid of stored previews. Show calls `POST /api/show`. Remove calls `DELETE /api/library/{id}`.

**Settings** (`/settings`) edits the panel address, shows idle or refreshing, and has the Wi-Fi form. The note under the form is the real constraint: after you switch the board, this computer cannot reach it until the computer is on that network too. The password field is cleared in the browser after a successful submit. The API does not write it.

## Library on disk

```
data/library/
  index.json          ordered public list
  now.json            {id, title, host} of the last push
  <id>/
    image.png         original, before crop
    frame.bin         960000 packed bytes
    meta.json         title, kind, url, crop, size, added
```

`index.json` is the order `POST /api/next` walks. `kind` is `image` or `page`. `url` is set for pieces that started as a URL. `added` is UTC, `YYYY-MM-DDTHH:MM:SSZ`.

`now.json` is updated only by Show and Next. Clear and Delete do not change it. `GET /api/now` merges that file with a live health check, so `busy` is the panel's flag and `id` is the last piece the studio pushed.

`data/` is gitignored. Losing the directory loses the library. The glass will keep showing whatever was last refreshed.

## Crop

The cropper works in source-image pixels. Its view is 360×480, which is the panel's 3:4 aspect. On first load the zoom is large enough that the image covers that view, centered. The object sent to the API is:

```json
{ "x": 0, "y": 120, "width": 900, "height": 1200 }
```

`x` and `y` are the top-left of the visible region. `width` and `height` are the visible region in source pixels (`360 / zoom` and `480 / zoom` in the cropper's coordinate space). The studio crops that box and resizes it to 1200×1600 with Lanczos, then dithers.

## Screenshots

Page URLs go through Chrome at `/Applications/Google Chrome.app`, headless, window 1200×1600, device scale factor 1, virtual time budget 10 seconds. The profile is `tools/.cache/chrome-profile`. Image URLs are downloaded and opened with Pillow, so a direct image link is not screenshotted.

## Timeouts worth knowing

| Step | Budget |
| --- | --- |
| Download a URL | 30 s |
| Chrome screenshot | 90 s |
| Health check before a push | 10 s |
| `POST /frame` and `POST /clear` | 180 s |
| Next.js proxy | 180 s |

Dithering a full frame is a few seconds and happens at save time, not on Next. Next and Show only read `frame.bin` and post it.
