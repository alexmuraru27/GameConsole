# Asset Packer

`packer.py` bundles a set of binary assets into a single `.pak` container and
emits a C enum of their ids. A game loads the one `.pak` from the SD card and
looks assets up by id instead of shipping dozens of loose `.bin` files.

The on-disk format is defined once, in C, in [`pak_format.h`](pak_format.h).
That header is the contract: the packer writes bytes that match it exactly, and
console/game code includes it to read them back.

## Requirements

- Python 3.8+ (developed and tested on 3.12)
- [PyYAML](https://pyyaml.org/) (already used elsewhere in this repo)

## What it generates

Running the packer with `--output-name Level1` writes two files into the output
directory:

| File                 | Purpose                                                        |
|----------------------|----------------------------------------------------------------|
| `Level1.pak`         | The binary container (header + entries + raw asset blobs).      |
| `Level1AssetEnum.h`  | `typedef enum { ... } Level1AssetId;` mapping each asset name to its id. |

The game includes `Level1AssetEnum.h` (for the ids) and `pak_format.h` (for the
`PakHeader` / `PakEntry` structs) to read the pak at runtime.

The enum type is named after the output (`Level1AssetId`, `<name>AssetId` in
general) rather than a fixed `AssetId`, so several generated headers can be
included in the same project without their enum types colliding.

## Manifest format

The manifest is a YAML file listing the assets to pack, in the order they should
be stored:

```yaml
version: 1
assets:
  - id: 1
    name: ASSET_LEVEL1_TILESET
    path: assets/level1/tileset.bin

  - id: 2
    name: ASSET_LEVEL1_MAP
    path: assets/level1/map.bin

  - id: 3
    name: ASSET_LEVEL1_PALETTE
    path: assets/level1/palette.bin
```

Rules (all enforced, with a clear error if violated):

- `version` must be `1`.
- Each asset needs `id`, `name`, and `path`.
- `id` is an integer in `uint32` range and must be **unique**.
- `name` must be a valid C identifier and must be **unique** (it becomes an enum
  constant).
- `path` points to the raw blob to embed. Relative paths resolve against the
  manifest's directory by default, or against `--assets-root` if given.

Asset blobs are stored verbatim — the packer does not interpret, compress, or
transform them. Entry order in the `.pak` follows manifest order.

## CLI usage

```
python packer.py <manifest> --output-name <name> [--output-dir DIR] [--assets-root DIR]
```

| Argument                | Description                                                              |
|-------------------------|--------------------------------------------------------------------------|
| `manifest`              | Path to the YAML manifest.                                                |
| `-o`, `--output-name`   | Base name for outputs: writes `<name>.pak` and `<name>AssetEnum.h`. Required. |
| `-d`, `--output-dir`    | Directory for the generated files (default: current directory).          |
| `--assets-root`         | Base directory for relative asset paths (default: the manifest's directory). |

The packer self-verifies the bytes it produced before writing anything to disk,
so a corrupt pak is never emitted.

## Binary layout

All fields are little-endian `uint32`. Because every field is a `uint32`, the
structs in `pak_format.h` are naturally contiguous (no padding) and map directly
onto the file bytes.

```
offset 0    +-----------------------------------------+
            |  PakHeader fixed fields  (24 bytes)     |
            |    magic, version, assetCount,          |
            |    dataOffset, pakSize, pakCrc          |
offset 24   +-----------------------------------------+
            |  PakEntry[0]             (16 bytes)     |
            |  PakEntry[1]                            |
            |  ...                                    |
            |  PakEntry[assetCount-1]                 |
dataOffset  +-----------------------------------------+
            |  asset 0 raw bytes                      |
            |  asset 1 raw bytes                      |
            |  ...                                    |
            |  asset N-1 raw bytes                    |
pakSize     +-----------------------------------------+
```

**PakHeader** (fixed fields):

| Field        | Type     | Meaning                                                   |
|--------------|----------|-----------------------------------------------------------|
| `magic`      | uint32   | `0x314B4150` — the ASCII bytes `PAK1` in file order.      |
| `version`    | uint32   | Format version (`1`).                                     |
| `assetCount` | uint32   | Number of `PakEntry` records.                             |
| `dataOffset` | uint32   | Byte offset of the first asset blob: `24 + assetCount*16`. |
| `pakSize`    | uint32   | Total file size in bytes.                                 |
| `pakCrc`     | uint32   | CRC32 of the whole file, excluding these 4 bytes.        |

**PakEntry** (one per asset):

| Field    | Type   | Meaning                                          |
|----------|--------|--------------------------------------------------|
| `id`     | uint32 | Asset id (matches an `AssetId` enum constant).   |
| `offset` | uint32 | Blob offset **relative to `dataOffset`**.        |
| `size`   | uint32 | Blob size in bytes.                              |
| `crc32`  | uint32 | CRC32 of this blob.                              |

## How offsets work

`PakEntry.offset` is **relative to `dataOffset`**, not to the start of the file.
This keeps entries independent of the header/table size. The absolute file
position of an asset's bytes is:

```
absolute_offset = dataOffset + entry.offset
```

Blobs are stored back-to-back in manifest order, so the first asset has
`offset == 0`, and each subsequent `offset` equals the sum of the preceding
sizes. The blobs exactly tile the data region (`pakSize - dataOffset`) with no
gaps or padding.

## CRC behavior

Both checksums use **CRC32 (IEEE 802.3 / zlib-compatible)** — the same algorithm
as `zlib.crc32` and `binascii.crc32` in Python, or zlib's `crc32()` in C.

- **`PakEntry.crc32`** covers that asset's raw blob only.
- **`PakHeader.pakCrc`** covers the entire file *except its own 4 bytes* (the
  `pakCrc` field at byte offset 20). The 4 bytes are skipped entirely, not zeroed.
  A matching C/host check computes it as:

  ```
  crc = crc32(file_bytes[0 .. 20))        // up to the pakCrc field
  crc = crc32(file_bytes[24 .. pakSize), crc)   // continue past it
  ```

Because `pakCrc` spans the entries table and all blobs too, it detects any
corruption anywhere in the file, while the per-entry CRCs let you validate a
single asset without hashing the whole pak.

## Verification

`verify_pak()` re-parses generated bytes and raises `PakVerificationError` on the
first mismatch. It checks:

- `magic`, `version`, `assetCount`, `dataOffset`, and `pakSize` are consistent
  with the actual byte length;
- every entry's `[offset, offset+size)` lies within the data region, and the
  entries exactly cover it (no gaps/overlaps);
- every `PakEntry.crc32` recomputed from its blob;
- `pakCrc` recomputed over the whole file excluding its own field.

It runs automatically on every pack. It is also a plain function you can import
and point at any `.pak` bytes.

## Using a pak from C

```c
#include "pak_format.h"
#include "Level1AssetEnum.h"   // ASSET_PLAYER_CHR, ..., Level1AssetId

// pak_base points at the loaded .pak file bytes.
static const uint8_t *asset_data(const uint8_t *pak_base, Level1AssetId id, uint32_t *out_size)
{
    const PakHeader *pak = (const PakHeader *)pak_base;
    for (uint32_t i = 0; i < pak->assetCount; i++) {
        const PakEntry *e = &pak->entries[i];
        if (e->id == (uint32_t)id) {
            *out_size = e->size;
            return pak_base + pak->dataOffset + e->offset;
        }
    }
    return NULL; // not found
}
```

## Code layout

The logic lives in the `pak` package; `packer.py` is just the CLI front end:

| File | Responsibility |
|------|----------------|
| `pak/format.py`   | Constants, struct layouts, CRC helpers (the Python mirror of `pak_format.h`). |
| `pak/manifest.py` | Manifest model + YAML loader/validation. |
| `pak/builder.py`  | `build_pak()` — assemble the container bytes. |
| `pak/verify.py`   | `verify_pak()` — re-parse and validate. |
| `pak/codegen.py`  | `render_enum_header()` — the C asset-id enum. |
| `pak/report.py`   | `format_summary()` — the printed summary. |
| `packer.py`       | Argument parsing and orchestration. |

You can also use the library directly: `from pak import build_pak, verify_pak`.

## Example

The `example/` directory contains a ready-to-run manifest and a few small blobs.
From the repo root:

```
python tools/packer/packer.py tools/packer/example/manifest.yaml \
    --output-name Level1 --output-dir tools/packer/example/out
```

There is also an **Asset Packer (example)** entry in `.vscode/launch.json` that
runs exactly this — pick it in the Run and Debug panel, and edit its `args` to
point at your own manifest.

Output:

```
tools/packer/example/out/Level1.pak  -  5 assets, 600 bytes

  magic        0x314B4150  "PAK1"
  version      1
  assetCount   5
  dataOffset   104  (header 24 + entries 5 x 16 = 80)
  pakSize      600  (dataOffset 104 + data 496)
  pakCrc       0xC0625A52

  id  name                  rel.off  abs.off  size  crc32
  --  --------------------  -------  -------  ----  ----------
   1  ASSET_LEVEL1_TILESET        0      104   256  0x1A5C07A3
   2  ASSET_LEVEL1_MAP          256      360    64  0x7FEC9F3B
   3  ASSET_LEVEL1_PALETTE      320      424    16  0xCECEE288
   4  ASSET_PLAYER_CHR          336      440   128  0x83E0A386
   5  ASSET_SFX_JUMP            464      568    32  0x7353BBB3

  tools/packer/example/out/Level1AssetEnum.h  (5 ids)
  verification: OK
```
