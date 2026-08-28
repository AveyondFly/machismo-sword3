#if !defined(__aarch64__)
#error "sword3_objc_msgsend.S is AArch64-only"
#endif

    .text
    .p2align 2

    .globl objc_msgSend
    .type objc_msgSend, %function
objc_msgSend:
    cbz     x0, .Lmsg_nil
    sub     sp, sp, #224
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    str     x8, [sp, #64]
    stp     q0, q1, [sp, #80]
    stp     q2, q3, [sp, #112]
    stp     q4, q5, [sp, #144]
    stp     q6, q7, [sp, #176]
    stp     x29, x30, [sp, #208]
    add     x29, sp, #208

    mov     x2, xzr
    bl      sword3_objc_lookup_imp
    mov     x16, x0

    ldp     x29, x30, [sp, #208]
    ldp     q6, q7, [sp, #176]
    ldp     q4, q5, [sp, #144]
    ldp     q2, q3, [sp, #112]
    ldp     q0, q1, [sp, #80]
    ldr     x8, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #224
    br      x16

.Lmsg_nil:
    mov     x0, xzr
    mov     x1, xzr
    movi    v0.2d, #0
    movi    v1.2d, #0
    movi    v2.2d, #0
    movi    v3.2d, #0
    ret
    .size objc_msgSend, .-objc_msgSend

    .globl objc_msgSendSuper2
    .type objc_msgSendSuper2, %function
objc_msgSendSuper2:
    cbz     x0, .Lsuper_nil
    ldr     x9, [x0]
    cbz     x9, .Lsuper_nil
    sub     sp, sp, #224
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    str     x8, [sp, #64]
    stp     q0, q1, [sp, #80]
    stp     q2, q3, [sp, #112]
    stp     q4, q5, [sp, #144]
    stp     q6, q7, [sp, #176]
    stp     x29, x30, [sp, #208]
    add     x29, sp, #208

    ldr     x9, [sp, #0]
    ldp     x0, x2, [x9]
    ldr     x2, [x2, #8]
    bl      sword3_objc_lookup_imp
    mov     x16, x0

    ldr     x17, [sp, #0]
    ldr     x17, [x17]
    ldp     x29, x30, [sp, #208]
    ldp     q6, q7, [sp, #176]
    ldp     q4, q5, [sp, #144]
    ldp     q2, q3, [sp, #112]
    ldp     q0, q1, [sp, #80]
    ldr     x8, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    mov     x0, x17
    add     sp, sp, #224
    br      x16

.Lsuper_nil:
    mov     x0, xzr
    mov     x1, xzr
    movi    v0.2d, #0
    movi    v1.2d, #0
    movi    v2.2d, #0
    movi    v3.2d, #0
    ret
    .size objc_msgSendSuper2, .-objc_msgSendSuper2

    .section .note.GNU-stack,"",%progbits
