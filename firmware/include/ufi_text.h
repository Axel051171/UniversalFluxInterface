#ifndef UFI_TEXT_H
#define UFI_TEXT_H
#include <stddef.h>

/* Append one NUL-terminated field. A full buffer is a stable terminal state. */
static inline size_t ufi_text_append(char* buf, size_t cap, size_t n, const char* s)
{
    if (n >= cap) return cap;
    while (*s && n + 1u < cap) buf[n++] = *s++;
    buf[n++] = '\0';
    return n;
}
#endif
