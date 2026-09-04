#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "sword3_ios_shim.h"
#include "sword3_objc_shim.h"

#include <dirent.h>
#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

extern sword3_objc_id objc_msgSend(
    sword3_objc_id self,
    sword3_objc_sel selector,
    sword3_objc_id object
) __attribute__((weak));

static _Atomic uint64_t unsupported_call_count;

struct proxy_string {
    sword3_objc_Class isa;
    char *value;
};

struct proxy_data {
    sword3_objc_Class isa;
    unsigned char *bytes;
    size_t length;
};

struct proxy_array {
    sword3_objc_Class isa;
    sword3_objc_id *items;
    size_t count;
    size_t capacity;
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

struct proxy_color {
    sword3_objc_Class isa;
};

struct proxy_image {
    sword3_objc_Class isa;
};

struct proxy_uikit_object {
    sword3_objc_Class isa;
    uintptr_t pad[16];
};

struct proxy_av_audio_player {
    sword3_objc_Class isa;
    sword3_objc_id delegate;
    long loops;
    void *copy;
    size_t copy_len;
    int playing;
};

struct proxy_url {
    sword3_objc_Class isa;
    char *path;
};

struct proxy_notification_center {
    sword3_objc_Class isa;
};

struct proxy_display_link {
    sword3_objc_Class isa;
    sword3_objc_id target;
    sword3_objc_sel callback;
    int scheduled;
    int paused;
    int frame_interval;
    int preferred_fps;
};

struct nc_observer {
    sword3_objc_id observer;
    sword3_objc_sel selector;
    char *name;
};

struct proxy_number {
    sword3_objc_Class isa;
    long long ivalue;
    double dvalue;
    int is_float;
};

struct proxy_date {
    sword3_objc_Class isa;
    double unix_sec;
};

struct proxy_date_formatter {
    sword3_objc_Class isa;
    char format[80];
};

struct proxy_calendar {
    sword3_objc_Class isa;
};

struct proxy_date_components {
    sword3_objc_Class isa;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
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
    struct sword3_objc_method methods[20];
};

extern struct sword3_objc_class proxy_string_class
    __asm__("OBJC_CLASS_$_NSString");
static struct sword3_objc_class proxy_string_metaclass;
extern struct sword3_objc_class proxy_data_class
    __asm__("OBJC_CLASS_$_NSData");
static struct sword3_objc_class proxy_data_metaclass;
extern struct sword3_objc_class proxy_array_class
    __asm__("OBJC_CLASS_$_NSArray");
static struct sword3_objc_class proxy_array_metaclass;
extern struct sword3_objc_class proxy_mutable_array_class
    __asm__("OBJC_CLASS_$_NSMutableArray");
static struct sword3_objc_class proxy_mutable_array_metaclass;
extern struct sword3_objc_class proxy_number_class
    __asm__("OBJC_CLASS_$_NSNumber");
static struct sword3_objc_class proxy_number_metaclass;
extern struct sword3_objc_class proxy_date_class
    __asm__("OBJC_CLASS_$_NSDate");
static struct sword3_objc_class proxy_date_metaclass;
extern struct sword3_objc_class proxy_date_formatter_class
    __asm__("OBJC_CLASS_$_NSDateFormatter");
static struct sword3_objc_class proxy_date_formatter_metaclass;
extern struct sword3_objc_class proxy_calendar_class
    __asm__("OBJC_CLASS_$_NSCalendar");
static struct sword3_objc_class proxy_calendar_metaclass;
static struct sword3_objc_class proxy_date_components_class;
static struct proxy_calendar current_calendar;
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
extern struct sword3_objc_class proxy_color_class
    __asm__("OBJC_CLASS_$_UIColor");
static struct sword3_objc_class proxy_color_metaclass;
static struct proxy_color clear_color;
extern struct sword3_objc_class proxy_image_class
    __asm__("OBJC_CLASS_$_UIImage");
static struct sword3_objc_class proxy_image_metaclass;
static struct proxy_image named_image;
extern struct sword3_objc_class proxy_responder_class
    __asm__("OBJC_CLASS_$_UIResponder");
extern struct sword3_objc_class proxy_responder_metaclass
    __asm__("OBJC_METACLASS_$_UIResponder");
extern struct sword3_objc_class proxy_view_class
    __asm__("OBJC_CLASS_$_UIView");
extern struct sword3_objc_class proxy_view_metaclass
    __asm__("OBJC_METACLASS_$_UIView");
extern struct sword3_objc_class proxy_window_class
    __asm__("OBJC_CLASS_$_UIWindow");
extern struct sword3_objc_class proxy_window_metaclass
    __asm__("OBJC_METACLASS_$_UIWindow");
extern struct sword3_objc_class proxy_view_controller_class
    __asm__("OBJC_CLASS_$_UIViewController");
extern struct sword3_objc_class proxy_view_controller_metaclass
    __asm__("OBJC_METACLASS_$_UIViewController");
extern struct sword3_objc_class proxy_image_view_class
    __asm__("OBJC_CLASS_$_UIImageView");
static struct sword3_objc_class proxy_image_view_metaclass;
extern struct sword3_objc_class proxy_exception_class
    __asm__("OBJC_CLASS_$_NSException");
static struct sword3_objc_class proxy_exception_metaclass;
extern struct sword3_objc_class proxy_url_class
    __asm__("OBJC_CLASS_$_NSURL");
static struct sword3_objc_class proxy_url_metaclass;
extern struct sword3_objc_class proxy_notification_center_class
    __asm__("OBJC_CLASS_$_NSNotificationCenter");
static struct sword3_objc_class proxy_notification_center_metaclass;
static struct proxy_notification_center default_notification_center;
extern struct sword3_objc_class proxy_av_player_class
    __asm__("OBJC_CLASS_$_AVPlayer");
static struct sword3_objc_class proxy_av_player_metaclass;
extern struct sword3_objc_class proxy_av_player_layer_class
    __asm__("OBJC_CLASS_$_AVPlayerLayer");
static struct sword3_objc_class proxy_av_player_layer_metaclass;
extern struct sword3_objc_class proxy_av_audio_player_class
    __asm__("OBJC_CLASS_$_AVAudioPlayer");
static struct sword3_objc_class proxy_av_audio_player_metaclass;
extern struct sword3_objc_class proxy_display_link_class
    __asm__("OBJC_CLASS_$_CADisplayLink");
static struct sword3_objc_class proxy_display_link_metaclass;
extern struct sword3_objc_class proxy_runloop_class
    __asm__("OBJC_CLASS_$_NSRunLoop");
static struct sword3_objc_class proxy_runloop_metaclass;
static struct proxy_uikit_object current_runloop;
static struct nc_observer notification_observers[32];
static size_t notification_observer_count;

#define DISPLAY_LINK_CAP 8
static pthread_mutex_t display_link_lock = PTHREAD_MUTEX_INITIALIZER;
static struct proxy_display_link *display_links[DISPLAY_LINK_CAP];
static size_t display_link_count;
static _Atomic int display_link_reentrant;
static unsigned char cf_runloop_token[64];
static struct constant_string_layout cf_runloop_default_mode_string;
static struct constant_string_layout nsfile_modification_date_string = {
    .flags = 0x07c8u,
    .bytes = "NSFileModificationDate",
    .length = 22,
};

static struct proxy_array *make_proxy_array(size_t count);
static sword3_objc_id proxy_array_with_array(sword3_objc_id cls,
                                             sword3_objc_sel selector,
                                             struct proxy_array *other);
static const char *path_from_object(sword3_objc_id object);
static int mkdir_parents(const char *path);

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
    array->capacity = count;
    return array;
}

static struct proxy_array *make_mutable_array(size_t count)
{
    struct proxy_array *array = make_proxy_array(count);
    if (array)
        array->isa = &proxy_mutable_array_class;
    return array;
}

static sword3_objc_id proxy_array_last(struct proxy_array *self,
                                       sword3_objc_sel selector)
{
    (void)selector;
    return self && self->count ? self->items[self->count - 1] : NULL;
}

static void proxy_array_add(struct proxy_array *self,
                            sword3_objc_sel selector,
                            sword3_objc_id object)
{
    (void)selector;
    if (!self || !object)
        return;
    if (self->count >= self->capacity) {
        size_t cap = self->capacity ? self->capacity * 2 : 4;
        sword3_objc_id *items;

        if (cap < self->count + 1)
            cap = self->count + 1;
        items = realloc(self->items, cap * sizeof(*items));
        if (!items)
            return;
        self->items = items;
        self->capacity = cap;
    }
    self->items[self->count++] = object;
}

static void proxy_array_add_from(struct proxy_array *self,
                                 sword3_objc_sel selector,
                                 struct proxy_array *other)
{
    size_t i;
    (void)selector;
    if (!self || !other)
        return;
    for (i = 0; i < other->count; i++)
        proxy_array_add(self, selector, other->items[i]);
}

static sword3_objc_id proxy_array_copy(struct proxy_array *self,
                                       sword3_objc_sel selector)
{
    return proxy_array_with_array((sword3_objc_id)&proxy_array_class,
                                  selector, self);
}

static sword3_objc_id proxy_array_mutable_copy(struct proxy_array *self,
                                               sword3_objc_sel selector)
{
    return proxy_array_with_array((sword3_objc_id)&proxy_mutable_array_class,
                                  selector, self);
}

static int proxy_array_contains(struct proxy_array *self,
                                sword3_objc_sel selector,
                                sword3_objc_id object)
{
    size_t i;
    (void)selector;
    if (!self)
        return 0;
    for (i = 0; i < self->count; i++) {
        if (self->items[i] == object)
            return 1;
    }
    return 0;
}

static sword3_objc_id proxy_array_init_capacity(struct proxy_array *self,
                                                sword3_objc_sel selector,
                                                uintptr_t capacity)
{
    (void)selector;
    if (!self)
        return NULL;
    if (capacity > self->capacity) {
        sword3_objc_id *items = realloc(self->items, capacity * sizeof(*items));
        if (!items)
            return self;
        self->items = items;
        self->capacity = capacity;
    }
    return self;
}

static sword3_objc_id proxy_mutable_array_with_capacity(
    sword3_objc_id cls,
    sword3_objc_sel selector,
    uintptr_t capacity
)
{
    (void)cls;
    return proxy_array_init_capacity(make_mutable_array(0), selector, capacity);
}

static void proxy_array_remove_all(struct proxy_array *self,
                                   sword3_objc_sel selector)
{
    (void)selector;
    if (self)
        self->count = 0;
}

static void proxy_array_remove_last(struct proxy_array *self,
                                    sword3_objc_sel selector)
{
    (void)selector;
    if (self && self->count)
        self->count--;
}

static sword3_objc_id proxy_array_init_with_array(
    struct proxy_array *self,
    sword3_objc_sel selector,
    struct proxy_array *other
)
{
    size_t count = other ? other->count : 0;
    (void)selector;
    if (!self)
        return NULL;
    if (!count)
        return self;
    if (count > self->capacity) {
        sword3_objc_id *items = realloc(self->items, count * sizeof(*items));
        if (!items)
            return self;
        self->items = items;
        self->capacity = count;
    }
    memcpy(self->items, other->items, count * sizeof(*self->items));
    self->count = count;
    return self;
}

