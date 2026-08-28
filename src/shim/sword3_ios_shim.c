#define _POSIX_C_SOURCE 200809L
#include "sword3_ios_shim.h"
#include "sword3_objc_shim.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


static _Atomic uint64_t unsupported_call_count;

struct proxy_string {
    sword3_objc_Class isa;
    char *value;
};

struct proxy_array {
    sword3_objc_Class isa;
    sword3_objc_id *items;
    size_t count;
};

struct proxy_bundle {
    sword3_objc_Class isa;
};

struct proxy_dict_entry {
    const char *key;
    const char *value;
    sword3_objc_id object;
};

struct proxy_dictionary {
    sword3_objc_Class isa;
    struct proxy_dict_entry *entries;
    size_t count;
};

struct proxy_file_manager {
    sword3_objc_Class isa;
};

struct proxy_device {
    sword3_objc_Class isa;
};

struct proxy_screen {
    sword3_objc_Class isa;
};

struct proxy_rect {
    double x;
    double y;
    double width;
    double height;
};

struct constant_string_layout {
    sword3_objc_Class isa;
    uint32_t flags;
    uint32_t reserved;
    const char *bytes;
    uintptr_t length;
};

struct proxy_method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_method methods[12];
};

extern struct sword3_objc_class proxy_string_class
    __asm__("OBJC_CLASS_$_NSString");
static struct sword3_objc_class proxy_string_metaclass;
static struct sword3_objc_class proxy_array_class;
extern struct sword3_objc_class proxy_bundle_class
    __asm__("OBJC_CLASS_$_NSBundle");
extern struct sword3_objc_class proxy_bundle_metaclass
    __asm__("OBJC_METACLASS_$_NSBundle");
static struct proxy_bundle main_bundle;
extern struct sword3_objc_class proxy_dictionary_class
    __asm__("OBJC_CLASS_$_NSDictionary");
static struct sword3_objc_class proxy_dictionary_metaclass;
static struct proxy_dictionary main_info_dictionary;
extern struct sword3_objc_class proxy_file_manager_class
    __asm__("OBJC_CLASS_$_NSFileManager");
static struct sword3_objc_class proxy_file_manager_metaclass;
static struct proxy_file_manager default_file_manager;
extern struct sword3_objc_class proxy_locale_class
    __asm__("OBJC_CLASS_$_NSLocale");
static struct sword3_objc_class proxy_locale_metaclass;
extern struct sword3_objc_class proxy_device_class
    __asm__("OBJC_CLASS_$_UIDevice");
static struct sword3_objc_class proxy_device_metaclass;
static struct proxy_device current_device;
extern struct sword3_objc_class proxy_screen_class
    __asm__("OBJC_CLASS_$_UIScreen");
static struct sword3_objc_class proxy_screen_metaclass;
static struct proxy_screen main_screen;
extern struct sword3_objc_class proxy_exception_class
    __asm__("OBJC_CLASS_$_NSException");
static struct sword3_objc_class proxy_exception_metaclass;

static struct proxy_array *make_proxy_array(size_t count);

static const char *object_cstring(const void *object)
{
    const struct proxy_string *proxy = object;
    const struct constant_string_layout *constant = object;

    if (!object)
        return "";
    if (proxy->isa == &proxy_string_class)
        return proxy->value ? proxy->value : "";
    /*
     * Clang's 64-bit NSConstantString layout stores the byte pointer after
     * isa and two 32-bit fields.  Validate the cheap invariants before use.
     */
    if (constant->bytes && constant->length < (uintptr_t)SIZE_MAX)
        return constant->bytes;
    return "";
}

static void append_text(char *out, size_t *used, size_t cap, const char *text)
{
    size_t length;

    if (!text)
        text = "(null)";
    length = strlen(text);
    if (*used >= cap)
        return;
    if (length > cap - *used - 1)
        length = cap - *used - 1;
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
}

static const uintptr_t *consume_apple_slot(const uintptr_t **stack)
{
    const uintptr_t *slot;

    if (!stack || !*stack)
        return NULL;
    slot = *stack;
    *stack += 1;
    return slot;
}

