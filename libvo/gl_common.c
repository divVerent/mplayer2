/*
 * common OpenGL routines
 *
 * copyleft (C) 2005-2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 * Special thanks go to the xine team and Matthias Hopf, whose video_out_opengl.c
 * gave me lots of good ideas.
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * You can alternatively redistribute this file and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/**
 * \file gl_common.c
 * \brief OpenGL helper functions used by vo_gl.c and vo_gl2.c
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include "talloc.h"
#include "gl_common.h"
#include "csputils.h"
#include "aspect.h"
#include "pnm_loader.h"
#include "options.h"

//! \defgroup glgeneral OpenGL general helper functions

// GLU has this as gluErrorString (we don't use GLU, as it is legacy-OpenGL)
static const char *gl_error_to_string(GLenum error)
{
    switch (error) {
    case GL_INVALID_ENUM: return "INVALID_ENUM";
    case GL_INVALID_VALUE: return "INVALID_VALUE";
    case GL_INVALID_OPERATION: return "INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    default: return "unknown";
    }
}

void glCheckError(GL *gl, const char *info)
{
    for (;;) {
        GLenum error = gl->GetError();
        if (error == GL_NO_ERROR)
            break;
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] %s: OpenGL error %s.\n", info,
               gl_error_to_string(error));
    }
}

//! \defgroup glcontext OpenGL context management helper functions

//! \defgroup gltexture OpenGL texture handling helper functions

//! \defgroup glconversion OpenGL conversion helper functions

/**
 * \brief adjusts the GL_UNPACK_ALIGNMENT to fit the stride.
 * \param stride number of bytes per line for which alignment should fit.
 * \ingroup glgeneral
 */
void glAdjustAlignment(GL *gl, int stride)
{
    GLint gl_alignment;
    if (stride % 8 == 0)
        gl_alignment = 8;
    else if (stride % 4 == 0)
        gl_alignment = 4;
    else if (stride % 2 == 0)
        gl_alignment = 2;
    else
        gl_alignment = 1;
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, gl_alignment);
    gl->PixelStorei(GL_PACK_ALIGNMENT, gl_alignment);
}

//! always return this format as internal texture format in glFindFormat
#define TEXTUREFORMAT_ALWAYS GL_RGB8
#undef TEXTUREFORMAT_ALWAYS

/**
 * \brief find the OpenGL settings coresponding to format.
 *
 * All parameters may be NULL.
 * \param fmt MPlayer format to analyze.
 * \param bpp [OUT] bits per pixel of that format.
 * \param gl_texfmt [OUT] internal texture format that fits the
 * image format, not necessarily the best for performance.
 * \param gl_format [OUT] OpenGL format for this image format.
 * \param gl_type [OUT] OpenGL type for this image format.
 * \return 1 if format is supported by OpenGL, 0 if not.
 * \ingroup gltexture
 */
int glFindFormat(uint32_t fmt, int have_texture_rg, int *bpp, GLint *gl_texfmt,
                 GLenum *gl_format, GLenum *gl_type)
{
    int supported = 1;
    int dummy1;
    GLenum dummy2;
    GLint dummy3;
    if (!bpp)
        bpp = &dummy1;
    if (!gl_texfmt)
        gl_texfmt = &dummy3;
    if (!gl_format)
        gl_format = &dummy2;
    if (!gl_type)
        gl_type = &dummy2;

    if (mp_get_chroma_shift(fmt, NULL, NULL, NULL)) {
        // reduce the possible cases a bit
        if (IMGFMT_IS_YUVP16_LE(fmt))
            fmt = IMGFMT_420P16_LE;
        else if (IMGFMT_IS_YUVP16_BE(fmt))
            fmt = IMGFMT_420P16_BE;
        else
            fmt = IMGFMT_YV12;
    }

    *bpp = IMGFMT_IS_BGR(fmt) ? IMGFMT_BGR_DEPTH(fmt) : IMGFMT_RGB_DEPTH(fmt);
    *gl_texfmt = 3;
    switch (fmt) {
    case IMGFMT_RGB48NE:
        *gl_format = GL_RGB;
        *gl_type = GL_UNSIGNED_SHORT;
        break;
    case IMGFMT_RGB24:
        *gl_format = GL_RGB;
        *gl_type = GL_UNSIGNED_BYTE;
        break;
    case IMGFMT_RGBA:
        *gl_texfmt = 4;
        *gl_format = GL_RGBA;
        *gl_type = GL_UNSIGNED_BYTE;
        break;
    case IMGFMT_420P16:
        supported = 0; // no native YUV support
        *gl_texfmt = have_texture_rg ? GL_R16 : GL_LUMINANCE16;
        *bpp = 16;
        *gl_format = have_texture_rg ? GL_RED : GL_LUMINANCE;
        *gl_type = GL_UNSIGNED_SHORT;
        break;
    case IMGFMT_YV12:
        supported = 0; // no native YV12 support
    case IMGFMT_Y800:
    case IMGFMT_Y8:
        *gl_texfmt = 1;
        *bpp = 8;
        *gl_format = GL_LUMINANCE;
        *gl_type = GL_UNSIGNED_BYTE;
        break;
    case IMGFMT_UYVY:
    // IMGFMT_YUY2 would be more logical for the _REV format,
    // but gives clearly swapped colors.
    case IMGFMT_YVYU:
        *gl_texfmt = GL_YCBCR_MESA;
        *bpp = 16;
        *gl_format = GL_YCBCR_MESA;
        *gl_type = fmt == IMGFMT_UYVY ? GL_UNSIGNED_SHORT_8_8 : GL_UNSIGNED_SHORT_8_8_REV;
        break;
#if 0
    // we do not support palettized formats, although the format the
    // swscale produces works
    case IMGFMT_RGB8:
        *gl_format = GL_RGB;
        *gl_type = GL_UNSIGNED_BYTE_2_3_3_REV;
        break;
#endif
    case IMGFMT_RGB15:
        *gl_format = GL_RGBA;
        *gl_type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
        break;
    case IMGFMT_RGB16:
        *gl_format = GL_RGB;
        *gl_type = GL_UNSIGNED_SHORT_5_6_5_REV;
        break;
#if 0
    case IMGFMT_BGR8:
        // special case as red and blue have a different number of bits.
        // GL_BGR and GL_UNSIGNED_BYTE_3_3_2 isn't supported at least
        // by nVidia drivers, and in addition would give more bits to
        // blue than to red, which isn't wanted
        *gl_format = GL_RGB;
        *gl_type = GL_UNSIGNED_BYTE_3_3_2;
        break;
#endif
    case IMGFMT_BGR15:
        *gl_format = GL_BGRA;
        *gl_type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
        break;
    case IMGFMT_BGR16:
        *gl_format = GL_RGB;
        *gl_type = GL_UNSIGNED_SHORT_5_6_5;
        break;
    case IMGFMT_BGR24:
        *gl_format = GL_BGR;
        *gl_type = GL_UNSIGNED_BYTE;
        break;
    case IMGFMT_BGRA:
        *gl_texfmt = 4;
        *gl_format = GL_BGRA;
        *gl_type = GL_UNSIGNED_BYTE;
        break;
    default:
        *gl_texfmt = 4;
        *gl_format = GL_RGBA;
        *gl_type = GL_UNSIGNED_BYTE;
        supported = 0;
    }
#ifdef TEXTUREFORMAT_ALWAYS
    *gl_texfmt = TEXTUREFORMAT_ALWAYS;
#endif
    return supported;
}

#ifdef HAVE_LIBDL
#include <dlfcn.h>
#endif
/**
 * \brief find address of a linked function
 * \param s name of function to find
 * \return address of function or NULL if not found
 */
static void *getdladdr(const char *s)
{
    void *ret = NULL;
#ifdef HAVE_LIBDL
    void *handle = dlopen(NULL, RTLD_LAZY);
    if (!handle)
        return NULL;
    ret = dlsym(handle, s);
    dlclose(handle);
#endif
    return ret;
}

typedef struct {
    ptrdiff_t offset;           // offset to the function pointer in struct GL
    const char *extstr;
    const char *funcnames[7];
    void *fallback;
    bool is_gl3;
} extfunc_desc_t;