static sword3_objc_id proxy_array_empty(sword3_objc_id cls,
                                        sword3_objc_sel selector)
{
    (void)selector;
    if (cls == (sword3_objc_id)&proxy_mutable_array_class)
        return make_mutable_array(0);
    return make_proxy_array(0);
}

static sword3_objc_id proxy_array_with_array(sword3_objc_id cls,
                                             sword3_objc_sel selector,
                                             struct proxy_array *other)
{
    struct proxy_array *array;
    size_t count = other ? other->count : 0;
    (void)selector;
    array = (cls == (sword3_objc_id)&proxy_mutable_array_class)
                ? make_mutable_array(count)
                : make_proxy_array(count);
    if (!array || !count)
        return array;
    memcpy(array->items, other->items, count * sizeof(*array->items));
    return array;
}

static sword3_objc_id proxy_array_with_objects_count(
    sword3_objc_id cls,
    sword3_objc_sel selector,
    sword3_objc_id *objects,
    uintptr_t count
)
{
    struct proxy_array *array;
    (void)selector;
    array = (cls == (sword3_objc_id)&proxy_mutable_array_class)
                ? make_mutable_array(count)
                : make_proxy_array(count);
    if (!array || !count)
        return array;
    if (objects)
        memcpy(array->items, objects, count * sizeof(*array->items));
    return array;
}

__attribute__((used, visibility("hidden")))
sword3_objc_id sword3_array_with_objects_impl(
    sword3_objc_id cls,
    sword3_objc_sel selector,
    sword3_objc_id first,
    const uintptr_t *apple_stack
)
{
    sword3_objc_id objects[64];
    size_t count = 0;

    if (first != NULL) {
        objects[count++] = first;
        if (apple_stack) {
            while (count < 64) {
                sword3_objc_id object = (sword3_objc_id)apple_stack[count - 1];
                if (object == NULL)
                    break;
                objects[count++] = object;
            }
        }
    }
    return proxy_array_with_objects_count(cls, selector, objects, count);
}

#if defined(__aarch64__)
void proxy_array_with_objects(void);
#else
static sword3_objc_id proxy_array_with_objects(sword3_objc_id cls,
                                                sword3_objc_sel selector,
                                                sword3_objc_id first)
{
    return sword3_array_with_objects_impl(cls, selector, first, NULL);
}
#endif

static struct proxy_number *make_proxy_number_int(long long value)
{
    struct proxy_number *number = calloc(1, sizeof(*number));
    if (!number)
        return NULL;
    number->isa = &proxy_number_class;
    number->ivalue = value;
    number->dvalue = (double)value;
    return number;
}

static struct proxy_number *make_proxy_number_float(double value)
{
    struct proxy_number *number = calloc(1, sizeof(*number));
    if (!number)
        return NULL;
    number->isa = &proxy_number_class;
    number->is_float = 1;
    number->dvalue = value;
    number->ivalue = (long long)value;
    return number;
}

static sword3_objc_id proxy_number_with_bool(sword3_objc_id cls,
                                              sword3_objc_sel selector,
                                              int value)
{
    (void)cls;
    (void)selector;
    return make_proxy_number_int(value ? 1 : 0);
}

static sword3_objc_id proxy_number_with_long(sword3_objc_id cls,
                                              sword3_objc_sel selector,
                                              long long value)
{
    (void)cls;
    (void)selector;
    return make_proxy_number_int(value);
}

static sword3_objc_id proxy_number_with_double(sword3_objc_id cls,
                                                sword3_objc_sel selector,
                                                double value)
{
    (void)cls;
    (void)selector;
    return make_proxy_number_float(value);
}

static sword3_objc_id proxy_number_with_float(sword3_objc_id cls,
                                               sword3_objc_sel selector,
                                               float value)
{
    (void)cls;
    (void)selector;
    return make_proxy_number_float(value);
}

static sword3_objc_id proxy_number_string_value(struct proxy_number *self,
                                                sword3_objc_sel selector)
{
    char text[64];
    (void)selector;
    if (!self)
        return make_proxy_string("0");
    if (self->is_float)
        snprintf(text, sizeof(text), "%g", self->dvalue);
    else
        snprintf(text, sizeof(text), "%lld", self->ivalue);
    return make_proxy_string(text);
}

static int proxy_number_bool(struct proxy_number *self,
                             sword3_objc_sel selector)
{
    (void)selector;
    return self && self->ivalue != 0;
}

static int proxy_number_int(struct proxy_number *self,
                            sword3_objc_sel selector)
{
    (void)selector;
    return self ? (int)self->ivalue : 0;
}

static long long proxy_number_long(struct proxy_number *self,
                                   sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->ivalue : 0;
}

static double proxy_number_double(struct proxy_number *self,
                                  sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->dvalue : 0.0;
}

static float proxy_number_float(struct proxy_number *self,
                                sword3_objc_sel selector)
{
    (void)selector;
    return self ? (float)self->dvalue : 0.0f;
}

