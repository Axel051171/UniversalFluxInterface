/* Minimal check framework for the native firmware tests (no dependencies). */
#pragma once
#include <stdio.h>
#include <stdlib.h>

static int checks_run, checks_failed;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        checks_run++;                                                           \
        if (!(cond)) {                                                          \
            checks_failed++;                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                         \
            printf(__VA_ARGS__);                                                \
            printf("\n");                                                       \
        }                                                                       \
    } while (0)

#define CHECK_SUMMARY(name)                                                     \
    do {                                                                        \
        printf("%s: %d/%d checks passed\n", name, checks_run - checks_failed, checks_run); \
        return checks_failed ? 1 : 0;                                           \
    } while (0)
