/**
 * UFI Flux Engine - minimal newlib system calls
 *
 * The firmware has no console or file system; newlib-nano's stdio (pulled in by
 * snprintf) still references these.  Explicit stubs replace libnosys' warning
 * versions: output is discarded, input reports EOF.
 */

#include <errno.h>
#include <sys/stat.h>

int _write(int fd, const char* buf, int len)
{
    (void)fd;
    (void)buf;
    return len;
}

int _read(int fd, char* buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    return 0;
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _lseek(int fd, int ptr, int dir)
{
    (void)fd;
    (void)ptr;
    (void)dir;
    return 0;
}

int _fstat(int fd, struct stat* st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}
