MEMORY
{
  RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 128K
  BOOTLOADERROM (rx)    : ORIGIN = 0x8000000, LENGTH = 64K
  APPROM (rx)     : ORIGIN = 0x8010000, LENGTH = 448K
}

PROVIDE(__bootloaderrom_start__ = ORIGIN(BOOTLOADERROM));
PROVIDE(__bootloaderrom_size__ = LENGTH(BOOTLOADERROM));
PROVIDE(__approm_start__ = ORIGIN(APPROM));
PROVIDE(__approm_size__ = LENGTH(APPROM));