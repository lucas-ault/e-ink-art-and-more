#!/usr/bin/env python3
"""Screenshot a page, dither it to Spectra 6, and push the frame to the panel.

The panel is 1200 by 1600. Two pixels share a byte, high nibble first.
Rows run left to right: the pair at x and x+1 is stored at y * 600 + x / 2.
"""

import argparse
import subprocess
import sys
from pathlib import Path

import requests
from PIL import Image

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
WIDTH = 1200
HEIGHT = 1600
ROW_BYTES = WIDTH // 2
FRAME_BYTES = ROW_BYTES * HEIGHT

# Nibbles match the panel: 0 black, 1 white, 2 yellow, 3 red, 5 blue, 6 green.
PALETTE = (
    (0, 0, 0, 0x0),
    (255, 255, 255, 0x1),
    (255, 255, 0, 0x2),
    (255, 0, 0, 0x3),
    (0, 0, 255, 0x5),
    (0, 255, 0, 0x6),
)


def screenshot(url: str, dest: Path) -> None:
    profile = dest.parent / "chrome-profile"
    profile.mkdir(parents=True, exist_ok=True)
    cmd = [
        CHROME,
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        "--force-device-scale-factor=1",
        "--no-first-run",
        "--no-default-browser-check",
        f"--user-data-dir={profile}",
        f"--window-size={WIDTH},{HEIGHT}",
        "--virtual-time-budget=10000",
        f"--screenshot={dest}",
        url,
    ]
    print(f"screenshot {url}")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
    if result.returncode != 0 or not dest.exists():
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"chrome screenshot failed ({result.returncode})")


def closest(r: int, g: int, b: int) -> int:
    best = 0
    best_d = 1 << 62
    for index, (pr, pg, pb, _nibble) in enumerate(PALETTE):
        dr = r - pr
        dg = g - pg
        db = b - pb
        dist = dr * dr + dg * dg + db * db
        if dist < best_d:
            best_d = dist
            best = index
    return best


def clamp(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def dither_and_pack(image: Image.Image) -> bytes:
    image = image.convert("RGB")
    if image.size != (WIDTH, HEIGHT):
        image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)

    src = image.tobytes()
    count = WIDTH * HEIGHT
    red = [src[i] for i in range(0, count * 3, 3)]
    green = [src[i + 1] for i in range(0, count * 3, 3)]
    blue = [src[i + 2] for i in range(0, count * 3, 3)]
    nibbles = [0] * count

    for y in range(HEIGHT):
        row = y * WIDTH
        for x in range(WIDTH):
            index = row + x
            cr = clamp(red[index])
            cg = clamp(green[index])
            cb = clamp(blue[index])
            chosen = closest(cr, cg, cb)
            pr, pg, pb, nibble = PALETTE[chosen]
            nibbles[index] = nibble
            err_r = red[index] - pr
            err_g = green[index] - pg
            err_b = blue[index] - pb
            for nx, ny, weight in (
                (x + 1, y, 7),
                (x - 1, y + 1, 3),
                (x, y + 1, 5),
                (x + 1, y + 1, 1),
            ):
                if 0 <= nx < WIDTH and ny < HEIGHT:
                    dest = ny * WIDTH + nx
                    red[dest] += err_r * weight // 16
                    green[dest] += err_g * weight // 16
                    blue[dest] += err_b * weight // 16

    frame = bytearray(FRAME_BYTES)
    for y in range(HEIGHT):
        row = y * WIDTH
        base = y * ROW_BYTES
        for x in range(0, WIDTH, 2):
            color1 = nibbles[row + x]
            color2 = nibbles[row + x + 1]
            frame[base + x // 2] = (color1 << 4) | color2
    return bytes(frame)


def push(host: str, port: int, frame: bytes) -> None:
    base = f"http://{host}:{port}"
    health = requests.get(f"{base}/health", timeout=10)
    health.raise_for_status()
    print(health.text.strip())
    if '"busy":true' in health.text.replace(" ", ""):
        raise SystemExit("panel is busy")
    print(f"posting {len(frame)} bytes to {base}/frame")
    response = requests.post(
        f"{base}/frame",
        data=frame,
        headers={"Content-Type": "application/octet-stream"},
        timeout=180,
    )
    print(response.status_code, response.text.strip())
    response.raise_for_status()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="Page or image URL to render")
    parser.add_argument("--host", required=True, help="ESP IP address")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--save", help="Also copy the screenshot PNG to this path")
    args = parser.parse_args()

    cache = Path(__file__).resolve().parent / ".cache"
    cache.mkdir(parents=True, exist_ok=True)
    png = cache / "page.png"
    screenshot(args.url, png)
    if args.save:
        Path(args.save).write_bytes(png.read_bytes())

    with Image.open(png) as image:
        print(f"screenshot {image.size[0]}x{image.size[1]}")
        frame = dither_and_pack(image)
    if len(frame) != FRAME_BYTES:
        raise SystemExit(f"frame is {len(frame)} bytes, expected {FRAME_BYTES}")
    push(args.host, args.port, frame)


if __name__ == "__main__":
    main()
