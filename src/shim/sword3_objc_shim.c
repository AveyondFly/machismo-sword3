#define _GNU_SOURCE
#include "sword3_objc_shim.h"

#include <dlfcn.h>
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
static void write_diagnostic(const char *prefix, const char *detail);
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

static void nsobject_ignore(sword3_objc_id self, sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
}

struct audio_blob {
    sword3_objc_Class isa;
    char *path;
    void *bytes;
    size_t length;
    int loops;
};

static sword3_objc_id movie_observer;
static sword3_objc_sel movie_callback;
static sword3_objc_id g_av_player;
static char *g_pending_video_path;

static int is_movie_finish_selector(sword3_objc_sel selector)
{
    if (selector == NULL)
        return 0;
    if (strcmp(selector, "moviePlayBackDidFinish:") == 0)
        return 1;
    return strstr(selector, "DidPlayToEnd") != NULL;
}

static void fire_movie_finish(void)
{
    sword3_objc_id observer = movie_observer;
    sword3_objc_sel callback = movie_callback;
    sword3_objc_imp imp;

    movie_observer = NULL;
    movie_callback = NULL;
    if (observer == NULL || callback == NULL)
        return;
    write_diagnostic("[sword3-objc-shim] movie finished via ", callback);
    imp = sword3_objc_lookup_imp(observer, callback, NULL);
    if (imp == NULL || imp == (sword3_objc_imp)nsobject_init)
        return;
    ((void (*)(sword3_objc_id, sword3_objc_sel, sword3_objc_id))imp)(
        observer, callback, NULL
    );
}

static void nsobject_add_observer(sword3_objc_id self,
                                 sword3_objc_sel selector,
                                 sword3_objc_id observer,
                                 sword3_objc_sel callback,
                                 sword3_objc_id name,
                                 sword3_objc_id object)
{
    (void)self;
    (void)selector;
    (void)name;
    (void)object;
    if (!is_movie_finish_selector(callback))
        return;
    movie_observer = observer;
    movie_callback = callback;
    write_diagnostic("[sword3-objc-shim] movie finish observer: ", callback);
}

