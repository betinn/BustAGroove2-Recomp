/* Funções e constantes GL do núcleo (privado de src/remake/): o subconjunto comum de GL 3.3 core e GLES 3.0 que
 * rmk_gl.c e rmk_stage.c usam, carregado pelo get_proc de quem chama. */
#ifndef RMK_GLFN_H
#define RMK_GLFN_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define RGLAPI __stdcall
#else
#define RGLAPI
#endif

typedef uint32_t GLenum_;
typedef uint32_t GLuint_;
typedef int32_t GLint_;
typedef int32_t GLsizei_;
typedef uint8_t GLboolean_;
typedef ptrdiff_t GLsizeiptr_;

#define R_TRIANGLES 0x0004
#define R_UNSIGNED_BYTE 0x1401
#define R_UNSIGNED_SHORT 0x1403
#define R_FLOAT 0x1406
#define R_TEXTURE_2D 0x0DE1
#define R_TEXTURE_2D_ARRAY 0x8C1A
#define R_TEXTURE0 0x84C0
#define R_RGBA 0x1908
#define R_RGBA8 0x8058
#define R_RGBA32F 0x8814
#define R_TEXTURE_MAG_FILTER 0x2800
#define R_TEXTURE_MIN_FILTER 0x2801
#define R_TEXTURE_WRAP_S 0x2802
#define R_TEXTURE_WRAP_T 0x2803
#define R_NEAREST 0x2600
#define R_LINEAR 0x2601
#define R_LINEAR_MIPMAP_LINEAR 0x2703
#define R_CLAMP_TO_EDGE 0x812F
#define R_TEXTURE_MAX_ANISOTROPY 0x84FE
#define R_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define R_ARRAY_BUFFER 0x8892
#define R_ELEMENT_ARRAY_BUFFER 0x8893
#define R_STATIC_DRAW 0x88E4
#define R_STREAM_DRAW 0x88E0
#define R_LESS 0x0201
#define R_FRAGMENT_SHADER 0x8B30
#define R_VERTEX_SHADER 0x8B31
#define R_COMPILE_STATUS 0x8B81
#define R_LINK_STATUS 0x8B82
#define R_DEPTH_TEST 0x0B71
#define R_CULL_FACE 0x0B44
#define R_BLEND 0x0BE2
#define R_STENCIL_TEST 0x0B90
#define R_BACK 0x0405
#define R_CCW 0x0901
#define R_LEQUAL 0x0203
#define R_DEPTH_BUFFER_BIT 0x0100
#define R_FRAMEBUFFER 0x8D40
#define R_DEPTH_ATTACHMENT 0x8D00
#define R_DEPTH_COMPONENT 0x1902
#define R_DEPTH_COMPONENT24 0x81A6
#define R_UNSIGNED_INT 0x1405
#define R_TEXTURE_COMPARE_MODE 0x884C
#define R_TEXTURE_COMPARE_FUNC 0x884D
#define R_COMPARE_REF_TO_TEXTURE 0x884E
#define R_FRAMEBUFFER_COMPLETE 0x8CD5
#define R_POLYGON_OFFSET_FILL 0x8037
#define R_SCISSOR_TEST 0x0C11
#define R_UNPACK_ALIGNMENT 0x0CF5
#define R_UNPACK_ROW_LENGTH 0x0CF2

