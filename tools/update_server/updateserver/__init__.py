"""Dependency-free core for the GameConsole update server.

A thin HTTP frontend (``update_server.py``) wraps these:
  - :mod:`updateserver.catalog`  — scan the content tree into entries
  - :mod:`updateserver.manifest` — (de)serialize the manifest CSV
  - :mod:`updateserver.crc`      — zlib CRC-32 (matches the console)
"""
from .catalog import Catalog, ContentEntry, MANIFEST_NAME, VERSIONS_NAME
from .crc import crc32
from .manifest import HEADER, parse_csv, to_csv

__all__ = [
    "Catalog",
    "ContentEntry",
    "MANIFEST_NAME",
    "VERSIONS_NAME",
    "crc32",
    "HEADER",
    "parse_csv",
    "to_csv",
]
