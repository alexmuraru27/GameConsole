#!/usr/bin/env python3
"""GameConsole update server.

Serves a content tree (games + .paks, the ESP-01 WiFi firmware, and — later —
console OS images) over plain HTTP so the console can poll for and pull updates
once its WiFi link is implemented. It exposes:

    GET /manifest.csv   the live manifest (category,name,path,size,crc32,version)
    GET /<path>         a file from the content tree (e.g. /games/GameXO.bin)

The console fetches the manifest, compares each entry's CRC-32 (and/or version)
against what it already has, downloads what changed, and re-checks the CRC-32
after download to confirm integrity.

Standard library only. Intended for a trusted LAN, not the public internet.

Usage:
    python update_server.py                              # serve content/ on :25568, refresh manifest.csv every 15s
    python update_server.py --root content --port 25568  # same, explicit
    python update_server.py --refresh 0                  # serve without the periodic on-disk manifest refresh
    python update_server.py --generate                   # only write manifest.csv, then exit
"""
from __future__ import annotations

import argparse
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from updateserver import Catalog, MANIFEST_NAME, manifest


def _make_handler(root: Path, catalog: Catalog):
    """Build a request handler that serves a live manifest + static files.

    Downloads are confined to `root`: SimpleHTTPRequestHandler.translate_path()
    already strips '..' and percent-encoding (blocking '/../../.env'-style
    traversal), and `_within_root()` additionally resolves symlinks and re-checks
    containment, so a symlink inside the tree can't leak files from elsewhere in
    the project. Anything resolving outside `root` is refused with 403.
    """
    root = root.resolve()

    class UpdateHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(root), **kwargs)

        def log_message(self, fmt, *args):  # quieter, prefixed logging
            sys.stderr.write(f"[update-server] {self.address_string()} - {fmt % args}\n")

        def _within_root(self) -> bool:
            """True iff the requested path resolves to a file inside `root`."""
            target = Path(self.translate_path(self.path)).resolve()
            return target == root or target.is_relative_to(root)

        def do_GET(self):
            if self.path.split("?", 1)[0] in ("/", "/" + MANIFEST_NAME):
                self._send_manifest()
            elif not self._within_root():
                self.send_error(403, "Forbidden")
            else:
                super().do_GET()  # static download from `directory`

        def do_HEAD(self):
            if not self._within_root():
                self.send_error(403, "Forbidden")
            else:
                super().do_HEAD()

        def _send_manifest(self):
            body = manifest.to_csv(catalog.scan()).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/csv; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return UpdateHandler


def _print_catalog(catalog: Catalog) -> None:
    entries = catalog.scan()
    if not entries:
        print("  (no content found)")
        return
    for e in entries:
        print(f"  {e.category:<8} {e.path:<32} {e.size:>10} B  crc={e.crc32:08x}  v{e.version}")


def _write_manifest(root: Path, catalog: Catalog) -> str:
    """Write a snapshot manifest.csv into the root (the live endpoint stays fresh).
    Returns the CSV text written, so callers can track changes without re-reading."""
    text = manifest.to_csv(catalog.scan())
    out = root / MANIFEST_NAME
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out} ({len(text.splitlines()) - 1} entries)")
    return text


def _manifest_refresher(root: Path, catalog: Catalog, interval: float,
                        initial_text: str, stop: threading.Event) -> None:
    """Rewrite manifest.csv on disk every `interval` seconds while serving, so
    files dropped into the tree (e.g. by `make deploy`) are reflected in the
    on-disk snapshot without restarting the server. The live /manifest.csv
    endpoint already re-scans per request; this only keeps the disk copy in sync.
    Stays quiet unless the manifest actually changes, and exits promptly when
    `stop` is set."""
    last_text = initial_text
    out = root / MANIFEST_NAME
    while not stop.wait(interval):
        try:
            text = manifest.to_csv(catalog.scan())
            if text != last_text:
                out.write_text(text, encoding="utf-8")
                last_text = text
                print(f"[update-server] manifest refreshed ({len(text.splitlines()) - 1} entries)")
        except OSError as exc:  # transient FS hiccup mid-deploy; try again next tick
            print(f"[update-server] manifest refresh failed: {exc}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path("content"),
                        help="content directory to serve (default: ./content)")
    parser.add_argument("--host", default="0.0.0.0", help="bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=25568, help="bind port (default: 25568)")
    parser.add_argument("--refresh", type=float, default=15.0, metavar="SECONDS",
                        help="rewrite manifest.csv on disk every SECONDS while serving "
                             "(default: 15; use 0 to disable)")
    parser.add_argument("--generate", action="store_true",
                        help="only write manifest.csv into --root and exit (don't serve)")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    if not root.is_dir():
        parser.error(f"content root does not exist: {root}")

    catalog = Catalog(root)

    # Always write a manifest snapshot to disk; --generate stops there.
    initial_manifest = _write_manifest(root, catalog)
    if args.generate:
        return 0

    print(f"Serving {root}")
    _print_catalog(catalog)

    try:
        httpd = ThreadingHTTPServer((args.host, args.port), _make_handler(root, catalog))
    except OSError as exc:
        print(f"\nerror: cannot bind {args.host}:{args.port} - {exc}")
        print("is another instance already running? stop it, or pick another port with --port")
        return 1

    # Keep the on-disk manifest.csv in sync with the content tree while serving.
    stop = threading.Event()
    if args.refresh > 0:
        threading.Thread(
            target=_manifest_refresher,
            args=(root, catalog, args.refresh, initial_manifest, stop),
            daemon=True,
        ).start()
        print(f"Refreshing {MANIFEST_NAME} every {args.refresh:g}s")

    print(f"\nListening on http://{args.host}:{args.port}/  (manifest at /{MANIFEST_NAME})")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        stop.set()
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
