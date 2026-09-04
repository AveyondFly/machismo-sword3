#include "sword3_objc_shim.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


struct selector_node {
    struct selector_node *next;
    char name[];
};

struct autorelease_pool {
    struct autorelease_pool *previous;
};

struct decoded_method {
    sword3_objc_sel name;
    const char *types;
    sword3_objc_imp imp;
    const struct sword3_objc_method *original;
    const void *identity;
};

struct cached_method {
    struct cached_method *next;
    const void *identity;
    struct sword3_objc_method method;
};

static atomic_flag selector_lock = ATOMIC_FLAG_INIT;
static atomic_flag storage_lock = ATOMIC_FLAG_INIT;
static atomic_flag class_lock = ATOMIC_FLAG_INIT;
static atomic_flag method_cache_lock = ATOMIC_FLAG_INIT;
static struct selector_node *selectors;
static struct cached_method *cached_methods;
static sword3_objc_Class observed_classes[256];
static size_t observed_class_count;
static _Thread_local struct autorelease_pool *current_pool;

static sword3_objc_id nsobject_init(sword3_objc_id self,
                                    sword3_objc_sel selector)
{
    (void)selector;
    return self;
}

static sword3_objc_id nsobject_init_with_frame(sword3_objc_id self,
                                              sword3_objc_sel selector)
{
    (void)selector;
    return self;
}

static void nsobject_set_background_color(sword3_objc_id self,
                                          sword3_objc_sel selector,
                                          sword3_objc_id color)
{
    (void)self;
    (void)selector;
    (void)color;
}

static bool nsobject_responds(sword3_objc_id self,
                              sword3_objc_sel selector,
                              sword3_objc_sel requested)
{
    sword3_objc_Class cls;
    (void)selector;
    if (!self || !requested)
        return false;
    cls = *(sword3_objc_Class *)self;
    return class_getInstanceMethod(cls, requested) != NULL;
}

static bool nsobject_instances_respond(sword3_objc_Class cls,
                                       sword3_objc_sel selector,
                                       sword3_objc_sel requested)
{
    (void)selector;
    if (!cls || !requested)
        return false;
    return class_getInstanceMethod(cls, requested) != NULL;
}

static sword3_objc_id nsobject_instance_class(sword3_objc_id self,
                                              sword3_objc_sel selector)
{
    (void)selector;
    return self == NULL ? NULL : *(sword3_objc_id *)self;
}

static sword3_objc_id nsobject_self(sword3_objc_id self,
                                    sword3_objc_sel selector)
{
    (void)selector;
    return self;
}

static sword3_objc_id nsobject_alloc(sword3_objc_Class cls,
                                     sword3_objc_sel selector)
{
    (void)selector;
    return objc_alloc(cls);
}

static sword3_objc_id nsobject_new(sword3_objc_Class cls,
                                   sword3_objc_sel selector)
{
    (void)selector;
    return nsobject_init(objc_alloc(cls), selector);
}

struct nsobject_method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_method methods[7];
};

struct nsobject_class_method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_method methods[4];
};

static const struct nsobject_method_list nsobject_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 7,
    .methods = {
        {
            .name = "init",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_init,
        },
        {
            .name = "initWithFrame:",
            .types = "@48@0:8{CGRect={CGPoint=dd}{CGSize=dd}}16",
            .imp = (sword3_objc_imp)nsobject_init_with_frame,
        },
        {
            .name = "initWithURL:",
            .types = "@24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_init,
        },
        {
            .name = "setBackgroundColor:",
            .types = "v24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_set_background_color,
        },
        {
            .name = "respondsToSelector:",
            .types = "B24@0:8:16",
            .imp = (sword3_objc_imp)nsobject_responds,
        },
        {
            .name = "class",
            .types = "#16@0:8",
            .imp = (sword3_objc_imp)nsobject_instance_class,
        },
        {
            .name = "self",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_self,
        },
    },
};

static const struct nsobject_class_method_list nsobject_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 4,
    .methods = {
        {
            .name = "instancesRespondToSelector:",
            .types = "B24@0:8:16",
            .imp = (sword3_objc_imp)nsobject_instances_respond,
        },
        {
            .name = "alloc",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_alloc,
        },
        {
            .name = "new",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_new,
        },
        {
            .name = "class",
            .types = "#16@0:8",
            .imp = (sword3_objc_imp)nsobject_self,
        },
    },
};