#define DEF_FUNC_DESC(name) \
    {offsetof(GL, name), NULL, {"gl" # name}, gl ## name}
#define DEF_EXT_FUNCS(...) __VA_ARGS__
#define DEF_EXT_DESC(name, ext, funcnames) \
    {offsetof(GL, name), ext, {DEF_EXT_FUNCS funcnames}}
// These are mostly handled the same, but needed because at least the MESA
// headers don't define any function prototypes for these.
#define DEF_GL3_DESC(name) \
    {offsetof(GL, name), NULL, {"gl" # name}, NULL, .is_gl3 = true}

static const extfunc_desc_t extfuncs[] = {
    // these aren't extension functions but we query them anyway to allow
    // different "backends" with one binary
    DEF_FUNC_DESC(Viewport),
    DEF_FUNC_DESC(Clear),
    DEF_FUNC_DESC(GenTextures),
    DEF_FUNC_DESC(DeleteTextures),
    DEF_FUNC_DESC(TexEnvi),
    DEF_FUNC_DESC(ClearColor),
    DEF_FUNC_DESC(Enable),
    DEF_FUNC_DESC(Disable),
    DEF_FUNC_DESC(DrawBuffer),
    DEF_FUNC_DESC(DepthMask),
    DEF_FUNC_DESC(BlendFunc),
    DEF_FUNC_DESC(Flush),
    DEF_FUNC_DESC(Finish),
    DEF_FUNC_DESC(PixelStorei),
    DEF_FUNC_DESC(TexImage1D),
    DEF_FUNC_DESC(TexImage2D),
    DEF_FUNC_DESC(TexSubImage2D),
    DEF_FUNC_DESC(GetTexImage),
    DEF_FUNC_DESC(TexParameteri),
    DEF_FUNC_DESC(TexParameterf),
    DEF_FUNC_DESC(TexParameterfv),
    DEF_FUNC_DESC(GetIntegerv),
    DEF_FUNC_DESC(GetBooleanv),
    DEF_FUNC_DESC(ColorMask),
    DEF_FUNC_DESC(ReadPixels),
    DEF_FUNC_DESC(ReadBuffer),
    DEF_FUNC_DESC(DrawArrays),
    DEF_FUNC_DESC(GetString),
    DEF_FUNC_DESC(GetError),

    // legacy GL functions (1.x - 2.x)
    DEF_FUNC_DESC(Begin),
    DEF_FUNC_DESC(End),
    DEF_FUNC_DESC(MatrixMode),
    DEF_FUNC_DESC(LoadIdentity),
    DEF_FUNC_DESC(Translated),
    DEF_FUNC_DESC(Scaled),
    DEF_FUNC_DESC(Ortho),
    DEF_FUNC_DESC(PushMatrix),
    DEF_FUNC_DESC(PopMatrix),
    DEF_FUNC_DESC(GenLists),
    DEF_FUNC_DESC(DeleteLists),
    DEF_FUNC_DESC(NewList),
    DEF_FUNC_DESC(EndList),
    DEF_FUNC_DESC(CallList),
    DEF_FUNC_DESC(CallLists),
    DEF_FUNC_DESC(Color4ub),
    DEF_FUNC_DESC(Color4f),
    DEF_FUNC_DESC(TexCoord2f),
    DEF_FUNC_DESC(TexCoord2fv),
    DEF_FUNC_DESC(Vertex2f),
    DEF_FUNC_DESC(VertexPointer),
    DEF_FUNC_DESC(ColorPointer),
    DEF_FUNC_DESC(TexCoordPointer),
    DEF_FUNC_DESC(EnableClientState),
    DEF_FUNC_DESC(DisableClientState),

    // OpenGL extension functions
    DEF_EXT_DESC(GenBuffers, NULL,
                 ("glGenBuffers", "glGenBuffersARB")),
    DEF_EXT_DESC(DeleteBuffers, NULL,
                 ("glDeleteBuffers", "glDeleteBuffersARB")),
    DEF_EXT_DESC(BindBuffer, NULL,
                 ("glBindBuffer", "glBindBufferARB")),
    DEF_EXT_DESC(MapBuffer, NULL,
                 ("glMapBuffer", "glMapBufferARB")),
    DEF_EXT_DESC(UnmapBuffer, NULL,
                 ("glUnmapBuffer", "glUnmapBufferARB")),
    DEF_EXT_DESC(BufferData, NULL,
                 ("glBufferData", "glBufferDataARB")),
    DEF_EXT_DESC(ActiveTexture, NULL,
                 ("glActiveTexture", "glActiveTextureARB")),
    DEF_EXT_DESC(BindTexture, NULL,
                 ("glBindTexture", "glBindTextureARB", "glBindTextureEXT")),
    DEF_EXT_DESC(MultiTexCoord2f, NULL,
                 ("glMultiTexCoord2f", "glMultiTexCoord2fARB")),
    DEF_EXT_DESC(GenPrograms, "_program",
                 ("glGenProgramsARB")),
    DEF_EXT_DESC(DeletePrograms, "_program",
                 ("glDeleteProgramsARB")),
    DEF_EXT_DESC(BindProgram, "_program",
                 ("glBindProgramARB")),
    DEF_EXT_DESC(ProgramString, "_program",
                 ("glProgramStringARB")),
    DEF_EXT_DESC(GetProgramivARB, "_program",
                 ("glGetProgramivARB")),
    DEF_EXT_DESC(ProgramEnvParameter4f, "_program",
                 ("glProgramEnvParameter4fARB")),
    DEF_EXT_DESC(SwapInterval, "_swap_control",
                 ("glXSwapIntervalSGI", "glXSwapInterval", "wglSwapIntervalSGI",
                  "wglSwapInterval", "wglSwapIntervalEXT")),
    DEF_EXT_DESC(TexImage3D, NULL,
                 ("glTexImage3D")),

    // ancient ATI extensions
    DEF_EXT_DESC(BeginFragmentShader, "ATI_fragment_shader",
                 ("glBeginFragmentShaderATI")),
    DEF_EXT_DESC(EndFragmentShader, "ATI_fragment_shader",
                 ("glEndFragmentShaderATI")),
    DEF_EXT_DESC(SampleMap, "ATI_fragment_shader",
                 ("glSampleMapATI")),
    DEF_EXT_DESC(ColorFragmentOp2, "ATI_fragment_shader",
                 ("glColorFragmentOp2ATI")),
    DEF_EXT_DESC(ColorFragmentOp3, "ATI_fragment_shader",
                 ("glColorFragmentOp3ATI")),
    DEF_EXT_DESC(SetFragmentShaderConstant, "ATI_fragment_shader",
                 ("glSetFragmentShaderConstantATI")),

    // GL 3, possibly in GL 2.x as well in form of extensions
    DEF_GL3_DESC(GenBuffers),
    DEF_GL3_DESC(DeleteBuffers),
    DEF_GL3_DESC(BindBuffer),
    DEF_GL3_DESC(MapBuffer),
    DEF_GL3_DESC(UnmapBuffer),
    DEF_GL3_DESC(BufferData),
    DEF_GL3_DESC(ActiveTexture),
    DEF_GL3_DESC(BindTexture),
    DEF_GL3_DESC(GenVertexArrays),
    DEF_GL3_DESC(BindVertexArray),
    DEF_GL3_DESC(GetAttribLocation),
    DEF_GL3_DESC(EnableVertexAttribArray),
    DEF_GL3_DESC(DisableVertexAttribArray),
    DEF_GL3_DESC(VertexAttribPointer),
    DEF_GL3_DESC(DeleteVertexArrays),
    DEF_GL3_DESC(UseProgram),
    DEF_GL3_DESC(GetUniformLocation),
    DEF_GL3_DESC(CompileShader),
    DEF_GL3_DESC(CreateProgram),
    DEF_GL3_DESC(CreateShader),
    DEF_GL3_DESC(ShaderSource),
    DEF_GL3_DESC(LinkProgram),
    DEF_GL3_DESC(AttachShader),
    DEF_GL3_DESC(DeleteShader),
    DEF_GL3_DESC(DeleteProgram),
    DEF_GL3_DESC(GetShaderInfoLog),
    DEF_GL3_DESC(GetShaderiv),
    DEF_GL3_DESC(GetProgramInfoLog),
    DEF_GL3_DESC(GetProgramiv),
    DEF_GL3_DESC(GetStringi),
    DEF_GL3_DESC(BindAttribLocation),
    DEF_GL3_DESC(BindFramebuffer),
    DEF_GL3_DESC(GenFramebuffers),
    DEF_GL3_DESC(DeleteFramebuffers),
    DEF_GL3_DESC(CheckFramebufferStatus),
    DEF_GL3_DESC(FramebufferTexture2D),
    DEF_GL3_DESC(Uniform1f),
    DEF_GL3_DESC(Uniform3f),
    DEF_GL3_DESC(Uniform1i),
    DEF_GL3_DESC(UniformMatrix3fv),
    DEF_GL3_DESC(UniformMatrix4x3fv),

    {-1}
};

/**
 * \brief find the function pointers of some useful OpenGL extensions
 * \param getProcAddress function to resolve function names, may be NULL
 * \param ext2 an extra extension string
 */
static void getFunctions(GL *gl, void *(*getProcAddress)(const GLubyte *),
                         const char *ext2, bool is_gl3)
{
    const extfunc_desc_t *dsc;
    char *allexts = talloc_strdup(NULL, ext2 ? ext2 : "");

    *gl = (GL) {0};

    if (!getProcAddress)
        getProcAddress = (void *)getdladdr;

    if (is_gl3) {
        gl->GetStringi = getProcAddress("glGetStringi");
        gl->GetIntegerv = getProcAddress("glGetIntegerv");

        if (!(gl->GetStringi && gl->GetIntegerv))
            return;

        GLint exts;
        gl->GetIntegerv(GL_NUM_EXTENSIONS, &exts);
        for (int n = 0; n < exts; n++) {
            allexts = talloc_asprintf_append(allexts, " %s",
                                             gl->GetStringi(GL_EXTENSIONS, n));
        }
    } else {
        gl->GetString = getProcAddress("glGetString");
        if (!gl->GetString)
            gl->GetString = glGetString;
        const char *ext = (char*)gl->GetString(GL_EXTENSIONS);
        allexts = talloc_asprintf_append(allexts, " %s", ext);
    }

    mp_msg(MSGT_VO, MSGL_DBG2, "OpenGL extensions string:\n%s\n", allexts);
    for (dsc = extfuncs; dsc->offset >= 0; dsc++) {
        void *ptr = NULL;
        if (!dsc->extstr || strstr(allexts, dsc->extstr)) {
            for (int i = 0; !ptr && dsc->funcnames[i]; i++)
                ptr = getProcAddress((const GLubyte *)dsc->funcnames[i]);
        }
        if (!ptr)
            ptr = dsc->fallback;
        if (!ptr && !dsc->extstr && (!dsc->is_gl3 || is_gl3))
            mp_msg(MSGT_VO, MSGL_WARN, "[gl] OpenGL function not found: %s\n",
                   dsc->funcnames[0]);
        void **funcptr = (void**)(((char*)gl) + dsc->offset);
        *funcptr = ptr;
    }
    talloc_free(allexts);
}

/**
 * \brief create a texture and set some defaults
 * \param target texture taget, usually GL_TEXTURE_2D
 * \param fmt internal texture format
 * \param format texture host data format
 * \param type texture host data type
 * \param filter filter used for scaling, e.g. GL_LINEAR
 * \param w texture width
 * \param h texture height
 * \param val luminance value to fill texture with
 * \ingroup gltexture
 */
void glCreateClearTex(GL *gl, GLenum target, GLenum fmt, GLenum format,
                      GLenum type, GLint filter, int w, int h,
                      unsigned char val)
{
    GLfloat fval = (GLfloat)val / 255.0;
    GLfloat border[4] = {
        fval, fval, fval, fval
    };
    int stride;
    char *init;
    if (w == 0)
        w = 1;
    if (h == 0)
        h = 1;
    stride = w * glFmt2bpp(format, type);
    if (!stride)
        return;
    init = malloc(stride * h);
    memset(init, val, stride * h);
    glAdjustAlignment(gl, stride);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, w);
    gl->TexImage2D(target, 0, fmt, w, h, 0, format, type, init);
    gl->TexParameterf(target, GL_TEXTURE_PRIORITY, 1.0);
    gl->TexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
    gl->TexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
    gl->TexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Border texels should not be used with CLAMP_TO_EDGE
    // We set a sane default anyway.
    gl->TexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border);
    free(init);
}

static GLint detect_hqtexfmt(GL *gl)
{
    const char *extensions = (const char *)gl->GetString(GL_EXTENSIONS);
    if (strstr(extensions, "_texture_float"))
        return GL_RGB32F;
    else if (strstr(extensions, "NV_float_buffer"))
        return GL_FLOAT_RGB32_NV;
    return GL_RGB16;
}

/**
 * \brief creates a texture from a PPM file
 * \param target texture taget, usually GL_TEXTURE_2D
 * \param fmt internal texture format, 0 for default
 * \param filter filter used for scaling, e.g. GL_LINEAR
 * \param f file to read PPM from
 * \param width [out] width of texture
 * \param height [out] height of texture
 * \param maxval [out] maxval value from PPM file
 * \return 0 on error, 1 otherwise
 * \ingroup gltexture
 */
int glCreatePPMTex(GL *gl, GLenum target, GLenum fmt, GLint filter,
                   FILE *f, int *width, int *height, int *maxval)
{
    int w, h, m, bpp;
    GLenum type;
    uint8_t *data = read_pnm(f, &w, &h, &bpp, &m);
    GLint hqtexfmt = detect_hqtexfmt(gl);
    if (!data || (bpp != 3 && bpp != 6)) {
        free(data);
        return 0;
    }
    if (!fmt) {
        fmt = bpp == 6 ? hqtexfmt : 3;
        if (fmt == GL_FLOAT_RGB32_NV && target != GL_TEXTURE_RECTANGLE)
            fmt = GL_RGB16;
    }
    type = bpp == 6 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    glCreateClearTex(gl, target, fmt, GL_RGB, type, filter, w, h, 0);
    glUploadTex(gl, target, GL_RGB, type,
                data, w * bpp, 0, 0, w, h, 0);
    free(data);
    if (width)
        *width = w;
    if (height)
        *height = h;
    if (maxval)
        *maxval = m;
    return 1;
}

/**
 * \brief return the number of bytes per pixel for the given format
 * \param format OpenGL format
 * \param type OpenGL type
 * \return bytes per pixel
 * \ingroup glgeneral
 *
 * Does not handle all possible variants, just those used by MPlayer
 */
int glFmt2bpp(GLenum format, GLenum type)
{
    int component_size = 0;
    switch (type) {
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
        return 1;
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
        return 2;
    case GL_UNSIGNED_BYTE:
        component_size = 1;
        break;
    case GL_UNSIGNED_SHORT:
        component_size = 2;
        break;
    }
    switch (format) {
    case GL_LUMINANCE:
    case GL_ALPHA:
        return component_size;
    case GL_YCBCR_MESA:
        return 2;
    case GL_RGB:
    case GL_BGR:
        return 3 * component_size;
    case GL_RGBA:
    case GL_BGRA:
        return 4 * component_size;
    case GL_RED:
        return component_size;
    case GL_RG:
    case GL_LUMINANCE_ALPHA:
        return 2 * component_size;
    }
    return 0; // unknown
}

/**
 * \brief upload a texture, handling things like stride and slices
 * \param target texture target, usually GL_TEXTURE_2D
 * \param format OpenGL format of data
 * \param type OpenGL type of data
 * \param dataptr data to upload
 * \param stride data stride
 * \param x x offset in texture
 * \param y y offset in texture
 * \param w width of the texture part to upload
 * \param h height of the texture part to upload
 * \param slice height of an upload slice, 0 for all at once
 * \ingroup gltexture
 */
void glUploadTex(GL *gl, GLenum target, GLenum format, GLenum type,
                 const void *dataptr, int stride,
                 int x, int y, int w, int h, int slice)
{
    const uint8_t *data = dataptr;
    int y_max = y + h;
    if (w <= 0 || h <= 0)
        return;
    if (slice <= 0)
        slice = h;
    if (stride < 0) {
        data += (h - 1) * stride;
        stride = -stride;
    }
    // this is not always correct, but should work for MPlayer
    glAdjustAlignment(gl, stride);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, stride / glFmt2bpp(format, type));
    for (; y + slice <= y_max; y += slice) {
        gl->TexSubImage2D(target, 0, x, y, w, slice, format, type, data);
        data += stride * slice;
    }
    if (y < y_max)
        gl->TexSubImage2D(target, 0, x, y, w, y_max - y, format, type, data);
}

/**
 * \brief download a texture, handling things like stride and slices
 * \param target texture target, usually GL_TEXTURE_2D
 * \param format OpenGL format of data
 * \param type OpenGL type of data
 * \param dataptr destination memory for download
 * \param stride data stride (must be positive)
 * \ingroup gltexture
 */
void glDownloadTex(GL *gl, GLenum target, GLenum format, GLenum type,
                   void *dataptr, int stride)
{
    // this is not always correct, but should work for MPlayer
    glAdjustAlignment(gl, stride);
    gl->PixelStorei(GL_PACK_ROW_LENGTH, stride / glFmt2bpp(format, type));
    gl->GetTexImage(target, 0, format, type, dataptr);
}

/**
 * \brief Setup ATI version of register combiners for YUV to RGB conversion.
 * \param csp_params parameters used for colorspace conversion
 * \param text if set use the GL_ATI_text_fragment_shader API as
 *             used on OS X.
 */
static void glSetupYUVFragmentATI(GL *gl, struct mp_csp_params *csp_params,
                                  int text)
{
    GLint i;
    float yuv2rgb[3][4];

    gl->GetIntegerv(GL_MAX_TEXTURE_UNITS, &i);
    if (i < 3)
        mp_msg(MSGT_VO, MSGL_ERR,
               "[gl] 3 texture units needed for YUV combiner (ATI) support (found %i)\n", i);

    mp_get_yuv2rgb_coeffs(csp_params, yuv2rgb);
    for (i = 0; i < 3; i++) {
        int j;
        yuv2rgb[i][3] -= -0.5 * (yuv2rgb[i][1] + yuv2rgb[i][2]);
        for (j = 0; j < 4; j++) {
            yuv2rgb[i][j] *= 0.125;
            yuv2rgb[i][j] += 0.5;
            if (yuv2rgb[i][j] > 1)
                yuv2rgb[i][j] = 1;
            if (yuv2rgb[i][j] < 0)
                yuv2rgb[i][j] = 0;
        }
    }
    if (text == 0) {
        GLfloat c0[4] = { yuv2rgb[0][0], yuv2rgb[1][0], yuv2rgb[2][0] };
        GLfloat c1[4] = { yuv2rgb[0][1], yuv2rgb[1][1], yuv2rgb[2][1] };
        GLfloat c2[4] = { yuv2rgb[0][2], yuv2rgb[1][2], yuv2rgb[2][2] };
        GLfloat c3[4] = { yuv2rgb[0][3], yuv2rgb[1][3], yuv2rgb[2][3] };
        if (!gl->BeginFragmentShader || !gl->EndFragmentShader ||
            !gl->SetFragmentShaderConstant || !gl->SampleMap ||
            !gl->ColorFragmentOp2 || !gl->ColorFragmentOp3) {
            mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Combiner (ATI) functions missing!\n");
            return;
        }
        gl->GetIntegerv(GL_NUM_FRAGMENT_REGISTERS_ATI, &i);
        if (i < 3)
            mp_msg(MSGT_VO, MSGL_ERR,
                   "[gl] 3 registers needed for YUV combiner (ATI) support (found %i)\n", i);
        gl->BeginFragmentShader();
        gl->SetFragmentShaderConstant(GL_CON_0_ATI, c0);
        gl->SetFragmentShaderConstant(GL_CON_1_ATI, c1);
        gl->SetFragmentShaderConstant(GL_CON_2_ATI, c2);
        gl->SetFragmentShaderConstant(GL_CON_3_ATI, c3);
        gl->SampleMap(GL_REG_0_ATI, GL_TEXTURE0, GL_SWIZZLE_STR_ATI);
        gl->SampleMap(GL_REG_1_ATI, GL_TEXTURE1, GL_SWIZZLE_STR_ATI);
        gl->SampleMap(GL_REG_2_ATI, GL_TEXTURE2, GL_SWIZZLE_STR_ATI);
        gl->ColorFragmentOp2(GL_MUL_ATI, GL_REG_1_ATI, GL_NONE, GL_NONE,
                             GL_REG_1_ATI, GL_NONE, GL_BIAS_BIT_ATI,
                             GL_CON_1_ATI, GL_NONE, GL_BIAS_BIT_ATI);
        gl->ColorFragmentOp3(GL_MAD_ATI, GL_REG_2_ATI, GL_NONE, GL_NONE,
                             GL_REG_2_ATI, GL_NONE, GL_BIAS_BIT_ATI,
                             GL_CON_2_ATI, GL_NONE, GL_BIAS_BIT_ATI,
                             GL_REG_1_ATI, GL_NONE, GL_NONE);
        gl->ColorFragmentOp3(GL_MAD_ATI, GL_REG_0_ATI, GL_NONE, GL_NONE,
                             GL_REG_0_ATI, GL_NONE, GL_NONE,
                             GL_CON_0_ATI, GL_NONE, GL_BIAS_BIT_ATI,
                             GL_REG_2_ATI, GL_NONE, GL_NONE);
        gl->ColorFragmentOp2(GL_ADD_ATI, GL_REG_0_ATI, GL_NONE, GL_8X_BIT_ATI,
                             GL_REG_0_ATI, GL_NONE, GL_NONE,
                             GL_CON_3_ATI, GL_NONE, GL_BIAS_BIT_ATI);
        gl->EndFragmentShader();
    } else {
        static const char template[] =
            "!!ATIfs1.0\n"
            "StartConstants;\n"
            "  CONSTANT c0 = {%e, %e, %e};\n"
            "  CONSTANT c1 = {%e, %e, %e};\n"
            "  CONSTANT c2 = {%e, %e, %e};\n"
            "  CONSTANT c3 = {%e, %e, %e};\n"
            "EndConstants;\n"
            "StartOutputPass;\n"
            "  SampleMap r0, t0.str;\n"
            "  SampleMap r1, t1.str;\n"
            "  SampleMap r2, t2.str;\n"
            "  MUL r1.rgb, r1.bias, c1.bias;\n"
            "  MAD r2.rgb, r2.bias, c2.bias, r1;\n"
            "  MAD r0.rgb, r0, c0.bias, r2;\n"
            "  ADD r0.rgb.8x, r0, c3.bias;\n"
            "EndPass;\n";
        char buffer[512];
        snprintf(buffer, sizeof(buffer), template,
                 yuv2rgb[0][0], yuv2rgb[1][0], yuv2rgb[2][0],
                 yuv2rgb[0][1], yuv2rgb[1][1], yuv2rgb[2][1],
                 yuv2rgb[0][2], yuv2rgb[1][2], yuv2rgb[2][2],
                 yuv2rgb[0][3], yuv2rgb[1][3], yuv2rgb[2][3]);
        mp_msg(MSGT_VO, MSGL_DBG2, "[gl] generated fragment program:\n%s\n",
               buffer);
        loadGPUProgram(gl, GL_TEXT_FRAGMENT_SHADER_ATI, buffer);
    }
}

// Replace all occurances of variables named "$"+name (e.g. $foo) in *text with
// replace, and return the result. *text must have been allocated with talloc.
static void replace_var_str(char **text, const char *name, const char *replace)
{
    size_t namelen = strlen(name);
    char *nextvar = *text;
    void *parent = talloc_parent(*text);
    for (;;) {
        nextvar = strchr(nextvar, '$');
        if (!nextvar)
            break;
        char *until = nextvar;
        nextvar++;
        if (strncmp(nextvar, name, namelen) != 0)
            continue;
        nextvar += namelen;
        // try not to replace prefixes of other vars (e.g. $foo vs. $foo_bar)
        char term = nextvar[0];
        if (isalnum(term) || term == '_')
            continue;
        int prelength = until - *text;
        int postlength = nextvar - *text;
        char *n = talloc_asprintf(parent, "%.*s%s%s", prelength, *text, replace,
                                  nextvar);
        talloc_free(*text);
        *text = n;
        nextvar = *text + postlength;
    }
}

static void replace_var_float(char **text, const char *name, float replace)
{
    char *s = talloc_asprintf(NULL, "%e", replace);
    replace_var_str(text, name, s);
    talloc_free(s);
}

static void replace_var_char(char **text, const char *name, char replace)
{
    char s[2] = { replace, '\0' };
    replace_var_str(text, name, s);
}

// Append template to *text. Possibly initialize *text if it's NULL.
static void append_template(char **text, const char* template)
{
    if (!text)
        *text = talloc_strdup(NULL, template);
    else
        *text = talloc_strdup_append(*text, template);
}

/**
 * \brief helper function for gen_spline_lookup_tex
 * \param x subpixel-position ((0,1) range) to calculate weights for
 * \param dst where to store transformed weights, must provide space for 4 GLfloats
 *
 * calculates the weights and stores them after appropriate transformation
 * for the scaler fragment program.
 */
static void store_weights(float x, GLfloat *dst)
{
    float w0 = (((-1 * x + 3) * x - 3) * x + 1) / 6;
    float w1 = (((3 * x - 6) * x + 0) * x + 4) / 6;
    float w2 = (((-3 * x + 3) * x + 3) * x + 1) / 6;
    float w3 = (((1 * x + 0) * x + 0) * x + 0) / 6;
    *dst++ = 1 + x - w1 / (w0 + w1);
    *dst++ = 1 - x + w3 / (w2 + w3);
    *dst++ = w0 + w1;
    *dst++ = 0;
}

//! to avoid artefacts this should be rather large
#define LOOKUP_BSPLINE_RES (2 * 1024)
/**
 * \brief creates the 1D lookup texture needed for fast higher-order filtering
 * \param unit texture unit to attach texture to
 */
static void gen_spline_lookup_tex(GL *gl, GLenum unit)
{
    GLfloat *tex = calloc(4 * LOOKUP_BSPLINE_RES, sizeof(*tex));
    GLfloat *tp = tex;
    int i;
    for (i = 0; i < LOOKUP_BSPLINE_RES; i++) {
        float x = (float)(i + 0.5) / LOOKUP_BSPLINE_RES;
        store_weights(x, tp);
        tp += 4;
    }
    store_weights(0, tex);
    store_weights(1, &tex[4 * (LOOKUP_BSPLINE_RES - 1)]);
    gl->ActiveTexture(unit);
    gl->TexImage1D(GL_TEXTURE_1D, 0, GL_RGBA16, LOOKUP_BSPLINE_RES, 0, GL_RGBA,
                   GL_FLOAT, tex);
    gl->TexParameterf(GL_TEXTURE_1D, GL_TEXTURE_PRIORITY, 1.0);
    gl->TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->ActiveTexture(GL_TEXTURE0);
    free(tex);
}

#define NOISE_RES 2048

/**
 * \brief creates the 1D lookup texture needed to generate pseudo-random numbers.
 * \param unit texture unit to attach texture to
 */
static void gen_noise_lookup_tex(GL *gl, GLenum unit) {
    GLfloat *tex = calloc(NOISE_RES, sizeof(*tex));
    uint32_t lcg = 0x79381c11;
    int i;
    for (i = 0; i < NOISE_RES; i++)
        tex[i] = (double)i / (NOISE_RES - 1);
    for (i = 0; i < NOISE_RES - 1; i++) {
        int remain = NOISE_RES - i;
        int idx = i + (lcg >> 16) % remain;
        GLfloat tmp = tex[i];
        tex[i] = tex[idx];
        tex[idx] = tmp;
        lcg = lcg * 1664525 + 1013904223;
    }
    gl->ActiveTexture(unit);
    gl->TexImage1D(GL_TEXTURE_1D, 0, 1, NOISE_RES, 0, GL_RED, GL_FLOAT, tex);
    gl->TexParameterf(GL_TEXTURE_1D, GL_TEXTURE_PRIORITY, 1.0);
    gl->TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->ActiveTexture(GL_TEXTURE0);
    free(tex);
}

#define SAMPLE(dest, coord, texture) \
    "TEX textemp, " coord ", " texture ", $tex_type;\n" \
    "MOV " dest ", textemp.r;\n"

static const char bilin_filt_template[] =
    SAMPLE("yuv.$out_comp","fragment.texcoord[$in_tex]","texture[$in_tex]");

#define BICUB_FILT_MAIN \
    /* first y-interpolation */ \
    "ADD coord, fragment.texcoord[$in_tex].xyxy, cdelta.xyxw;\n" \
    "ADD coord2, fragment.texcoord[$in_tex].xyxy, cdelta.zyzw;\n" \
    SAMPLE("a.r","coord.xyxy","texture[$in_tex]") \
    SAMPLE("a.g","coord.zwzw","texture[$in_tex]") \
    /* second y-interpolation */ \
    SAMPLE("b.r","coord2.xyxy","texture[$in_tex]") \
    SAMPLE("b.g","coord2.zwzw","texture[$in_tex]") \
    "LRP a.b, parmy.b, a.rrrr, a.gggg;\n" \
    "LRP a.a, parmy.b, b.rrrr, b.gggg;\n" \
    /* x-interpolation */ \
    "LRP yuv.$out_comp, parmx.b, a.bbbb, a.aaaa;\n"

static const char bicub_filt_template_2D[] =
    "MAD coord.xy, fragment.texcoord[$in_tex], {$texw, $texh}, {0.5, 0.5};\n"
    "TEX parmx, coord.x, texture[$texs], 1D;\n"
    "MUL cdelta.xz, parmx.rrgg, {-$ptw, 0, $ptw, 0};\n"
    "TEX parmy, coord.y, texture[$texs], 1D;\n"
    "MUL cdelta.yw, parmy.rrgg, {0, -$pth, 0, $pth};\n"
    BICUB_FILT_MAIN;

static const char bicub_filt_template_RECT[] =
    "ADD coord, fragment.texcoord[$in_tex], {0.5, 0.5};\n"
    "TEX parmx, coord.x, texture[$texs], 1D;\n"
    "MUL cdelta.xz, parmx.rrgg, {-1, 0, 1, 0};\n"
    "TEX parmy, coord.y, texture[$texs], 1D;\n"
    "MUL cdelta.yw, parmy.rrgg, {0, -1, 0, 1};\n"
    BICUB_FILT_MAIN;

#define CALCWEIGHTS(t, s) \
    "MAD "t ", {-0.5, 0.1666, 0.3333, -0.3333}, "s ", {1, 0, -0.5, 0.5};\n" \
    "MAD "t ", "t ", "s ", {0, 0, -0.5, 0.5};\n" \
    "MAD "t ", "t ", "s ", {-0.6666, 0, 0.8333, 0.1666};\n" \
    "RCP a.x, "t ".z;\n" \
    "RCP a.y, "t ".w;\n" \
    "MAD "t ".xy, "t ".xyxy, a.xyxy, {1, 1, 0, 0};\n" \
    "ADD "t ".x, "t ".xxxx, "s ";\n" \
    "SUB "t ".y, "t ".yyyy, "s ";\n"

static const char bicub_notex_filt_template_2D[] =
    "MAD coord.xy, fragment.texcoord[$in_tex], {$texw, $texh}, {0.5, 0.5};\n"
    "FRC coord.xy, coord.xyxy;\n"
    CALCWEIGHTS("parmx", "coord.xxxx")
    "MUL cdelta.xz, parmx.rrgg, {-$ptw, 0, $ptw, 0};\n"
    CALCWEIGHTS("parmy", "coord.yyyy")
    "MUL cdelta.yw, parmy.rrgg, {0, -$pth, 0, $pth};\n"
    BICUB_FILT_MAIN;

static const char bicub_notex_filt_template_RECT[] =
    "ADD coord, fragment.texcoord[$in_tex], {0.5, 0.5};\n"
    "FRC coord.xy, coord.xyxy;\n"
    CALCWEIGHTS("parmx", "coord.xxxx")
    "MUL cdelta.xz, parmx.rrgg, {-1, 0, 1, 0};\n"
    CALCWEIGHTS("parmy", "coord.yyyy")
    "MUL cdelta.yw, parmy.rrgg, {0, -1, 0, 1};\n"
    BICUB_FILT_MAIN;

#define BICUB_X_FILT_MAIN \
    "ADD coord.xy, fragment.texcoord[$in_tex].xyxy, cdelta.xyxy;\n" \
    "ADD coord2.xy, fragment.texcoord[$in_tex].xyxy, cdelta.zyzy;\n" \
    SAMPLE("a.r","coord","texture[$in_tex]") \
    SAMPLE("b.r","coord2","texture[$in_tex]") \
    /* x-interpolation */ \
    "LRP yuv.$out_comp, parmx.b, a.rrrr, b.rrrr;\n"

static const char bicub_x_filt_template_2D[] =
    "MAD coord.x, fragment.texcoord[$in_tex], {$texw}, {0.5};\n"
    "TEX parmx, coord, texture[$texs], 1D;\n"
    "MUL cdelta.xyz, parmx.rrgg, {-$ptw, 0, $ptw};\n"
    BICUB_X_FILT_MAIN;

static const char bicub_x_filt_template_RECT[] =
    "ADD coord.x, fragment.texcoord[$in_tex], {0.5};\n"
    "TEX parmx, coord, texture[$texs], 1D;\n"
    "MUL cdelta.xyz, parmx.rrgg, {-1, 0, 1};\n"
    BICUB_X_FILT_MAIN;

static const char unsharp_filt_template[] =
    "PARAM dcoord$out_comp = {$ptw_05, $pth_05, $ptw_05, -$pth_05};\n"
    "ADD coord, fragment.texcoord[$in_tex].xyxy, dcoord$out_comp;\n"
    "SUB coord2, fragment.texcoord[$in_tex].xyxy, dcoord$out_comp;\n"
    SAMPLE("a.r","fragment.texcoord[$in_tex]","texture[$in_tex]")
    SAMPLE("b.r","coord.xyxy","texture[$in_tex]")
    SAMPLE("b.g","coord.zwzw","texture[$in_tex]")
    "ADD b.r, b.r, b.g;\n"
    SAMPLE("b.b","coord2.xyxy","texture[$in_tex]")
    SAMPLE("b.g","coord2.zwzw","texture[$in_tex]")
    "DP3 b, b, {0.25, 0.25, 0.25};\n"
    "SUB b.r, a.r, b.r;\n"
    "MAD textemp.r, b.r, {$strength}, a.r;\n"
    "MOV yuv.$out_comp, textemp.r;\n";

static const char unsharp_filt_template2[] =
    "PARAM dcoord$out_comp = {$ptw_12, $pth_12, $ptw_12, -$pth_12};\n"
    "PARAM dcoord2$out_comp = {$ptw_15, 0, 0, $pth_15};\n"
    "ADD coord, fragment.texcoord[$in_tex].xyxy, dcoord$out_comp;\n"
    "SUB coord2, fragment.texcoord[$in_tex].xyxy, dcoord$out_comp;\n"
    SAMPLE("a.r","fragment.texcoord[$in_tex]","texture[$in_tex]")
    SAMPLE("b.r","coord.xyxy","texture[$in_tex]")
    SAMPLE("b.g","coord.zwzw","texture[$in_tex]")
    "ADD b.r, b.r, b.g;\n"
    SAMPLE("b.b","coord2.xyxy","texture[$in_tex]")
    SAMPLE("b.g","coord2.zwzw","texture[$in_tex]")
    "ADD b.r, b.r, b.b;\n"
    "ADD b.a, b.r, b.g;\n"
    "ADD coord, fragment.texcoord[$in_tex].xyxy, dcoord2$out_comp;\n"
    "SUB coord2, fragment.texcoord[$in_tex].xyxy, dcoord2$out_comp;\n"
    SAMPLE("b.r","coord.xyxy","texture[$in_tex]")
    SAMPLE("b.g","coord.zwzw","texture[$in_tex]")
    "ADD b.r, b.r, b.g;\n"
    SAMPLE("b.b","coord2.xyxy","texture[$in_tex]")
    SAMPLE("b.g","coord2.zwzw","texture[$in_tex]")
    "DP4 b.r, b, {-0.1171875, -0.1171875, -0.1171875, -0.09765625};\n"
    "MAD b.r, a.r, {0.859375}, b.r;\n"
    "MAD textemp.r, b.r, {$strength}, a.r;\n"
    "MOV yuv.$out_comp, textemp.r;\n";

static const char yuv_prog_template[] =
    "PARAM ycoef = {$cm11, $cm21, $cm31};\n"
    "PARAM ucoef = {$cm12, $cm22, $cm32};\n"
    "PARAM vcoef = {$cm13, $cm23, $cm33};\n"
    "PARAM offsets = {$cm14, $cm24, $cm34};\n"
    "TEMP res;\n"
    "MAD res.rgb, yuv.rrrr, ycoef, offsets;\n"
    "MAD res.rgb, yuv.gggg, ucoef, res;\n"
    "MAD res.rgb, yuv.bbbb, vcoef, res;\n";

static const char yuv_pow_prog_template[] =
    "PARAM ycoef = {$cm11, $cm21, $cm31};\n"
    "PARAM ucoef = {$cm12, $cm22, $cm32};\n"
    "PARAM vcoef = {$cm13, $cm23, $cm33};\n"
    "PARAM offsets = {$cm14, $cm24, $cm34};\n"
    "PARAM gamma = {$gamma_r, $gamma_g, $gamma_b};\n"
    "TEMP res;\n"
    "MAD res.rgb, yuv.rrrr, ycoef, offsets;\n"
    "MAD res.rgb, yuv.gggg, ucoef, res;\n"
    "MAD_SAT res.rgb, yuv.bbbb, vcoef, res;\n"
    "POW res.r, res.r, gamma.r;\n"
    "POW res.g, res.g, gamma.g;\n"
    "POW res.b, res.b, gamma.b;\n";

static const char yuv_lookup_prog_template[] =
    "PARAM ycoef = {$cm11, $cm21, $cm31, 0};\n"
    "PARAM ucoef = {$cm12, $cm22, $cm32, 0};\n"
    "PARAM vcoef = {$cm13, $cm23, $cm33, 0};\n"
    "PARAM offsets = {$cm14, $cm24, $cm34, 0.125};\n"
    "TEMP res;\n"
    "MAD res, yuv.rrrr, ycoef, offsets;\n"
    "MAD res.rgb, yuv.gggg, ucoef, res;\n"
    "MAD res.rgb, yuv.bbbb, vcoef, res;\n"
    "TEX res.r, res.raaa, texture[$conv_tex0], 2D;\n"
    "ADD res.a, res.a, 0.25;\n"
    "TEX res.g, res.gaaa, texture[$conv_tex0], 2D;\n"
    "ADD res.a, res.a, 0.25;\n"
    "TEX res.b, res.baaa, texture[$conv_tex0], 2D;\n";

static const char yuv_lookup3d_prog_template[] =
    "TEMP res;\n"
    "TEX res, yuv, texture[$conv_tex0], 3D;\n";

static const char noise_filt_template[] =
    "MUL coord.xy, fragment.texcoord[0], {$noise_sx, $noise_sy};\n"
    "TEMP rand;\n"
    "TEX rand.r, coord.x, texture[$noise_filt_tex], 1D;\n"
    "ADD rand.r, rand.r, coord.y;\n"
    "TEX rand.r, rand.r, texture[$noise_filt_tex], 1D;\n"
    "MAD res.rgb, rand.rrrr, {$noise_str, $noise_str, $noise_str}, res;\n";

/**
 * \brief creates and initializes helper textures needed for scaling texture read
 * \param scaler scaler type to create texture for
 * \param texu contains next free texture unit number
 * \param texs texture unit ids for the scaler are stored in this array
 */
static void create_scaler_textures(GL *gl, int scaler, int *texu, char *texs)
{
    switch (scaler) {
    case YUV_SCALER_BILIN:
    case YUV_SCALER_BICUB_NOTEX:
    case YUV_SCALER_UNSHARP:
    case YUV_SCALER_UNSHARP2:
        break;
    case YUV_SCALER_BICUB:
    case YUV_SCALER_BICUB_X:
        texs[0] = (*texu)++;
        gen_spline_lookup_tex(gl, GL_TEXTURE0 + texs[0]);
        texs[0] += '0';
        break;
    default:
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] unknown scaler type %i\n", scaler);
    }
}

