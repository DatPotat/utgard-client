/* Minimal checks for the portable tests: count failures, report, exit 1. */
#ifndef UTGARD_TEST_CHECK_H
#define UTGARD_TEST_CHECK_H
#include <stdio.h>
static int g_fails;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fails++; } } while (0)
#define DONE(name) (printf("%s: %s\n", name, g_fails ? "FAILED" : "ok"), g_fails != 0)
#endif