static const struct sword3_objc_class_ro nsobject_class_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_start = 0,
    .instance_size = sizeof(void *),
    .name = "NSObject",
    .base_methods =
        (const struct sword3_objc_method_list*)&nsobject_methods,
};

static const struct sword3_objc_class_ro nsobject_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_start = sizeof(struct sword3_objc_class),
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSObject",
    .base_methods =
        (const struct sword3_objc_method_list *)&nsobject_class_methods,
};

SWORD3_OBJC_EXPORT uintptr_t sword3_objc_empty_cache[4]
    __asm__("_objc_empty_cache") = {0};
SWORD3_OBJC_EXPORT uintptr_t sword3_objc_empty_vtable[1]
    __asm__("_objc_empty_vtable") = {0};

SWORD3_OBJC_EXPORT struct sword3_objc_class sword3_nsobject_metaclass
    __asm__("OBJC_METACLASS_$_NSObject") = {
        .isa = &sword3_nsobject_metaclass,
        .superclass = &sword3_nsobject_class,
        .cache = sword3_objc_empty_cache,
        .vtable = sword3_objc_empty_vtable,
        .data_bits = (uintptr_t)&nsobject_metaclass_ro,
    };

SWORD3_OBJC_EXPORT struct sword3_objc_class sword3_nsobject_class
    __asm__("OBJC_CLASS_$_NSObject") = {
        .isa = &sword3_nsobject_metaclass,
        .superclass = NULL,
        .cache = sword3_objc_empty_cache,
        .vtable = sword3_objc_empty_vtable,
        .data_bits = (uintptr_t)&nsobject_class_ro,
    };


static void lock(atomic_flag *flag)
{
    while (atomic_flag_test_and_set_explicit(flag, memory_order_acquire)) {
        /* Selector and storage operations are deliberately tiny. */
    }
}

static void unlock(atomic_flag *flag)
{
    atomic_flag_clear_explicit(flag, memory_order_release);
}

static void write_diagnostic(const char *prefix, const char *detail)
{
    char message[512];
    size_t prefix_length = strlen(prefix);
    size_t detail_length;
    size_t position;

    if (detail == NULL)
        detail = "(null)";
    detail_length = 0;
    while (detail_length < sizeof(message) - 2 && detail[detail_length] != '\0')
        ++detail_length;
    if (prefix_length > sizeof(message) - 2)
        prefix_length = sizeof(message) - 2;
    if (detail_length > sizeof(message) - prefix_length - 2)
        detail_length = sizeof(message) - prefix_length - 2;

    memcpy(message, prefix, prefix_length);
    position = prefix_length;
    memcpy(message + position, detail, detail_length);
    position += detail_length;
    message[position++] = '\n';
    (void)write(STDERR_FILENO, message, position);
}

SWORD3_OBJC_HIDDEN SWORD3_OBJC_NORETURN
void sword3_objc_unsupported_symbol(const char *symbol)
{
    write_diagnostic("[sword3-objc-shim] unsupported runtime symbol: ", symbol);
    abort();
}

static sword3_objc_id nsobject_return_nil(sword3_objc_id self,
                                          sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return NULL;
}

static sword3_objc_imp stub_unknown_selector(sword3_objc_sel selector)
{
    static unsigned seen;

    if (seen < 64) {
        seen++;
        write_diagnostic(
            "[sword3-objc-shim] stubbing selector: ",
            selector == NULL ? "(null)" : selector
        );
    }
    return (sword3_objc_imp)nsobject_return_nil;
}

static const struct sword3_objc_class_ro *
class_ro(sword3_objc_Class cls)
{
    uintptr_t data;

    if (cls == NULL)
        return NULL;
    data = cls->data_bits & ~(uintptr_t)7;
    if (data == 0 ||
        data % _Alignof(struct sword3_objc_class_ro) != 0)
        return NULL;
    return (const struct sword3_objc_class_ro *)data;
}

static bool aligned_instance_size(
    const struct sword3_objc_class_ro *ro,
    size_t *size
)
{
    const size_t alignment = sizeof(void *);
    size_t unaligned;

    if (ro == NULL || size == NULL)
        return false;
    unaligned = ro->instance_size;
    if (unaligned > SIZE_MAX - (alignment - 1))
        return false;
    *size = (unaligned + alignment - 1) & ~(alignment - 1);
    return true;
}

