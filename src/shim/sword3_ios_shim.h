#ifndef SWORD3_IOS_SHIM_H
#define SWORD3_IOS_SHIM_H

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define SWORD3_EXPORT __attribute__((visibility("default")))
#define SWORD3_NORETURN __attribute__((noreturn))
#define SWORD3_ALIGNED(bytes) __attribute__((aligned(bytes)))
#else
#define SWORD3_EXPORT
#define SWORD3_NORETURN _Noreturn
#define SWORD3_ALIGNED(bytes)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Common fail-fast boundary for unsupported Apple framework functions.
 * This logs the exact ELF symbol, increments the audit counter, and aborts.
 */
SWORD3_EXPORT SWORD3_NORETURN
void sword3_unsupported_symbol(const char *symbol_name);

/*
 * Deliberately opt-in: production code must not depend on shim test state.
 * Define SWORD3_SHIM_ENABLE_TEST_API while compiling sword3_ios_shim.c to
 * expose the query.
 */
#ifdef SWORD3_SHIM_ENABLE_TEST_API
SWORD3_EXPORT uint64_t sword3_unsupported_call_count(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