//! resolution of texture for gamma lookup table
#define LOOKUP_RES 512
//! resolution for 3D yuv->rgb conversion lookup table
#define LOOKUP_3DRES 32
/**
 * \brief creates and initializes helper textures needed for yuv conversion
 * \param params struct containing parameters like brightness, gamma, ...
 * \param texu contains next free texture unit number
 * \param texs texture unit ids for the conversion are stored in this array
 */
static void create_conv_textures(GL *gl, gl_conversion_params_t *params,
                                 int *texu, char *texs)
{
    unsigned char *lookup_data = NULL;
    int conv = YUV_CONVERSION(params->type);
    switch (conv) {
    case YUV_CONVERSION_FRAGMENT:
    case YUV_CONVERSION_FRAGMENT_POW:
        break;
    case YUV_CONVERSION_FRAGMENT_LOOKUP:
        texs[0] = (*texu)++;
        gl->ActiveTexture(GL_TEXTURE0 + texs[0]);
        lookup_data = malloc(4 * LOOKUP_RES);
        mp_gen_gamma_map(lookup_data, LOOKUP_RES, params->csp_params.rgamma);
        mp_gen_gamma_map(&lookup_data[LOOKUP_RES], LOOKUP_RES,
                         params->csp_params.ggamma);
        mp_gen_gamma_map(&lookup_data[2 * LOOKUP_RES], LOOKUP_RES,
                         params->csp_params.bgamma);
        glCreateClearTex(gl, GL_TEXTURE_2D, GL_LUMINANCE8, GL_LUMINANCE,
                         GL_UNSIGNED_BYTE, GL_LINEAR, LOOKUP_RES, 4, 0);
        glUploadTex(gl, GL_TEXTURE_2D, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    lookup_data, LOOKUP_RES, 0, 0, LOOKUP_RES, 4, 0);
        gl->ActiveTexture(GL_TEXTURE0);
        texs[0] += '0';
        break;
    case YUV_CONVERSION_FRAGMENT_LOOKUP3D:
    {
        int sz = LOOKUP_3DRES + 2; // texture size including borders
        if (!gl->TexImage3D) {
            mp_msg(MSGT_VO, MSGL_ERR, "[gl] Missing 3D texture function!\n");
            break;
        }
        texs[0] = (*texu)++;
        gl->ActiveTexture(GL_TEXTURE0 + texs[0]);
        lookup_data = malloc(3 * sz * sz * sz);
        mp_gen_yuv2rgb_map(&params->csp_params, lookup_data, LOOKUP_3DRES);
        glAdjustAlignment(gl, sz);
        gl->PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl->TexImage3D(GL_TEXTURE_3D, 0, 3, sz, sz, sz, 1,
                       GL_RGB, GL_UNSIGNED_BYTE, lookup_data);
        gl->TexParameterf(GL_TEXTURE_3D, GL_TEXTURE_PRIORITY, 1.0);
        gl->TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        gl->TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        gl->TexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP);
        gl->ActiveTexture(GL_TEXTURE0);
        texs[0] += '0';
    }
    break;
    default:
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] unknown conversion type %i\n", conv);
    }
    free(lookup_data);
}

