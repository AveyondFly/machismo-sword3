#if !defined(__aarch64__)
#error "sword3_ios_raise.S is AArch64-only"
#endif

	.text
	.p2align 2
	.globl	proxy_exception_raise_format
	.type	proxy_exception_raise_format, %function
proxy_exception_raise_format:
	mov	x4, sp
	b	sword3_ns_raise_impl
	.size	proxy_exception_raise_format, .-proxy_exception_raise_format

	.globl	proxy_array_with_objects
	.type	proxy_array_with_objects, %function
proxy_array_with_objects:
	mov	x3, sp
	b	sword3_array_with_objects_impl
	.size	proxy_array_with_objects, .-proxy_array_with_objects

	.section .note.GNU-stack,"",%progbits
