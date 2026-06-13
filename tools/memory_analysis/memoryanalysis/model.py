"""
Data model for memory analysis.

Defines the core types used throughout the package: memory regions
(defined by the linker script), output sections (placed by the linker),
and the aggregate application memory layout that ties them together.
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple


@dataclass
class MemoryRegion:
    """A memory region defined in a linker script.

    Attributes:
        name:       Region identifier (e.g. ``CONSOLE_FLASH``, ``GAME_RAM_CCM``).
        origin:     Start address in the MCU address space.
        length:     Region capacity in bytes.
        attributes: Access flags as a string (``rw``, ``rx``, ``rwx``, …).
    """

    name: str
    origin: int
    length: int
    attributes: str = "rw"

    @property
    def end(self) -> int:
        """First byte *after* this region (origin + length)."""
        return self.origin + self.length

    @property
    def size_bytes(self) -> int:
        return self.length

    @property
    def size_kb(self) -> float:
        return self.length / 1024.0


@dataclass
class SectionInfo:
    """A linker output section parsed from a ``.map`` file.

    Attributes:
        name:       Section name without the leading dot (e.g. ``text``).
        vma:        Virtual Memory Address — where the section lives at runtime.
        size:       Section size in bytes.
        lma:        Load Memory Address — where the initialisation image is stored.
                    ``None`` when LMA equals VMA.
        is_noload:  ``True`` for sections that consume address space but are not
                    part of the binary image (``.bss``, ``._user_heap_stack``, …).
        region:     Name of the :class:`MemoryRegion` this section is placed in.
                    Set after parsing by matching VMA against region bounds.
    """

    name: str
    vma: int
    size: int
    lma: Optional[int] = None
    is_noload: bool = False
    region: Optional[str] = None

    @property
    def has_separate_lma(self) -> bool:
        """``True`` when LMA differs from VMA (e.g. ``.data`` in flash, copied to RAM)."""
        return self.lma is not None and self.lma != self.vma


@dataclass
class AppMemory:
    """Complete memory layout for one application (Console firmware or a game).

    Holds the region capacities (from the linker script or map's Memory
    Configuration table) and the actual section placements (from the map's
    linker script and memory map).
    """

    name: str
    regions: Dict[str, MemoryRegion] = field(default_factory=dict)
    sections: List[SectionInfo] = field(default_factory=list)
    map_path: Optional[str] = None
    bin_path: Optional[str] = None
    bin_size: int = 0

    # ------------------------------------------------------------------
    # Queries
    # ------------------------------------------------------------------

    def get_sections_in_region(self, region_name: str) -> List[SectionInfo]:
        """Return every section whose VMA falls inside *region_name*."""
        return [s for s in self.sections if s.region == region_name]

    def region_used(self, region_name: str, *, include_noload: bool = True) -> int:
        """Total bytes consumed in *region_name*.

        By default includes NOLOAD sections (``.bss``, ``._user_heap_stack``)
        since they occupy address space at runtime.  Pass ``include_noload=False``
        to count only sections that contribute to the binary image.
        """
        sections = self.get_sections_in_region(region_name)
        return sum(s.size for s in sections if include_noload or not s.is_noload)

    def region_free(self, region_name: str) -> int:
        """Bytes remaining in *region_name*."""
        if region_name not in self.regions:
            return 0
        return self.regions[region_name].length - self.region_used(region_name)

    def region_capacity(self, region_name: str) -> int:
        if region_name not in self.regions:
            return 0
        return self.regions[region_name].length

    # ------------------------------------------------------------------
    # High-level summaries
    # ------------------------------------------------------------------

    def rom_usage(self) -> Tuple[int, int, int]:
        """
        Return ``(used, free, capacity)`` for the non-volatile storage region.

        For Console firmware this is ``CONSOLE_FLASH``.
        For games this is the CCM image (non-NOLOAD sections that make up the
        ``.bin`` file on the SD card).
        """
        if "CONSOLE_FLASH" in self.regions:
            region = self.regions["CONSOLE_FLASH"]
            used = self.region_used("CONSOLE_FLASH", include_noload=False)
            return (used, region.length - used, region.length)
        elif "GAME_RAM_CCM" in self.regions:
            region = self.regions["GAME_RAM_CCM"]
            used = self.region_used("GAME_RAM_CCM", include_noload=False)
            return (used, region.length - used, region.length)
        return (0, 0, 0)

    def ram_usage(self) -> Tuple[int, int, int]:
        """
        Return ``(used, free, capacity)`` for the primary RAM region.

        Includes NOLOAD sections (``.bss``, stack) since they consume RAM at
        runtime even though they occupy no space in the binary image.
        """
        if "CONSOLE_RAM" in self.regions:
            region = self.regions["CONSOLE_RAM"]
            used = self.region_used("CONSOLE_RAM", include_noload=True)
            return (used, region.length - used, region.length)
        elif "GAME_RAM_CCM" in self.regions:
            region = self.regions["GAME_RAM_CCM"]
            used = self.region_used("GAME_RAM_CCM", include_noload=True)
            return (used, region.length - used, region.length)
        return (0, 0, 0)

    def regions_with_usage(self) -> List[str]:
        """Region names that contain at least one non-zero-size section."""
        used: Set[str] = set()
        for s in self.sections:
            if s.region and s.size > 0:
                used.add(s.region)
        return sorted(used, key=lambda r: self.regions[r].origin if r in self.regions else 0)