#define GL_FUNCS(X) \
    X(void, ActiveTexture, (GLenum_ t)) \
    X(void, AttachShader, (GLuint_ p, GLuint_ s)) \
    X(void, BindBuffer, (GLenum_ t, GLuint_ b)) \
    X(void, BindTexture, (GLenum_ t, GLuint_ x)) \
    X(void, BindVertexArray, (GLuint_ a)) \
    X(void, BindFramebuffer, (GLenum_ t, GLuint_ f)) \
    X(GLenum_, CheckFramebufferStatus, (GLenum_ t)) \
    X(void, DrawBuffers, (GLsizei_ n, const GLenum_* b)) \
    X(void, FramebufferTexture2D, (GLenum_ t, GLenum_ a, GLenum_ tt, GLuint_ x, GLint_ l)) \
    X(void, GenFramebuffers, (GLsizei_ n, GLuint_* f)) \
    X(void, PolygonOffset, (float f, float u)) \
    X(void, ReadBuffer, (GLenum_ b)) \
    X(void, Viewport, (GLint_ x, GLint_ y, GLsizei_ w, GLsizei_ h)) \
    X(void, BufferData, (GLenum_ t, GLsizeiptr_ n, const void* d, GLenum_ u)) \
    X(void, BufferSubData, (GLenum_ t, GLsizeiptr_ o, GLsizeiptr_ n, const void* d)) \
    X(void, Clear, (uint32_t m)) \
    X(void, ColorMask, (GLboolean_ r, GLboolean_ g, GLboolean_ b, GLboolean_ a)) \
    X(void, CompileShader, (GLuint_ s)) \
    X(GLuint_, CreateProgram, (void)) \
    X(GLuint_, CreateShader, (GLenum_ t)) \
    X(void, CullFace, (GLenum_ m)) \
    X(void, DeleteShader, (GLuint_ s)) \
    X(void, DepthFunc, (GLenum_ f)) \
    X(void, DepthMask, (GLboolean_ f)) \
    X(void, Disable, (GLenum_ c)) \
    X(void, DrawElementsInstanced, (GLenum_ m, GLsizei_ n, GLenum_ t, const void* i, GLsizei_ c)) \
    X(void, Enable, (GLenum_ c)) \
    X(void, EnableVertexAttribArray, (GLuint_ i)) \
    X(void, FrontFace, (GLenum_ m)) \
    X(void, GenBuffers, (GLsizei_ n, GLuint_* b)) \
    X(void, GenTextures, (GLsizei_ n, GLuint_* t)) \
    X(void, GenVertexArrays, (GLsizei_ n, GLuint_* a)) \
    X(void, GenerateMipmap, (GLenum_ t)) \
    X(GLenum_, GetError, (void)) \
    X(void, GetFloatv, (GLenum_ p, float* v)) \
    X(void, GetProgramInfoLog, (GLuint_ p, GLsizei_ n, GLsizei_* l, char* s)) \
    X(void, GetProgramiv, (GLuint_ p, GLenum_ q, GLint_* v)) \
    X(void, GetShaderInfoLog, (GLuint_ s, GLsizei_ n, GLsizei_* l, char* t)) \
    X(void, GetShaderiv, (GLuint_ s, GLenum_ q, GLint_* v)) \
    X(GLint_, GetUniformLocation, (GLuint_ p, const char* n)) \
    X(void, LinkProgram, (GLuint_ p)) \
    X(void, PixelStorei, (GLenum_ p, GLint_ v)) \
    X(void, Scissor, (GLint_ x, GLint_ y, GLsizei_ w, GLsizei_ h)) \
    X(void, ShaderSource, (GLuint_ s, GLsizei_ n, const char* const* t, const GLint_* l)) \
    X(void, TexImage2D, (GLenum_ t, GLint_ l, GLint_ f, GLsizei_ w, GLsizei_ h, GLint_ b, GLenum_ pf, GLenum_ ty, const void* d)) \
    X(void, TexImage3D, (GLenum_ t, GLint_ l, GLint_ f, GLsizei_ w, GLsizei_ h, GLsizei_ dd, GLint_ b, GLenum_ pf, GLenum_ ty, const void* d)) \
    X(void, TexParameterf, (GLenum_ t, GLenum_ p, float v)) \
    X(void, TexParameteri, (GLenum_ t, GLenum_ p, GLint_ v)) \
    X(void, TexSubImage2D, (GLenum_ t, GLint_ l, GLint_ x, GLint_ y, GLsizei_ w, GLsizei_ h, GLenum_ pf, GLenum_ ty, const void* d)) \
    X(void, TexSubImage3D, (GLenum_ t, GLint_ l, GLint_ x, GLint_ y, GLint_ z, GLsizei_ w, GLsizei_ h, GLsizei_ dd, GLenum_ pf, GLenum_ ty, const void* d)) \
    X(void, Uniform1f, (GLint_ l, float v)) \
    X(void, Uniform1i, (GLint_ l, GLint_ v)) \
    X(void, Uniform3f, (GLint_ l, float x, float y, float z)) \
    X(void, Uniform3fv, (GLint_ l, GLsizei_ n, const float* v)) \
    X(void, Uniform4f, (GLint_ l, float x, float y, float z, float w)) \
    X(void, Uniform4fv, (GLint_ l, GLsizei_ n, const float* v)) \
    X(void, UseProgram, (GLuint_ p)) \
    X(void, VertexAttribDivisor, (GLuint_ i, GLuint_ d)) \
    X(void, VertexAttribIPointer, (GLuint_ i, GLint_ n, GLenum_ t, GLsizei_ s, const void* p)) \
    X(void, VertexAttribPointer, (GLuint_ i, GLint_ n, GLenum_ t, GLboolean_ nm, GLsizei_ s, const void* p)) \
    X(void, BindSampler, (GLuint_ u, GLuint_ s)) \
    X(void, BlendFunc, (GLenum_ s, GLenum_ d)) \
    X(void, BlendColor, (float r, float gg, float b, float a)) \
    X(void, BlendEquation, (GLenum_ m)) \
    X(void, DrawArrays, (GLenum_ m, GLint_ f, GLsizei_ n)) \
    X(void, DrawArraysInstanced, (GLenum_ m, GLint_ f, GLsizei_ n, GLsizei_ c)) \
    X(void, GenSamplers, (GLsizei_ n, GLuint_* s)) \
    X(void, SamplerParameteri, (GLuint_ s, GLenum_ p, GLint_ v)) \
    X(void, DeleteProgram, (GLuint_ p)) \
    X(void, DeleteBuffers, (GLsizei_ n, const GLuint_* b)) \
    X(void, DeleteTextures, (GLsizei_ n, const GLuint_* t)) \
    X(void, DeleteVertexArrays, (GLsizei_ n, const GLuint_* a)) \
    X(void, DeleteFramebuffers, (GLsizei_ n, const GLuint_* f)) \
    X(void, DeleteSamplers, (GLsizei_ n, const GLuint_* s))

#endif
