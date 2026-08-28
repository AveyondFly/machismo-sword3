#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <GLES2/gl2.h>

/*
 * OpenGLES.framework is a single namespace on iOS, while Linux exposes GLES1
 * and GLES2 as separate DSOs.  This bridge is linked --no-as-needed against
 * both, allowing Machismo's dlsym(handle, name) lookup to search both
 * dependencies.  The explicit resolver is also used by diagnostics and for
 * extension entry points that only EGL advertises.
 */

typedef void *(*egl_get_proc_address_fn)(const char *);

static void *gles2_handle;
static void *gles1_handle;
static void *egl_handle;

__attribute__((visibility("default"), aligned(16)))
unsigned char sword3_eagl_class[64] __asm__("OBJC_CLASS_$_EAGLContext");
__attribute__((visibility("default"), aligned(16)))
unsigned char sword3_eagl_metaclass[64] __asm__("OBJC_METACLASS_$_EAGLContext");

#define EAGL_STRING_SYMBOL(identifier, exported_name) \
	__attribute__((visibility("default"))) \
	void *identifier __asm__(exported_name)
EAGL_STRING_SYMBOL(sword3_eagl_rgb565, "kEAGLColorFormatRGB565");
EAGL_STRING_SYMBOL(sword3_eagl_rgba8, "kEAGLColorFormatRGBA8");
EAGL_STRING_SYMBOL(sword3_eagl_srgba8, "kEAGLColorFormatSRGBA8");
EAGL_STRING_SYMBOL(sword3_eagl_color_property,
		   "kEAGLDrawablePropertyColorFormat");
EAGL_STRING_SYMBOL(sword3_eagl_retained_property,
		   "kEAGLDrawablePropertyRetainedBacking");

static void open_backends(void)
{
	if (!gles2_handle)
		gles2_handle = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
	if (!gles1_handle)
		gles1_handle = dlopen("libGLESv1_CM.so.1", RTLD_NOW | RTLD_GLOBAL);
	if (!egl_handle)
		egl_handle = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
}

__attribute__((visibility("default")))
void *sword3_gl_bridge_resolve(const char *name)
{
	void *symbol = NULL;
	egl_get_proc_address_fn get_proc;

	if (!name || !*name)
		return NULL;
	open_backends();

	if (gles2_handle)
		symbol = dlsym(gles2_handle, name);
	if (!symbol && gles1_handle)
		symbol = dlsym(gles1_handle, name);
	if (!symbol && egl_handle)
		symbol = dlsym(egl_handle, name);
	if (symbol)
		return symbol;

	get_proc = egl_handle
		? (egl_get_proc_address_fn)dlsym(egl_handle, "eglGetProcAddress")
		: NULL;
	return get_proc ? get_proc(name) : NULL;
}

/* GLES1 exposes framebuffer and blend extensions with OES suffixes while
 * Mesa's GLES2 library exports the equivalent core entry points. */
__attribute__((visibility("default")))
void glBindFramebufferOES(GLenum target, GLuint framebuffer)
{
	glBindFramebuffer(target, framebuffer);
}

__attribute__((visibility("default")))
void glBlendEquationOES(GLenum mode)
{
	glBlendEquation(mode);
}

__attribute__((visibility("default")))
void glBlendEquationSeparateOES(GLenum rgb, GLenum alpha)
{
	glBlendEquationSeparate(rgb, alpha);
}

__attribute__((visibility("default")))
void glBlendFuncSeparateOES(GLenum src_rgb, GLenum dst_rgb,
			    GLenum src_alpha, GLenum dst_alpha)
{
	glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
}

__attribute__((visibility("default")))
GLenum glCheckFramebufferStatusOES(GLenum target)
{
	return glCheckFramebufferStatus(target);
}

__attribute__((visibility("default")))
void glDeleteFramebuffersOES(GLsizei count, const GLuint *framebuffers)
{
	glDeleteFramebuffers(count, framebuffers);
}

__attribute__((visibility("default")))
void glFramebufferTexture2DOES(GLenum target, GLenum attachment,
			       GLenum textarget, GLuint texture, GLint level)
{
	glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

__attribute__((visibility("default")))
void glGenFramebuffersOES(GLsizei count, GLuint *framebuffers)
{
	glGenFramebuffers(count, framebuffers);
}

__attribute__((visibility("default")))
void glDiscardFramebufferEXT(GLenum target, GLsizei count,
			     const GLenum *attachments)
{
	typedef void (*function_type)(GLenum, GLsizei, const GLenum *);
	static function_type function;
	static int resolved;

	if (!resolved) {
		function = (function_type)
			sword3_gl_bridge_resolve("glDiscardFramebufferEXT");
		resolved = 1;
	}
	/* Discard is an optimization; absence does not affect rendered output. */
	if (function)
		function(target, count, attachments);
}

__attribute__((visibility("default")))
void glDrawTexfOES(GLfloat x, GLfloat y, GLfloat z,
		   GLfloat width, GLfloat height)
{
	typedef void (*function_type)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
	static function_type function;
	static int resolved;

	if (!resolved) {
		function =
			(function_type)sword3_gl_bridge_resolve("glDrawTexfOES");
		resolved = 1;
	}
	if (function)
		function(x, y, z, width, height);
}

__attribute__((visibility("default")))
void glLabelObjectEXT(GLenum type, GLuint object, GLsizei length,
		      const GLchar *label)
{
	typedef void (*function_type)(GLenum, GLuint, GLsizei, const GLchar *);
	static function_type function;
	static int resolved;

	if (!resolved) {
		function =
			(function_type)sword3_gl_bridge_resolve("glLabelObjectEXT");
		resolved = 1;
	}
	/* Debug labels never alter rendering semantics. */
	if (function)
		function(type, object, length, label);
}

__attribute__((visibility("default")))
void glResolveMultisampleFramebufferAPPLE(void)
{
	typedef void (*function_type)(void);
	static function_type function;
	static int resolved;

	if (!resolved) {
		function = (function_type)sword3_gl_bridge_resolve(
			"glResolveMultisampleFramebufferAPPLE");
		resolved = 1;
	}
	if (function)
		function();
}

__attribute__((visibility("default")))
int sword3_gl_bridge_probe(void)
{
	static const char *required[] = {
		"glGetString",
		"glCreateShader",
		"glMatrixMode",
		"eglGetDisplay",
		NULL,
	};
	int missing = 0;

	for (size_t i = 0; required[i]; i++) {
		if (!sword3_gl_bridge_resolve(required[i])) {
			fprintf(stderr, "sword3-gl: missing required symbol %s\n",
			        required[i]);
			missing++;
		}
	}
	return missing == 0 ? 0 : -1;
}