static void format_exception_reason(char *out, size_t cap, const char *format,
				    const uintptr_t *apple_stack)
{
    const uintptr_t *cursor = apple_stack;
    size_t used = 0;
    const char *p;

    if (!out || cap == 0)
        return;
    out[0] = '\0';
    if (!format) {
        append_text(out, &used, cap, "(null format)");
        return;
    }
    for (p = format; *p && used + 1 < cap; p++) {
        char tmp[64];
        const uintptr_t *slot;
        char specifier;

        if (*p != '%') {
            out[used++] = *p;
            out[used] = '\0';
            continue;
        }
        p++;
        if (*p == '%') {
            out[used++] = '%';
            out[used] = '\0';
            continue;
        }
        while (*p && strchr("-+ #0", *p))
            p++;
        while (*p && *p >= '0' && *p <= '9')
            p++;
        if (*p == '.') {
            p++;
            while (*p && *p >= '0' && *p <= '9')
                p++;
        }
        while (*p && strchr("hlLzjt", *p))
            p++;
        specifier = *p;
        if (!specifier)
            break;
        slot = consume_apple_slot(&cursor);
        if (specifier == '@') {
            append_text(out, &used, cap,
                        slot ? object_cstring((const void *)*slot) : "(null)");
        } else if (specifier == 's') {
            append_text(out, &used, cap,
                        slot && *slot ? (const char *)*slot : "(null)");
        } else if (specifier == 'p') {
            snprintf(tmp, sizeof(tmp), "%p",
                     slot ? (void *)*slot : NULL);
            append_text(out, &used, cap, tmp);
        } else if (specifier == 'f' || specifier == 'g' || specifier == 'e' ||
                   specifier == 'G' || specifier == 'E') {
            double value = 0.0;
            if (slot)
                memcpy(&value, slot, sizeof(value));
            snprintf(tmp, sizeof(tmp), "%g", value);
            append_text(out, &used, cap, tmp);
        } else if (specifier == 'u' || specifier == 'x' || specifier == 'X' ||
                   specifier == 'o') {
            snprintf(tmp, sizeof(tmp),
                     specifier == 'u' ? "%llu" : "%llx",
                     slot ? (unsigned long long)*slot : 0ull);
            append_text(out, &used, cap, tmp);
        } else {
            snprintf(tmp, sizeof(tmp), "%lld",
                     slot ? (long long)*slot : 0ll);
            append_text(out, &used, cap, tmp);
        }
    }
}

__attribute__((used, visibility("hidden")))
void sword3_ns_raise_impl(sword3_objc_id cls, sword3_objc_sel selector,
			  sword3_objc_id name, sword3_objc_id format,
			  const uintptr_t *apple_stack)
{
    const char *name_text = object_cstring(name);
    const char *format_text = object_cstring(format);
    char reason[1024];
    static unsigned seen;
    (void)cls;
    (void)selector;

    format_exception_reason(reason, sizeof(reason), format_text, apple_stack);
    seen++;
    if (seen <= 3) {
	fprintf(stderr,
		"sword3-ios-shim: NSException raise name=%s format=%s\n",
		name_text, format_text);
	fprintf(stderr, "sword3-ios-shim: NSException reason=%s\n", reason);
    } else if (seen == 4) {
	fprintf(stderr,
		"sword3-ios-shim: NSException further raises suppressed\n");
    }
    /*
     * raise:format: is NS_NORETURN on iOS. Returning lets this binary's
     * SDL_image loader fall through into its SDL_RWFromFile path, which is
     * the same block used when ImageIO construction fails.
     */
}

#if defined(__aarch64__)
void proxy_exception_raise_format(void);
#else
static void proxy_exception_raise_format(sword3_objc_id cls,
					 sword3_objc_sel selector,
					 sword3_objc_id name,
					 sword3_objc_id format)
{
	sword3_ns_raise_impl(cls, selector, name, format, NULL);
}
#endif

static void proxy_exception_raise_arguments(sword3_objc_id cls,
					    sword3_objc_sel selector,
					    sword3_objc_id name,
					    sword3_objc_id format,
					    const uintptr_t *apple_stack)
{
	sword3_ns_raise_impl(cls, selector, name, format, apple_stack);
}

static const char *proxy_string_utf8(struct proxy_string *self,
                                     sword3_objc_sel selector)
{
    (void)selector;
    return self && self->value ? self->value : "";
}

static uintptr_t proxy_string_length(struct proxy_string *self,
                                     sword3_objc_sel selector)
{
    (void)selector;
    return self && self->value ? strlen(self->value) : 0;
}

static struct proxy_string *make_proxy_string(const char *value)
{
    struct proxy_string *string = calloc(1, sizeof(*string));
    if (!string)
        return NULL;
    string->value = strdup(value ? value : "");
    if (!string->value) {
        free(string);
        return NULL;
    }
    string->isa = &proxy_string_class;
    return string;
}

static sword3_objc_id proxy_string_from_utf8(sword3_objc_id cls,
					     sword3_objc_sel selector,
					     const char *utf8)
{
    (void)cls;
    (void)selector;
    return make_proxy_string(utf8);
}

static struct proxy_string *proxy_string_append(
    struct proxy_string *self,
    sword3_objc_sel selector,
    struct proxy_string *component
)
{
    const char *left = object_cstring(self);
    const char *right = object_cstring(component);
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    int slash = left_length > 0 && left[left_length - 1] != '/';
    char *joined;
    struct proxy_string *result;
    (void)selector;

    if (left_length > SIZE_MAX - right_length - (size_t)slash - 1)
        return NULL;
    joined = malloc(left_length + right_length + (size_t)slash + 1);
    if (!joined)
        return NULL;
    memcpy(joined, left, left_length);
    if (slash)
        joined[left_length++] = '/';
    memcpy(joined + left_length, right, right_length + 1);
    result = make_proxy_string(joined);
    free(joined);
    return result;
}

static sword3_objc_id proxy_string_to_index(struct proxy_string *self,
                                             sword3_objc_sel selector,
                                             uintptr_t index)
{
    const char *value = object_cstring(self);
    size_t length = strlen(value);
    char *copy;
    sword3_objc_id result;
    (void)selector;
    if (index > length)
        index = length;
    copy = malloc(index + 1);
    if (!copy)
        return NULL;
    memcpy(copy, value, index);
    copy[index] = '\0';
    result = make_proxy_string(copy);
    free(copy);
    return result;
}