/**
 * \brief adds a scaling texture read at the current fragment program position
 * \param scaler type of scaler to insert
 * \param prog pointer to fragment program so far
 * \param texs array containing the texture unit identifiers for this scaler
 * \param in_tex texture unit the scaler should read from
 * \param out_comp component of the yuv variable the scaler stores the result in
 * \param rect if rectangular (pixel) adressing should be used for in_tex
 * \param texw width of the in_tex texture
 * \param texh height of the in_tex texture
 * \param strength strength of filter effect if the scaler does some kind of filtering
 */
static void add_scaler(int scaler, char **prog, char *texs,
                       char in_tex, char out_comp, int rect, int texw, int texh,
                       double strength)
{
    const char *ttype = rect ? "RECT" : "2D";
    const float ptw = rect ? 1.0 : 1.0 / texw;
    const float pth = rect ? 1.0 : 1.0 / texh;
    switch (scaler) {
    case YUV_SCALER_BILIN:
        append_template(prog, bilin_filt_template);
        break;
    case YUV_SCALER_BICUB:
        if (rect)
            append_template(prog, bicub_filt_template_RECT);
        else
            append_template(prog, bicub_filt_template_2D);
        break;
    case YUV_SCALER_BICUB_X:
        if (rect)
            append_template(prog, bicub_x_filt_template_RECT);
        else
            append_template(prog, bicub_x_filt_template_2D);
        break;
    case YUV_SCALER_BICUB_NOTEX:
        if (rect)
            append_template(prog, bicub_notex_filt_template_RECT);
        else
            append_template(prog, bicub_notex_filt_template_2D);
        break;
    case YUV_SCALER_UNSHARP:
        append_template(prog, unsharp_filt_template);
        break;
    case YUV_SCALER_UNSHARP2:
        append_template(prog, unsharp_filt_template2);
        break;
    }

    replace_var_char(prog, "texs", texs[0]);
    replace_var_char(prog, "in_tex", in_tex);
    replace_var_char(prog, "out_comp", out_comp);
    replace_var_str(prog, "tex_type", ttype);
    replace_var_float(prog, "texw", texw);
    replace_var_float(prog, "texh", texh);
    replace_var_float(prog, "ptw", ptw);
    replace_var_float(prog, "pth", pth);

    // this is silly, not sure if that couldn't be in the shader source instead
    replace_var_float(prog, "ptw_05", ptw * 0.5);
    replace_var_float(prog, "pth_05", pth * 0.5);
    replace_var_float(prog, "ptw_15", ptw * 1.5);
    replace_var_float(prog, "pth_15", pth * 1.5);
    replace_var_float(prog, "ptw_12", ptw * 1.2);
    replace_var_float(prog, "pth_12", pth * 1.2);

    replace_var_float(prog, "strength", strength);
}

