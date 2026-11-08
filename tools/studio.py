#!/usr/bin/env python3
"""Local API for loading a picture, cropping it, and pushing it to the panel.

    python3 tools/studio.py

Listens on 127.0.0.1:8765. The page at / is one client. The same routes take
curl. The password in a Wi-Fi change is forwarded to the board and is not
written here.
"""

import io
import json
import socket
import sys
import threading
from email.parser import BytesParser
from email.policy import HTTP
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse
from uuid import uuid4

import requests
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render_and_push import (  # noqa: E402
    FRAME_BYTES,
    HEIGHT,
    WIDTH,
    dither_and_pack,
    push,
    screenshot,
)

HOST = "127.0.0.1"
PORT = 8765
CACHE = Path(__file__).resolve().parent / ".cache"
PAGE = Path(__file__).resolve().parent / "studio.html"
SETTINGS = CACHE / "studio.json"
MAX_BODY = 40 * 1024 * 1024

LOCK = threading.Lock()
SOURCES = {}

RGB = {
    0x0: bytes((0, 0, 0)),
    0x1: bytes((255, 255, 255)),
    0x2: bytes((255, 255, 0)),
    0x3: bytes((255, 0, 0)),
    0x5: bytes((0, 0, 255)),
    0x6: bytes((0, 255, 0)),
}
WHITE = RGB[0x1]
FRAME_LUT = tuple(
    RGB.get(value >> 4, WHITE) + RGB.get(value & 0x0F, WHITE) for value in range(256)
)


class StudioError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def load_settings():
    try:
        return json.loads(SETTINGS.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def remember_host(host):
    CACHE.mkdir(parents=True, exist_ok=True)
    data = load_settings()
    data["host"] = host
    data.pop("password", None)
    data.pop("ssid", None)
    SETTINGS.write_text(json.dumps(data))


def split_host(value):
    host = str(value or "").strip()
    if not host or any(ch in host for ch in " /\\?#@"):
        raise StudioError(400, "invalid host")
    if host.count(":") == 1:
        name, port_text = host.rsplit(":", 1)
        if not name or not port_text.isdigit():
            raise StudioError(400, "invalid host")
        port = int(port_text)
        if not 1 <= port <= 65535:
            raise StudioError(400, "invalid host")
        return name, port
    if ":" in host:
        raise StudioError(400, "invalid host")
    return host, 80


def read_json(raw):
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise StudioError(400, "invalid json") from exc
    if not isinstance(data, dict):
        raise StudioError(400, "expected a json object")
    return data


def crop_box(image, crop):
    if not isinstance(crop, dict):
        raise StudioError(400, "crop is required")
    try:
        x = float(crop["x"])
        y = float(crop["y"])
        w = float(crop["width"])
        h = float(crop["height"])
    except (KeyError, TypeError, ValueError) as exc:
        raise StudioError(400, "crop needs x, y, width, and height") from exc
    if w <= 0 or h <= 0:
        raise StudioError(400, "crop is empty")
    x = min(max(0.0, x), max(0, image.width - 1))
    y = min(max(0.0, y), max(0, image.height - 1))
    w = min(w, image.width - x)
    h = min(h, image.height - y)
    left = int(round(x))
    top = int(round(y))
    right = max(left + 1, min(image.width, int(round(x + w))))
    bottom = max(top + 1, min(image.height, int(round(y + h))))
    return image.crop((left, top, right, bottom)).resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)


def store_source(image, kind):
    image = image.convert("RGB")
    source_id = uuid4().hex
    with LOCK:
        SOURCES[source_id] = {"image": image, "kind": kind}
    return {
        "id": source_id,
        "kind": kind,
        "width": image.width,
        "height": image.height,
    }


def source_image(source_id):
    if not isinstance(source_id, str) or len(source_id) != 32:
        raise StudioError(404, "unknown source")
    with LOCK:
        found = SOURCES.get(source_id)
        if found is None:
            raise StudioError(404, "unknown source")
        return found["image"].copy(), found["kind"]


def open_bytes(data):
    try:
        image = Image.open(io.BytesIO(data))
        image.load()
        return image
    except Exception:
        return None