static sword3_objc_id proxy_string_from_index(struct proxy_string *self,
                                               sword3_objc_sel selector,
                                               uintptr_t index)
{
    const char *value = object_cstring(self);
    size_t length = strlen(value);
    (void)selector;
    if (index > length)
        index = length;
    return make_proxy_string(value + index);
}

static int proxy_string_equal(struct proxy_string *self,
                              sword3_objc_sel selector,
                              sword3_objc_id other)
{
    (void)selector;
    return strcmp(object_cstring(self), object_cstring(other)) == 0;
}

static int proxy_string_has_prefix(struct proxy_string *self,
                                   sword3_objc_sel selector,
                                   sword3_objc_id prefix_object)
{
    const char *value = object_cstring(self);
    const char *prefix = object_cstring(prefix_object);
    size_t length = strlen(prefix);
    (void)selector;
    return strncmp(value, prefix, length) == 0;
}

static sword3_objc_id proxy_string_components(
    struct proxy_string *self,
    sword3_objc_sel selector,
    sword3_objc_id separator_object
)
{
    const char *value = object_cstring(self);
    const char *separator = object_cstring(separator_object);
    size_t separator_length = strlen(separator);
    size_t count = 1;
    const char *cursor;
    struct proxy_array *array;
    size_t index = 0;
    (void)selector;

    if (!separator_length) {
        array = make_proxy_array(1);
        if (array)
            array->items[0] = make_proxy_string(value);
        return array;
    }
    cursor = value;
    while ((cursor = strstr(cursor, separator)) != NULL) {
        count++;
        cursor += separator_length;
    }
    array = make_proxy_array(count);
    if (!array)
        return NULL;
    cursor = value;
    while (index < count) {
        const char *next = strstr(cursor, separator);
        size_t length = next ? (size_t)(next - cursor) : strlen(cursor);
        char *part = malloc(length + 1);
        if (!part)
            return array;
        memcpy(part, cursor, length);
        part[length] = '\0';
        array->items[index++] = make_proxy_string(part);
        free(part);
        if (!next)
            break;
        cursor = next + separator_length;
    }
    array->count = index;
    return array;
}

static int proxy_string_int_value(struct proxy_string *self,
                                  sword3_objc_sel selector)
{
    (void)selector;
    return (int)strtol(object_cstring(self), NULL, 10);
}

static sword3_objc_id proxy_array_first(struct proxy_array *self,
                                        sword3_objc_sel selector)
{
    (void)selector;
    return self && self->count ? self->items[0] : NULL;
}

static sword3_objc_id proxy_array_at(struct proxy_array *self,
                                     sword3_objc_sel selector,
                                     uintptr_t index)
{
    (void)selector;
    return self && index < self->count ? self->items[index] : NULL;
}

static uintptr_t proxy_array_count(struct proxy_array *self,
                                   sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->count : 0;
}

struct fast_enumeration_state {
    uintptr_t state;
    sword3_objc_id *items_ptr;
    uintptr_t *mutations_ptr;
    uintptr_t extra[5];
};

static uintptr_t proxy_array_enumerate(
    struct proxy_array *self,
    sword3_objc_sel selector,
    struct fast_enumeration_state *state,
    sword3_objc_id *stack_buffer,
    uintptr_t requested
)
{
    static uintptr_t mutation_token;
    (void)selector;
    (void)stack_buffer;
    (void)requested;
    if (!self || !state || state->state)
        return 0;
    state->state = 1;
    state->items_ptr = self->items;
    state->mutations_ptr = &mutation_token;
    return self->count;
}

static struct proxy_array *make_proxy_array(size_t count)
{
    struct proxy_array *array = calloc(1, sizeof(*array));
    if (!array)
        return NULL;
    if (count) {
        array->items = calloc(count, sizeof(*array->items));
        if (!array->items) {
            free(array);
            return NULL;
        }
    }
    array->isa = &proxy_array_class;
    array->count = count;
    return array;
}

static sword3_objc_id proxy_locale_preferred_languages(
    sword3_objc_id cls,
    sword3_objc_sel selector
)
{
    struct proxy_array *languages = make_proxy_array(1);
    (void)cls;
    (void)selector;
    if (!languages)
        return NULL;
    languages->items[0] = make_proxy_string("zh-Hans");
    if (!languages->items[0]) {
        free(languages->items);
        free(languages);
        return NULL;
    }
    return languages;
}

static sword3_objc_id proxy_device_current(sword3_objc_id cls,
                                            sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    current_device.isa = &proxy_device_class;
    return &current_device;
}

static uintptr_t proxy_device_idiom(struct proxy_device *self,
                                    sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return 0; /* UIUserInterfaceIdiomPhone */
}

static uintptr_t proxy_device_orientation(struct proxy_device *self,
                                          sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return 3; /* UIDeviceOrientationLandscapeLeft */
}

static sword3_objc_id proxy_device_model(struct proxy_device *self,
                                         sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return make_proxy_string("ROCKNIX");
}

static sword3_objc_id proxy_device_system_name(struct proxy_device *self,
                                               sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return make_proxy_string("Linux");
}

static sword3_objc_id proxy_device_system_version(struct proxy_device *self,
                                                  sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return make_proxy_string("13.5");
}