static const char *raw_class_name(sword3_objc_Class cls)
{
    const struct sword3_objc_class_ro *ro = class_ro(cls);
    return ro == NULL ? NULL : ro->name;
}

static void remember_class(sword3_objc_Class cls)
{
    size_t index;

    if (cls == NULL)
        return;
    lock(&class_lock);
    for (index = 0; index < observed_class_count; ++index) {
        if (observed_classes[index] == cls) {
            unlock(&class_lock);
            return;
        }
    }
    if (observed_class_count < sizeof(observed_classes) / sizeof(observed_classes[0]))
        observed_classes[observed_class_count++] = cls;
    unlock(&class_lock);
}

SWORD3_OBJC_EXPORT sword3_objc_sel sel_registerName(const char *name)
{
    struct selector_node *node;
    size_t length;

    if (name == NULL)
        return NULL;
    lock(&selector_lock);
    for (node = selectors; node != NULL; node = node->next) {
        if (strcmp(node->name, name) == 0) {
            unlock(&selector_lock);
            return node->name;
        }
    }

    length = strlen(name);
    node = malloc(sizeof(*node) + length + 1);
    if (node == NULL) {
        unlock(&selector_lock);
        sword3_objc_unsupported_symbol("sel_registerName(out of memory)");
    }
    memcpy(node->name, name, length + 1);
    node->next = selectors;
    selectors = node;
    unlock(&selector_lock);
    return node->name;
}

SWORD3_OBJC_EXPORT sword3_objc_sel sel_getUid(const char *name)
{
    return sel_registerName(name);
}

SWORD3_OBJC_EXPORT const char *sel_getName(sword3_objc_sel selector)
{
    return selector == NULL ? "<null selector>" : selector;
}

SWORD3_OBJC_EXPORT sword3_objc_id objc_retain(sword3_objc_id object)
{
    return object;
}

SWORD3_OBJC_EXPORT void objc_release(sword3_objc_id object)
{
    (void)object;
}

SWORD3_OBJC_EXPORT sword3_objc_id objc_autorelease(sword3_objc_id object)
{
    return object;
}

SWORD3_OBJC_EXPORT sword3_objc_id
objc_retainAutorelease(sword3_objc_id object)
{
    return object;
}

SWORD3_OBJC_EXPORT sword3_objc_id
objc_retainAutoreleaseReturnValue(sword3_objc_id object)
{
    return object;
}

SWORD3_OBJC_EXPORT sword3_objc_id
objc_retainAutoreleasedReturnValue(sword3_objc_id object)
{
    return object;
}

SWORD3_OBJC_EXPORT sword3_objc_id
objc_autoreleaseReturnValue(sword3_objc_id object)
{
    return object;
}

SWORD3_OBJC_EXPORT void *objc_autoreleasePoolPush(void)
{
    struct autorelease_pool *pool = malloc(sizeof(*pool));
    if (pool == NULL)
        sword3_objc_unsupported_symbol("objc_autoreleasePoolPush(out of memory)");
    pool->previous = current_pool;
    current_pool = pool;
    return pool;
}

SWORD3_OBJC_EXPORT void objc_autoreleasePoolPop(void *token)
{
    struct autorelease_pool *pool;

    if (token == NULL)
        return;
    pool = current_pool;
    if (pool != token)
        sword3_objc_unsupported_symbol("objc_autoreleasePoolPop(invalid token)");
    current_pool = pool->previous;
    free(pool);
}

SWORD3_OBJC_EXPORT sword3_objc_id
objc_storeWeak(sword3_objc_id *location, sword3_objc_id object)
{
    if (location == NULL)
        return object;
    lock(&storage_lock);
    *location = object;
    unlock(&storage_lock);
    return object;
}

SWORD3_OBJC_EXPORT sword3_objc_id
objc_loadWeakRetained(sword3_objc_id *location)
{
    sword3_objc_id object;

    if (location == NULL)
        return NULL;
    lock(&storage_lock);
    object = *location;
    unlock(&storage_lock);
    return object;
}

