/*
 * Minimal newlib retarget stubs for loaded games.
 *
 * Games use a few libc functions — notably snprintf — to format text locally before
 * handing it to rendererDrawText. libc's stdio reentrancy references
 * _read/_write/_close/_lseek even though a game does no real I/O. -lnosys does supply
 * these, but with a "not implemented and will always fail" link-time warning;
 * providing our own (equally non-functional, but silent) definitions resolves those
 * references to us instead, keeping game builds warning-free. They are unreachable in
 * practice and garbage-collected, so this adds nothing to the image — it exists only
 * to quiet the linker.
 *
 * One shared file, compiled into each game alongside console_syscalls.c — replacing
 * the per-app STM32CubeMX syscalls.c (~130 lines of the same dead stubs, duplicated
 * in every app). A game that genuinely wants I/O or a heap should retarget properly
 * (back __io_putchar, provide a real _sbrk), not lean on these.
 */
#include <errno.h>

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _close(int file)
{
    (void)file;
    errno = ENOSYS;
    return -1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    errno = ENOSYS;
    return -1;
}