static void proxy_noop(sword3_objc_id self, sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
}

static sword3_objc_id proxy_screen_main(sword3_objc_id cls,
                                        sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    main_screen.isa = &proxy_screen_class;
    return &main_screen;
}

static struct proxy_rect proxy_screen_bounds(struct proxy_screen *self,
                                              sword3_objc_sel selector)
{
    struct proxy_rect result = {0.0, 0.0, 640.0, 480.0};
    (void)self;
    (void)selector;
    return result;
}

static double proxy_screen_scale(struct proxy_screen *self,
                                 sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return 1.0;
}

static sword3_objc_id proxy_bundle_main(sword3_objc_id cls,
                                        sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    main_bundle.isa = &proxy_bundle_class;
    return &main_bundle;
}

static sword3_objc_id proxy_bundle_resource_path(struct proxy_bundle *self,
                                                 sword3_objc_sel selector)
{
    const char *path = getenv("SWORD3_BUNDLE_DIR");
    (void)self;
    (void)selector;
    return make_proxy_string(path && path[0] == '/' ? path : ".");
}

static sword3_objc_id proxy_bundle_path_for_resource(
    struct proxy_bundle *self,
    sword3_objc_sel selector,
    sword3_objc_id name_object,
    sword3_objc_id extension_object
)
{
    const char *root = getenv("SWORD3_BUNDLE_DIR");
    const char *name = object_cstring(name_object);
    const char *extension = object_cstring(extension_object);
    size_t root_length;
    size_t name_length = strlen(name);
    size_t extension_length = strlen(extension);
    size_t total;
    char *path;
    sword3_objc_id result;
    (void)self;
    (void)selector;

    if (!root || root[0] != '/')
        root = ".";
    root_length = strlen(root);
    if (root_length > SIZE_MAX - name_length - extension_length - 3)
        return NULL;
    total = root_length + 1 + name_length +
            (extension_length ? 1 + extension_length : 0) + 1;
    path = malloc(total);
    if (!path)
        return NULL;
    memcpy(path, root, root_length);
    path[root_length++] = '/';
    memcpy(path + root_length, name, name_length);
    root_length += name_length;
    if (extension_length) {
        path[root_length++] = '.';
        memcpy(path + root_length, extension, extension_length);
        root_length += extension_length;
    }
    path[root_length] = '\0';
    result = make_proxy_string(path);
    free(path);
    return result;
}

static struct proxy_dict_entry info_dictionary_entries[] = {
    {"CFBundleDevelopmentRegion", "English", NULL},
    {"CFBundleExecutable", "SWD3", NULL},
    {"CFBundleIdentifier", "com.softstar.swd3", NULL},
    {"CFBundleInfoDictionaryVersion", "6.0", NULL},
    {"CFBundleName", "SWD3", NULL},
    {"CFBundlePackageType", "APPL", NULL},
    {"CFBundleShortVersionString", "3.2", NULL},
    {"CFBundleSignature", "????", NULL},
    {"CFBundleVersion", "336", NULL},
    {"DTPlatformName", "iphoneos", NULL},
    {"DTPlatformVersion", "13.5", NULL},
    {"DTSDKName", "iphoneos13.5", NULL},
    {"MinimumOSVersion", "10.0", NULL},
    {"UILaunchImageFile", "LaunchImage", NULL},
    {"UILaunchStoryboardName", "Launch Screen", NULL},
    {"UIStatusBarStyle", "UIStatusBarStyleDefault", NULL},
};

static void init_info_dictionary(void)
{
    size_t i;

    main_info_dictionary.isa = &proxy_dictionary_class;
    main_info_dictionary.entries = info_dictionary_entries;
    main_info_dictionary.count =
        sizeof(info_dictionary_entries) / sizeof(info_dictionary_entries[0]);
    for (i = 0; i < main_info_dictionary.count; i++) {
        info_dictionary_entries[i].object =
            make_proxy_string(info_dictionary_entries[i].value);
    }
}

static sword3_objc_id proxy_bundle_info_dictionary(
    struct proxy_bundle *self,
    sword3_objc_sel selector
)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    (void)self;
    (void)selector;
    pthread_once(&once, init_info_dictionary);
    return &main_info_dictionary;
}

static sword3_objc_id proxy_dictionary_object_for_key(
    struct proxy_dictionary *self,
    sword3_objc_sel selector,
    sword3_objc_id key
)
{
    const char *name = object_cstring(key);
    size_t i;
    (void)selector;

    if (!self)
        return NULL;
    for (i = 0; i < self->count; i++) {
        if (strcmp(self->entries[i].key, name) == 0)
            return self->entries[i].object;
    }
    fprintf(stderr, "sword3-ios-shim: NSDictionary missing key %s\n",
            name && name[0] ? name : "(empty)");
    return NULL;
}

static uintptr_t proxy_dictionary_count(struct proxy_dictionary *self,
                                        sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->count : 0;
}

static sword3_objc_id proxy_bundle_object_for_info_key(
    struct proxy_bundle *self,
    sword3_objc_sel selector,
    sword3_objc_id key
)
{
    return proxy_dictionary_object_for_key(
        proxy_bundle_info_dictionary(self, selector), selector, key);
}