static double unix_now(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
        return (double)time(NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void date_local_tm(double unix_sec, struct tm *out)
{
    time_t stamp = (time_t)unix_sec;

    memset(out, 0, sizeof(*out));
    if (!localtime_r(&stamp, out)) {
        out->tm_year = 70;
        out->tm_mday = 1;
    }
}

static struct proxy_date *make_proxy_date(double unix_sec)
{
    struct proxy_date *stamp = calloc(1, sizeof(*stamp));

    if (!stamp)
        return NULL;
    stamp->isa = &proxy_date_class;
    stamp->unix_sec = unix_sec;
    return stamp;
}

static sword3_objc_id proxy_date_now(sword3_objc_id cls,
                                     sword3_objc_sel selector)
{
    static int logged;

    (void)cls;
    (void)selector;
    if (!logged) {
        logged = 1;
        fprintf(stderr, "[sword3-ios-shim] NSDate date\n");
    }
    return make_proxy_date(unix_now());
}

static sword3_objc_id proxy_date_distant_future(sword3_objc_id cls,
                                                sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    return make_proxy_date(64060588800.0);
}

static sword3_objc_id proxy_date_with_unix(sword3_objc_id cls,
                                           sword3_objc_sel selector,
                                           double unix_sec)
{
    (void)cls;
    (void)selector;
    return make_proxy_date(unix_sec);
}

static sword3_objc_id proxy_date_with_interval_since_now(
    sword3_objc_id cls,
    sword3_objc_sel selector,
    double interval
)
{
    (void)cls;
    (void)selector;
    return make_proxy_date(unix_now() + interval);
}

static double proxy_date_unix(struct proxy_date *self,
                              sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->unix_sec : 0.0;
}

static double proxy_date_since_now(struct proxy_date *self,
                                   sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->unix_sec - unix_now() : 0.0;
}

static sword3_objc_id proxy_date_description(struct proxy_date *self,
                                             sword3_objc_sel selector)
{
    struct tm local;
    char text[64];

    (void)selector;
    date_local_tm(self ? self->unix_sec : 0.0, &local);
    if (strftime(text, sizeof(text), "%Y/%m/%d %H:%M:%S", &local) == 0)
        snprintf(text, sizeof(text), "%.0f", self ? self->unix_sec : 0.0);
    return make_proxy_string(text);
}

static sword3_objc_id proxy_date_copy(struct proxy_date *self,
                                      sword3_objc_sel selector,
                                      void *zone)
{
    (void)selector;
    (void)zone;
    return make_proxy_date(self ? self->unix_sec : 0.0);
}

static void unicode_format_to_strftime(const char *src, char *dst, size_t cap)
{
    size_t out = 0;

    if (!src || !src[0])
        src = "yyyy/MM/dd HH:mm";
    while (*src && out + 3 < cap) {
        if (strncmp(src, "yyyy", 4) == 0) {
            dst[out++] = '%';
            dst[out++] = 'Y';
            src += 4;
            continue;
        }
        if (strncmp(src, "yy", 2) == 0) {
            dst[out++] = '%';
            dst[out++] = 'y';
            src += 2;
            continue;
        }
        if (strncmp(src, "MM", 2) == 0) {
            dst[out++] = '%';
            dst[out++] = 'm';
            src += 2;
            continue;
        }
        if (strncmp(src, "dd", 2) == 0) {
            dst[out++] = '%';
            dst[out++] = 'd';
            src += 2;
            continue;
        }
        if (strncmp(src, "HH", 2) == 0) {
            dst[out++] = '%';
            dst[out++] = 'H';
            src += 2;
            continue;
        }
        if (strncmp(src, "mm", 2) == 0) {
            dst[out++] = '%';
            dst[out++] = 'M';
            src += 2;
            continue;
        }
        if (strncmp(src, "ss", 2) == 0) {
            dst[out++] = '%';
            dst[out++] = 'S';
            src += 2;
            continue;
        }
        dst[out++] = *src++;
    }
    dst[out] = '\0';
}

static void proxy_formatter_set_format(
    struct proxy_date_formatter *self,
    sword3_objc_sel selector,
    sword3_objc_id format
)
{
    const char *text = object_cstring(format);

    (void)selector;
    if (!self)
        return;
    snprintf(self->format, sizeof(self->format), "%s", text ? text : "");
}

static sword3_objc_id proxy_formatter_string_from_date(
    struct proxy_date_formatter *self,
    sword3_objc_sel selector,
    struct proxy_date *stamp
)
{
    struct tm local;
    char spec[96];
    char text[64];

    (void)selector;
    date_local_tm(stamp ? stamp->unix_sec : 0.0, &local);
    unicode_format_to_strftime(self ? self->format : NULL, spec, sizeof(spec));
    if (strftime(text, sizeof(text), spec, &local) == 0)
        snprintf(text, sizeof(text), "%.0f", stamp ? stamp->unix_sec : 0.0);
    return make_proxy_string(text);
}

static sword3_objc_id proxy_calendar_current(sword3_objc_id cls,
                                             sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    current_calendar.isa = &proxy_calendar_class;
    return &current_calendar;
}

static sword3_objc_id proxy_calendar_components(
    struct proxy_calendar *self,
    sword3_objc_sel selector,
    uintptr_t unit_flags,
    struct proxy_date *stamp
)
{
    struct proxy_date_components *parts;
    struct tm local;

    (void)self;
    (void)selector;
    (void)unit_flags;
    parts = calloc(1, sizeof(*parts));
    if (!parts)
        return NULL;
    date_local_tm(stamp ? stamp->unix_sec : unix_now(), &local);
    parts->isa = &proxy_date_components_class;
    parts->year = local.tm_year + 1900;
    parts->month = local.tm_mon + 1;
    parts->day = local.tm_mday;
    parts->hour = local.tm_hour;
    parts->minute = local.tm_min;
    parts->second = local.tm_sec;
    parts->weekday = local.tm_wday + 1;
    return parts;
}

static long long proxy_components_year(struct proxy_date_components *self,
                                        sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->year : 0;
}

static long long proxy_components_month(struct proxy_date_components *self,
                                         sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->month : 0;
}

static long long proxy_components_day(struct proxy_date_components *self,
                                       sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->day : 0;
}

static long long proxy_components_hour(struct proxy_date_components *self,
                                        sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->hour : 0;
}

static long long proxy_components_minute(struct proxy_date_components *self,
                                          sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->minute : 0;
}

static long long proxy_components_second(struct proxy_date_components *self,
                                          sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->second : 0;
}

static long long proxy_components_weekday(struct proxy_date_components *self,
                                           sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->weekday : 0;
}

static sword3_objc_id proxy_string_append_string(
    struct proxy_string *self,
    sword3_objc_sel selector,
    sword3_objc_id other
)
{
    const char *left = object_cstring(self);
    const char *right = object_cstring(other);
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    char *joined;
    struct proxy_string *result;
    (void)selector;

    joined = malloc(left_length + right_length + 1);
    if (!joined)
        return NULL;
    memcpy(joined, left, left_length);
    memcpy(joined + left_length, right, right_length + 1);
    result = make_proxy_string(joined);
    free(joined);
    return result;
}

static sword3_objc_id proxy_string_with_format(sword3_objc_id cls,
                                                sword3_objc_sel selector,
                                                sword3_objc_id format)
{
    (void)cls;
    (void)selector;
    return make_proxy_string(object_cstring(format));
}

static sword3_objc_id proxy_string_with_cstring(sword3_objc_id cls,
                                                 sword3_objc_sel selector,
                                                 const char *utf8,
                                                 uintptr_t encoding)
{
    (void)cls;
    (void)selector;
    (void)encoding;
    return make_proxy_string(utf8);
}

static sword3_objc_id proxy_string_init_cstring(struct proxy_string *self,
                                                 sword3_objc_sel selector,
                                                 const char *utf8,
                                                 uintptr_t encoding)
{
    (void)selector;
    (void)encoding;
    if (!self)
        return make_proxy_string(utf8);
    free(self->value);
    self->value = strdup(utf8 ? utf8 : "");
    if (!self->value)
        return NULL;
    self->isa = &proxy_string_class;
    return self;
}

static sword3_objc_id proxy_string_init_utf8(struct proxy_string *self,
                                              sword3_objc_sel selector,
                                              const char *utf8)
{
    return proxy_string_init_cstring(self, selector, utf8, 4);
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

static sword3_objc_id proxy_return_self(sword3_objc_id self,
                                        sword3_objc_sel selector)
{
    (void)selector;
    return self;
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

static sword3_objc_id proxy_color_named(sword3_objc_id cls,
                                        sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    clear_color.isa = &proxy_color_class;
    return &clear_color;
}

static sword3_objc_id proxy_color_rgba(sword3_objc_id cls,
                                       sword3_objc_sel selector,
                                       double red, double green, double blue,
                                       double alpha)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    return proxy_color_named(cls, selector);
}

static sword3_objc_id proxy_color_white(sword3_objc_id cls,
                                        sword3_objc_sel selector,
                                        double white, double alpha)
{
    (void)white;
    (void)alpha;
    return proxy_color_named(cls, selector);
}

static sword3_objc_id proxy_image_named(sword3_objc_id cls,
                                        sword3_objc_sel selector,
                                        sword3_objc_id name)
{
    (void)cls;
    (void)selector;
    (void)name;
    named_image.isa = &proxy_image_class;
    return &named_image;
}

static sword3_objc_id make_uikit_object(struct sword3_objc_class *cls)
{
    struct proxy_uikit_object *object = calloc(1, sizeof(*object));
    if (object)
        object->isa = cls;
    return object;
}

static void ensure_cf_constants(void)
{
    if (cf_runloop_default_mode_string.bytes)
        return;
    cf_runloop_default_mode_string.isa = &proxy_string_class;
    cf_runloop_default_mode_string.flags = 0x07c8u;
    cf_runloop_default_mode_string.bytes = "kCFRunLoopDefaultMode";
    cf_runloop_default_mode_string.length = 21;
    nsfile_modification_date_string.isa = &proxy_string_class;
}

static void register_display_link(struct proxy_display_link *link)
{
    pthread_mutex_lock(&display_link_lock);
    if (display_link_count < DISPLAY_LINK_CAP)
        display_links[display_link_count++] = link;
    pthread_mutex_unlock(&display_link_lock);
}

static sword3_objc_id proxy_display_link_with_target(
    sword3_objc_id cls,
    sword3_objc_sel selector,
    sword3_objc_id target,
    sword3_objc_sel callback
)
{
    struct proxy_display_link *link;
    static unsigned seen;
    (void)cls;
    (void)selector;

    link = calloc(1, sizeof(*link));
    if (!link)
        return NULL;
    link->isa = &proxy_display_link_class;
    link->target = target;
    link->callback = callback;
    link->frame_interval = 1;
    link->preferred_fps = 60;
    register_display_link(link);
    if (seen < 8) {
        seen++;
        fprintf(stderr,
            "[sword3-ios-shim] CADisplayLink target=%p selector=%s\n",
            target, callback ? callback : "(null)");
    }
    return link;
}

static void proxy_display_link_add(
    struct proxy_display_link *self,
    sword3_objc_sel selector,
    sword3_objc_id runloop,
    sword3_objc_id mode
)
{
    (void)selector;
    (void)runloop;
    (void)mode;
    if (self)
        self->scheduled = 1;
}

static void proxy_display_link_remove(
    struct proxy_display_link *self,
    sword3_objc_sel selector,
    sword3_objc_id runloop,
    sword3_objc_id mode
)
{
    (void)selector;
    (void)runloop;
    (void)mode;
    if (self)
        self->scheduled = 0;
}

static void proxy_display_link_invalidate(
    struct proxy_display_link *self,
    sword3_objc_sel selector
)
{
    (void)selector;
    if (!self)
        return;
    self->scheduled = 0;
    self->paused = 1;
    self->target = NULL;
    self->callback = NULL;
}

static void proxy_display_link_set_paused(
    struct proxy_display_link *self,
    sword3_objc_sel selector,
    int paused
)
{
    (void)selector;
    if (self)
        self->paused = paused ? 1 : 0;
}

static int proxy_display_link_is_paused(
    struct proxy_display_link *self,
    sword3_objc_sel selector
)
{
    (void)selector;
    return self && self->paused;
}

static void proxy_display_link_set_interval(
    struct proxy_display_link *self,
    sword3_objc_sel selector,
    intptr_t interval
)
{
    (void)selector;
    if (self)
        self->frame_interval = interval > 0 ? (int)interval : 1;
}

static void proxy_display_link_set_fps(
    struct proxy_display_link *self,
    sword3_objc_sel selector,
    intptr_t fps
)
{
    (void)selector;
    if (self)
        self->preferred_fps = fps > 0 ? (int)fps : 60;
}

static sword3_objc_id proxy_runloop_current(
    sword3_objc_id cls,
    sword3_objc_sel selector
)
{
    (void)cls;
    (void)selector;
    current_runloop.isa = &proxy_runloop_class;
    return &current_runloop;
}

static int proxy_runloop_run_mode(
    sword3_objc_id self,
    sword3_objc_sel selector,
    sword3_objc_id mode,
    sword3_objc_id limit_date
)
{
    (void)self;
    (void)selector;
    (void)mode;
    (void)limit_date;
    sword3_ios_tick_display_links();
    return 1;
}

SWORD3_EXPORT void sword3_ios_tick_display_links(void)
{
    struct proxy_display_link *snapshot[DISPLAY_LINK_CAP];
    size_t n;
    size_t i;
    struct timespec ts;
    static uint64_t last_ms;
    uint64_t now;
    static unsigned fired;

    ensure_cf_constants();
    if (atomic_exchange(&display_link_reentrant, 1))
        return;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
    if (last_ms != 0 && now - last_ms < 16ull) {
        atomic_store(&display_link_reentrant, 0);
        return;
    }
    last_ms = now;

    pthread_mutex_lock(&display_link_lock);
    n = display_link_count;
    memcpy(snapshot, display_links, n * sizeof(*snapshot));
    pthread_mutex_unlock(&display_link_lock);

    for (i = 0; i < n; i++) {
        struct proxy_display_link *link = snapshot[i];
        if (!link || !link->scheduled || link->paused)
            continue;
        if (!link->target || !link->callback)
            continue;
        if (fired < 8) {
            fired++;
            fprintf(stderr,
                "[sword3-ios-shim] CADisplayLink fire #%u selector=%s\n",
                fired, link->callback);
        }
        {
            static void (*msgsend)(sword3_objc_id, sword3_objc_sel, sword3_objc_id);
            static int msgsend_resolved;
            if (!msgsend_resolved) {
                msgsend_resolved = 1;
                msgsend = (void (*)(sword3_objc_id, sword3_objc_sel,
                                    sword3_objc_id))
                    dlsym(RTLD_DEFAULT, "objc_msgSend");
            }
            if (msgsend)
                msgsend(link->target, link->callback, link);
        }
    }
    atomic_store(&display_link_reentrant, 0);
}

SWORD3_EXPORT void *CFRunLoopGetCurrent(void)
{
    ensure_cf_constants();
    return cf_runloop_token;
}

SWORD3_EXPORT int32_t CFRunLoopRunInMode(
    const void *mode,
    double seconds,
    unsigned char return_after_source
)
{
    struct timespec ts;
    double wait = seconds;
    (void)mode;
    (void)return_after_source;

    ensure_cf_constants();
    sword3_ios_tick_display_links();
    if (wait < 0.0)
        wait = 0.0;
    if (wait > 0.016)
        wait = 0.016;
    if (wait > 0.0) {
        ts.tv_sec = 0;
        ts.tv_nsec = (long)(wait * 1000000000.0);
        nanosleep(&ts, NULL);
    }
    return 3; /* kCFRunLoopRunTimedOut */
}

static sword3_objc_id proxy_url_from_cstring(const char *path)
{
    struct proxy_url *url = calloc(1, sizeof(*url));
    if (!url)
        return NULL;
    url->isa = &proxy_url_class;
    url->path = strdup(path ? path : "");
    return url;
}

static sword3_objc_id proxy_url_with_path(sword3_objc_id cls,
                                          sword3_objc_sel selector,
                                          sword3_objc_id path_object)
{
    (void)cls;
    (void)selector;
    return proxy_url_from_cstring(object_cstring(path_object));
}

static sword3_objc_id proxy_url_with_path_dir(sword3_objc_id cls,
                                              sword3_objc_sel selector,
                                              sword3_objc_id path_object,
                                              int is_directory)
{
    (void)is_directory;
    return proxy_url_with_path(cls, selector, path_object);
}

static sword3_objc_id proxy_url_init_path(struct proxy_url *self,
                                          sword3_objc_sel selector,
                                          sword3_objc_id path_object)
{
    (void)selector;
    if (!self)
        return NULL;
    free(self->path);
    self->path = strdup(object_cstring(path_object));
    return self;
}

static sword3_objc_id proxy_url_init_path_dir(struct proxy_url *self,
                                              sword3_objc_sel selector,
                                              sword3_objc_id path_object,
                                              int is_directory)
{
    (void)is_directory;
    return proxy_url_init_path(self, selector, path_object);
}

static const char *url_path_text(struct proxy_url *self)
{
    return self && self->path ? self->path : "";
}

static sword3_objc_id proxy_url_path(struct proxy_url *self,
                                     sword3_objc_sel selector)
{
    (void)selector;
    return make_proxy_string(url_path_text(self));
}

static sword3_objc_id proxy_url_absolute_string(struct proxy_url *self,
                                                sword3_objc_sel selector)
{
    char text[PATH_MAX + 8];
    const char *path = url_path_text(self);
    (void)selector;
    if (!strncmp(path, "file:", 5) || !strncmp(path, "http", 4))
        return make_proxy_string(path);
    snprintf(text, sizeof(text), "file://%s", path);
    return make_proxy_string(text);
}

static sword3_objc_id proxy_url_last_component(struct proxy_url *self,
                                               sword3_objc_sel selector)
{
    const char *path = url_path_text(self);
    const char *slash = strrchr(path, '/');
    (void)selector;
    return make_proxy_string(slash ? slash + 1 : path);
}

static sword3_objc_id proxy_url_append_component(
    struct proxy_url *self,
    sword3_objc_sel selector,
    sword3_objc_id component
)
{
    char text[PATH_MAX];
    const char *path = url_path_text(self);
    const char *piece = object_cstring(component);
    size_t path_length = strlen(path);
    (void)selector;
    if (path_length && path[path_length - 1] == '/')
        snprintf(text, sizeof(text), "%s%s", path, piece);
    else
        snprintf(text, sizeof(text), "%s/%s", path, piece);
    return proxy_url_from_cstring(text);
}

static void dispatch_notification(const char *name, sword3_objc_id object)
{
    static void (*msgsend)(sword3_objc_id, sword3_objc_sel, sword3_objc_id);
    static int msgsend_resolved;
    size_t i;
    (void)object;
    if (!msgsend_resolved) {
        msgsend_resolved = 1;
        msgsend = (void (*)(sword3_objc_id, sword3_objc_sel, sword3_objc_id))
            dlsym(RTLD_DEFAULT, "objc_msgSend");
    }
    for (i = 0; i < notification_observer_count; i++) {
        struct nc_observer *entry = &notification_observers[i];
        if (!entry->observer || !entry->selector)
            continue;
        if (entry->name && entry->name[0] && name && strcmp(entry->name, name) != 0)
            continue;
        if (msgsend)
            msgsend(entry->observer, entry->selector, NULL);
    }
}

static sword3_objc_id proxy_nc_default(sword3_objc_id cls,
                                       sword3_objc_sel selector)
{
    (void)cls;
    (void)selector;
    default_notification_center.isa = &proxy_notification_center_class;
    return &default_notification_center;
}

static void proxy_nc_add(struct proxy_notification_center *self,
                         sword3_objc_sel selector,
                         sword3_objc_id observer,
                         sword3_objc_sel callback,
                         sword3_objc_id name_object,
                         sword3_objc_id object)
{
    const char *name = object_cstring(name_object);
    (void)self;
    (void)selector;
    (void)object;
    if (!observer || notification_observer_count >=
            sizeof(notification_observers) / sizeof(notification_observers[0]))
        return;
    notification_observers[notification_observer_count].observer = observer;
    notification_observers[notification_observer_count].selector = callback;
    notification_observers[notification_observer_count].name =
        name && name[0] ? strdup(name) : NULL;
    notification_observer_count++;
    /*
     * Intro movies cannot decode here. Treat AVPlayer completion observers
     * as already finished so startup is not blocked on a black video layer.
     */
    if (name && strstr(name, "AVPlayerItemDidPlayToEndTime"))
        dispatch_notification(name, observer);
}

static void proxy_nc_remove(struct proxy_notification_center *self,
                            sword3_objc_sel selector,
                            sword3_objc_id observer)
{
    size_t i;
    (void)self;
    (void)selector;
    for (i = 0; i < notification_observer_count; i++) {
        if (notification_observers[i].observer == observer)
            notification_observers[i].observer = NULL;
    }
}

static void proxy_nc_post(struct proxy_notification_center *self,
                          sword3_objc_sel selector,
                          sword3_objc_id name_object,
                          sword3_objc_id object)
{
    (void)self;
    (void)selector;
    dispatch_notification(object_cstring(name_object), object);
}

static sword3_objc_id proxy_av_player_with_url(sword3_objc_id cls,
                                               sword3_objc_sel selector,
                                               sword3_objc_id url)
{
    (void)cls;
    (void)selector;
    (void)url;
    return make_uikit_object(&proxy_av_player_class);
}

static sword3_objc_id proxy_av_init_url(sword3_objc_id self,
                                        sword3_objc_sel selector,
                                        sword3_objc_id url)
{
    (void)selector;
    (void)url;
    return self;
}

static void proxy_av_play(sword3_objc_id self, sword3_objc_sel selector)
{
    (void)selector;
    dispatch_notification("AVPlayerItemDidPlayToEndTimeNotification", self);
}

static int copy_nsdata_bytes(sword3_objc_id object, void **out, size_t *out_len)
{
    static sword3_objc_id (*msgsend)(sword3_objc_id, sword3_objc_sel, ...);
    static sword3_objc_sel (*regsel)(const char *);
    static int resolved;
    const void *bytes = NULL;
    unsigned long length = 0;
    const uintptr_t *words;
    void *copy;

    if (!object || !out || !out_len)
        return -1;
    if (!resolved) {
        resolved = 1;
        msgsend = (sword3_objc_id (*)(sword3_objc_id, sword3_objc_sel, ...))
            dlsym(RTLD_DEFAULT, "objc_msgSend");
        regsel = (sword3_objc_sel (*)(const char *))
            dlsym(RTLD_DEFAULT, "sel_registerName");
    }
    if (msgsend && regsel) {
        bytes = msgsend(object, regsel("bytes"));
        length = (unsigned long)(uintptr_t)msgsend(object, regsel("length"));
    }
    if ((!bytes || length == 0) && object) {
        words = object;
        /*
         * Common CFData / NSCFData layout on arm64: isa, info, length, bytes.
         * Only accept a heap-looking pointer and a bounded length.
         */
        if (words[2] > 0 && words[2] < 32u * 1024u * 1024u && words[3] > 0x1000) {
            length = (unsigned long)words[2];
            bytes = (const void *)words[3];
        }
    }
    if (!bytes || length == 0)
        return -1;
    copy = malloc(length);
    if (!copy)
        return -1;
    memcpy(copy, bytes, length);
    *out = copy;
    *out_len = (size_t)length;
    return 0;
}

static int copy_url_file_bytes(sword3_objc_id url, void **out, size_t *out_len)
{
    const char *path = NULL;
    const struct proxy_url *proxy = url;
    FILE *fp;
    long sz;
    void *copy;

    if (!url)
        return -1;
    if (proxy->isa == &proxy_url_class)
        path = proxy->path;
    else
        path = object_cstring(url);
    if (path && strncmp(path, "file://", 7) == 0)
        path += 7;
    if (!path || !path[0])
        return -1;
    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz <= 0 || sz > 32L * 1024L * 1024L) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    copy = malloc((size_t)sz);
    if (!copy) {
        fclose(fp);
        return -1;
    }
    if (fread(copy, 1, (size_t)sz, fp) != (size_t)sz) {
        free(copy);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *out = copy;
    *out_len = (size_t)sz;
    return 0;
}

static int host_play_bytes(const void *data, size_t size, int loops)
{
    static int (*play)(const void *, size_t, int);
    static int resolved;

    if (!resolved) {
        resolved = 1;
        play = (int (*)(const void *, size_t, int))
            dlsym(RTLD_DEFAULT, "sword3_host_play_memory_audio");
    }
    if (!play)
        return -1;
    return play(data, size, loops);
}

static void host_stop_bytes(void)
{
    static void (*stop)(void);
    static int resolved;

    if (!resolved) {
        resolved = 1;
        stop = (void (*)(void))dlsym(RTLD_DEFAULT, "sword3_host_stop_memory_audio");
    }
    if (stop)
        stop();
}

static sword3_objc_id proxy_av_audio_init_data(sword3_objc_id self,
                                                sword3_objc_sel selector,
                                                sword3_objc_id data,
                                                sword3_objc_id error)
{
    struct proxy_av_audio_player *player = self;
    const char *sel_name = selector ? (const char *)selector : "?";

    (void)error;
    if (!player)
        return NULL;
    free(player->copy);
    player->copy = NULL;
    player->copy_len = 0;
    player->playing = 0;
    if (copy_nsdata_bytes(data, &player->copy, &player->copy_len) != 0)
        copy_url_file_bytes(data, &player->copy, &player->copy_len);
    fprintf(stderr,
            "sword3-ios-shim: AVAudioPlayer %s data=%p bytes=%zu mag=%02x%02x%02x%02x -> %p\n",
            sel_name, data, player->copy_len,
            player->copy_len > 0 ? ((unsigned char *)player->copy)[0] : 0,
            player->copy_len > 1 ? ((unsigned char *)player->copy)[1] : 0,
            player->copy_len > 2 ? ((unsigned char *)player->copy)[2] : 0,
            player->copy_len > 3 ? ((unsigned char *)player->copy)[3] : 0,
            self);
    return self;
}

static sword3_objc_id proxy_av_audio_init_url(sword3_objc_id self,
                                               sword3_objc_sel selector,
                                               sword3_objc_id url,
                                               sword3_objc_id options,
                                               sword3_objc_id error)
{
    (void)options;
    return proxy_av_audio_init_data(self, selector, url, error);
}

static void proxy_av_audio_set_delegate(sword3_objc_id self,
                                        sword3_objc_sel selector,
                                        sword3_objc_id delegate)
{
    struct proxy_av_audio_player *player = self;

    (void)selector;
    if (player)
        player->delegate = delegate;
    fprintf(stderr, "sword3-ios-shim: AVAudioPlayer setDelegate: %p -> %p\n",
            delegate, self);
}

static void proxy_av_audio_set_loops(sword3_objc_id self,
                                     sword3_objc_sel selector,
                                     long loops)
{
    struct proxy_av_audio_player *player = self;

    (void)selector;
    if (player)
        player->loops = loops;
}

static void proxy_av_audio_set_pan(sword3_objc_id self, sword3_objc_sel selector,
                                   float pan)
{
    (void)self;
    (void)selector;
    (void)pan;
}

static void proxy_av_audio_set_volume(sword3_objc_id self,
                                      sword3_objc_sel selector, float volume)
{
    (void)self;
    (void)selector;
    (void)volume;
}

static void proxy_av_audio_play(sword3_objc_id self, sword3_objc_sel selector)
{
    struct proxy_av_audio_player *player = self;
    int loops;

    (void)selector;
    if (!player)
        return;
    loops = (int)player->loops;
    fprintf(stderr,
            "sword3-ios-shim: AVAudioPlayer play self=%p bytes=%zu loops=%d\n",
            self, player->copy_len, loops);
    if (!player->copy || player->copy_len == 0)
        return;
    if (host_play_bytes(player->copy, player->copy_len, loops) == 0)
        player->playing = 1;
}

static void proxy_av_audio_stop(sword3_objc_id self, sword3_objc_sel selector)
{
    struct proxy_av_audio_player *player = self;

    (void)selector;
    host_stop_bytes();
    if (player)
        player->playing = 0;
}

static double proxy_av_audio_duration(sword3_objc_id self,
                                       sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    return 1.0;
}

static unsigned proxy_av_audio_is_playing(sword3_objc_id self,
                                          sword3_objc_sel selector)
{
    struct proxy_av_audio_player *player = self;

    (void)selector;
    return player && player->playing;
}

static unsigned proxy_av_audio_prepare(sword3_objc_id self,
                                       sword3_objc_sel selector)
{
    (void)selector;
    return self != NULL;
}

static sword3_objc_id proxy_av_layer_with_player(sword3_objc_id cls,
                                                 sword3_objc_sel selector,
                                                 sword3_objc_id player)
{
    (void)cls;
    (void)selector;
    (void)player;
    return make_uikit_object(&proxy_av_player_layer_class);
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
    return stat(path_from_object(path_object), &status) == 0;
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

static const char *data_dir_root(void)
{
    const char *path = getenv("SWORD3_DATA_DIR");

    if (!path || path[0] != '/')
        return "/tmp/sword3";
    return path;
}

static int search_path_copy(char *out, size_t cap, uintptr_t directory)
{
    const char *data = data_dir_root();
    const char *tmpdir = getenv("TMPDIR");
    int written;

    /*
     * NSDocumentDirectory (9) is persistent game data. The old shim returned
     * that same directory for NSCachesDirectory and every other query, so the
     * game's startup cache cleanup enumerated and deleted PAL2_*.sav. Keep
     * non-document searches in a separate transient tree, matching master.
     */
    if (directory == 9)
        written = snprintf(out, cap, "%s", data);
    else {
        if (!tmpdir || tmpdir[0] != '/')
            tmpdir = "/tmp/sword3";
        written = snprintf(out, cap, "%s/ns-search-%llu", tmpdir,
                           (unsigned long long)directory);
    }
    if (written < 0 || (size_t)written >= cap)
        return -1;
    if (mkdir_parents(out) != 0 && errno != EEXIST)
        fprintf(stderr,
                "sword3-ios-shim: cannot create search path %s: %s\n",
                out, strerror(errno));
    return 0;
}

static int path_is_data_root(const char *path)
{
    const char *root = data_dir_root();
    size_t root_len;

    if (!path || !root)
        return 0;
    root_len = strlen(root);
    while (root_len > 1 && root[root_len - 1] == '/')
        root_len--;
    if (strncmp(path, root, root_len) != 0)
        return 0;
    return path[root_len] == '\0' ||
           (path[root_len] == '/' && path[root_len + 1] == '\0');
}

static int is_persistent_save_path(const char *path)
{
    const char *base;
    size_t length;

    if (!path || !path[0])
        return 0;
    if (path_is_data_root(path))
        return 1;
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!strcmp(base, "Interface.sav") || !strcmp(base, "setting_v2.lua"))
        return 1;
    if (strncmp(base, "PAL2_", 5) != 0)
        return 0;
    length = strlen(base);
    return length > 4 && !strcmp(base + length - 4, ".sav");
}

static const char *path_from_object(sword3_objc_id object)
{
    const struct proxy_url *url = object;
    const char *path;

    if (!object)
        return "";
    if (url->isa == &proxy_url_class)
        path = url->path ? url->path : "";
    else
        path = object_cstring(object);
    if (!strncmp(path, "file://", 7))
        path += 7;
    return path;
}

static int file_manager_path_allowed(const char *path)
{
    const char *bundle = getenv("SWORD3_BUNDLE_DIR");
    const char *tmpdir = getenv("TMPDIR");

    if (!path || path[0] != '/' || path_has_dotdot(path))
        return 0;
    if (path_is_data_root(path) ||
        path_under_root(path, data_dir_root()) ||
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

static int proxy_remove_cpath(const char *path)
{
    int ok = 0;

    if (is_persistent_save_path(path))
        return 1;
    if (!file_manager_path_allowed(path))
        errno = EPERM;
    else if (remove_tree(path) == 0)
        ok = 1;
    return ok;
}

static int proxy_file_manager_remove(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id path_object,
    sword3_objc_id *error
)
{
    const char *path = path_from_object(path_object);
    int ok;
    (void)self;
    (void)selector;

    if (error)
        *error = NULL;
    if (is_persistent_save_path(path)) {
        fprintf(stderr, "sword3-ios-shim: removeItemAtPath:%s -> keep save\n",
                path);
        return 1;
    }
    ok = proxy_remove_cpath(path);
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
    const char *path = path_from_object(path_object);
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
    directory = opendir(path_from_object(path_object));
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

static struct proxy_data *make_proxy_data(void *bytes, size_t length)
{
    struct proxy_data *data = calloc(1, sizeof(*data));

    if (!data) {
        free(bytes);
        return NULL;
    }
    data->isa = &proxy_data_class;
    data->bytes = bytes;
    data->length = length;
    return data;
}

static const void *proxy_data_bytes(struct proxy_data *self,
                                    sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->bytes : NULL;
}

static uintptr_t proxy_data_length(struct proxy_data *self,
                                   sword3_objc_sel selector)
{
    (void)selector;
    return self ? self->length : 0;
}

static sword3_objc_id proxy_data_with_bytes(sword3_objc_id cls,
                                            sword3_objc_sel selector,
                                            const void *bytes,
                                            uintptr_t length)
{
    unsigned char *copy;
    (void)cls;
    (void)selector;

    if (!bytes && length)
        return NULL;
    copy = malloc(length ? length : 1);
    if (!copy)
        return NULL;
    if (length)
        memcpy(copy, bytes, length);
    return make_proxy_data(copy, length);
}

static int read_file_bytes(const char *path, void **out, size_t *out_len)
{
    FILE *fp;
    long size;
    void *copy;

    *out = NULL;
    *out_len = 0;
    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    size = ftell(fp);
    if (size < 0 || size > 32L * 1024L * 1024L) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    copy = malloc(size ? (size_t)size : 1);
    if (!copy) {
        fclose(fp);
        return -1;
    }
    if (size && fread(copy, 1, (size_t)size, fp) != (size_t)size) {
        free(copy);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *out = copy;
    *out_len = (size_t)size;
    return 0;
}

static sword3_objc_id proxy_file_manager_contents_at_path(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id path_object
)
{
    void *bytes;
    size_t length;
    (void)self;
    (void)selector;

    if (read_file_bytes(path_from_object(path_object), &bytes, &length) != 0)
        return NULL;
    return make_proxy_data(bytes, length);
}

static int proxy_file_manager_contents_equal(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id left_object,
    sword3_objc_id right_object
)
{
    void *left = NULL;
    void *right = NULL;
    size_t left_len = 0;
    size_t right_len = 0;
    int equal = 0;
    (void)self;
    (void)selector;

    if (read_file_bytes(path_from_object(left_object), &left, &left_len) != 0 ||
        read_file_bytes(path_from_object(right_object), &right, &right_len) != 0)
        equal = 0;
    else
        equal = left_len == right_len &&
                (left_len == 0 || memcmp(left, right, left_len) == 0);
    free(left);
    free(right);
    return equal;
}

static int proxy_file_manager_exists_dir(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id path_object,
    signed char *is_directory
)
{
    struct stat status;
    int ok;
    (void)self;
    (void)selector;

    ok = stat(path_from_object(path_object), &status) == 0;
    if (is_directory)
        *is_directory = (signed char)(ok && S_ISDIR(status.st_mode));
    return ok;
}

static sword3_objc_id make_file_attributes(const char *path)
{
    struct stat status;
    struct proxy_dictionary *dict;
    struct proxy_dict_entry *entries;

    if (stat(path, &status) != 0)
        return NULL;
    dict = calloc(1, sizeof(*dict));
    entries = calloc(4, sizeof(*entries));
    if (!dict || !entries) {
        free(dict);
        free(entries);
        return NULL;
    }
    entries[0].key = "NSFileSize";
    entries[0].object = make_proxy_number_int((long long)status.st_size);
    entries[1].key = "NSFileType";
    entries[1].object = make_proxy_string(
        S_ISDIR(status.st_mode) ? "NSFileTypeDirectory" : "NSFileTypeRegular");
    entries[2].key = "NSFileModificationDate";
    entries[2].object = make_proxy_date((double)status.st_mtime);
    entries[3].key = "NSFileCreationDate";
    entries[3].object = make_proxy_date((double)status.st_ctime);
    dict->isa = &proxy_dictionary_class;
    dict->entries = entries;
    dict->count = 4;
    if (!entries[0].object || !entries[1].object || !entries[2].object ||
        !entries[3].object) {
        free(dict);
        free(entries);
        return NULL;
    }
    return dict;
}

static sword3_objc_id proxy_file_manager_attributes(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id path_object,
    sword3_objc_id *error
)
{
    (void)self;
    (void)selector;
    if (error)
        *error = NULL;
    return make_file_attributes(path_from_object(path_object));
}

static sword3_objc_id proxy_dictionary_file_attr(
    struct proxy_dictionary *self,
    sword3_objc_sel selector
)
{
    const char *key = "NSFileModificationDate";

    if (selector && !strcmp(selector, "fileSize"))
        key = "NSFileSize";
    else if (selector && !strcmp(selector, "fileCreationDate"))
        key = "NSFileCreationDate";
    else if (selector && !strcmp(selector, "fileType"))
        key = "NSFileType";
    return proxy_dictionary_object_for_key(self, selector,
                                           make_proxy_string(key));
}

static struct proxy_array *search_path_array(uintptr_t directory, int as_url)
{
    char path[PATH_MAX];
    struct proxy_array *array;

    if (search_path_copy(path, sizeof(path), directory) != 0)
        return NULL;
    array = make_proxy_array(1);
    if (!array)
        return NULL;
    array->items[0] = as_url ? proxy_url_from_cstring(path)
                             : make_proxy_string(path);
    if (!array->items[0]) {
        free(array->items);
        free(array);
        return NULL;
    }
    return array;
}

static sword3_objc_id proxy_file_manager_urls_for_directory(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    uintptr_t directory,
    uintptr_t domain_mask
)
{
    (void)self;
    (void)selector;
    (void)domain_mask;
    return search_path_array(directory, 1);
}

static int proxy_file_manager_create_directory_url(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id url,
    int with_intermediates,
    sword3_objc_id attributes,
    sword3_objc_id *error
)
{
    return proxy_file_manager_create_directory(
        self, selector, url, with_intermediates, attributes, error);
}

static int proxy_file_manager_remove_url(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id url,
    sword3_objc_id *error
)
{
    return proxy_file_manager_remove(self, selector, url, error);
}

static sword3_objc_id proxy_file_manager_ubiquity_container(
    struct proxy_file_manager *self,
    sword3_objc_sel selector,
    sword3_objc_id identifier
)
{
    (void)self;
    (void)selector;
    (void)identifier;
    return NULL;
}

static const struct proxy_method_list proxy_string_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 14,
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
        {"stringByAppendingString:", "@24@0:8@16",
         (sword3_objc_imp)proxy_string_append_string},
        {"intValue", "i16@0:8", (sword3_objc_imp)proxy_string_int_value},
        {"initWithCString:encoding:", "@32@0:8*16Q24",
         (sword3_objc_imp)proxy_string_init_cstring},
        {"initWithUTF8String:", "@24@0:8*16",
         (sword3_objc_imp)proxy_string_init_utf8},
    },
};

static const struct proxy_method_list proxy_string_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 3,
    .methods = {
        {"stringWithUTF8String:", "@24@0:8*16",
         (sword3_objc_imp)proxy_string_from_utf8},
        {"stringWithCString:encoding:", "@32@0:8*16Q24",
         (sword3_objc_imp)proxy_string_with_cstring},
        {"stringWithFormat:", "@24@0:8@16",
         (sword3_objc_imp)proxy_string_with_format},
    },
};

static const struct proxy_method_list proxy_array_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 10,
    .methods = {
        {"firstObject", "@16@0:8", (sword3_objc_imp)proxy_array_first},
        {"lastObject", "@16@0:8", (sword3_objc_imp)proxy_array_last},
        {"objectAtIndex:", "@24@0:8Q16", (sword3_objc_imp)proxy_array_at},
        {"objectAtIndexedSubscript:", "@24@0:8Q16",
         (sword3_objc_imp)proxy_array_at},
        {"count", "Q16@0:8", (sword3_objc_imp)proxy_array_count},
        {"containsObject:", "B24@0:8@16",
         (sword3_objc_imp)proxy_array_contains},
        {"copy", "@16@0:8", (sword3_objc_imp)proxy_array_copy},
        {"mutableCopy", "@16@0:8", (sword3_objc_imp)proxy_array_mutable_copy},
        {"countByEnumeratingWithState:objects:count:", "Q40@0:8^v16^@24Q32",
         (sword3_objc_imp)proxy_array_enumerate},
        {"initWithArray:", "@24@0:8@16",
         (sword3_objc_imp)proxy_array_init_with_array},
    },
};

static const struct proxy_method_list proxy_array_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 4,
    .methods = {
        {"array", "@16@0:8", (sword3_objc_imp)proxy_array_empty},
        {"arrayWithArray:", "@24@0:8@16",
         (sword3_objc_imp)proxy_array_with_array},
        {"arrayWithObjects:", "@24@0:8@16",
         (sword3_objc_imp)proxy_array_with_objects},
        {"arrayWithObjects:count:", "@32@0:8^@16Q24",
         (sword3_objc_imp)proxy_array_with_objects_count},
    },
};

static const struct proxy_method_list proxy_mutable_array_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 5,
    .methods = {
        {"addObject:", "v24@0:8@16", (sword3_objc_imp)proxy_array_add},
        {"addObjectsFromArray:", "v24@0:8@16",
         (sword3_objc_imp)proxy_array_add_from},
        {"initWithCapacity:", "@24@0:8Q16",
         (sword3_objc_imp)proxy_array_init_capacity},
        {"removeAllObjects", "v16@0:8",
         (sword3_objc_imp)proxy_array_remove_all},
        {"removeLastObject", "v16@0:8",
         (sword3_objc_imp)proxy_array_remove_last},
    },
};

static const struct proxy_method_list proxy_mutable_array_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"arrayWithCapacity:", "@24@0:8Q16",
         (sword3_objc_imp)proxy_mutable_array_with_capacity},
    },
};