def load_url(url):
    if not isinstance(url, str) or not url.startswith(("http://", "https://")):
        raise StudioError(400, "url must start with http:// or https://")
    try:
        response = requests.get(
            url,
            timeout=30,
            headers={"User-Agent": "spectra6-studio"},
        )
        response.raise_for_status()
    except requests.RequestException as exc:
        raise StudioError(502, f"could not download url: {exc}") from exc
    if len(response.content) > MAX_BODY:
        raise StudioError(400, "download is too large")
    ctype = response.headers.get("Content-Type", "").split(";")[0].strip().lower()
    image = open_bytes(response.content)
    if ctype.startswith("image/"):
        if image is None:
            raise StudioError(400, "response was not a readable image")
        return image, "image"
    if image is not None:
        return image, "image"
    CACHE.mkdir(parents=True, exist_ok=True)
    dest = CACHE / "page.png"
    try:
        screenshot(url, dest)
        with Image.open(dest) as shot:
            shot.load()
            return shot.copy(), "page"
    except RuntimeError as exc:
        raise StudioError(502, str(exc)) from exc


def parse_multipart(content_type, body):
    header = f"Content-Type: {content_type}\r\n\r\n".encode()
    message = BytesParser(policy=HTTP).parsebytes(header + body)
    fields = {}
    if not message.is_multipart():
        raise StudioError(400, "expected a file upload")
    for part in message.iter_parts():
        name = part.get_param("name", header="content-disposition")
        if not name:
            continue
        payload = part.get_payload(decode=True) or b""
        fields[name] = {
            "filename": part.get_filename(),
            "data": payload,
        }
    return fields


def frame_png(frame):
    raw = b"".join(FRAME_LUT[byte] for byte in frame)
    image = Image.frombytes("RGB", (WIDTH, HEIGHT), raw)
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def render_frame(source_id, crop):
    image, _kind = source_image(source_id)
    framed = crop_box(image, crop)
    frame = dither_and_pack(framed)
    if len(frame) != FRAME_BYTES:
        raise StudioError(500, "frame packing failed")
    return frame


def board_request(method, host, path, body=None, timeout=10, content_type=None):
    name, port = split_host(host)
    headers = {}
    if content_type:
        headers["Content-Type"] = content_type
    try:
        return requests.request(
            method,
            f"http://{name}:{port}{path}",
            data=body,
            headers=headers,
            timeout=timeout,
        )
    except requests.RequestException as exc:
        raise StudioError(502, f"board unreachable: {exc}") from exc