static const struct {
    const char *name;
    GLenum cur;
    GLenum max;
} progstats[] = {
    {"instructions", 0x88A0, 0x88A1},
    {"native instructions", 0x88A2, 0x88A3},
    {"temporaries", 0x88A4, 0x88A5},
    {"native temporaries", 0x88A6, 0x88A7},
    {"parameters", 0x88A8, 0x88A9},
    {"native parameters", 0x88AA, 0x88AB},
    {"attribs", 0x88AC, 0x88AD},
    {"native attribs", 0x88AE, 0x88AF},
    {"ALU instructions", 0x8805, 0x880B},
    {"TEX instructions", 0x8806, 0x880C},
    {"TEX indirections", 0x8807, 0x880D},
    {"native ALU instructions", 0x8808, 0x880E},
    {"native TEX instructions", 0x8809, 0x880F},
    {"native TEX indirections", 0x880A, 0x8810},
    {NULL, 0, 0}
};

/**
 * \brief load the specified GPU Program
 * \param target program target to load into, only GL_FRAGMENT_PROGRAM is tested
 * \param prog program string
 * \return 1 on success, 0 otherwise
 */
int loadGPUProgram(GL *gl, GLenum target, char *prog)
{
    int i;
    GLint cur = 0, max = 0, err = 0;
    if (!gl->ProgramString) {
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] Missing GPU program function\n");
        return 0;
    }
    gl->ProgramString(target, GL_PROGRAM_FORMAT_ASCII, strlen(prog), prog);
    gl->GetIntegerv(GL_PROGRAM_ERROR_POSITION, &err);
    if (err != -1) {
        mp_msg(MSGT_VO, MSGL_ERR,
               "[gl] Error compiling fragment program, make sure your card supports\n"
               "[gl]   GL_ARB_fragment_program (use glxinfo to check).\n"
               "[gl]   Error message:\n  %s at %.10s\n",
               gl->GetString(GL_PROGRAM_ERROR_STRING), &prog[err]);
        return 0;
    }
    if (!gl->GetProgramivARB || !mp_msg_test(MSGT_VO, MSGL_DBG2))
        return 1;
    mp_msg(MSGT_VO, MSGL_V, "[gl] Program statistics:\n");
    for (i = 0; progstats[i].name; i++) {
        gl->GetProgramivARB(target, progstats[i].cur, &cur);
        gl->GetProgramivARB(target, progstats[i].max, &max);
        mp_msg(MSGT_VO, MSGL_V, "[gl]   %s: %i/%i\n", progstats[i].name, cur,
               max);
    }
    return 1;
}

#define MAX_PROGSZ (1024 * 1024)

/**
 * \brief setup a fragment program that will do YUV->RGB conversion
 * \param parms struct containing parameters like conversion and scaler type,
 *              brightness, ...
 */
static void glSetupYUVFragprog(GL *gl, gl_conversion_params_t *params)
{
    int type = params->type;
    int texw = params->texw;
    int texh = params->texh;
    int rect = params->target == GL_TEXTURE_RECTANGLE;
    static const char prog_hdr[] =
        "!!ARBfp1.0\n"
        "OPTION ARB_precision_hint_fastest;\n"
        // all scaler variables must go here so they aren't defined
        // multiple times when the same scaler is used more than once
        "TEMP coord, coord2, cdelta, parmx, parmy, a, b, yuv, textemp;\n";
    char *yuv_prog = NULL;
    char **prog = &yuv_prog;
    int cur_texu = 3;
    char lum_scale_texs[1];
    char chrom_scale_texs[1];
    char conv_texs[1];
    char filt_texs[1] = {0};
    GLint i;
    // this is the conversion matrix, with y, u, v factors
    // for red, green, blue and the constant offsets
    float yuv2rgb[3][4];
    int noise = params->noise_strength != 0;
    create_conv_textures(gl, params, &cur_texu, conv_texs);
    create_scaler_textures(gl, YUV_LUM_SCALER(type), &cur_texu, lum_scale_texs);
    if (YUV_CHROM_SCALER(type) == YUV_LUM_SCALER(type))
        memcpy(chrom_scale_texs, lum_scale_texs, sizeof(chrom_scale_texs));
    else
        create_scaler_textures(gl, YUV_CHROM_SCALER(type), &cur_texu,
                               chrom_scale_texs);

    if (noise) {
        gen_noise_lookup_tex(gl, cur_texu);
        filt_texs[0] = '0' + cur_texu++;
    }

    gl->GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &i);
    if (i < cur_texu)
        mp_msg(MSGT_VO, MSGL_ERR,
               "[gl] %i texture units needed for this type of YUV fragment support (found %i)\n",
               cur_texu, i);
    if (!gl->ProgramString) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] ProgramString function missing!\n");
        return;
    }
    append_template(prog, prog_hdr);
    add_scaler(YUV_LUM_SCALER(type), prog, lum_scale_texs,
               '0', 'r', rect, texw, texh, params->filter_strength);
    add_scaler(YUV_CHROM_SCALER(type), prog,
               chrom_scale_texs, '1', 'g', rect, params->chrom_texw,
               params->chrom_texh, params->filter_strength);
    add_scaler(YUV_CHROM_SCALER(type), prog,
               chrom_scale_texs, '2', 'b', rect, params->chrom_texw,
               params->chrom_texh, params->filter_strength);
    mp_get_yuv2rgb_coeffs(&params->csp_params, yuv2rgb);
    switch (YUV_CONVERSION(type)) {
    case YUV_CONVERSION_FRAGMENT:
        append_template(prog, yuv_prog_template);
        break;
    case YUV_CONVERSION_FRAGMENT_POW:
        append_template(prog, yuv_pow_prog_template);
        break;
    case YUV_CONVERSION_FRAGMENT_LOOKUP:
        append_template(prog, yuv_lookup_prog_template);
        break;
    case YUV_CONVERSION_FRAGMENT_LOOKUP3D:
        append_template(prog, yuv_lookup3d_prog_template);
        break;
    default:
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] unknown conversion type %i\n",
               YUV_CONVERSION(type));
        break;
    }
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            // "cmRC"
            char var[] = { 'c', 'm', '1' + r, '1' + c, '\0' };
            replace_var_float(prog, var, yuv2rgb[r][c]);
        }
    }
    replace_var_float(prog, "gamma_r", (float)1.0 / params->csp_params.rgamma);
    replace_var_float(prog, "gamma_g", (float)1.0 / params->csp_params.ggamma);
    replace_var_float(prog, "gamma_b", (float)1.0 / params->csp_params.bgamma);
    replace_var_char(prog, "conv_tex0", conv_texs[0]);

    if (noise) {
        // 1.0 strength is suitable for dithering 8 to 6 bit
        double str = params->noise_strength * (1.0 / 64);
        double scale_x = (double)NOISE_RES / texw;
        double scale_y = (double)NOISE_RES / texh;
        if (rect) {
            scale_x /= texw;
            scale_y /= texh;
        }
        append_template(prog, noise_filt_template);
        replace_var_float(prog, "noise_sx", scale_x);
        replace_var_float(prog, "noise_sy", scale_y);
        replace_var_char(prog, "noise_filt_tex", filt_texs[0]);
        replace_var_float(prog, "noise_str", str);
    }

    append_template(prog, "MOV result.color.rgb, res;\nEND");

    mp_msg(MSGT_VO, MSGL_DBG2, "[gl] generated fragment program:\n%s\n",
           yuv_prog);
    loadGPUProgram(gl, GL_FRAGMENT_PROGRAM, yuv_prog);
    talloc_free(yuv_prog);
}