static const struct proxy_method_list proxy_number_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 8,
    .methods = {
        {"boolValue", "B16@0:8", (sword3_objc_imp)proxy_number_bool},
        {"intValue", "i16@0:8", (sword3_objc_imp)proxy_number_int},
        {"integerValue", "q16@0:8", (sword3_objc_imp)proxy_number_long},
        {"longLongValue", "q16@0:8", (sword3_objc_imp)proxy_number_long},
        {"unsignedLongLongValue", "Q16@0:8",
         (sword3_objc_imp)proxy_number_long},
        {"doubleValue", "d16@0:8", (sword3_objc_imp)proxy_number_double},
        {"floatValue", "f16@0:8", (sword3_objc_imp)proxy_number_float},
        {"stringValue", "@16@0:8", (sword3_objc_imp)proxy_number_string_value},
    },
};

static const struct proxy_method_list proxy_number_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 9,
    .methods = {
        {"numberWithBool:", "@20@0:8B16",
         (sword3_objc_imp)proxy_number_with_bool},
        {"numberWithInt:", "@20@0:8i16",
         (sword3_objc_imp)proxy_number_with_long},
        {"numberWithInteger:", "@24@0:8q16",
         (sword3_objc_imp)proxy_number_with_long},
        {"numberWithUnsignedInteger:", "@24@0:8Q16",
         (sword3_objc_imp)proxy_number_with_long},
        {"numberWithUnsignedInt:", "@20@0:8I16",
         (sword3_objc_imp)proxy_number_with_long},
        {"numberWithLongLong:", "@24@0:8q16",
         (sword3_objc_imp)proxy_number_with_long},
        {"numberWithUnsignedLongLong:", "@24@0:8Q16",
         (sword3_objc_imp)proxy_number_with_long},
        {"numberWithDouble:", "@24@0:8d16",
         (sword3_objc_imp)proxy_number_with_double},
        {"numberWithFloat:", "@20@0:8f16",
         (sword3_objc_imp)proxy_number_with_float},
    },
};