static sword3_objc_id proxy_file_manager_default(sword3_objc_id cls,
                                                  sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    default_file_manager.isa = &proxy_file_manager_class;
    return &default_file_manager;
}

static int proxy_file_manager_exists(struct proxy_file_manager *self,
                                     sword3_objc_sel selector,
                                     sword3_objc_id path_object)
{
    struct stat status;
    (void)self;
    (void)selector;
    return stat(object_cstring(path_object), &status) == 0;
}

static int path_has_dotdot(const char *path)
{
    const char *cursor = path;

    while (cursor && *cursor) {
        if (cursor[0] == '.' && cursor[1] == '.' &&
            (cursor[2] == '/' || cursor[2] == '\0') &&
            (cursor == path || cursor[-1] == '/'))
            return 1;
        cursor++;
    }
    return 0;
}

static int path_under_root(const char *path, const char *root)
{
    size_t root_len;

    if (!path || !root || root[0] != '/' || path[0] != '/')
        return 0;
    root_len = strlen(root);
    while (root_len > 1 && root[root_len - 1] == '/')
        root_len--;
    if (strncmp(path, root, root_len) != 0)
        return 0;
    return path[root_len] == '/' && path[root_len + 1] != '\0';
}

static int file_manager_path_allowed(const char *path)
{
    const char *data = getenv("SWORD3_DATA_DIR");
    const char *bundle = getenv("SWORD3_BUNDLE_DIR");
    const char *tmpdir = getenv("TMPDIR");

    if (!path || path[0] != '/' || path_has_dotdot(path))
        return 0;
    if (path_under_root(path, data) ||
        path_under_root(path, bundle) ||
        path_under_root(path, "/tmp/sword3") ||
        path_under_root(path, tmpdir))
        return 1;
    return 0;
}

static int remove_tree(const char *path)
{
    struct stat status;
    DIR *directory;
    struct dirent *entry;

    if (lstat(path, &status) != 0)
        return -1;
    if (S_ISDIR(status.st_mode) && !S_ISLNK(status.st_mode)) {
        directory = opendir(path);
        if (!directory)
            return -1;
        while ((entry = readdir(directory)) != NULL) {
            char child[PATH_MAX];
            int written;

            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            written = snprintf(child, sizeof(child), "%s/%s", path,
                               entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(child) ||
                remove_tree(child) != 0) {
                closedir(directory);
                return -1;
            }
        }
        closedir(directory);
        return rmdir(path);
    }
    return unlink(path);
}

static int proxy_file_manager_remove(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id path_object,
    sword3_objc_id *error
)
{
    const char *path = object_cstring(path_object);
    int ok = 0;
    (void)self;
    (void)selector;

    if (error)
        *error = NULL;
    if (!file_manager_path_allowed(path))
        errno = EPERM;
    else if (remove_tree(path) == 0)
        ok = 1;
    fprintf(stderr, "sword3-ios-shim: removeItemAtPath:%s -> %s%s%s\n",
            path, ok ? "ok" : "no",
            ok ? "" : " ", ok ? "" : strerror(errno));
    return ok;
}

static int mkdir_parents(const char *path)
{
    char copy[PATH_MAX];
    size_t length;
    size_t index;

    if (!path || path[0] != '/' || strlen(path) >= sizeof(copy)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(copy, path, strlen(path) + 1);
    length = strlen(copy);
    for (index = 1; index < length; index++) {
        if (copy[index] != '/')
            continue;
        copy[index] = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
            copy[index] = '/';
            return -1;
        }
        copy[index] = '/';
    }
    if (mkdir(copy, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int proxy_file_manager_create_directory(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id path_object,
    int with_intermediates,
    sword3_objc_id attributes,
    sword3_objc_id *error
)
{
    const char *path = object_cstring(path_object);
    int ok = 0;
    (void)self;
    (void)selector;
    (void)attributes;

    if (error)
        *error = NULL;
    if (file_manager_path_allowed(path)) {
        if (with_intermediates)
            ok = mkdir_parents(path) == 0;
        else
            ok = mkdir(path, 0755) == 0 || errno == EEXIST;
    } else {
        errno = EPERM;
    }
    fprintf(stderr,
            "sword3-ios-shim: createDirectoryAtPath:%s intermediates=%d -> %s\n",
            path, with_intermediates, ok ? "ok" : strerror(errno));
    return ok;
}

static sword3_objc_id proxy_file_manager_contents(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id path_object,
    sword3_objc_id *error
)
{
    DIR *directory;
    struct dirent *entry;
    struct proxy_array *array;
    size_t count = 0;
    size_t capacity = 16;
    sword3_objc_id *items;
    (void)self;
    (void)selector;

    if (error)
        *error = NULL;
    directory = opendir(object_cstring(path_object));
    if (!directory)
        return make_proxy_array(0);
    items = calloc(capacity, sizeof(*items));
    if (!items) {
        closedir(directory);
        return NULL;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (count == capacity) {
            size_t next_capacity = capacity * 2;
            sword3_objc_id *grown;
            if (next_capacity < capacity ||
                next_capacity > SIZE_MAX / sizeof(*items))
                break;
            grown = realloc(items, next_capacity * sizeof(*items));
            if (!grown)
                break;
            items = grown;
            capacity = next_capacity;
        }
        items[count] = make_proxy_string(entry->d_name);
        if (!items[count])
            break;
        count++;
    }
    closedir(directory);
    array = make_proxy_array(count);
    if (!array) {
        free(items);
        return NULL;
    }
    memcpy(array->items, items, count * sizeof(*items));
    free(items);
    return array;
}

static const struct proxy_method_list proxy_string_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 11,
    .methods = {
        {"UTF8String", "*16@0:8", (sword3_objc_imp)proxy_string_utf8},
        {"fileSystemRepresentation", "*16@0:8",
         (sword3_objc_imp)proxy_string_utf8},
        {"length", "Q16@0:8", (sword3_objc_imp)proxy_string_length},
        {"stringByAppendingPathComponent:", "@24@0:8@16",
         (sword3_objc_imp)proxy_string_append},
        {"substringToIndex:", "@24@0:8Q16",
         (sword3_objc_imp)proxy_string_to_index},
        {"substringFromIndex:", "@24@0:8Q16",
         (sword3_objc_imp)proxy_string_from_index},
        {"isEqualToString:", "B24@0:8@16",
         (sword3_objc_imp)proxy_string_equal},
        {"isEqual:", "B24@0:8@16",
         (sword3_objc_imp)proxy_string_equal},
        {"hasPrefix:", "B24@0:8@16",
         (sword3_objc_imp)proxy_string_has_prefix},
        {"componentsSeparatedByString:", "@24@0:8@16",
         (sword3_objc_imp)proxy_string_components},
        {"intValue", "i16@0:8", (sword3_objc_imp)proxy_string_int_value},
    },
};

static const struct proxy_method_list proxy_string_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"stringWithUTF8String:", "@24@0:8*16",
         (sword3_objc_imp)proxy_string_from_utf8},
    },
};