SWORD3_OBJC_EXPORT void objc_destroyWeak(sword3_objc_id *location)
{
    (void)objc_storeWeak(location, NULL);
}

SWORD3_OBJC_EXPORT void
objc_storeStrong(sword3_objc_id *location, sword3_objc_id object)
{
    if (location == NULL)
        return;
    lock(&storage_lock);
    *location = object;
    unlock(&storage_lock);
}

SWORD3_OBJC_EXPORT sword3_objc_Class object_getClass(sword3_objc_id object)
{
    sword3_objc_Class cls;

    if (object == NULL)
        return NULL;
    cls = *(sword3_objc_Class *)object;
    remember_class(cls);
    return cls;
}

SWORD3_OBJC_EXPORT sword3_objc_Class
class_getSuperclass(sword3_objc_Class cls)
{
    return cls == NULL ? NULL : cls->superclass;
}

SWORD3_OBJC_EXPORT bool class_isMetaClass(sword3_objc_Class cls)
{
    const struct sword3_objc_class_ro *ro = class_ro(cls);
    return ro != NULL && (ro->flags & SWORD3_OBJC_RO_META) != 0;
}

SWORD3_OBJC_EXPORT const char *class_getName(sword3_objc_Class cls)
{
    const char *name = raw_class_name(cls);
    remember_class(cls);
    return name == NULL ? "" : name;
}

static bool relative_target(const int32_t *field, const void **target)
{
    uintptr_t address;
    int64_t offset;

    if (field == NULL || target == NULL)
        return false;
    address = (uintptr_t)field;
    offset = *field;
    if (offset >= 0) {
        if ((uint64_t)offset > UINTPTR_MAX - address)
            return false;
        address += (uintptr_t)offset;
    } else {
        uint64_t magnitude = (uint64_t)(-offset);
        if (magnitude > address)
            return false;
        address -= (uintptr_t)magnitude;
    }
    if (address == 0)
        return false;
    *target = (const void *)address;
    return true;
}

static bool decode_method(
    const struct sword3_objc_method_list *list,
    uint32_t index,
    struct decoded_method *decoded
)
{
    uint32_t stride;
    const unsigned char *entry;

    if (list == NULL || decoded == NULL ||
        (uintptr_t)list % _Alignof(uint32_t) != 0)
        return false;
    stride = (list->entsize_and_flags & UINT32_C(0xffff)) & ~UINT32_C(3);
    if (index >= list->count)
        return false;
    if ((list->entsize_and_flags & SWORD3_OBJC_METHOD_LIST_SMALL) != 0) {
        const struct sword3_objc_relative_method *small;
        const void *name_target;
        const void *types_target;
        const void *imp_target;

        if (stride < sizeof(*small) ||
            stride % _Alignof(struct sword3_objc_relative_method) != 0)
            return false;
        entry = list->entries + (size_t)index * stride;
        if ((uintptr_t)entry % _Alignof(struct sword3_objc_relative_method) != 0)
            return false;
        small = (const struct sword3_objc_relative_method *)entry;
        if (!relative_target(&small->name, &name_target) ||
            !relative_target(&small->types, &types_target) ||
            !relative_target(&small->imp, &imp_target))
            return false;
        if ((list->entsize_and_flags &
             SWORD3_OBJC_METHOD_LIST_DIRECT_SELECTORS) != 0) {
            decoded->name = name_target;
        } else {
            memcpy(&decoded->name, name_target, sizeof(decoded->name));
        }
        decoded->types = types_target;
        decoded->imp = (sword3_objc_imp)(uintptr_t)imp_target;
        decoded->original = NULL;
        decoded->identity = entry;
        return true;
    }

    if (stride < sizeof(struct sword3_objc_method) ||
        stride % _Alignof(struct sword3_objc_method) != 0)
        return false;
    entry = list->entries + (size_t)index * stride;
    if ((uintptr_t)entry % _Alignof(struct sword3_objc_method) != 0)
        return false;
    decoded->original = (const struct sword3_objc_method *)entry;
    decoded->name = decoded->original->name;
    decoded->types = decoded->original->types;
    decoded->imp = decoded->original->imp;
    decoded->identity = entry;
    return true;
}

