"""The asset manifest: data model and YAML loader."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

import yaml

from .errors import PakError
from .format import PAK_VERSION, UINT32_MAX

_C_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


@dataclass(frozen=True)
class AssetSpec:
    """A single asset as declared in the manifest."""

    id: int
    name: str
    path: Path


@dataclass(frozen=True)
class Manifest:
    """A parsed and validated manifest."""

    version: int
    assets: tuple[AssetSpec, ...]


def load_manifest(path: Path) -> Manifest:
    """Parse and validate a YAML manifest. Raises PakError on any problem."""
    try:
        raw = yaml.safe_load(path.read_text())
    except OSError as exc:
        raise PakError(f"cannot read manifest {path}: {exc}") from exc
    except yaml.YAMLError as exc:
        raise PakError(f"invalid YAML in manifest {path}: {exc}") from exc

    if not isinstance(raw, dict):
        raise PakError(f"manifest {path} must be a mapping with 'version' and 'assets'")

    version = raw.get("version")
    if version != PAK_VERSION:
        raise PakError(f"manifest version {version!r} unsupported, expected {PAK_VERSION}")

    raw_assets = raw.get("assets")
    if not isinstance(raw_assets, list) or not raw_assets:
        raise PakError(f"manifest {path} must list at least one asset under 'assets'")

    assets: list[AssetSpec] = []
    ids: dict[int, str] = {}
    names: set[str] = set()
    for index, item in enumerate(raw_assets):
        spec = _parse_asset(item, index)
        if spec.id in ids:
            raise PakError(
                f"duplicate asset id {spec.id} (used by '{ids[spec.id]}' and '{spec.name}')"
            )
        if spec.name in names:
            raise PakError(f"duplicate asset name '{spec.name}'")
        ids[spec.id] = spec.name
        names.add(spec.name)
        assets.append(spec)

    return Manifest(version=version, assets=tuple(assets))


def _parse_asset(item: object, index: int) -> AssetSpec:
    where = f"assets[{index}]"
    if not isinstance(item, dict):
        raise PakError(f"{where} must be a mapping with id, name, path")

    missing = [key for key in ("id", "name", "path") if key not in item]
    if missing:
        raise PakError(f"{where} is missing required key(s): {', '.join(missing)}")

    asset_id, name, path = item["id"], item["name"], item["path"]

    # bool is a subclass of int, so reject it explicitly.
    if not isinstance(asset_id, int) or isinstance(asset_id, bool):
        raise PakError(f"{where}: id must be an integer, got {asset_id!r}")
    if not 0 <= asset_id <= UINT32_MAX:
        raise PakError(f"{where}: id {asset_id} is out of uint32 range")
    if not isinstance(name, str) or not _C_IDENTIFIER.fullmatch(name):
        raise PakError(f"{where}: name {name!r} must be a valid C identifier")
    if not isinstance(path, str) or not path:
        raise PakError(f"{where}: path must be a non-empty string")

    return AssetSpec(id=asset_id, name=name, path=Path(path))