static const struct proxy_method_list proxy_array_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 4,
    .methods = {
        {"firstObject", "@16@0:8", (sword3_objc_imp)proxy_array_first},
        {"objectAtIndex:", "@24@0:8Q16", (sword3_objc_imp)proxy_array_at},
        {"count", "Q16@0:8", (sword3_objc_imp)proxy_array_count},
        {"countByEnumeratingWithState:objects:count:", "Q40@0:8^v16^@24Q32",
         (sword3_objc_imp)proxy_array_enumerate},
    },
};

static const struct proxy_method_list proxy_bundle_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 5,
    .methods = {
        {"resourcePath", "@16@0:8",
         (sword3_objc_imp)proxy_bundle_resource_path},
        {"bundlePath", "@16@0:8",
         (sword3_objc_imp)proxy_bundle_resource_path},
        {"pathForResource:ofType:", "@32@0:8@16@24",
         (sword3_objc_imp)proxy_bundle_path_for_resource},
        {"infoDictionary", "@16@0:8",
         (sword3_objc_imp)proxy_bundle_info_dictionary},
        {"objectForInfoDictionaryKey:", "@24@0:8@16",
         (sword3_objc_imp)proxy_bundle_object_for_info_key},
    },
};

static const struct proxy_method_list proxy_bundle_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"mainBundle", "@16@0:8", (sword3_objc_imp)proxy_bundle_main},
    },
};

static const struct proxy_method_list proxy_dictionary_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 3,
    .methods = {
        {"objectForKey:", "@24@0:8@16",
         (sword3_objc_imp)proxy_dictionary_object_for_key},
        {"objectForKeyedSubscript:", "@24@0:8@16",
         (sword3_objc_imp)proxy_dictionary_object_for_key},
        {"count", "Q16@0:8", (sword3_objc_imp)proxy_dictionary_count},
    },
};

static const struct proxy_method_list proxy_file_manager_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 4,
    .methods = {
        {"fileExistsAtPath:", "B24@0:8@16",
         (sword3_objc_imp)proxy_file_manager_exists},
        {"contentsOfDirectoryAtPath:error:", "@32@0:8@16^@24",
         (sword3_objc_imp)proxy_file_manager_contents},
        {"removeItemAtPath:error:", "B32@0:8@16^@24",
         (sword3_objc_imp)proxy_file_manager_remove},
        {"createDirectoryAtPath:withIntermediateDirectories:attributes:error:",
         "B44@0:8@16B24@28^@36",
         (sword3_objc_imp)proxy_file_manager_create_directory},
    },
};

static const struct proxy_method_list proxy_file_manager_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"defaultManager", "@16@0:8",
         (sword3_objc_imp)proxy_file_manager_default},
    },
};

static const struct proxy_method_list proxy_locale_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"preferredLanguages", "@16@0:8",
         (sword3_objc_imp)proxy_locale_preferred_languages},
    },
};

static const struct proxy_method_list proxy_device_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 7,
    .methods = {
        {"userInterfaceIdiom", "Q16@0:8",
         (sword3_objc_imp)proxy_device_idiom},
        {"orientation", "Q16@0:8",
         (sword3_objc_imp)proxy_device_orientation},
        {"model", "@16@0:8", (sword3_objc_imp)proxy_device_model},
        {"systemName", "@16@0:8", (sword3_objc_imp)proxy_device_system_name},
        {"systemVersion", "@16@0:8",
         (sword3_objc_imp)proxy_device_system_version},
        {"beginGeneratingDeviceOrientationNotifications", "v16@0:8",
         (sword3_objc_imp)proxy_noop},
        {"endGeneratingDeviceOrientationNotifications", "v16@0:8",
         (sword3_objc_imp)proxy_noop},
    },
};

