#ifndef SWORD3_OBJC_SHIM_H
#define SWORD3_OBJC_SHIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unwind.h>

#if defined(__GNUC__) || defined(__clang__)
#define SWORD3_OBJC_EXPORT __attribute__((visibility("default")))
#define SWORD3_OBJC_HIDDEN __attribute__((visibility("hidden")))
#define SWORD3_OBJC_NORETURN __attribute__((noreturn))
#else
#define SWORD3_OBJC_EXPORT
#define SWORD3_OBJC_HIDDEN
#define SWORD3_OBJC_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef const char *sword3_objc_sel;
typedef void *sword3_objc_id;
typedef void (*sword3_objc_imp)(void);

struct sword3_objc_class;
typedef struct sword3_objc_class *sword3_objc_Class;

struct sword3_objc_method {
    sword3_objc_sel name;
    const char *types;
    sword3_objc_imp imp;
};

struct sword3_objc_relative_method {
    int32_t name;
    int32_t types;
    int32_t imp;
};

struct sword3_objc_method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    unsigned char entries[];
};

struct sword3_objc_ivar {
    const ptrdiff_t *offset;
    const char *name;
    const char *type;
    uint32_t alignment;
    uint32_t size;
};

struct sword3_objc_ivar_list {
    uint32_t entsize;
    uint32_t count;
    struct sword3_objc_ivar entries[];
};

/*
 * The 64-bit Apple ObjC2 read-only metadata layouts emitted by clang.  This
 * shim reads them in place; it never realizes or rewrites a class.
 */
struct sword3_objc_class_ro {
    uint32_t flags;
    uint32_t instance_start;
    uint32_t instance_size;
    uint32_t reserved;
    const uint8_t *ivar_layout;
    const char *name;
    const struct sword3_objc_method_list *base_methods;
    const void *base_protocols;
    const struct sword3_objc_ivar_list *ivars;
    const uint8_t *weak_ivar_layout;
    const void *base_properties;
};

struct sword3_objc_class {
    sword3_objc_Class isa;
    sword3_objc_Class superclass;
    void *cache;
    void *vtable;
    uintptr_t data_bits;
};

struct sword3_objc_super {
    sword3_objc_id receiver;
    sword3_objc_Class current_class;
};

typedef struct sword3_objc_method *sword3_objc_Method;
typedef struct sword3_objc_ivar *sword3_objc_Ivar;

#define SWORD3_OBJC_METHOD_LIST_SMALL UINT32_C(0x80000000)
#define SWORD3_OBJC_METHOD_LIST_DIRECT_SELECTORS UINT32_C(0x40000000)
#define SWORD3_OBJC_RO_META UINT32_C(0x1)
#define SWORD3_OBJC_RO_ROOT UINT32_C(0x2)

SWORD3_OBJC_EXPORT sword3_objc_sel sel_registerName(const char *name);
SWORD3_OBJC_EXPORT sword3_objc_sel sel_getUid(const char *name);
SWORD3_OBJC_EXPORT const char *sel_getName(sword3_objc_sel selector);

SWORD3_OBJC_EXPORT sword3_objc_id objc_retain(sword3_objc_id object);
SWORD3_OBJC_EXPORT void objc_release(sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_id objc_autorelease(sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_id objc_retainAutorelease(sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_id
objc_retainAutoreleaseReturnValue(sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_id
objc_retainAutoreleasedReturnValue(sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_id
objc_autoreleaseReturnValue(sword3_objc_id object);
SWORD3_OBJC_EXPORT void *objc_autoreleasePoolPush(void);
SWORD3_OBJC_EXPORT void objc_autoreleasePoolPop(void *token);
SWORD3_OBJC_EXPORT sword3_objc_id
objc_storeWeak(sword3_objc_id *location, sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_id
objc_loadWeakRetained(sword3_objc_id *location);
SWORD3_OBJC_EXPORT void objc_destroyWeak(sword3_objc_id *location);
SWORD3_OBJC_EXPORT void
objc_storeStrong(sword3_objc_id *location, sword3_objc_id object);

SWORD3_OBJC_EXPORT sword3_objc_Class object_getClass(sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_Class
class_getSuperclass(sword3_objc_Class cls);
SWORD3_OBJC_EXPORT bool class_isMetaClass(sword3_objc_Class cls);
SWORD3_OBJC_EXPORT const char *class_getName(sword3_objc_Class cls);
SWORD3_OBJC_EXPORT sword3_objc_Method
class_getInstanceMethod(sword3_objc_Class cls, sword3_objc_sel selector);
SWORD3_OBJC_EXPORT void *object_getIndexedIvars(sword3_objc_id object);
SWORD3_OBJC_EXPORT sword3_objc_Class objc_getClass(const char *name);
SWORD3_OBJC_EXPORT sword3_objc_Class objc_lookUpClass(const char *name);
SWORD3_OBJC_EXPORT sword3_objc_Class objc_getRequiredClass(const char *name);
SWORD3_OBJC_EXPORT sword3_objc_Class objc_getMetaClass(const char *name);
SWORD3_OBJC_EXPORT sword3_objc_id objc_alloc(sword3_objc_Class cls);
SWORD3_OBJC_EXPORT sword3_objc_id
objc_constructInstance(sword3_objc_Class cls, void *storage);

SWORD3_OBJC_EXPORT sword3_objc_id objc_getProperty(
    sword3_objc_id object,
    sword3_objc_sel selector,
    ptrdiff_t offset,
    bool atomic
);
SWORD3_OBJC_EXPORT void objc_setProperty_atomic(
    sword3_objc_id object,
    sword3_objc_sel selector,
    sword3_objc_id value,
    ptrdiff_t offset
);

SWORD3_OBJC_EXPORT SWORD3_OBJC_NORETURN
_Unwind_Reason_Code __objc_personality_v0(
    int version,
    _Unwind_Action actions,
    uint64_t exception_class,
    struct _Unwind_Exception *exception,
    struct _Unwind_Context *context
);

/*
 * Assembly dispatch enters this helper only after preserving all argument
 * registers.  start_class is NULL for objc_msgSend and already advanced to
 * the superclass for objc_msgSendSuper2.
 */
SWORD3_OBJC_HIDDEN sword3_objc_imp sword3_objc_lookup_imp(
    sword3_objc_id receiver,
    sword3_objc_sel selector,
    sword3_objc_Class start_class
);
SWORD3_OBJC_HIDDEN SWORD3_OBJC_NORETURN
void sword3_objc_unsupported_symbol(const char *symbol);

extern SWORD3_OBJC_EXPORT struct sword3_objc_class sword3_nsobject_class
    __asm__("OBJC_CLASS_$_NSObject");
extern SWORD3_OBJC_EXPORT struct sword3_objc_class sword3_nsobject_metaclass
    __asm__("OBJC_METACLASS_$_NSObject");
extern SWORD3_OBJC_EXPORT uintptr_t sword3_objc_empty_cache[4]
    __asm__("_objc_empty_cache");
extern SWORD3_OBJC_EXPORT uintptr_t sword3_objc_empty_vtable[1]
    __asm__("_objc_empty_vtable");

#ifdef __cplusplus
}
#endif

#endif