/**
 * \brief detect the best YUV->RGB conversion method available
 */
int glAutodetectYUVConversion(GL *gl)
{
    const char *extensions = gl->GetString(GL_EXTENSIONS);
    if (!extensions || !gl->MultiTexCoord2f)
        return YUV_CONVERSION_NONE;
    if (strstr(extensions, "GL_ARB_fragment_program"))
        return YUV_CONVERSION_FRAGMENT;
    if (strstr(extensions, "GL_ATI_text_fragment_shader"))
        return YUV_CONVERSION_TEXT_FRAGMENT;
    if (strstr(extensions, "GL_ATI_fragment_shader"))
        return YUV_CONVERSION_COMBINERS_ATI;
    return YUV_CONVERSION_NONE;
}

/**
 * \brief setup YUV->RGB conversion
 * \param parms struct containing parameters like conversion and scaler type,
 *              brightness, ...
 * \ingroup glconversion
 */
void glSetupYUVConversion(GL *gl, gl_conversion_params_t *params)
{
    if (params->chrom_texw == 0)
        params->chrom_texw = 1;
    if (params->chrom_texh == 0)
        params->chrom_texh = 1;
    switch (YUV_CONVERSION(params->type)) {
    case YUV_CONVERSION_COMBINERS_ATI:
        glSetupYUVFragmentATI(gl, &params->csp_params, 0);
        break;
    case YUV_CONVERSION_TEXT_FRAGMENT:
        glSetupYUVFragmentATI(gl, &params->csp_params, 1);
        break;
    case YUV_CONVERSION_FRAGMENT_LOOKUP:
    case YUV_CONVERSION_FRAGMENT_LOOKUP3D:
    case YUV_CONVERSION_FRAGMENT:
    case YUV_CONVERSION_FRAGMENT_POW:
        glSetupYUVFragprog(gl, params);
        break;
    case YUV_CONVERSION_NONE:
        break;
    default:
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] unknown conversion type %i\n",
               YUV_CONVERSION(params->type));
    }
}

/**
 * \brief enable the specified YUV conversion
 * \param target texture target for Y, U and V textures (e.g. GL_TEXTURE_2D)
 * \param type type of YUV conversion
 * \ingroup glconversion
 */
void glEnableYUVConversion(GL *gl, GLenum target, int type)
{
    switch (YUV_CONVERSION(type)) {
    case YUV_CONVERSION_COMBINERS_ATI:
        gl->ActiveTexture(GL_TEXTURE1);
        gl->Enable(target);
        gl->ActiveTexture(GL_TEXTURE2);
        gl->Enable(target);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->Enable(GL_FRAGMENT_SHADER_ATI);
        break;
    case YUV_CONVERSION_TEXT_FRAGMENT:
        gl->ActiveTexture(GL_TEXTURE1);
        gl->Enable(target);
        gl->ActiveTexture(GL_TEXTURE2);
        gl->Enable(target);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->Enable(GL_TEXT_FRAGMENT_SHADER_ATI);
        break;
    case YUV_CONVERSION_FRAGMENT_LOOKUP3D:
    case YUV_CONVERSION_FRAGMENT_LOOKUP:
    case YUV_CONVERSION_FRAGMENT_POW:
    case YUV_CONVERSION_FRAGMENT:
    case YUV_CONVERSION_NONE:
        gl->Enable(GL_FRAGMENT_PROGRAM);
        break;
    }
}

/**
 * \brief disable the specified YUV conversion
 * \param target texture target for Y, U and V textures (e.g. GL_TEXTURE_2D)
 * \param type type of YUV conversion
 * \ingroup glconversion
 */
void glDisableYUVConversion(GL *gl, GLenum target, int type)
{
    switch (YUV_CONVERSION(type)) {
    case YUV_CONVERSION_COMBINERS_ATI:
        gl->ActiveTexture(GL_TEXTURE1);
        gl->Disable(target);
        gl->ActiveTexture(GL_TEXTURE2);
        gl->Disable(target);
        gl->ActiveTexture(GL_TEXTURE0);
        gl->Disable(GL_FRAGMENT_SHADER_ATI);
        break;
    case YUV_CONVERSION_TEXT_FRAGMENT:
        gl->Disable(GL_TEXT_FRAGMENT_SHADER_ATI);
        // HACK: at least the Mac OS X 10.5 PPC Radeon drivers are broken and
        // without this disable the texture units while the program is still
        // running (10.4 PPC seems to work without this though).
        gl->Flush();
        gl->ActiveTexture(GL_TEXTURE1);
        gl->Disable(target);
        gl->ActiveTexture(GL_TEXTURE2);
        gl->Disable(target);
        gl->ActiveTexture(GL_TEXTURE0);
        break;
    case YUV_CONVERSION_FRAGMENT_LOOKUP3D:
    case YUV_CONVERSION_FRAGMENT_LOOKUP:
    case YUV_CONVERSION_FRAGMENT_POW:
    case YUV_CONVERSION_FRAGMENT:
    case YUV_CONVERSION_NONE:
        gl->Disable(GL_FRAGMENT_PROGRAM);
        break;
    }
}

void glEnable3DLeft(GL *gl, int type)
{
    GLint buffer;
    switch (type) {
    case GL_3D_RED_CYAN:
        gl->ColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
        break;
    case GL_3D_GREEN_MAGENTA:
        gl->ColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
        break;
    case GL_3D_QUADBUFFER:
        gl->GetIntegerv(GL_DRAW_BUFFER, &buffer);
        switch (buffer) {
        case GL_FRONT:
        case GL_FRONT_LEFT:
        case GL_FRONT_RIGHT:
            buffer = GL_FRONT_LEFT;
            break;
        case GL_BACK:
        case GL_BACK_LEFT:
        case GL_BACK_RIGHT:
            buffer = GL_BACK_LEFT;
            break;
        }
        gl->DrawBuffer(buffer);
        break;
    }
}

void glEnable3DRight(GL *gl, int type)
{
    GLint buffer;
    switch (type) {
    case GL_3D_RED_CYAN:
        gl->ColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE);
        break;
    case GL_3D_GREEN_MAGENTA:
        gl->ColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        break;
    case GL_3D_QUADBUFFER:
        gl->GetIntegerv(GL_DRAW_BUFFER, &buffer);
        switch (buffer) {
        case GL_FRONT:
        case GL_FRONT_LEFT:
        case GL_FRONT_RIGHT:
            buffer = GL_FRONT_RIGHT;
            break;
        case GL_BACK:
        case GL_BACK_LEFT:
        case GL_BACK_RIGHT:
            buffer = GL_BACK_RIGHT;
            break;
        }
        gl->DrawBuffer(buffer);
        break;
    }
}

void glDisable3D(GL *gl, int type)
{
    GLint buffer;
    switch (type) {
    case GL_3D_RED_CYAN:
    case GL_3D_GREEN_MAGENTA:
        gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        break;
    case GL_3D_QUADBUFFER:
        gl->DrawBuffer(vo_doublebuffering ? GL_BACK : GL_FRONT);
        gl->GetIntegerv(GL_DRAW_BUFFER, &buffer);
        switch (buffer) {
        case GL_FRONT:
        case GL_FRONT_LEFT:
        case GL_FRONT_RIGHT:
            buffer = GL_FRONT;
            break;
        case GL_BACK:
        case GL_BACK_LEFT:
        case GL_BACK_RIGHT:
            buffer = GL_BACK;
            break;
        }
        gl->DrawBuffer(buffer);
        break;
    }
}

/**
 * \brief draw a texture part at given 2D coordinates
 * \param x screen top coordinate
 * \param y screen left coordinate
 * \param w screen width coordinate
 * \param h screen height coordinate
 * \param tx texture top coordinate in pixels
 * \param ty texture left coordinate in pixels
 * \param tw texture part width in pixels
 * \param th texture part height in pixels
 * \param sx width of texture in pixels
 * \param sy height of texture in pixels
 * \param rect_tex whether this texture uses texture_rectangle extension
 * \param is_yv12 if != 0, also draw the textures from units 1 and 2,
 *                bits 8 - 15 and 16 - 23 specify the x and y scaling of those textures
 * \param flip flip the texture upside down
 * \ingroup gltexture
 */
void glDrawTex(GL *gl, GLfloat x, GLfloat y, GLfloat w, GLfloat h,
               GLfloat tx, GLfloat ty, GLfloat tw, GLfloat th,
               int sx, int sy, int rect_tex, int is_yv12, int flip)
{
    int chroma_x_shift = (is_yv12 >>  8) & 31;
    int chroma_y_shift = (is_yv12 >> 16) & 31;
    GLfloat xscale = 1 << chroma_x_shift;
    GLfloat yscale = 1 << chroma_y_shift;
    GLfloat tx2 = tx / xscale, ty2 = ty / yscale, tw2 = tw / xscale, th2 = th / yscale;
    if (!rect_tex) {
        tx /= sx;
        ty /= sy;
        tw /= sx;
        th /= sy;
        tx2 = tx, ty2 = ty, tw2 = tw, th2 = th;
    }
    if (flip) {
        y += h;
        h = -h;
    }
    gl->Begin(GL_QUADS);
    gl->TexCoord2f(tx, ty);
    if (is_yv12) {
        gl->MultiTexCoord2f(GL_TEXTURE1, tx2, ty2);
        gl->MultiTexCoord2f(GL_TEXTURE2, tx2, ty2);
    }
    gl->Vertex2f(x, y);
    gl->TexCoord2f(tx, ty + th);
    if (is_yv12) {
        gl->MultiTexCoord2f(GL_TEXTURE1, tx2, ty2 + th2);
        gl->MultiTexCoord2f(GL_TEXTURE2, tx2, ty2 + th2);
    }
    gl->Vertex2f(x, y + h);
    gl->TexCoord2f(tx + tw, ty + th);
    if (is_yv12) {
        gl->MultiTexCoord2f(GL_TEXTURE1, tx2 + tw2, ty2 + th2);
        gl->MultiTexCoord2f(GL_TEXTURE2, tx2 + tw2, ty2 + th2);
    }
    gl->Vertex2f(x + w, y + h);
    gl->TexCoord2f(tx + tw, ty);
    if (is_yv12) {
        gl->MultiTexCoord2f(GL_TEXTURE1, tx2 + tw2, ty2);
        gl->MultiTexCoord2f(GL_TEXTURE2, tx2 + tw2, ty2);
    }
    gl->Vertex2f(x + w, y);
    gl->End();
}

#ifdef CONFIG_GL_COCOA
#include "cocoa_common.h"
static int create_window_cocoa(struct MPGLContext *ctx, uint32_t d_width,
                               uint32_t d_height, uint32_t flags)
{
    if (vo_cocoa_create_window(ctx->vo, d_width, d_height, flags, 0) == 0) {
        return SET_WINDOW_OK;
    } else {
        return SET_WINDOW_FAILED;
    }
}

static int create_window_cocoa_gl3(struct MPGLContext *ctx, int gl_flags,
                                 int gl_version, uint32_t d_width,
                                 uint32_t d_height, uint32_t flags)
{
    int rv = vo_cocoa_create_window(ctx->vo, d_width, d_height, flags, 1);
    getFunctions(ctx->gl, (void *)vo_cocoa_glgetaddr, NULL, true);
    ctx->depth_r = vo_cocoa_cgl_color_size();
    ctx->depth_g = vo_cocoa_cgl_color_size();
    ctx->depth_b = vo_cocoa_cgl_color_size();
    return rv;
}

static int setGlWindow_cocoa(MPGLContext *ctx)
{
    vo_cocoa_change_attributes(ctx->vo);
    getFunctions(ctx->gl, (void *)vo_cocoa_glgetaddr, NULL, false);
    if (!ctx->gl->SwapInterval)
        ctx->gl->SwapInterval = vo_cocoa_swap_interval;
    return SET_WINDOW_OK;
}