static const struct proxy_method_list proxy_device_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"currentDevice", "@16@0:8", (sword3_objc_imp)proxy_device_current},
    },
};

static const struct proxy_method_list proxy_screen_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 4,
    .methods = {
        {"bounds", "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8",
         (sword3_objc_imp)proxy_screen_bounds},
        {"nativeBounds", "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8",
         (sword3_objc_imp)proxy_screen_bounds},
        {"scale", "d16@0:8", (sword3_objc_imp)proxy_screen_scale},
        {"nativeScale", "d16@0:8", (sword3_objc_imp)proxy_screen_scale},
    },
};

static const struct proxy_method_list proxy_screen_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"mainScreen", "@16@0:8", (sword3_objc_imp)proxy_screen_main},
    },
};

static const struct proxy_method_list proxy_exception_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 2,
    .methods = {
        {"raise:format:", "v32@0:8@16@24",
         (sword3_objc_imp)proxy_exception_raise_format},
        {"raise:format:arguments:", "v40@0:8@16@24^v32",
         (sword3_objc_imp)proxy_exception_raise_arguments},
    },
};

static const struct sword3_objc_class_ro proxy_string_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_string),
    .name = "NSString",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_string_methods,
};

static const struct sword3_objc_class_ro proxy_string_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSString",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_string_class_methods,
};

static const struct sword3_objc_class_ro proxy_array_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_array),
    .name = "Sword3HostArray",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_array_methods,
};

static const struct sword3_objc_class_ro proxy_bundle_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_bundle),
    .name = "NSBundle",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_bundle_methods,
};

static const struct sword3_objc_class_ro proxy_bundle_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSBundle",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_bundle_class_methods,
};

static const struct sword3_objc_class_ro proxy_dictionary_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_dictionary),
    .name = "NSDictionary",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_dictionary_methods,
};

static const struct sword3_objc_class_ro proxy_dictionary_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSDictionary",
};

static const struct sword3_objc_class_ro proxy_file_manager_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_file_manager),
    .name = "NSFileManager",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_file_manager_methods,
};

static const struct sword3_objc_class_ro proxy_file_manager_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSFileManager",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_file_manager_class_methods,
};

static const struct sword3_objc_class_ro proxy_locale_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSLocale",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_locale_class_methods,
};

static const struct sword3_objc_class_ro proxy_device_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_device),
    .name = "UIDevice",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_device_methods,
};

static const struct sword3_objc_class_ro proxy_device_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIDevice",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_device_class_methods,
};

static const struct sword3_objc_class_ro proxy_screen_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_screen),
    .name = "UIScreen",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_screen_methods,
};

static const struct sword3_objc_class_ro proxy_screen_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIScreen",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_screen_class_methods,
};

static const struct sword3_objc_class_ro proxy_exception_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(void *),
    .name = "NSException",
};

static const struct sword3_objc_class_ro proxy_exception_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSException",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_exception_class_methods,
};