static bool find_method_in_class(
    sword3_objc_Class cls,
    sword3_objc_sel selector,
    struct decoded_method *decoded
)
{
    const struct sword3_objc_class_ro *ro = class_ro(cls);
    const struct sword3_objc_method_list *list;
    uint32_t index;

    if (ro == NULL || selector == NULL)
        return false;
    list = ro->base_methods;
    if (list == NULL || (uintptr_t)list % _Alignof(uint32_t) != 0)
        return false;
    for (index = 0; index < list->count; ++index) {
        if (!decode_method(list, index, decoded))
            return false;
        if (decoded->name != NULL &&
            (decoded->name == selector || strcmp(decoded->name, selector) == 0))
            return true;
    }
    return false;
}

static sword3_objc_Method cache_decoded_method(
    const struct decoded_method *decoded
)
{
    struct cached_method *cached;

    lock(&method_cache_lock);
    for (cached = cached_methods; cached != NULL; cached = cached->next) {
        if (cached->identity == decoded->identity) {
            unlock(&method_cache_lock);
            return &cached->method;
        }
    }

    cached = malloc(sizeof(*cached));
    if (cached == NULL) {
        unlock(&method_cache_lock);
        sword3_objc_unsupported_symbol("class_getInstanceMethod(out of memory)");
    }
    cached->identity = decoded->identity;
    cached->method.name = decoded->name;
    cached->method.types = decoded->types;
    cached->method.imp = decoded->imp;
    cached->next = cached_methods;
    cached_methods = cached;
    unlock(&method_cache_lock);
    return &cached->method;
}

SWORD3_OBJC_EXPORT sword3_objc_Method
class_getInstanceMethod(sword3_objc_Class cls, sword3_objc_sel selector)
{
    struct decoded_method decoded;
    size_t depth;

    for (depth = 0; cls != NULL && depth < 4096; ++depth, cls = cls->superclass) {
        if (!find_method_in_class(cls, selector, &decoded))
            continue;
        if (decoded.original != NULL)
            return (sword3_objc_Method)(uintptr_t)decoded.original;
        return cache_decoded_method(&decoded);
    }
    return NULL;
}

SWORD3_OBJC_HIDDEN sword3_objc_imp sword3_objc_lookup_imp(
    sword3_objc_id receiver,
    sword3_objc_sel selector,
    sword3_objc_Class start_class
)
{
    struct decoded_method decoded;
    sword3_objc_Class cls;
    size_t depth;

    if (receiver == NULL)
        return NULL;
    cls = start_class == NULL ? *(sword3_objc_Class *)receiver : start_class;
    for (depth = 0; cls != NULL && depth < 4096; ++depth, cls = cls->superclass) {
        if (find_method_in_class(cls, selector, &decoded) && decoded.imp != NULL)
            return decoded.imp;
    }
    if (find_method_in_class(&sword3_nsobject_class, selector, &decoded) &&
        decoded.imp != NULL)
        return decoded.imp;
    if (find_method_in_class(&sword3_nsobject_metaclass, selector, &decoded) &&
        decoded.imp != NULL)
        return decoded.imp;
    return stub_unknown_selector(selector);
}

SWORD3_OBJC_EXPORT void *object_getIndexedIvars(sword3_objc_id object)
{
    const struct sword3_objc_class_ro *ro;
    sword3_objc_Class cls;
    uintptr_t base;
    size_t size;

    if (object == NULL)
        return NULL;
    cls = *(sword3_objc_Class *)object;
    ro = class_ro(cls);
    if (!aligned_instance_size(ro, &size))
        return NULL;
    base = (uintptr_t)object;
    if (size > UINTPTR_MAX - base)
        return NULL;
    return (void *)(base + size);
}

SWORD3_OBJC_EXPORT sword3_objc_Class objc_lookUpClass(const char *name)
{
    size_t index;
    sword3_objc_Class result = NULL;

    if (name == NULL)
        return NULL;
    if (strcmp(name, "NSObject") == 0)
        return &sword3_nsobject_class;

    lock(&class_lock);
    for (index = 0; index < observed_class_count; ++index) {
        const char *candidate = raw_class_name(observed_classes[index]);
        if (candidate != NULL && strcmp(candidate, name) == 0) {
            result = observed_classes[index];
            break;
        }
    }
    unlock(&class_lock);
    return result;
}

