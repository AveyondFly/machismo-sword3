#include <stdlib.h>

/*
 * GCC 14 emits this helper for calls that must terminate while compiling
 * libc++.  The older libc++abi revision used by Machismo predates the exported
 * entry point.  Its observable contract is noreturn; abort is the safe
 * fallback and avoids pulling libstdc++ (and its incompatible C++ ABI) into
 * the Apple-ABI libc++ runtime.
 */
__attribute__((noreturn, visibility("default")))
void __cxa_call_terminate(void *exception)
{
	(void)exception;
	abort();
}