static void releaseGlContext_cocoa(MPGLContext *ctx)
{
}

static void swapGlBuffers_cocoa(MPGLContext *ctx)
{
    vo_cocoa_swap_buffers();
}

static int cocoa_check_events(struct vo *vo)
{
    return vo_cocoa_check_events(vo);
}

static void cocoa_update_xinerama_info(struct vo *vo)
{
    vo_cocoa_update_xinerama_info(vo);
}

static void cocoa_fullscreen(struct vo *vo)
{
    vo_cocoa_fullscreen(vo);
}
#endif

#ifdef CONFIG_GL_WIN32
#include <windows.h>
#include "w32_common.h"

struct w32_context {
    int vinfo;
    HGLRC context;
};

static int create_window_w32(struct MPGLContext *ctx, uint32_t d_width,
                             uint32_t d_height, uint32_t flags)
{
    if (!vo_w32_config(ctx->vo, d_width, d_height, flags))
        return -1;
    return 0;
}

/**
 * \brief little helper since wglGetProcAddress definition does not fit our
 *        getProcAddress
 * \param procName name of function to look up
 * \return function pointer returned by wglGetProcAddress
 */
static void *w32gpa(const GLubyte *procName)
{
    HMODULE oglmod;
    void *res = wglGetProcAddress(procName);
    if (res)
        return res;
    oglmod = GetModuleHandle("opengl32.dll");
    return GetProcAddress(oglmod, procName);
}

static int create_window_w32_gl3(struct MPGLContext *ctx, int gl_flags,
                                 int gl_version, uint32_t d_width,
                                 uint32_t d_height, uint32_t flags) {
    if (!vo_w32_config(ctx->vo, d_width, d_height, flags))
        return -1;

    struct w32_context *w32_ctx = ctx->priv;
    HGLRC *context = &w32_ctx->context;

    if (*context) // reuse existing context
        return 0; // not reusing it breaks gl3!

    HWND win = ctx->vo->w32->window;
    HDC windc = vo_w32_get_dc(ctx->vo, win);
    HGLRC new_context = 0;

    new_context = wglCreateContext(windc);
    if (!new_context) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not create GL context!\n");
        return -1;
    }

    // set context
    if (!wglMakeCurrent(windc, new_context)) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not set GL context!\n");
        goto out;
    }

    const char *(GLAPIENTRY *wglGetExtensionsStringARB)(HDC hdc)
        = w32gpa((const GLubyte*)"wglGetExtensionsStringARB");

    if (!wglGetExtensionsStringARB)
        goto unsupported;

    const char *wgl_exts = wglGetExtensionsStringARB(windc);
    if (!strstr(wgl_exts, "WGL_ARB_create_context"))
        goto unsupported;

    HGLRC (GLAPIENTRY *wglCreateContextAttribsARB)(HDC hDC, HGLRC hShareContext,
                                                   const int *attribList)
        = w32gpa((const GLubyte*)"wglCreateContextAttribsARB");

    if (!wglCreateContextAttribsARB)
        goto unsupported;

    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, MPGL_VER_GET_MAJOR(gl_version),
        WGL_CONTEXT_MINOR_VERSION_ARB, MPGL_VER_GET_MINOR(gl_version),
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    *context = wglCreateContextAttribsARB(windc, 0, attribs);
    if (! *context) {
        // NVidia, instead of ignoring WGL_CONTEXT_FLAGS_ARB, will error out if
        // it's present on pre-3.2 contexts.
        // Remove it from attribs and retry the context creation.
        attribs[6] = attribs[7] = 0;
        *context = wglCreateContextAttribsARB(windc, 0, attribs);
    }
    if (! *context) {
        int err = GetLastError();
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not create an OpenGL 3.x"
                                    " context: error 0x%x\n", err);
        goto out;
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(new_context);

    if (!wglMakeCurrent(windc, *context)) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not set GL3 context!\n");
        wglDeleteContext(*context);
        return -1;
    }

    /* update function pointers */
    getFunctions(ctx->gl, w32gpa, NULL, true);

    int pfmt = GetPixelFormat(windc);
    PIXELFORMATDESCRIPTOR pfd;
    if (DescribePixelFormat(windc, pfmt, sizeof(PIXELFORMATDESCRIPTOR), &pfd)) {
        ctx->depth_r = pfd.cRedBits;
        ctx->depth_g = pfd.cGreenBits;
        ctx->depth_b = pfd.cBlueBits;
    }

    return 0;

unsupported:
    mp_msg(MSGT_VO, MSGL_ERR, "[gl] The current OpenGL implementation does"
                              " not support OpenGL 3.x \n");
out:
    wglDeleteContext(new_context);
    return -1;
}

static int setGlWindow_w32(MPGLContext *ctx)
{
    HWND win = ctx->vo->w32->window;
    struct w32_context *w32_ctx = ctx->priv;
    int *vinfo = &w32_ctx->vinfo;
    HGLRC *context = &w32_ctx->context;
    int new_vinfo;
    HDC windc = vo_w32_get_dc(ctx->vo, win);
    HGLRC new_context = 0;
    int keep_context = 0;
    int res = SET_WINDOW_FAILED;
    GL *gl = ctx->gl;

    // should only be needed when keeping context, but not doing glFinish
    // can cause flickering even when we do not keep it.
    if (*context)
        gl->Finish();
    new_vinfo = GetPixelFormat(windc);
    if (*context && *vinfo && new_vinfo && *vinfo == new_vinfo) {
        // we can keep the wglContext
        new_context = *context;
        keep_context = 1;
    } else {
        // create a context
        new_context = wglCreateContext(windc);
        if (!new_context) {
            mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not create GL context!\n");
            goto out;
        }
    }

    // set context
    if (!wglMakeCurrent(windc, new_context)) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not set GL context!\n");
        if (!keep_context)
            wglDeleteContext(new_context);
        goto out;
    }

    // set new values
    {
        RECT rect;
        GetClientRect(win, &rect);
        ctx->vo->dwidth = rect.right;
        ctx->vo->dheight = rect.bottom;
    }
    if (!keep_context) {
        if (*context)
            wglDeleteContext(*context);
        *context = new_context;
        *vinfo = new_vinfo;

        getFunctions(ctx->gl, w32gpa, NULL, false);

        // and inform that reinit is neccessary
        res = SET_WINDOW_REINIT;
    } else
        res = SET_WINDOW_OK;

out:
    vo_w32_release_dc(ctx->vo, win, windc);
    return res;
}

static void releaseGlContext_w32(MPGLContext *ctx)
{
    struct w32_context *w32_ctx = ctx->priv;
    int *vinfo = &w32_ctx->vinfo;
    HGLRC *context = &w32_ctx->context;
    *vinfo = 0;
    if (*context) {
        wglMakeCurrent(0, 0);
        wglDeleteContext(*context);
    }
    *context = 0;
}

static void swapGlBuffers_w32(MPGLContext *ctx)
{
    HDC vo_hdc = vo_w32_get_dc(ctx->vo, ctx->vo->w32->window);
    SwapBuffers(vo_hdc);
    vo_w32_release_dc(ctx->vo, ctx->vo->w32->window, vo_hdc);
}
#endif

#ifdef CONFIG_GL_X11
#include <X11/Xlib.h>
#include <GL/glx.h>
#include "x11_common.h"

struct glx_context {
    XVisualInfo *vinfo;
    GLXContext context;
};

static int create_window_x11(struct MPGLContext *ctx, uint32_t d_width,
                             uint32_t d_height, uint32_t flags)
{
    struct vo *vo = ctx->vo;

    static int default_glx_attribs[] = {
        GLX_RGBA, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1,
        GLX_DOUBLEBUFFER, None
    };
    static int stereo_glx_attribs[]  = {
        GLX_RGBA, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1,
        GLX_DOUBLEBUFFER, GLX_STEREO, None
    };
    XVisualInfo *vinfo = NULL;
    if (flags & VOFLAG_STEREO) {
        vinfo = glXChooseVisual(vo->x11->display, vo->x11->screen,
                                stereo_glx_attribs);
        if (!vinfo)
            mp_msg(MSGT_VO, MSGL_ERR, "[gl] Could not find a stereo visual,"
                    " 3D will probably not work!\n");
    }
    if (!vinfo)
        vinfo = glXChooseVisual(vo->x11->display, vo->x11->screen,
                                default_glx_attribs);
    if (!vinfo) {
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] no GLX support present\n");
        return -1;
    }
    mp_msg(MSGT_VO, MSGL_V, "[gl] GLX chose visual with ID 0x%x\n",
            (int)vinfo->visualid);

    Colormap colormap = XCreateColormap(vo->x11->display, vo->x11->rootwin,
                                        vinfo->visual, AllocNone);
    vo_x11_create_vo_window(vo, vinfo, vo->dx, vo->dy, d_width, d_height,
                            flags, colormap, "gl");

    return 0;
}

/**
 * \brief Returns the XVisualInfo associated with Window win.
 * \param win Window whose XVisualInfo is returne.
 * \return XVisualInfo of the window. Caller must use XFree to free it.
 */
static XVisualInfo *getWindowVisualInfo(MPGLContext *ctx, Window win)
{
    XWindowAttributes xw_attr;
    XVisualInfo vinfo_template;
    int tmp;
    XGetWindowAttributes(ctx->vo->x11->display, win, &xw_attr);
    vinfo_template.visualid = XVisualIDFromVisual(xw_attr.visual);
    return XGetVisualInfo(ctx->vo->x11->display, VisualIDMask, &vinfo_template, &tmp);
}

/**
 * \brief Changes the window in which video is displayed.
 * If possible only transfers the context to the new window, otherwise
 * creates a new one, which must be initialized by the caller.
 * \param vinfo Currently used visual.
 * \param context Currently used context.
 * \param win window that should be used for drawing.
 * \return one of SET_WINDOW_FAILED, SET_WINDOW_OK or SET_WINDOW_REINIT.
 * In case of SET_WINDOW_REINIT the context could not be transfered
 * and the caller must initialize it correctly.
 * \ingroup glcontext
 */
static int setGlWindow_x11(MPGLContext *ctx)
{
    struct glx_context *glx_context = ctx->priv;
    XVisualInfo **vinfo = &glx_context->vinfo;
    GLXContext *context = &glx_context->context;
    Display *display = ctx->vo->x11->display;
    Window win = ctx->vo->x11->window;
    XVisualInfo *new_vinfo;
    GLXContext new_context = NULL;
    int keep_context = 0;
    GL *gl = ctx->gl;

    // should only be needed when keeping context, but not doing glFinish
    // can cause flickering even when we do not keep it.
    if (*context)
        gl->Finish();
    new_vinfo = getWindowVisualInfo(ctx, win);
    if (*context && *vinfo && new_vinfo &&
        (*vinfo)->visualid == new_vinfo->visualid) {
        // we can keep the GLXContext
        new_context = *context;
        XFree(new_vinfo);
        new_vinfo = *vinfo;
        keep_context = 1;
    } else {
        // create a context
        new_context = glXCreateContext(display, new_vinfo, NULL, True);
        if (!new_context) {
            mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not create GLX context!\n");
            XFree(new_vinfo);
            return SET_WINDOW_FAILED;
        }
    }

    // set context
    if (!glXMakeCurrent(display, ctx->vo->x11->window, new_context)) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not set GLX context!\n");
        if (!keep_context) {
            glXDestroyContext(display, new_context);
            XFree(new_vinfo);
        }
        return SET_WINDOW_FAILED;
    }

    // set new values
    ctx->vo->x11->window = win;
    vo_x11_update_geometry(ctx->vo, 1);
    if (!keep_context) {
        void *(*getProcAddress)(const GLubyte *);
        if (*context)
            glXDestroyContext(display, *context);
        *context = new_context;
        if (*vinfo)
            XFree(*vinfo);
        *vinfo = new_vinfo;
        getProcAddress = getdladdr("glXGetProcAddress");
        if (!getProcAddress)
            getProcAddress = getdladdr("glXGetProcAddressARB");

        const char *glxstr = "";
        const char *(*glXExtStr)(Display *, int)
            = getdladdr("glXQueryExtensionsString");
        if (glXExtStr)
            glxstr = glXExtStr(display, ctx->vo->x11->screen);

        getFunctions(gl, getProcAddress, glxstr, false);
        if (!gl->GenPrograms && gl->GetString &&
            getProcAddress &&
            strstr(gl->GetString(GL_EXTENSIONS), "GL_ARB_vertex_program")) {
            mp_msg(MSGT_VO, MSGL_WARN,
                   "Broken glXGetProcAddress detected, trying workaround\n");
            getFunctions(gl, NULL, glxstr, false);
        }

        // and inform that reinit is neccessary
        return SET_WINDOW_REINIT;
    }
    return SET_WINDOW_OK;
}