SWORD3_OBJC_EXPORT sword3_objc_Class objc_getClass(const char *name)
{
    return objc_lookUpClass(name);
}

SWORD3_OBJC_EXPORT sword3_objc_Class objc_getRequiredClass(const char *name)
{
    sword3_objc_Class cls = objc_lookUpClass(name);
    if (cls == NULL) {
        write_diagnostic("[sword3-objc-shim] required class not found: ", name);
        abort();
    }
    return cls;
}

SWORD3_OBJC_EXPORT sword3_objc_Class objc_getMetaClass(const char *name)
{
    sword3_objc_Class cls = objc_lookUpClass(name);
    return cls == NULL ? NULL : cls->isa;
}

SWORD3_OBJC_EXPORT sword3_objc_id objc_alloc(sword3_objc_Class cls)
{
    const struct sword3_objc_class_ro *ro = class_ro(cls);
    size_t size;
    sword3_objc_id object;

    if (!aligned_instance_size(ro, &size))
        return NULL;
    if (size < 2 * sizeof(void *))
        size = 2 * sizeof(void *);
    object = calloc(1, size);
    if (object != NULL)
        *(sword3_objc_Class *)object = cls;
    return object;
}

SWORD3_OBJC_EXPORT sword3_objc_id
objc_constructInstance(sword3_objc_Class cls, void *storage)
{
    if (cls == NULL || storage == NULL)
        return NULL;
    *(sword3_objc_Class *)storage = cls;
    return storage;
}

static sword3_objc_id *property_slot(
    sword3_objc_id object,
    ptrdiff_t offset
)
{
    const struct sword3_objc_class_ro *ro;
    sword3_objc_Class cls;
    uintptr_t base;
    uintptr_t address;
    size_t size;
    size_t unsigned_offset;

    if (object == NULL || offset < 0)
        return NULL;
    cls = *(sword3_objc_Class *)object;
    ro = class_ro(cls);
    if (!aligned_instance_size(ro, &size))
        return NULL;
    unsigned_offset = (size_t)offset;
    if (unsigned_offset > size ||
        sizeof(sword3_objc_id) > size - unsigned_offset)
        return NULL;
    base = (uintptr_t)object;
    if (unsigned_offset > UINTPTR_MAX - base)
        return NULL;
    address = base + unsigned_offset;
    if (address % _Alignof(sword3_objc_id) != 0)
        return NULL;
    return (sword3_objc_id *)address;
}

SWORD3_OBJC_EXPORT sword3_objc_id objc_getProperty(
    sword3_objc_id object,
    sword3_objc_sel selector,
    ptrdiff_t offset,
    bool atomic
)
{
    sword3_objc_id value;
    sword3_objc_id *slot = property_slot(object, offset);

    (void)selector;
    if (slot == NULL)
        return NULL;
    if (atomic)
        lock(&storage_lock);
    value = *slot;
    if (atomic)
        unlock(&storage_lock);
    return value;
}

static void set_property(
    sword3_objc_id object,
    ptrdiff_t offset,
    sword3_objc_id value,
    bool atomic
)
{
    sword3_objc_id *slot = property_slot(object, offset);

    if (slot == NULL)
        return;
    if (atomic)
        lock(&storage_lock);
    *slot = value;
    if (atomic)
        unlock(&storage_lock);
}

SWORD3_OBJC_EXPORT void objc_setProperty_atomic(
    sword3_objc_id object,
    sword3_objc_sel selector,
    sword3_objc_id value,
    ptrdiff_t offset
)
{
    (void)selector;
    set_property(object, offset, value, true);
}

SWORD3_OBJC_EXPORT SWORD3_OBJC_NORETURN
_Unwind_Reason_Code __objc_personality_v0(
    int version,
    _Unwind_Action actions,
    uint64_t exception_class,
    struct _Unwind_Exception *exception,
    struct _Unwind_Context *context
)
{
    (void)version;
    (void)actions;
    (void)exception_class;
    (void)exception;
    (void)context;
    /*
     * Apple ObjC exception matching and catch objects are not ABI-compatible
     * with the host C++ runtime.  Forwarding here would only appear to work
     * for some cleanup paths and could corrupt unwinding on typed catches.
     */
    sword3_objc_unsupported_symbol(
        "__objc_personality_v0(Apple exception ABI is unsupported)"
    );
}
