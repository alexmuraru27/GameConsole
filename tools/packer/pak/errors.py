"""Exceptions raised by the packer."""


class PakError(Exception):
    """A manifest, asset, or packing problem the user can fix."""


class PakVerificationError(PakError):
    """A generated pak failed its self-check (indicates a packer bug)."""
