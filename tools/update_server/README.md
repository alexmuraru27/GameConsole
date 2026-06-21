# Update Server

A tiny HTTP server that hosts updatable content for the console — games and their
`.pak` asset bundles, the ESP-01 WiFi firmware (`ESP01.bin`), and, later, console
OS images. The console polls it over WiFi (once that link exists; the runtime
network stack is still a stub) to discover and pull updates, verifying each file
with a CRC-32.

This is the PC-side half of the "put files on the server, the console pulls them"
flow. Standard library only (Python 3.10+); intended for a trusted LAN.

## Content layout

One subfolder per **category** under the content root. The category is just the
top-level folder name:

```
content/
  games/    GameXO.bin, GameXO.pak, Tetris.bin, Tetris.pak, ...
  wifi/     ESP01.bin                 (the ESP-01 firmware, see ../../Esp01s)
  os/       GameConsole.bin           (console firmware image — future)
  versions.csv                        (optional, see below)
```

The console maps a category to an action: `games` → copy onto the SD card,
`wifi` → flash the ESP (Settings → Upgrade WiFi module), `os` → console
self-update (future).

The repo's build targets stage straight into this tree: `make deploy` writes
`games/GameXO.bin` + `games/GameXO.pak` and (if it's been built with `make esp`)
`wifi/ESP01.bin`. Drop other files in by hand as needed.

## Endpoints

| Request | Response |
| ------- | -------- |
| `GET /manifest.csv` | the live manifest (regenerated per request) |
| `GET /<path>`       | a file from the tree, e.g. `GET /games/GameXO.bin` |

### Manifest format

Plain CSV — chosen so the **console can parse it without a JSON/YAML parser**
(split on commas and newlines). The first row is a header the device skips;
`crc32` is lower-case hex.

```
category,name,path,size,crc32,version
games,GameXO.bin,games/GameXO.bin,7520,1a2b3c4d,1
games,GameXO.pak,games/GameXO.pak,4096,99887766,1
wifi,ESP01.bin,wifi/ESP01.bin,270416,deadbeef,3
```

- **`crc32`** is the zlib/IEEE CRC-32 — bit-for-bit what the console computes with
  `crc32_calculate` (`Console/Src/Crc`) and what the asset packer uses. It is the
  integrity check: after downloading a file the console recomputes its CRC-32 and
  compares. It also doubles as a change detector — if the manifest CRC differs
  from the installed file's CRC, there's an update.
- **`version`** is advisory (defaults to `1`), useful for human-meaningful update
  tracking and OS images. Override per file via `versions.csv`.

### `versions.csv` (optional)

`path,version` rows in the content root; unlisted files default to `1`:

```
games/GameXO.bin,2
wifi/ESP01.bin,3
```

## Usage

```bash
cd tools/update_server

# Write content/manifest.csv and serve content/ on :25568 (Ctrl-C to stop).
# Defaults are --root content --port 25568, so the bare command is enough:
python update_server.py

# Serve without the periodic on-disk manifest refresh
python update_server.py --refresh 0

# Only write content/manifest.csv and exit (no server)
python update_server.py --generate
```

Serving writes a `manifest.csv` snapshot at startup and re-generates the manifest
live on every request (so the served copy is always fresh). It also rewrites the
on-disk `manifest.csv` every `--refresh` seconds (default 15, `0` disables), so a
`make deploy` into the tree is reflected in the snapshot without a restart. Then
from another machine / the console:

```bash
curl http://<pc-ip>:25568/manifest.csv
curl -O http://<pc-ip>:25568/games/GameXO.bin
```

## Architecture

Follows the repo's tool shape — a dependency-free core wrapped by a thin frontend:

- `updateserver/catalog.py` — scans the content tree into `ContentEntry` rows
  (size + CRC-32, cached by file mtime; advisory versions from `versions.csv`).
- `updateserver/manifest.py` — CSV (de)serialization.
- `updateserver/crc.py` — zlib CRC-32 wrapper (matches the console).
- `update_server.py` — argparse + `http.server` frontend (live `/manifest.csv`
  plus static file serving).

## Notes

- The server regenerates the manifest on every `/manifest.csv` request and caches
  CRCs by `(path, mtime, size)`, so dropping a new file in and re-polling just
  works — no restart needed.
- Downloads are confined to the content root: `http.server` strips `..` and
  percent-encoding, and the handler additionally resolves symlinks and refuses
  (403) anything pointing outside the root. The catalog likewise skips escaping
  symlinks, so the manifest never lists — or leaks the size/CRC of — files
  elsewhere in the project.
- It is a LAN/dev tool: there's no auth or TLS. Don't expose it to the public
  internet.