class StudioServer(ThreadingHTTPServer):
    # TCPServer.server_bind asks for a reverse DNS name and can sit on mDNS.
    def server_bind(self):
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind(self.server_address)
        self.server_address = self.socket.getsockname()
        self.server_name = "127.0.0.1"
        self.server_port = self.server_address[1]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def address_string(self):
        return self.client_address[0]

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.client_address[0], fmt % args))

    def do_GET(self):
        try:
            self.route_get()
        except StudioError as exc:
            self.send_json(exc.status, {"error": str(exc)})
        except Exception as exc:
            self.send_json(500, {"error": str(exc)})

    def do_POST(self):
        try:
            self.route_post()
        except StudioError as exc:
            self.send_json(exc.status, {"error": str(exc)})
        except Exception as exc:
            self.send_json(500, {"error": str(exc)})

    def route_get(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        if parsed.path == "/":
            self.send_bytes(200, "text/html; charset=utf-8", PAGE.read_bytes())
            return
        if parsed.path == "/api/settings":
            self.send_json(200, {"host": load_settings().get("host", "")})
            return
        if parsed.path == "/api/device":
            self.proxy_get(query, "/health", 5)
            return
        if parsed.path == "/api/wifi":
            self.proxy_get(query, "/wifi", 5)
            return
        prefix = "/api/sources/"
        if parsed.path.startswith(prefix):
            image, _kind = source_image(parsed.path[len(prefix):])
            out = io.BytesIO()
            image.save(out, format="PNG")
            self.send_bytes(200, "image/png", out.getvalue())
            return
        raise StudioError(404, "not found")

    def route_post(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        length = int(self.headers.get("Content-Length", "0") or 0)
        if length < 0 or length > MAX_BODY:
            raise StudioError(400, "body is too large")
        body = self.rfile.read(length) if length else b""
        ctype = self.headers.get("Content-Type", "")
        if parsed.path == "/api/sources":
            self.post_source(ctype, body)
            return
        data = read_json(body or b"{}")
        if parsed.path == "/api/preview":
            frame = render_frame(data.get("id"), data.get("crop"))
            if query.get("format", [""])[0] == "frame":
                self.send_bytes(200, "application/octet-stream", frame)
            else:
                self.send_bytes(200, "image/png", frame_png(frame))
            return
        if parsed.path == "/api/push":
            self.post_push(data)
            return
        if parsed.path == "/api/clear":
            self.post_clear(data)
            return
        if parsed.path == "/api/wifi":
            self.post_wifi(data)
            return
        raise StudioError(404, "not found")

    def post_source(self, ctype, body):
        if ctype.startswith("application/json"):
            data = read_json(body)
            image, kind = load_url(data.get("url"))
        elif ctype.startswith("multipart/form-data"):
            fields = parse_multipart(ctype, body)
            upload = fields.get("file")
            if not upload or not upload["data"]:
                raise StudioError(400, "file is required")
            image = open_bytes(upload["data"])
            if image is None:
                raise StudioError(400, "file is not a readable image")
            kind = "image"
        else:
            raise StudioError(400, "send a file or a json url")
        self.send_json(200, store_source(image, kind))

    def post_push(self, data):
        host = data.get("host", "")
        name, port = split_host(host)
        remember_host(name if port == 80 else f"{name}:{port}")
        frame = render_frame(data.get("id"), data.get("crop"))
        try:
            push(name, port, frame)
        except RuntimeError as exc:
            if "busy" in str(exc):
                raise StudioError(409, "panel is busy") from exc
            raise StudioError(502, str(exc)) from exc
        except requests.HTTPError as exc:
            code = exc.response.status_code if exc.response is not None else 502
            if code == 409:
                raise StudioError(409, "panel is busy") from exc
            raise StudioError(502, f"board returned {code}") from exc
        except requests.RequestException as exc:
            raise StudioError(502, f"board unreachable: {exc}") from exc
        self.send_json(200, {"ok": True})

    def post_clear(self, data):
        host = data.get("host", "")
        name, port = split_host(host)
        remember_host(name if port == 80 else f"{name}:{port}")
        response = board_request("POST", host, "/clear", body=b"", timeout=180)
        if response.status_code == 409:
            raise StudioError(409, "panel is busy")
        if response.status_code >= 400:
            raise StudioError(502, f"board returned {response.status_code}")
        self.send_json(200, {"ok": True})

    def post_wifi(self, data):
        host = data.get("host", "")
        ssid = data.get("ssid", "")
        password = data.get("password", "")
        if not isinstance(ssid, str) or not ssid.strip():
            raise StudioError(400, "ssid is required")
        if not isinstance(password, str):
            raise StudioError(400, "password must be a string")
        name, port = split_host(host)
        remember_host(name if port == 80 else f"{name}:{port}")
        payload = json.dumps({"ssid": ssid, "password": password}).encode()
        response = board_request(
            "POST",
            host,
            "/wifi",
            body=payload,
            timeout=10,
            content_type="application/json",
        )
        self.forward(response)

    def proxy_get(self, query, path, timeout):
        host = query.get("host", [""])[0]
        name, port = split_host(host)
        remember_host(name if port == 80 else f"{name}:{port}")
        response = board_request("GET", host, path, timeout=timeout)
        self.forward(response)

    def forward(self, response):
        ctype = response.headers.get("Content-Type", "application/json")
        payload = response.content
        self.send_response(response.status_code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def send_json(self, status, payload):
        self.send_bytes(status, "application/json", json.dumps(payload).encode())

    def send_bytes(self, status, content_type, payload):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    server = StudioServer((HOST, PORT), Handler)
    print(f"studio http://{HOST}:{PORT}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