static int ascii_lower(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int path_has_extension(const char *path, const char *ext)
{
    size_t path_len;
    size_t ext_len;
    size_t index;

    if (path == NULL || ext == NULL)
        return 0;
    path_len = strlen(path);
    ext_len = strlen(ext);
    if (path_len < ext_len)
        return 0;
    for (index = 0; index < ext_len; ++index) {
        if (ascii_lower((unsigned char)path[path_len - ext_len + index]) !=
            ascii_lower((unsigned char)ext[index]))
            return 0;
    }
    return 1;
}

static int is_audio_path(const char *path)
{
    return path_has_extension(path, ".mp3") ||
           path_has_extension(path, ".ogg") ||
           path_has_extension(path, ".wav") ||
           path_has_extension(path, ".m4a") ||
           path_has_extension(path, ".aac") ||
           path_has_extension(path, ".caf") ||
           path_has_extension(path, ".mp2") ||
           path_has_extension(path, ".mid") ||
           path_has_extension(path, ".midi");
}

static int play_blob(struct audio_blob *blob)
{
    typedef int (*host_play_file_fn)(const char *, int);
    typedef int (*host_play_data_fn)(const void *, size_t, int);
    static host_play_file_fn host_play_file;
    static host_play_data_fn host_play_data;
    static int resolved;

    if (blob == NULL)
        return 0;
    if (!resolved) {
        resolved = 1;
        host_play_file = (host_play_file_fn)dlsym(
            RTLD_DEFAULT, "sword3_host_play_music_file"
        );
        host_play_data = (host_play_data_fn)dlsym(
            RTLD_DEFAULT, "sword3_host_play_music_data"
        );
    }
    if (blob->path != NULL && blob->path[0] != '\0' && is_audio_path(blob->path)) {
        if (host_play_file)
            return host_play_file(blob->path, blob->loops) == 0;
        write_diagnostic(
            "[sword3-objc-shim] no host music player for ",
            blob->path
        );
        return 0;
    }
    if (blob->bytes != NULL && blob->length > 0) {
        if (host_play_data)
            return host_play_data(blob->bytes, blob->length, blob->loops) == 0;
        write_diagnostic(
            "[sword3-objc-shim] no host music decoder for in-memory audio",
            ""
        );
        return 0;
    }
    return 0;
}

static int is_video_path(const char *path)
{
    return path_has_extension(path, ".mp4") ||
           path_has_extension(path, ".m4v") ||
           path_has_extension(path, ".mov") ||
           path_has_extension(path, ".mpv");
}

static int play_video_path(const char *path)
{
    typedef int (*host_play_video_fn)(const char *, void (*)(void));
    static host_play_video_fn host_play_video;
    static int resolved;

    if (path == NULL || !is_video_path(path))
        return 0;
    if (!resolved) {
        resolved = 1;
        host_play_video = (host_play_video_fn)dlsym(
            RTLD_DEFAULT, "sword3_host_play_video_file"
        );
    }
    if (!host_play_video) {
        write_diagnostic(
            "[sword3-objc-shim] no host video player for ",
            path
        );
        return 0;
    }
    if (host_play_video(path, fire_movie_finish) != 0) {
        write_diagnostic(
            "[sword3-objc-shim] host video failed for ",
            path
        );
        return 0;
    }
    return 1;
}

static int play_video_blob(struct audio_blob *blob)
{
    const char *path = NULL;

    if (blob != NULL)
        path = blob->path;
    if (play_video_path(path))
        return 1;
    return play_video_path(g_pending_video_path);
}

static sword3_objc_id nsobject_play(sword3_objc_id self,
                                   sword3_objc_sel selector)
{
    (void)selector;
    if (play_blob((struct audio_blob *)self))
        return (sword3_objc_id)(uintptr_t)1;
    if (play_video_blob((struct audio_blob *)self))
        return (sword3_objc_id)(uintptr_t)1;
    fire_movie_finish();
    return (sword3_objc_id)(uintptr_t)1;
}

static void host_skip_video(void)
{
    typedef void (*host_stop_video_fn)(void);
    static host_stop_video_fn host_stop_video;
    static int resolved;

    if (!resolved) {
        resolved = 1;
        host_stop_video = (host_stop_video_fn)dlsym(
            RTLD_DEFAULT, "sword3_host_stop_video"
        );
    }
    if (host_stop_video)
        host_stop_video();
}

static void nsobject_stop(sword3_objc_id self, sword3_objc_sel selector)
{
    typedef void (*host_stop_fn)(void);
    static host_stop_fn host_stop;
    static int resolved;

    (void)self;
    (void)selector;
    if (!resolved) {
        resolved = 1;
        host_stop = (host_stop_fn)dlsym(RTLD_DEFAULT, "sword3_host_stop_music");
    }
    if (host_stop)
        host_stop();
}

static void nsobject_pause(sword3_objc_id self, sword3_objc_sel selector)
{
    (void)self;
    (void)selector;
    host_skip_video();
}

static void nsobject_set_player(sword3_objc_id self,
                               sword3_objc_sel selector,
                               sword3_objc_id player)
{
    (void)self;
    (void)selector;
    g_av_player = player;
}

static sword3_objc_id nsobject_player(sword3_objc_id self,
                                     sword3_objc_sel selector)
{
    (void)selector;
    return g_av_player != NULL ? g_av_player : self;
}

static sword3_objc_id nsobject_default_center(sword3_objc_id self,
                                             sword3_objc_sel selector)
{
    (void)selector;
    return self;
}

static int looks_like_path(const char *path)
{
    uintptr_t address = (uintptr_t)path;
    size_t index;

    if (path == NULL || address < 4096)
        return 0;
    if (path[0] == '/')
        return 1;
    for (index = 0; index < 4096; ++index) {
        unsigned char c = (unsigned char)path[index];
        if (c == '\0')
            return index > 0;
        if (c < 32 || c == 127)
            return 0;
    }
    return 0;
}

static const char *object_path(sword3_objc_id object)
{
    sword3_objc_imp imp;
    const char *path;
    struct constant_string {
        void *isa;
        uint32_t flags;
        uint32_t reserved;
        const char *bytes;
    };
    struct audio_blob *blob;

    if (object == NULL)
        return NULL;
    imp = sword3_objc_lookup_imp(object, "UTF8String", NULL);
    if (imp != NULL && imp != (sword3_objc_imp)nsobject_init) {
        path = ((const char *(*)(sword3_objc_id, sword3_objc_sel))imp)(
            object, "UTF8String"
        );
        if (looks_like_path(path))
            return path;
    }
    imp = sword3_objc_lookup_imp(object, "fileSystemRepresentation", NULL);
    if (imp != NULL && imp != (sword3_objc_imp)nsobject_init) {
        path = ((const char *(*)(sword3_objc_id, sword3_objc_sel))imp)(
            object, "fileSystemRepresentation"
        );
        if (looks_like_path(path))
            return path;
    }
    blob = (struct audio_blob *)object;
    if (looks_like_path(blob->path))
        return blob->path;
    path = ((const struct constant_string *)object)->bytes;
    if (looks_like_path(path) && path[0] == '/')
        return path;
    return NULL;
}

static void blob_set_path(struct audio_blob *blob, const char *path)
{
    char *copy;
    size_t length;

    if (blob == NULL || path == NULL)
        return;
    length = strlen(path);
    copy = malloc(length + 1);
    if (copy == NULL)
        return;
    memcpy(copy, path, length + 1);
    free(blob->path);
    blob->path = copy;
    free(blob->bytes);
    blob->bytes = NULL;
    blob->length = 0;
    if (is_video_path(copy)) {
        free(g_pending_video_path);
        g_pending_video_path = malloc(length + 1);
        if (g_pending_video_path != NULL)
            memcpy(g_pending_video_path, copy, length + 1);
    }
}

static sword3_objc_id nsobject_file_url(sword3_objc_id self,
                                       sword3_objc_sel selector,
                                       sword3_objc_id path)
{
    (void)self;
    (void)selector;
    return path;
}

static sword3_objc_id nsobject_init_with_url(sword3_objc_id self,
                                            sword3_objc_sel selector,
                                            sword3_objc_id url)
{
    const char *path;

    (void)selector;
    if (self == NULL)
        return NULL;
    path = object_path(url);
    if (path == NULL)
        return self;
    blob_set_path((struct audio_blob *)self, path);
    write_diagnostic("[sword3-objc-shim] audio URL ", path);
    return self;
}

static sword3_objc_id nsobject_set_url(sword3_objc_id self,
                                      sword3_objc_sel selector,
                                      sword3_objc_id url)
{
    const char *path;

    (void)selector;
    path = object_path(url);
    if (path == NULL)
        return NULL;
    blob_set_path((struct audio_blob *)self, path);
    write_diagnostic("[sword3-objc-shim] AVAudioPlayer Set: ", path);
    /* Guest tests BOOL bit 0; a pointer return looks like NO. */
    return (sword3_objc_id)(uintptr_t)1;
}

static sword3_objc_id nsobject_init_with_data(sword3_objc_id self,
                                             sword3_objc_sel selector,
                                             sword3_objc_id data)
{
    struct audio_blob *blob = (struct audio_blob *)self;
    struct audio_blob *source = (struct audio_blob *)data;

    (void)selector;
    if (blob == NULL || source == NULL)
        return self;
    if (looks_like_path(source->path))
        blob_set_path(blob, source->path);
    else if (source->bytes != NULL && source->length > 0) {
        void *copy = malloc(source->length);
        if (copy == NULL)
            return self;
        memcpy(copy, source->bytes, source->length);
        free(blob->bytes);
        free(blob->path);
        blob->path = NULL;
        blob->bytes = copy;
        blob->length = source->length;
        write_diagnostic(
            "[sword3-objc-shim] AVAudioPlayer initWithData bytes",
            ""
        );
    }
    return self;
}

static sword3_objc_id nsobject_data_with_bytes(sword3_objc_id cls,
                                              sword3_objc_sel selector,
                                              const void *bytes,
                                              intptr_t length)
{
    struct audio_blob *blob;
    void *copy;

    (void)selector;
    if (cls == NULL || bytes == NULL || length <= 0)
        return NULL;
    blob = calloc(1, 64);
    if (blob == NULL)
        return NULL;
    copy = malloc((size_t)length);
    if (copy == NULL) {
        free(blob);
        return NULL;
    }
    memcpy(copy, bytes, (size_t)length);
    blob->isa = (sword3_objc_Class)cls;
    blob->bytes = copy;
    blob->length = (size_t)length;
    return blob;
}

static void nsobject_set_loops(sword3_objc_id self,
                              sword3_objc_sel selector,
                              intptr_t loops)
{
    (void)selector;
    if (self != NULL)
        ((struct audio_blob *)self)->loops = (int)loops;
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

struct nsobject_method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_method methods[32];
};

struct nsobject_class_method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_method methods[1];
};