static const struct proxy_method_list proxy_data_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 2,
    .methods = {
        {"bytes", "^v16@0:8", (sword3_objc_imp)proxy_data_bytes},
        {"length", "Q16@0:8", (sword3_objc_imp)proxy_data_length},
    },
};

static const struct proxy_method_list proxy_data_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"dataWithBytes:length:", "@32@0:8^v16Q24",
         (sword3_objc_imp)proxy_data_with_bytes},
    },
};

static const struct proxy_method_list proxy_date_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 5,
    .methods = {
        {"timeIntervalSince1970", "d16@0:8",
         (sword3_objc_imp)proxy_date_unix},
        {"timeIntervalSinceNow", "d16@0:8",
         (sword3_objc_imp)proxy_date_since_now},
        {"description", "@16@0:8",
         (sword3_objc_imp)proxy_date_description},
        {"copy", "@16@0:8", (sword3_objc_imp)proxy_date_copy},
        {"copyWithZone:", "@24@0:8^v16",
         (sword3_objc_imp)proxy_date_copy},
    },
};

static const struct proxy_method_list proxy_date_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 4,
    .methods = {
        {"date", "@16@0:8", (sword3_objc_imp)proxy_date_now},
        {"distantFuture", "@16@0:8",
         (sword3_objc_imp)proxy_date_distant_future},
        {"dateWithTimeIntervalSince1970:", "@24@0:8d16",
         (sword3_objc_imp)proxy_date_with_unix},
        {"dateWithTimeIntervalSinceNow:", "@24@0:8d16",
         (sword3_objc_imp)proxy_date_with_interval_since_now},
    },
};