// The GL3 initialization code roughly follows/copies from:
//  http://www.opengl.org/wiki/Tutorial:_OpenGL_3.0_Context_Creation_(GLX)
// but also uses some of the old code.

static GLXFBConfig select_fb_config(struct vo *vo, const int *attribs)
{
    int fbcount;
    GLXFBConfig *fbc = glXChooseFBConfig(vo->x11->display, vo->x11->screen,
                                         attribs, &fbcount);
    if (!fbc)
        return NULL;

    // The list in fbc is sorted (so that the first element is the best).
    GLXFBConfig fbconfig = fbc[0];

    XFree(fbc);

    return fbconfig;
}

typedef GLXContext (*glXCreateContextAttribsARBProc)
    (Display*, GLXFBConfig, GLXContext, Bool, const int*);

static int create_window_x11_gl3(struct MPGLContext *ctx, int gl_flags,
                                 int gl_version, uint32_t d_width,
                                 uint32_t d_height, uint32_t flags)
{
    struct vo *vo = ctx->vo;
    struct glx_context *glx_ctx = ctx->priv;

    if (glx_ctx->context) {
        // GL context and window already exist.
        // Only update window geometry etc.
        Colormap colormap = XCreateColormap(vo->x11->display, vo->x11->rootwin,
                                            glx_ctx->vinfo->visual, AllocNone);
        vo_x11_create_vo_window(vo, glx_ctx->vinfo, vo->dx, vo->dy, d_width,
                                d_height, flags, colormap, "gl");
        XFreeColormap(vo->x11->display, colormap);
        return SET_WINDOW_OK;
    }

    int glx_major, glx_minor;

    // FBConfigs were added in GLX version 1.3.
    if (!glXQueryVersion(vo->x11->display, &glx_major, &glx_minor) ||
        (MPGL_VER(glx_major, glx_minor) <  MPGL_VER(1, 3)))
    {
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] GLX version older than 1.3.\n");
        return SET_WINDOW_FAILED;
    }

    const int glx_attribs_stereo_value_idx = 1; // index of GLX_STEREO + 1
    int glx_attribs[] = {
        GLX_STEREO, False,
        GLX_X_RENDERABLE, True,
        GLX_RED_SIZE, 1,
        GLX_GREEN_SIZE, 1,
        GLX_BLUE_SIZE, 1,
        GLX_DOUBLEBUFFER, True,
        None
    };
    GLXFBConfig fbc = NULL;
    if (flags & VOFLAG_STEREO) {
        glx_attribs[glx_attribs_stereo_value_idx] = True;
        fbc = select_fb_config(vo, glx_attribs);
        if (!fbc) {
            mp_msg(MSGT_VO, MSGL_ERR, "[gl] Could not find a stereo visual,"
                   " 3D will probably not work!\n");
            glx_attribs[glx_attribs_stereo_value_idx] = False;
        }
    }
    if (!fbc)
        fbc = select_fb_config(vo, glx_attribs);
    if (!fbc) {
        mp_msg(MSGT_VO, MSGL_ERR, "[gl] no GLX support present\n");
        return SET_WINDOW_FAILED;
    }

    glXGetFBConfigAttrib(vo->x11->display, fbc, GLX_RED_SIZE, &ctx->depth_r);
    glXGetFBConfigAttrib(vo->x11->display, fbc, GLX_GREEN_SIZE, &ctx->depth_g);
    glXGetFBConfigAttrib(vo->x11->display, fbc, GLX_BLUE_SIZE, &ctx->depth_b);

    XVisualInfo *vinfo = glXGetVisualFromFBConfig(vo->x11->display, fbc);
    mp_msg(MSGT_VO, MSGL_V, "[gl] GLX chose visual with ID 0x%x\n",
            (int)vinfo->visualid);
    Colormap colormap = XCreateColormap(vo->x11->display, vo->x11->rootwin,
                                        vinfo->visual, AllocNone);
    vo_x11_create_vo_window(vo, vinfo, vo->dx, vo->dy, d_width, d_height,
                            flags, colormap, "gl");
    XFreeColormap(vo->x11->display, colormap);

    glXCreateContextAttribsARBProc glXCreateContextAttribsARB =
        (glXCreateContextAttribsARBProc)
            glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");

    const char *glxstr = "";
    const char *(*glXExtStr)(Display *, int)
        = getdladdr("glXQueryExtensionsString");
    if (glXExtStr)
        glxstr = glXExtStr(vo->x11->display, vo->x11->screen);
    bool have_ctx_ext = glxstr && !!strstr(glxstr, "GLX_ARB_create_context");

    if (!(have_ctx_ext && glXCreateContextAttribsARB)) {
        XFree(vinfo);
        return SET_WINDOW_FAILED;
    }

    int context_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, MPGL_VER_GET_MAJOR(gl_version),
        GLX_CONTEXT_MINOR_VERSION_ARB, MPGL_VER_GET_MINOR(gl_version),
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB
            | (gl_flags & MPGLFLAG_DEBUG ? GLX_CONTEXT_DEBUG_BIT_ARB : 0),
        None
    };
    GLXContext context = glXCreateContextAttribsARB(vo->x11->display, fbc, 0,
                                                    True, context_attribs);
    if (!context) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not create GLX context!\n");
        XFree(vinfo);
        return SET_WINDOW_FAILED;
    }

    // set context
    if (!glXMakeCurrent(vo->x11->display, vo->x11->window, context)) {
        mp_msg(MSGT_VO, MSGL_FATAL, "[gl] Could not set GLX context!\n");
        glXDestroyContext(vo->x11->display, context);
        XFree(vinfo);
        return SET_WINDOW_FAILED;
    }

    glx_ctx->vinfo = vinfo;
    glx_ctx->context = context;

    getFunctions(ctx->gl, (void *)glXGetProcAddress, glxstr, true);

    return SET_WINDOW_REINIT;
}

/**
 * \brief free the VisualInfo and GLXContext of an OpenGL context.
 * \ingroup glcontext
 */
static void releaseGlContext_x11(MPGLContext *ctx)
{
    struct glx_context *glx_ctx = ctx->priv;
    XVisualInfo **vinfo = &glx_ctx->vinfo;
    GLXContext *context = &glx_ctx->context;
    Display *display = ctx->vo->x11->display;
    GL *gl = ctx->gl;
    if (*vinfo)
        XFree(*vinfo);
    *vinfo = NULL;
    if (*context) {
        gl->Finish();
        glXMakeCurrent(display, None, NULL);
        glXDestroyContext(display, *context);
    }
    *context = 0;
}

static void swapGlBuffers_x11(MPGLContext *ctx)
{
    glXSwapBuffers(ctx->vo->x11->display, ctx->vo->x11->window);
}
#endif


struct backend {
    const char *name;
    enum MPGLType type;
};

static struct backend backends[] = {
    {"auto", GLTYPE_AUTO},
    {"cocoa", GLTYPE_COCOA},
    {"win", GLTYPE_W32},
    {"x11", GLTYPE_X11},
    // mplayer-svn aliases (note that mplayer-svn couples these with the numeric
    // values of the internal GLTYPE_* constants)
    {"-1", GLTYPE_AUTO},
    { "0", GLTYPE_W32},
    { "1", GLTYPE_X11},

    {0}
};

int mpgl_find_backend(const char *name)
{
    for (const struct backend *entry = backends; entry->name; entry++) {
        if (strcmp(entry->name, name) == 0)
            return entry->type;
    }
    return -1;
}

MPGLContext *init_mpglcontext(enum MPGLType type, struct vo *vo)
{
    MPGLContext *ctx;
    if (type == GLTYPE_AUTO) {
        ctx = init_mpglcontext(GLTYPE_COCOA, vo);
        if (ctx)
            return ctx;
        ctx = init_mpglcontext(GLTYPE_W32, vo);
        if (ctx)
            return ctx;
        return init_mpglcontext(GLTYPE_X11, vo);
    }
    ctx = talloc_zero(NULL, MPGLContext);
    ctx->gl = talloc_zero(ctx, GL);
    ctx->type = type;
    ctx->vo = vo;
    switch (ctx->type) {
#ifdef CONFIG_GL_COCOA
    case GLTYPE_COCOA:
        ctx->create_window = create_window_cocoa;
        ctx->create_window_gl3 = create_window_cocoa_gl3;
        ctx->setGlWindow = setGlWindow_cocoa;
        ctx->releaseGlContext = releaseGlContext_cocoa;
        ctx->swapGlBuffers = swapGlBuffers_cocoa;
        ctx->check_events = cocoa_check_events;
        ctx->update_xinerama_info = cocoa_update_xinerama_info;
        ctx->fullscreen = cocoa_fullscreen;
        ctx->ontop = vo_cocoa_ontop;
        ctx->vo_uninit = vo_cocoa_uninit;
        if (vo_cocoa_init(vo))
            return ctx;
        break;
#endif
#ifdef CONFIG_GL_WIN32
    case GLTYPE_W32:
        ctx->priv = talloc_zero(ctx, struct w32_context);
        ctx->create_window = create_window_w32;
        ctx->create_window_gl3 = create_window_w32_gl3;
        ctx->setGlWindow = setGlWindow_w32;
        ctx->releaseGlContext = releaseGlContext_w32;
        ctx->swapGlBuffers = swapGlBuffers_w32;
        ctx->update_xinerama_info = w32_update_xinerama_info;
        ctx->border = vo_w32_border;
        ctx->check_events = vo_w32_check_events;
        ctx->fullscreen = vo_w32_fullscreen;
        ctx->ontop = vo_w32_ontop;
        ctx->vo_uninit = vo_w32_uninit;
        if (vo_w32_init(vo))
            return ctx;
        break;
#endif
#ifdef CONFIG_GL_X11
    case GLTYPE_X11:
        ctx->priv = talloc_zero(ctx, struct glx_context);
        ctx->create_window = create_window_x11;
        ctx->setGlWindow = setGlWindow_x11;
        ctx->create_window_gl3 = create_window_x11_gl3;
        ctx->releaseGlContext = releaseGlContext_x11;
        ctx->swapGlBuffers = swapGlBuffers_x11;
        ctx->update_xinerama_info = update_xinerama_info;
        ctx->border = vo_x11_border;
        ctx->check_events = vo_x11_check_events;
        ctx->fullscreen = vo_x11_fullscreen;
        ctx->ontop = vo_x11_ontop;
        ctx->vo_uninit = vo_x11_uninit;
        if (vo_init(vo))
            return ctx;
        break;
#endif
    }
    talloc_free(ctx);
    return NULL;
}

int create_mpglcontext(struct MPGLContext *ctx, int gl_flags, int gl_version,
                       uint32_t d_width, uint32_t d_height, uint32_t flags)
{
    if (gl_version < MPGL_VER(3, 0)) {
        if (ctx->create_window(ctx, d_width, d_height, flags) < 0)
            return SET_WINDOW_FAILED;
        return ctx->setGlWindow(ctx);
    } else {
        if (!ctx->create_window_gl3) {
            mp_msg(MSGT_VO, MSGL_ERR, "[gl] OpenGL 3.x context creation not "
                   "implemented.\n");
            return SET_WINDOW_FAILED;
        }
        return ctx->create_window_gl3(ctx, gl_flags, gl_version, d_width,
                                      d_height, flags);
    }
}

void uninit_mpglcontext(MPGLContext *ctx)
{
    if (!ctx)
        return;
    ctx->releaseGlContext(ctx);
    ctx->vo_uninit(ctx->vo);
    talloc_free(ctx);
}

void mp_log_source(int mod, int lev, const char *src)
{
    int line = 1;
    if (!src)
        return;
    while (*src) {
        const char *end = strchr(src, '\n');
        const char *next = end + 1;
        if (!end)
            next = end = src + strlen(src);
        mp_msg(mod, lev, "[%3d] %.*s\n", line, (int)(end - src), src);
        line++;
        src = next;
    }
}
