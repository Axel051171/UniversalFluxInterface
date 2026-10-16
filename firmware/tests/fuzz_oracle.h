/* Values the generated stubs hand back to production code under fuzzing.  The
 * harness feeds them from the tail of the fuzz input, so the fuzzer also explores
 * what the stubbed drivers report (errors, states, counts). */
#ifndef FUZZ_ORACLE_H
#define FUZZ_ORACLE_H
#include <stdbool.h>
#include <stdint.h>

uint8_t fuzz_byte(void);
bool fuzz_bit(void);
uint16_t fuzz_u16(void);
uint32_t fuzz_u32(void);
int fuzz_err(void);                 /* 0 most of the time, otherwise -1..-21 */
void* fuzz_zero_buffer(void);       /* 8 KiB, zeroed at the start of every input */
const char* fuzz_string(void);      /* "" */

#endif