static const struct proxy_method_list proxy_date_formatter_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 4,
    .methods = {
        {"setDateFormat:", "v24@0:8@16",
         (sword3_objc_imp)proxy_formatter_set_format},
        {"stringFromDate:", "@24@0:8@16",
         (sword3_objc_imp)proxy_formatter_string_from_date},
        {"setTimeZone:", "v24@0:8@16", (sword3_objc_imp)proxy_noop},
        {"setLocale:", "v24@0:8@16", (sword3_objc_imp)proxy_noop},
    },
};

static const struct proxy_method_list proxy_calendar_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"currentCalendar", "@16@0:8",
         (sword3_objc_imp)proxy_calendar_current},
    },
};

static const struct proxy_method_list proxy_calendar_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"components:fromDate:", "@32@0:8Q16@24",
         (sword3_objc_imp)proxy_calendar_components},
    },
};

static const struct proxy_method_list proxy_date_components_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 7,
    .methods = {
        {"year", "q16@0:8", (sword3_objc_imp)proxy_components_year},
        {"month", "q16@0:8", (sword3_objc_imp)proxy_components_month},
        {"day", "q16@0:8", (sword3_objc_imp)proxy_components_day},
        {"hour", "q16@0:8", (sword3_objc_imp)proxy_components_hour},
        {"minute", "q16@0:8", (sword3_objc_imp)proxy_components_minute},
        {"second", "q16@0:8", (sword3_objc_imp)proxy_components_second},
        {"weekday", "q16@0:8", (sword3_objc_imp)proxy_components_weekday},
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
    .count = 6,
    .methods = {
        {"objectForKey:", "@24@0:8@16",
         (sword3_objc_imp)proxy_dictionary_object_for_key},
        {"objectForKeyedSubscript:", "@24@0:8@16",
         (sword3_objc_imp)proxy_dictionary_object_for_key},
        {"count", "Q16@0:8", (sword3_objc_imp)proxy_dictionary_count},
        {"fileModificationDate", "@16@0:8",
         (sword3_objc_imp)proxy_dictionary_file_attr},
        {"fileSize", "@16@0:8",
         (sword3_objc_imp)proxy_dictionary_file_attr},
        {"fileCreationDate", "@16@0:8",
         (sword3_objc_imp)proxy_dictionary_file_attr},
    },
};

static const struct proxy_method_list proxy_file_manager_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 12,
    .methods = {
        {"fileExistsAtPath:", "B24@0:8@16",
         (sword3_objc_imp)proxy_file_manager_exists},
        {"fileExistsAtPath:isDirectory:", "B32@0:8@16^B24",
         (sword3_objc_imp)proxy_file_manager_exists_dir},
        {"contentsOfDirectoryAtPath:error:", "@32@0:8@16^@24",
         (sword3_objc_imp)proxy_file_manager_contents},
        {"removeItemAtPath:error:", "B32@0:8@16^@24",
         (sword3_objc_imp)proxy_file_manager_remove},
        {"removeItemAtURL:error:", "B32@0:8@16^@24",
         (sword3_objc_imp)proxy_file_manager_remove_url},
        {"createDirectoryAtPath:withIntermediateDirectories:attributes:error:",
         "B44@0:8@16B24@28^@36",
         (sword3_objc_imp)proxy_file_manager_create_directory},
        {"createDirectoryAtURL:withIntermediateDirectories:attributes:error:",
         "B48@0:8@16B24@28^@36",
         (sword3_objc_imp)proxy_file_manager_create_directory_url},
        {"attributesOfItemAtPath:error:", "@32@0:8@16^@24",
         (sword3_objc_imp)proxy_file_manager_attributes},
        {"contentsAtPath:", "@24@0:8@16",
         (sword3_objc_imp)proxy_file_manager_contents_at_path},
        {"contentsEqualAtPath:andPath:", "B32@0:8@16@24",
         (sword3_objc_imp)proxy_file_manager_contents_equal},
        {"URLsForDirectory:inDomains:", "@32@0:8Q16Q24",
         (sword3_objc_imp)proxy_file_manager_urls_for_directory},
        {"URLForUbiquityContainerIdentifier:", "@24@0:8@16",
         (sword3_objc_imp)proxy_file_manager_ubiquity_container},
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

static const struct proxy_method_list proxy_color_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 11,
    .methods = {
        {"clearColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"whiteColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"blackColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"grayColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"darkGrayColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"lightGrayColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"redColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"greenColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"blueColor", "@16@0:8", (sword3_objc_imp)proxy_color_named},
        {"colorWithRed:green:blue:alpha:", "@48@0:8dddd",
         (sword3_objc_imp)proxy_color_rgba},
        {"colorWithWhite:alpha:", "@32@0:8dd",
         (sword3_objc_imp)proxy_color_white},
    },
};

static const struct proxy_method_list proxy_image_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"imageNamed:", "@24@0:8@16", (sword3_objc_imp)proxy_image_named},
    },
};