static struct sword3_objc_class proxy_string_metaclass = {
    .isa = &proxy_string_metaclass,
    .data_bits = (uintptr_t)&proxy_string_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_string_class
    __asm__("OBJC_CLASS_$_NSString") = {
        .isa = &proxy_string_metaclass,
        .data_bits = (uintptr_t)&proxy_string_ro,
    };

static struct sword3_objc_class proxy_array_class = {
    .isa = &proxy_array_class,
    .data_bits = (uintptr_t)&proxy_array_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_bundle_metaclass
    __asm__("OBJC_METACLASS_$_NSBundle") = {
        .isa = &proxy_bundle_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_bundle_metaclass_ro,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_bundle_class
    __asm__("OBJC_CLASS_$_NSBundle") = {
        .isa = &proxy_bundle_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_bundle_ro,
    };

static struct sword3_objc_class proxy_dictionary_metaclass = {
    .isa = &proxy_dictionary_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_dictionary_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_dictionary_class
    __asm__("OBJC_CLASS_$_NSDictionary") = {
        .isa = &proxy_dictionary_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_dictionary_ro,
    };

static struct sword3_objc_class proxy_file_manager_metaclass = {
    .isa = &proxy_file_manager_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_file_manager_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_file_manager_class
    __asm__("OBJC_CLASS_$_NSFileManager") = {
        .isa = &proxy_file_manager_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_file_manager_ro,
    };

static struct sword3_objc_class proxy_locale_metaclass = {
    .isa = &proxy_locale_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_locale_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_locale_class
    __asm__("OBJC_CLASS_$_NSLocale") = {
        .isa = &proxy_locale_metaclass,
        .superclass = NULL,
        .data_bits = 0,
    };

static struct sword3_objc_class proxy_device_metaclass = {
    .isa = &proxy_device_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_device_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_device_class
    __asm__("OBJC_CLASS_$_UIDevice") = {
        .isa = &proxy_device_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_device_ro,
    };

static struct sword3_objc_class proxy_screen_metaclass = {
    .isa = &proxy_screen_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_screen_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_screen_class
    __asm__("OBJC_CLASS_$_UIScreen") = {
        .isa = &proxy_screen_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_screen_ro,
    };

static struct sword3_objc_class proxy_exception_metaclass = {
    .isa = &proxy_exception_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_exception_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_exception_class
    __asm__("OBJC_CLASS_$_NSException") = {
        .isa = &proxy_exception_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_exception_ro,
    };

SWORD3_EXPORT double CGRectGetMinX(struct proxy_rect rect) { return rect.x; }
SWORD3_EXPORT double CGRectGetMinY(struct proxy_rect rect) { return rect.y; }
SWORD3_EXPORT double CGRectGetWidth(struct proxy_rect rect) { return rect.width; }
SWORD3_EXPORT double CGRectGetHeight(struct proxy_rect rect) { return rect.height; }
SWORD3_EXPORT double CGRectGetMaxX(struct proxy_rect rect)
{
    return rect.x + rect.width;
}
SWORD3_EXPORT double CGRectGetMaxY(struct proxy_rect rect)
{
    return rect.y + rect.height;
}
SWORD3_EXPORT double CGRectGetMidX(struct proxy_rect rect)
{
    return rect.x + rect.width * 0.5;
}
SWORD3_EXPORT double CGRectGetMidY(struct proxy_rect rect)
{
    return rect.y + rect.height * 0.5;
}

SWORD3_EXPORT void NSLog(sword3_objc_id format)
{
    static const char message[] = "[sword3-nslog] message suppressed\n";
    (void)format;
    /*
     * NSLog is variadic under Apple's stack-only vararg ABI.  Logging the
     * format argument without consuming that foreign va_list keeps startup
     * diagnostics safe; Machismo's own traces carry the actionable details.
     */
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
}

SWORD3_EXPORT
sword3_objc_id NSSearchPathForDirectoriesInDomains(
    uintptr_t directory,
    uintptr_t domain_mask,
    int expand_tilde
)
{
    struct proxy_array *array;
    const char *data = getenv("SWORD3_DATA_DIR");
    const char *tmpdir = getenv("TMPDIR");
    const char *path;
    char transient_path[PATH_MAX];
    int written;
    static unsigned seen;
    (void)domain_mask;
    (void)expand_tilde;

    /*
     * NSDocumentDirectory (9) is persistent game data. The old shim returned
     * that same directory for NSCachesDirectory and every other query, so the
     * game's startup cache cleanup enumerated and deleted CommonSave.lua and
     * every *.sav file. Keep non-document searches in a separate transient
     * tree.
     */
    if (directory == 9) {
        path = data && data[0] == '/' ? data : "/tmp/sword3/documents";
    } else {
        if (!tmpdir || tmpdir[0] != '/')
            tmpdir = "/tmp/sword3";
        written = snprintf(transient_path, sizeof(transient_path),
                           "%s/ns-search-%llu", tmpdir,
                           (unsigned long long)directory);
        if (written < 0 || (size_t)written >= sizeof(transient_path)) {
            path = "/tmp/sword3/ns-search";
        } else {
            path = transient_path;
        }
    }
    if (mkdir_parents(path) != 0 && errno != EEXIST)
        fprintf(stderr, "sword3-ios-shim: cannot create search path %s: %s\n",
                path, strerror(errno));
    if (seen++ < 32)
        fprintf(stderr,
                "sword3-ios-shim: NSSearchPath directory=%llu -> %s\n",
                (unsigned long long)directory, path);
    array = make_proxy_array(1);
    if (!array)
        return NULL;
    array->items[0] = make_proxy_string(path);
    if (!array->items[0]) {
        free(array->items);
        free(array);
        return NULL;
    }
    return array;
}

/*
 * Keep the only initially supported facility intentionally small: diagnostic
 * logging to stderr. This does not initialize or emulate UIKit (or any other
 * Apple framework).
 */
static void log_unsupported_symbol(const char *symbol_name)
{
    static const char prefix[] =
        "[sword3-ios-shim] unsupported Apple symbol: ";
    static const char null_name[] = "(null)";
    char message[512];
    size_t position = 0;
    size_t available;
    size_t name_length;

    memcpy(message, prefix, sizeof(prefix) - 1);
    position = sizeof(prefix) - 1;
    if (symbol_name == NULL)
        symbol_name = null_name;

    available = sizeof(message) - position - 1;
    name_length = 0;
    while (name_length < available && symbol_name[name_length] != '\0')
        ++name_length;
    memcpy(message + position, symbol_name, name_length);
    position += name_length;
    message[position++] = '\n';

    /* One write keeps concurrent diagnostics auditable without stdio state. */
    (void)write(STDERR_FILENO, message, position);
}

void sword3_unsupported_symbol(const char *symbol_name)
{
    atomic_fetch_add_explicit(
        &unsupported_call_count, UINT64_C(1), memory_order_relaxed
    );
    log_unsupported_symbol(symbol_name);
    abort();
}

#ifdef SWORD3_SHIM_ENABLE_TEST_API
uint64_t sword3_unsupported_call_count(void)
{
    return atomic_load_explicit(&unsupported_call_count, memory_order_relaxed);
}
#endif
