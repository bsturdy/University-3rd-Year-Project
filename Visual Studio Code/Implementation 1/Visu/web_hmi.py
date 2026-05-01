from __future__ import annotations

import argparse
import json
import mimetypes
import os
import sys
import threading
import time
from dataclasses import asdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from ads_snapshot import DemoSnapshotReader, LazyAdsSnapshotReader, MeshSnapshot


APP_DIR = Path(__file__).resolve().parent
WEB_DIR = APP_DIR / "web"
CONFIG_PATH = APP_DIR / "config.json"


class SnapshotCache:
    def __init__(self, reader, refresh_s: float) -> None:
        self.reader = reader
        self.refresh_s = refresh_s
        self.lock = threading.Lock()
        self.snapshot = MeshSnapshot(0, 0, 0, [], "Starting")
        self.running = False
        self.thread: threading.Thread | None = None

    def start(self) -> None:
        self.running = True
        self.thread = threading.Thread(target=self._run, name="ads-snapshot-reader", daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.running = False
        if self.thread is not None:
            self.thread.join(timeout=2.0)
        try:
            self.reader.set_visualisation_ready(False)
        except Exception as exc:
            print(f"Failed to clear visualisation ready flag: {type(exc).__name__}: {exc}", flush=True)
        self.reader.close()

    def get(self) -> MeshSnapshot:
        with self.lock:
            return self.snapshot

    def _run(self) -> None:
        while self.running:
            try:
                snapshot = self.reader.read()
                self.reader.set_visualisation_ready(True)
            except Exception as exc:
                previous = self.get()
                snapshot = MeshSnapshot(
                    previous.master_uid,
                    previous.device_count,
                    previous.update_counter,
                    previous.devices,
                    f"{type(exc).__name__}: {exc}",
                )

            with self.lock:
                self.snapshot = snapshot

            time.sleep(self.refresh_s)


class HmiRequestHandler(BaseHTTPRequestHandler):
    cache: SnapshotCache

    def do_GET(self) -> None:
        parsed = urlparse(self.path)

        if parsed.path == "/api/snapshot":
            self._send_json(snapshot_to_json(self.cache.get()))
            return

        if parsed.path in ("", "/"):
            requested = WEB_DIR / "index.html"
        else:
            requested = (WEB_DIR / parsed.path.lstrip("/")).resolve()
            if WEB_DIR.resolve() not in requested.parents and requested != WEB_DIR.resolve():
                self.send_error(403)
                return

        if not requested.exists() or not requested.is_file():
            self.send_error(404)
            return

        content_type = mimetypes.guess_type(str(requested))[0] or "application/octet-stream"
        data = requested.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store" if requested.name == "index.html" else "public, max-age=60")
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format: str, *args) -> None:
        sys.stdout.write("%s - %s\n" % (self.address_string(), format % args))

    def _send_json(self, payload: dict) -> None:
        data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)


def snapshot_to_json(snapshot: MeshSnapshot) -> dict:
    payload = asdict(snapshot)
    payload["server_time"] = time.time()
    return payload


def load_config() -> dict:
    if CONFIG_PATH.exists():
        return json.loads(CONFIG_PATH.read_text(encoding="utf-8-sig"))
    return {}


def parse_args(argv: list[str]) -> argparse.Namespace:
    config = load_config()
    parser = argparse.ArgumentParser(description="LAN web HMI for TwinCAT ESP mesh data")
    parser.add_argument("--netid", default=config.get("ams_net_id") or os.environ.get("TC_AMS_NET_ID") or "local")
    parser.add_argument("--ads-port", type=int, default=int(config.get("ads_port", 851)))
    parser.add_argument("--host", default=config.get("http_host", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(config.get("http_port", 8080)))
    parser.add_argument("--refresh-ms", type=int, default=int(config.get("refresh_ms", 1000)))
    parser.add_argument("--demo", action="store_true", default=bool(config.get("demo", False)))
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if args.demo:
        reader = DemoSnapshotReader()
    else:
        reader = LazyAdsSnapshotReader(args.netid, args.ads_port)

    cache = SnapshotCache(reader, max(args.refresh_ms / 1000.0, 0.1))
    cache.start()

    HmiRequestHandler.cache = cache
    server = ThreadingHTTPServer((args.host, args.port), HmiRequestHandler)

    print(f"ESP mesh HMI serving on http://{args.host}:{args.port}", flush=True)
    print(f"Local page: http://127.0.0.1:{args.port}", flush=True)
    print("Use the controller LAN IP from another device on the router network.", flush=True)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        cache.stop()

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