static const struct proxy_method_list proxy_uikit_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 11,
    .methods = {
        {"addSubview:", "v24@0:8@16", (sword3_objc_imp)proxy_noop},
        {"removeFromSuperview", "v16@0:8", (sword3_objc_imp)proxy_noop},
        {"bounds", "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8",
         (sword3_objc_imp)proxy_screen_bounds},
        {"frame", "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8",
         (sword3_objc_imp)proxy_screen_bounds},
        {"setFrame:", "v48@0:8{CGRect={CGPoint=dd}{CGSize=dd}}16",
         (sword3_objc_imp)proxy_noop},
        {"setHidden:", "v20@0:8B16", (sword3_objc_imp)proxy_noop},
        {"makeKeyAndVisible", "v16@0:8", (sword3_objc_imp)proxy_noop},
        {"setRootViewController:", "v24@0:8@16", (sword3_objc_imp)proxy_noop},
        {"setImage:", "v24@0:8@16", (sword3_objc_imp)proxy_noop},
        {"view", "@16@0:8", (sword3_objc_imp)proxy_return_self},
        {"initWithImage:", "@24@0:8@16", (sword3_objc_imp)proxy_return_self},
    },
};

static const struct proxy_method_list proxy_url_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 6,
    .methods = {
        {"path", "@16@0:8", (sword3_objc_imp)proxy_url_path},
        {"absoluteString", "@16@0:8",
         (sword3_objc_imp)proxy_url_absolute_string},
        {"lastPathComponent", "@16@0:8",
         (sword3_objc_imp)proxy_url_last_component},
        {"URLByAppendingPathComponent:", "@24@0:8@16",
         (sword3_objc_imp)proxy_url_append_component},
        {"initFileURLWithPath:", "@24@0:8@16",
         (sword3_objc_imp)proxy_url_init_path},
        {"initFileURLWithPath:isDirectory:", "@28@0:8@16B24",
         (sword3_objc_imp)proxy_url_init_path_dir},
    },
};

static const struct proxy_method_list proxy_url_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 3,
    .methods = {
        {"fileURLWithPath:", "@24@0:8@16",
         (sword3_objc_imp)proxy_url_with_path},
        {"fileURLWithPath:isDirectory:", "@28@0:8@16B24",
         (sword3_objc_imp)proxy_url_with_path_dir},
        {"URLWithString:", "@24@0:8@16",
         (sword3_objc_imp)proxy_url_with_path},
    },
};

static const struct proxy_method_list proxy_nc_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 3,
    .methods = {
        {"addObserver:selector:name:object:", "v48@0:8@16:24@32@40",
         (sword3_objc_imp)proxy_nc_add},
        {"removeObserver:", "v24@0:8@16", (sword3_objc_imp)proxy_nc_remove},
        {"postNotificationName:object:", "v32@0:8@16@24",
         (sword3_objc_imp)proxy_nc_post},
    },
};

static const struct proxy_method_list proxy_nc_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"defaultCenter", "@16@0:8", (sword3_objc_imp)proxy_nc_default},
    },
};

static const struct proxy_method_list proxy_av_player_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 3,
    .methods = {
        {"play", "v16@0:8", (sword3_objc_imp)proxy_av_play},
        {"pause", "v16@0:8", (sword3_objc_imp)proxy_noop},
        {"initWithURL:", "@24@0:8@16", (sword3_objc_imp)proxy_av_init_url},
    },
};

static const struct proxy_method_list proxy_av_player_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"playerWithURL:", "@24@0:8@16",
         (sword3_objc_imp)proxy_av_player_with_url},
    },
};

static const struct proxy_method_list proxy_av_audio_player_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 12,
    .methods = {
        {"initWithContentsOfURL:options:error:", "@32@0:8@16@24^@32",
         (sword3_objc_imp)proxy_av_audio_init_url},
        {"initWithData:error:", "@32@0:8@16^@24",
         (sword3_objc_imp)proxy_av_audio_init_data},
        {"setDelegate:", "v24@0:8@16",
         (sword3_objc_imp)proxy_av_audio_set_delegate},
        {"setNumberOfLoops:", "v24@0:8q16",
         (sword3_objc_imp)proxy_av_audio_set_loops},
        {"setPan:", "v20@0:8f16", (sword3_objc_imp)proxy_av_audio_set_pan},
        {"setVolume:", "v20@0:8f16", (sword3_objc_imp)proxy_av_audio_set_volume},
        {"play", "v16@0:8", (sword3_objc_imp)proxy_av_audio_play},
        {"stop", "v16@0:8", (sword3_objc_imp)proxy_av_audio_stop},
        {"pause", "v16@0:8", (sword3_objc_imp)proxy_noop},
        {"prepareToPlay", "B16@0:8", (sword3_objc_imp)proxy_av_audio_prepare},
        {"isPlaying", "B16@0:8", (sword3_objc_imp)proxy_av_audio_is_playing},
        {"duration", "d16@0:8", (sword3_objc_imp)proxy_av_audio_duration},
    },
};

static const struct proxy_method_list proxy_av_layer_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"playerLayerWithPlayer:", "@24@0:8@16",
         (sword3_objc_imp)proxy_av_layer_with_player},
    },
};

static const struct proxy_method_list proxy_display_link_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 7,
    .methods = {
        {"addToRunLoop:forMode:", "v32@0:8@16@24",
         (sword3_objc_imp)proxy_display_link_add},
        {"removeFromRunLoop:forMode:", "v32@0:8@16@24",
         (sword3_objc_imp)proxy_display_link_remove},
        {"invalidate", "v16@0:8",
         (sword3_objc_imp)proxy_display_link_invalidate},
        {"setPaused:", "v20@0:8B16",
         (sword3_objc_imp)proxy_display_link_set_paused},
        {"isPaused", "B16@0:8",
         (sword3_objc_imp)proxy_display_link_is_paused},
        {"setFrameInterval:", "v24@0:8q16",
         (sword3_objc_imp)proxy_display_link_set_interval},
        {"setPreferredFramesPerSecond:", "v24@0:8q16",
         (sword3_objc_imp)proxy_display_link_set_fps},
    },
};

static const struct proxy_method_list proxy_display_link_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"displayLinkWithTarget:selector:", "@32@0:8@16:24",
         (sword3_objc_imp)proxy_display_link_with_target},
    },
};

static const struct proxy_method_list proxy_runloop_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {"runMode:beforeDate:", "B32@0:8@16@24",
         (sword3_objc_imp)proxy_runloop_run_mode},
    },
};

static const struct proxy_method_list proxy_runloop_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 2,
    .methods = {
        {"currentRunLoop", "@16@0:8", (sword3_objc_imp)proxy_runloop_current},
        {"mainRunLoop", "@16@0:8", (sword3_objc_imp)proxy_runloop_current},
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
    .name = "NSArray",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_array_methods,
};

static const struct sword3_objc_class_ro proxy_array_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSArray",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_array_class_methods,
};

static const struct sword3_objc_class_ro proxy_mutable_array_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_array),
    .name = "NSMutableArray",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_mutable_array_methods,
};

static const struct sword3_objc_class_ro proxy_mutable_array_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSMutableArray",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_mutable_array_class_methods,
};

static const struct sword3_objc_class_ro proxy_number_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_number),
    .name = "NSNumber",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_number_methods,
};

static const struct sword3_objc_class_ro proxy_number_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSNumber",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_number_class_methods,
};

static const struct sword3_objc_class_ro proxy_data_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_data),
    .name = "NSData",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_data_methods,
};

static const struct sword3_objc_class_ro proxy_data_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSData",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_data_class_methods,
};

static const struct sword3_objc_class_ro proxy_date_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_date),
    .name = "NSDate",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_date_methods,
};

static const struct sword3_objc_class_ro proxy_date_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSDate",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_date_class_methods,
};

static const struct sword3_objc_class_ro proxy_date_formatter_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_date_formatter),
    .name = "NSDateFormatter",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_date_formatter_methods,
};

static const struct sword3_objc_class_ro proxy_date_formatter_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSDateFormatter",
};

static const struct sword3_objc_class_ro proxy_calendar_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_calendar),
    .name = "NSCalendar",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_calendar_methods,
};

static const struct sword3_objc_class_ro proxy_calendar_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSCalendar",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_calendar_class_methods,
};

static const struct sword3_objc_class_ro proxy_date_components_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_date_components),
    .name = "NSDateComponents",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_date_components_methods,
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

static const struct sword3_objc_class_ro proxy_color_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_color),
    .name = "UIColor",
};

static const struct sword3_objc_class_ro proxy_color_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIColor",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_color_class_methods,
};

static const struct sword3_objc_class_ro proxy_image_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_image),
    .name = "UIImage",
};

static const struct sword3_objc_class_ro proxy_image_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIImage",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_image_class_methods,
};

static const struct sword3_objc_class_ro proxy_uikit_ro_responder = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "UIResponder",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_uikit_methods,
};

static const struct sword3_objc_class_ro proxy_uikit_ro_view = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "UIView",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_uikit_methods,
};

static const struct sword3_objc_class_ro proxy_uikit_ro_window = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "UIWindow",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_uikit_methods,
};

static const struct sword3_objc_class_ro proxy_uikit_ro_controller = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "UIViewController",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_uikit_methods,
};

static const struct sword3_objc_class_ro proxy_uikit_ro_image_view = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "UIImageView",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_uikit_methods,
};

static const struct sword3_objc_class_ro proxy_uikit_meta_ro_responder = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIResponder",
};

static const struct sword3_objc_class_ro proxy_uikit_meta_ro_view = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIView",
};

static const struct sword3_objc_class_ro proxy_uikit_meta_ro_window = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIWindow",
};

static const struct sword3_objc_class_ro proxy_uikit_meta_ro_controller = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIViewController",
};

static const struct sword3_objc_class_ro proxy_uikit_meta_ro_image_view = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "UIImageView",
};

static const struct sword3_objc_class_ro proxy_url_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_url),
    .name = "NSURL",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_url_methods,
};

static const struct sword3_objc_class_ro proxy_url_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSURL",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_url_class_methods,
};

static const struct sword3_objc_class_ro proxy_nc_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_notification_center),
    .name = "NSNotificationCenter",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_nc_methods,
};

static const struct sword3_objc_class_ro proxy_nc_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSNotificationCenter",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_nc_class_methods,
};

static const struct sword3_objc_class_ro proxy_av_player_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "AVPlayer",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_av_player_methods,
};

static const struct sword3_objc_class_ro proxy_av_player_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "AVPlayer",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_av_player_class_methods,
};

static const struct sword3_objc_class_ro proxy_av_audio_player_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_av_audio_player),
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_av_audio_player_methods,
};

static const struct sword3_objc_class_ro proxy_av_audio_player_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "AVAudioPlayer",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_uikit_methods,
};

static const struct sword3_objc_class_ro proxy_av_layer_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "AVPlayerLayer",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_uikit_methods,
};

static const struct sword3_objc_class_ro proxy_av_layer_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "AVPlayerLayer",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_av_layer_class_methods,
};

static const struct sword3_objc_class_ro proxy_display_link_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_display_link),
    .name = "CADisplayLink",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_display_link_methods,
};

static const struct sword3_objc_class_ro proxy_display_link_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "CADisplayLink",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_display_link_class_methods,
};

static const struct sword3_objc_class_ro proxy_runloop_ro = {
    .flags = SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct proxy_uikit_object),
    .name = "NSRunLoop",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_runloop_methods,
};