static const struct nsobject_method_list nsobject_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 29,
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
            .name = "setBackgroundColor:",
            .types = "v24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_ignore,
        },
        {
            .name = "respondsToSelector:",
            .types = "B24@0:8:16",
            .imp = (sword3_objc_imp)nsobject_responds,
        },
        {
            .name = "clearColor",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_init,
        },
        {
            .name = "whiteColor",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_init,
        },
        {
            .name = "colorWithAlphaComponent:",
            .types = "@24@0:8d16",
            .imp = (sword3_objc_imp)nsobject_init,
        },
        {
            .name = "setTextColor:",
            .types = "v24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_ignore,
        },
        {
            .name = "textColor",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_init,
        },
        {
            .name = "systemFontOfSize:",
            .types = "@24@0:8d16",
            .imp = (sword3_objc_imp)nsobject_init,
        },
        {
            .name = "setText:",
            .types = "v24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_ignore,
        },
        {
            .name = "addSubview:",
            .types = "v24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_ignore,
        },
        {
            .name = "addObserver:selector:name:object:",
            .types = "v40@0:8@16:24@32@40",
            .imp = (sword3_objc_imp)nsobject_add_observer,
        },
        {
            .name = "play",
            .types = "v16@0:8",
            .imp = (sword3_objc_imp)nsobject_play,
        },
        {
            .name = "defaultCenter",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_default_center,
        },
        {
            .name = "initWithURL:",
            .types = "@24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_init_with_url,
        },
        {
            .name = "fileURLWithPath:",
            .types = "@24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_file_url,
        },
        {
            .name = "initWithContentsOfURL:options:error:",
            .types = "@40@0:8@16Q24^@32",
            .imp = (sword3_objc_imp)nsobject_init_with_url,
        },
        {
            .name = "setNumberOfLoops:",
            .types = "v24@0:8q16",
            .imp = (sword3_objc_imp)nsobject_set_loops,
        },
        {
            .name = "initFileURLWithPath:",
            .types = "@24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_init_with_url,
        },
        {
            .name = "Set:",
            .types = "B24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_set_url,
        },
        {
            .name = "initWithData:error:",
            .types = "@32@0:8@16^@24",
            .imp = (sword3_objc_imp)nsobject_init_with_data,
        },
        {
            .name = "dataWithBytes:length:",
            .types = "@32@0:8*16Q24",
            .imp = (sword3_objc_imp)nsobject_data_with_bytes,
        },
        {
            .name = "setPan:",
            .types = "v20@0:8f16",
            .imp = (sword3_objc_imp)nsobject_ignore,
        },
        {
            .name = "stop",
            .types = "v16@0:8",
            .imp = (sword3_objc_imp)nsobject_stop,
        },
        {
            .name = "pause",
            .types = "v16@0:8",
            .imp = (sword3_objc_imp)nsobject_pause,
        },
        {
            .name = "setPlayer:",
            .types = "v24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_set_player,
        },
        {
            .name = "player",
            .types = "@16@0:8",
            .imp = (sword3_objc_imp)nsobject_player,
        },
        {
            .name = "setDelegate:",
            .types = "v24@0:8@16",
            .imp = (sword3_objc_imp)nsobject_ignore,
        },
    },
};

static const struct nsobject_class_method_list nsobject_class_methods = {
    .entsize_and_flags = sizeof(struct sword3_objc_method),
    .count = 1,
    .methods = {
        {
            .name = "instancesRespondToSelector:",
            .types = "B24@0:8:16",
            .imp = (sword3_objc_imp)nsobject_instances_respond,
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
    return (sword3_objc_imp)nsobject_init;
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
    const struct sword3_objc_class_ro *ro;
    size_t size;
    sword3_objc_id object;

    if (cls == NULL)
        return NULL;
    ro = class_ro(cls);
    /*
     * Imported UIKit/AVFoundation classes are 64-byte zero blobs with no
     * class_ro.  Give them a dummy instance so [[AVPlayer alloc] initWithURL:]
     * is not a nil receiver; otherwise play never runs and the intro hangs.
     */
    if (!aligned_instance_size(ro, &size))
        size = 64;
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
