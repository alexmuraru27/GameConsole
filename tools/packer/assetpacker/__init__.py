"""Asset Packer — the GameConsole ``.pak`` asset-container library.

Like the Pixel Forge and Music Creator tools, the logic is a dependency-light
core wrapped by a thin CLI front end (``packer.py``). The package is split by
concern:

- ``format``     on-disk layout constants, struct packing, CRC helpers (the
                 Python mirror of ``pak_format.h``)
- ``manifest``   YAML manifest model + loader/validation
- ``builder``    assemble the container bytes from a manifest
- ``verify``     re-parse built bytes and validate every field
- ``codegen``    render the C asset-id enum header
- ``report``     format the printed summary
- ``errors``     the package's exception types

See ``packer.py`` for the command-line front end and ``README.md`` for the
manifest format, binary layout, and CRC details.
"""

__version__ = "1.0.0"
