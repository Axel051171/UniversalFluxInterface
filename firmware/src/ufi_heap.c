/* Bound newlib's allocator to the actual linker reservation. libnosys _sbrk
 * does not know our split AXI heap / DTCM stack layout. Main-loop use only. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern char __heap_start[], __heap_end[];

void *_sbrk(ptrdiff_t increment)
{
    static uintptr_t cursor;
    const uintptr_t begin = (uintptr_t)__heap_start;
    const uintptr_t end = (uintptr_t)__heap_end;
    if (!cursor) cursor = begin;
    const uintptr_t old = cursor;
    if (increment >= 0) {
        if ((uintptr_t)increment > end - cursor) {
            errno = ENOMEM;
            return (void *)-1;
        }
        cursor += (uintptr_t)increment;
    } else {
        /* Avoid signed overflow for PTRDIFF_MIN. */
        const uintptr_t amount = (uintptr_t)(-(increment + 1)) + 1u;
        if (amount > cursor - begin) {
            errno = ENOMEM;
            return (void *)-1;
        }
        cursor -= amount;
    }
    return (void *)old;
}