static const struct sword3_objc_class_ro proxy_runloop_metaclass_ro = {
    .flags = SWORD3_OBJC_RO_META | SWORD3_OBJC_RO_ROOT,
    .instance_size = sizeof(struct sword3_objc_class),
    .name = "NSRunLoop",
    .base_methods =
        (const struct sword3_objc_method_list *)&proxy_runloop_class_methods,
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

static struct sword3_objc_class proxy_array_metaclass = {
    .isa = &proxy_array_metaclass,
    .data_bits = (uintptr_t)&proxy_array_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_array_class
    __asm__("OBJC_CLASS_$_NSArray") = {
        .isa = &proxy_array_metaclass,
        .data_bits = (uintptr_t)&proxy_array_ro,
    };

static struct sword3_objc_class proxy_mutable_array_metaclass = {
    .isa = &proxy_mutable_array_metaclass,
    .superclass = &proxy_array_metaclass,
    .data_bits = (uintptr_t)&proxy_mutable_array_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_mutable_array_class
    __asm__("OBJC_CLASS_$_NSMutableArray") = {
        .isa = &proxy_mutable_array_metaclass,
        .superclass = &proxy_array_class,
        .data_bits = (uintptr_t)&proxy_mutable_array_ro,
    };

static struct sword3_objc_class proxy_number_metaclass = {
    .isa = &proxy_number_metaclass,
    .data_bits = (uintptr_t)&proxy_number_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_number_class
    __asm__("OBJC_CLASS_$_NSNumber") = {
        .isa = &proxy_number_metaclass,
        .data_bits = (uintptr_t)&proxy_number_ro,
    };

static struct sword3_objc_class proxy_data_metaclass = {
    .isa = &proxy_data_metaclass,
    .data_bits = (uintptr_t)&proxy_data_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_data_class
    __asm__("OBJC_CLASS_$_NSData") = {
        .isa = &proxy_data_metaclass,
        .data_bits = (uintptr_t)&proxy_data_ro,
    };

static struct sword3_objc_class proxy_date_metaclass = {
    .isa = &proxy_date_metaclass,
    .data_bits = (uintptr_t)&proxy_date_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_date_class
    __asm__("OBJC_CLASS_$_NSDate") = {
        .isa = &proxy_date_metaclass,
        .data_bits = (uintptr_t)&proxy_date_ro,
    };

static struct sword3_objc_class proxy_date_formatter_metaclass = {
    .isa = &proxy_date_formatter_metaclass,
    .data_bits = (uintptr_t)&proxy_date_formatter_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_date_formatter_class
    __asm__("OBJC_CLASS_$_NSDateFormatter") = {
        .isa = &proxy_date_formatter_metaclass,
        .data_bits = (uintptr_t)&proxy_date_formatter_ro,
    };

static struct sword3_objc_class proxy_calendar_metaclass = {
    .isa = &proxy_calendar_metaclass,
    .data_bits = (uintptr_t)&proxy_calendar_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_calendar_class
    __asm__("OBJC_CLASS_$_NSCalendar") = {
        .isa = &proxy_calendar_metaclass,
        .data_bits = (uintptr_t)&proxy_calendar_ro,
    };

static struct sword3_objc_class proxy_date_components_class = {
    .isa = &proxy_date_components_class,
    .data_bits = (uintptr_t)&proxy_date_components_ro,
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

static struct sword3_objc_class proxy_color_metaclass = {
    .isa = &proxy_color_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_color_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_color_class
    __asm__("OBJC_CLASS_$_UIColor") = {
        .isa = &proxy_color_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_color_ro,
    };

static struct sword3_objc_class proxy_image_metaclass = {
    .isa = &proxy_image_metaclass,
    .superclass = NULL,
    .data_bits = (uintptr_t)&proxy_image_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_image_class
    __asm__("OBJC_CLASS_$_UIImage") = {
        .isa = &proxy_image_metaclass,
        .superclass = NULL,
        .data_bits = (uintptr_t)&proxy_image_ro,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_responder_metaclass
    __asm__("OBJC_METACLASS_$_UIResponder") = {
        .isa = &proxy_responder_metaclass,
        .data_bits = (uintptr_t)&proxy_uikit_meta_ro_responder,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_responder_class
    __asm__("OBJC_CLASS_$_UIResponder") = {
        .isa = &proxy_responder_metaclass,
        .data_bits = (uintptr_t)&proxy_uikit_ro_responder,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_view_metaclass
    __asm__("OBJC_METACLASS_$_UIView") = {
        .isa = &proxy_view_metaclass,
        .superclass = &proxy_responder_metaclass,
        .data_bits = (uintptr_t)&proxy_uikit_meta_ro_view,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_view_class
    __asm__("OBJC_CLASS_$_UIView") = {
        .isa = &proxy_view_metaclass,
        .superclass = &proxy_responder_class,
        .data_bits = (uintptr_t)&proxy_uikit_ro_view,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_window_metaclass
    __asm__("OBJC_METACLASS_$_UIWindow") = {
        .isa = &proxy_window_metaclass,
        .superclass = &proxy_view_metaclass,
        .data_bits = (uintptr_t)&proxy_uikit_meta_ro_window,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_window_class
    __asm__("OBJC_CLASS_$_UIWindow") = {
        .isa = &proxy_window_metaclass,
        .superclass = &proxy_view_class,
        .data_bits = (uintptr_t)&proxy_uikit_ro_window,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_view_controller_metaclass
    __asm__("OBJC_METACLASS_$_UIViewController") = {
        .isa = &proxy_view_controller_metaclass,
        .superclass = &proxy_responder_metaclass,
        .data_bits = (uintptr_t)&proxy_uikit_meta_ro_controller,
    };

SWORD3_EXPORT struct sword3_objc_class proxy_view_controller_class
    __asm__("OBJC_CLASS_$_UIViewController") = {
        .isa = &proxy_view_controller_metaclass,
        .superclass = &proxy_responder_class,
        .data_bits = (uintptr_t)&proxy_uikit_ro_controller,
    };

static struct sword3_objc_class proxy_image_view_metaclass = {
    .isa = &proxy_image_view_metaclass,
    .superclass = &proxy_view_metaclass,
    .data_bits = (uintptr_t)&proxy_uikit_meta_ro_image_view,
};

SWORD3_EXPORT struct sword3_objc_class proxy_image_view_class
    __asm__("OBJC_CLASS_$_UIImageView") = {
        .isa = &proxy_image_view_metaclass,
        .superclass = &proxy_view_class,
        .data_bits = (uintptr_t)&proxy_uikit_ro_image_view,
    };

static struct sword3_objc_class proxy_url_metaclass = {
    .isa = &proxy_url_metaclass,
    .data_bits = (uintptr_t)&proxy_url_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_url_class
    __asm__("OBJC_CLASS_$_NSURL") = {
        .isa = &proxy_url_metaclass,
        .data_bits = (uintptr_t)&proxy_url_ro,
    };

static struct sword3_objc_class proxy_notification_center_metaclass = {
    .isa = &proxy_notification_center_metaclass,
    .data_bits = (uintptr_t)&proxy_nc_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_notification_center_class
    __asm__("OBJC_CLASS_$_NSNotificationCenter") = {
        .isa = &proxy_notification_center_metaclass,
        .data_bits = (uintptr_t)&proxy_nc_ro,
    };

static struct sword3_objc_class proxy_av_player_metaclass = {
    .isa = &proxy_av_player_metaclass,
    .data_bits = (uintptr_t)&proxy_av_player_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_av_player_class
    __asm__("OBJC_CLASS_$_AVPlayer") = {
        .isa = &proxy_av_player_metaclass,
        .data_bits = (uintptr_t)&proxy_av_player_ro,
    };

static struct sword3_objc_class proxy_av_audio_player_metaclass = {
    .isa = &proxy_av_audio_player_metaclass,
    .data_bits = (uintptr_t)&proxy_av_audio_player_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_av_audio_player_class
    __asm__("OBJC_CLASS_$_AVAudioPlayer") = {
        .isa = &proxy_av_audio_player_metaclass,
        .data_bits = (uintptr_t)&proxy_av_audio_player_ro,
    };

static struct sword3_objc_class proxy_av_player_layer_metaclass = {
    .isa = &proxy_av_player_layer_metaclass,
    .data_bits = (uintptr_t)&proxy_av_layer_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_av_player_layer_class
    __asm__("OBJC_CLASS_$_AVPlayerLayer") = {
        .isa = &proxy_av_player_layer_metaclass,
        .data_bits = (uintptr_t)&proxy_av_layer_ro,
    };

static struct sword3_objc_class proxy_display_link_metaclass = {
    .isa = &proxy_display_link_metaclass,
    .data_bits = (uintptr_t)&proxy_display_link_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_display_link_class
    __asm__("OBJC_CLASS_$_CADisplayLink") = {
        .isa = &proxy_display_link_metaclass,
        .data_bits = (uintptr_t)&proxy_display_link_ro,
    };

static struct sword3_objc_class proxy_runloop_metaclass = {
    .isa = &proxy_runloop_metaclass,
    .data_bits = (uintptr_t)&proxy_runloop_metaclass_ro,
};

SWORD3_EXPORT struct sword3_objc_class proxy_runloop_class
    __asm__("OBJC_CLASS_$_NSRunLoop") = {
        .isa = &proxy_runloop_metaclass,
        .data_bits = (uintptr_t)&proxy_runloop_ro,
    };

SWORD3_EXPORT const struct constant_string_layout *kCFRunLoopDefaultMode
    __asm__("kCFRunLoopDefaultMode") = &cf_runloop_default_mode_string;
SWORD3_EXPORT const struct constant_string_layout *NSDefaultRunLoopMode
    __asm__("NSDefaultRunLoopMode") = &cf_runloop_default_mode_string;
SWORD3_EXPORT const struct constant_string_layout *NSFileModificationDate
    __asm__("NSFileModificationDate") = &nsfile_modification_date_string;

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
    const char *text = object_cstring(format);
    /*
     * NSLog is variadic under Apple's stack-only vararg ABI. Print the
     * format object as a C string and ignore extra arguments; that is
     * enough to see Lua/engine messages during boot.
     */
    fprintf(stderr, "[sword3-nslog] %s\n", text && text[0] ? text : "(empty)");
}

SWORD3_EXPORT
sword3_objc_id NSSearchPathForDirectoriesInDomains(
    uintptr_t directory,
    uintptr_t domain_mask,
    int expand_tilde
)
{
    char path[PATH_MAX];
    static unsigned seen;
    (void)domain_mask;
    (void)expand_tilde;

    if (search_path_copy(path, sizeof(path), directory) != 0)
        return NULL;
    if (seen++ < 32)
        fprintf(stderr,
                "sword3-ios-shim: NSSearchPath directory=%llu -> %s\n",
                (unsigned long long)directory, path);
    return search_path_array(directory, 0);
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

int sword3_test_search_path(uintptr_t directory, char *out, uint32_t cap)
{
    char path[PATH_MAX];

    if (!out || cap == 0)
        return -1;
    if (search_path_copy(path, sizeof(path), directory) != 0)
        return -1;
    if (strlen(path) >= cap)
        return -1;
    memcpy(out, path, strlen(path) + 1);
    return 0;
}

int sword3_test_keep_save_path(const char *path)
{
    return is_persistent_save_path(path);
}

int sword3_test_remove_path(const char *path)
{
    return proxy_remove_cpath(path);
}
#endif
