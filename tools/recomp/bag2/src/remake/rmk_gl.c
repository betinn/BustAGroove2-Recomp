/* Desenho GL do remake (núcleo portátil, rmk.h). Funciona em GL 3.3 core e GLES 3.0: o "#version" é
 * escolhido aqui, as funções vêm do carregador de quem chama e nada além do subconjunto comum é usado
 * (instancing, atributos inteiros, texelFetch, texture array; sem tessellation/geometry shader, SSBO).
 *
 * Phong Tessellation (Boubekeur & Alexa 2008) por instancing: os vértices são a grade baricêntrica fixa
 * de L = RMK_TESS_L (15 pontos, 16 triângulos), cada instância é uma face com os 3 índices de vértice,
 * UV, cores e camada como atributos por instância; o vertex shader lê posição/normal dos 3 cantos de uma
 * textura RGBA32F (8 floats por vértice, rmk_pack_geometry) e faz a interpolação de Phong. Luz por pixel
 * no espaço da câmera do GTE como nos renders offline (tools/remake/render.py, estilo "modern"): difusa
 * com wrap, especular GGX, preenchimento, contraluz, ambiente de hemisfério, oclusão ambiente, ACES.
 *
 * Ordem e profundidade: o modelo do PS1 foi feito para a ordem de pintor da OT (faces de chama e enfeite
 * ficam um pouco à frente da roupa, e o Phong da roupa passaria por elas num z-buffer comum). Por isso dois
 * passos, como o buffer de profundidade PGXP do framework: (1) só profundidade, verdadeira; (2) cor na
 * ordem de desenho do jogo (slot da OT (z0 + z1) >> 3 do mais longe ao mais perto, dentro do slot a face
 * exportada depois primeiro: a OT é reversa), com a profundidade puxada para a câmera por zbias (relativo
 * a z) e sem escrever profundidade. Dentro da tolerância a ordem de pintor decide; o que está claramente
 * atrás continua escondido. (3) Faces semitransparentes (RMK_MODE_SEMI, fase 4): os texels com o bit STP do PS1
 * (alfa do mapa de normal) saem dos passos 1 e 2 e são desenhados depois, na mesma ordem de pintor, com a mistura do
 * jogo (0,5 B + 0,5 F) e sem escrever profundidade nem o alfa; os outros texels da face ficam opacos, como no PS1.
 * Decalque semi (cor + textura): o que fica atrás dele no jogo é a cor base da própria face, então a mistura é feita
 * no shader, sem passo extra.
 *
 * Estado GL que rmk_gl_draw muda (quem chama restaura): programa, VAO, buffer de vértices, unidade ativa e
 * texturas das unidades 8 a 11, framebuffer e viewport (religados aos de quem chama, RmkTarget.fbo/viewport),
 * deslocamento de polígono, recorte (caixa), teste/função/máscara de profundidade, cull/front face,
 * blend (desligado; função, equação e cor da mistura mudam no passo 3) e stencil (desligado), máscara de cor. A
 * profundidade do FBO é limpa só no retângulo do desenho. */
#include "rmk.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    X(void, GenSamplers, (GLsizei_ n, GLuint_* s)) \
    X(void, SamplerParameteri, (GLuint_ s, GLenum_ p, GLint_ v))

#define GEO_RING 8                   /* texturas de geometria em rodízio (vários desenhos por quadro) */
#define SHADOW_SIZE 1024             /* mapa de sombra próprio (só o personagem) */
#define GRID_PTS ((RMK_TESS_L + 1) * (RMK_TESS_L + 2) / 2)
#define GRID_IDX (RMK_TESS_L * RMK_TESS_L * 3)

typedef struct {
    uint16_t vi[3], pad;
    float uv[6];
    uint8_t base[3][4], over[3][4];
    float layer, mode;
} Inst;                              /* 64 bytes por face */

struct RmkGL {
#define X(ret, name, args) ret(RGLAPI* name) args;
    GL_FUNCS(X)
#undef X
    void (RGLAPI* ClearDepthf)(float);
    void (RGLAPI* ClearDepth)(double);
    int gles, step, ready, failed, has_nmap;   /* has_nmap: mapas de normal na GPU (o asset pode soltar a cópia) */
    GLuint_ prog, vao, grid_vbo, grid_ibo, inst_vbo, tex, nmap, geo[GEO_RING], shadow_tex, shadow_fbo;
    float lbasis[3][3], lcenter[4];  /* espaço da luz da sombra do último quadro: u, v, direção; centro, 1/raio */
    int key;                         /* luz principal (a da sombra) do último quadro */
    int geo_w, geo_h, ring, have_serial, have_shadow, have_floor, floor_ok;
    uint32_t n_draw;                 /* faces ativas no último quadro (grupos do gesto) */
    uint32_t n_semi;                 /* delas, semitransparentes: repetidas depois das n_draw (passo 3) */
    uint32_t last_serial, shadow_serial, floor_serial;   /* quadro da geometria, da sombra própria e do chão */
    GLuint_ floor_prog, floor_vao, floor_tex, floor_fbo, floor_smp[2];
    GLint_ f_ndc, f_proj, f_quad, f_depth, f_shadow, f_lu, f_lv, f_ld, f_lc, f_fl;
    float fbasis[3][3], fcenter[4], fquad[4][3];   /* sombra no chão: espaço ortográfico e quadrilátero (câmera) */
    float* staging;
    Inst* inst;                      /* por face, na ordem do asset */
    Inst* inst_order;                /* reordenado por quadro (ordem de pintor do jogo), mais as semi de novo */
    uint16_t* slot;                  /* slot da OT por face, por quadro */
    GLint_ u_geo, u_geo_w, u_ndc, u_proj, u_depth, u_tex, u_nmap, u_nmap_on, u_misc, u_prepass;
    GLint_ u_key_dir, u_key_col, u_fill_dir, u_fill_col, u_rim_dir, u_rim_col, u_sky, u_ground;
    GLint_ u_stage, u_ldir, u_lcol, u_bk;
    GLint_ u_lu, u_lv, u_ld, u_lc, u_spass, u_shadow, u_shadow_on, u_key;
    GLint_ u_nrm, u_rm_rect, u_rm_off, u_semi;
    char err[512];
};

static const char* VS_BODY =
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp sampler2D u_geo;\n"
    "uniform int u_geo_w;\n"
    "uniform vec4 u_ndc;    /* NDC = tela * (x, z) + (y, w) */\n"
    "uniform vec4 u_proj;   /* OFX, OFY, H do GTE, deslocamento x na tela */\n"
    "uniform vec4 u_depth;  /* z de recorte = A z + B; alfa do Phong; zbias (fração de z) */\n"
    "uniform vec3 u_lu, u_lv, u_ld; /* base do espaço da luz da sombra (câmera) */\n"
    "uniform vec4 u_lc;     /* centro do personagem, 1/raio */\n"
    "uniform int u_spass;   /* 1: passo da sombra (posição no espaço da luz, ortográfica) */\n"
    "layout(location = 0) in vec3 a_bary;\n"
    "layout(location = 1) in uvec3 a_vi;\n"
    "layout(location = 2) in vec4 a_uv01;\n"
    "layout(location = 3) in vec2 a_uv2;\n"
    "layout(location = 4) in vec4 a_base0;\n"
    "layout(location = 5) in vec4 a_base1;\n"
    "layout(location = 6) in vec4 a_base2;\n"
    "layout(location = 7) in vec4 a_over0;\n"
    "layout(location = 8) in vec4 a_over1;\n"
    "layout(location = 9) in vec4 a_over2;\n"
    "layout(location = 10) in vec2 a_lm;\n"
    "out vec3 v_pos; out vec3 v_nrm; out vec3 v_base; out vec3 v_over; out vec2 v_uv; out float v_ao; out vec3 v_lpos;\n"
    "flat out int v_layer; flat out int v_mode;\n"
    "invariant gl_Position;   /* faces com os mesmos vértices (decalques) dão a mesma profundidade */\n"
    "vec4 geo(uint i) { int k = int(i); return texelFetch(u_geo, ivec2(k % u_geo_w, k / u_geo_w), 0); }\n"
    "void main() {\n"
    "  vec4 p0 = geo(a_vi.x * 2u), p1 = geo(a_vi.y * 2u), p2 = geo(a_vi.z * 2u);\n"
    "  vec3 n0 = geo(a_vi.x * 2u + 1u).xyz, n1 = geo(a_vi.y * 2u + 1u).xyz, n2 = geo(a_vi.z * 2u + 1u).xyz;\n"
    "  vec3 b = a_bary;\n"
    "  vec3 q = p0.xyz * b.x + p1.xyz * b.y + p2.xyz * b.z;\n"
    "  vec3 ph = b.x * (q - dot(q - p0.xyz, n0) * n0) + b.y * (q - dot(q - p1.xyz, n1) * n1)\n"
    "          + b.z * (q - dot(q - p2.xyz, n2) * n2);\n"
    "  vec3 p = mix(q, ph, u_depth.z);\n"
    "  v_pos = p;\n"
    "  v_nrm = n0 * b.x + n1 * b.y + n2 * b.z;\n"
    "  v_ao = p0.w * b.x + p1.w * b.y + p2.w * b.z;\n"
    "  v_base = a_base0.rgb * b.x + a_base1.rgb * b.y + a_base2.rgb * b.z;\n"
    "  v_over = (a_over0.rgb * b.x + a_over1.rgb * b.y + a_over2.rgb * b.z) * (255.0 / 128.0);\n"
    "  v_uv = (a_uv01.xy * b.x + a_uv01.zw * b.y + a_uv2 * b.z + 0.5) / 256.0;\n"
    "  v_layer = int(floor(a_lm.x + 0.5)); v_mode = int(floor(a_lm.y + 0.5));\n"
    "  float z = max(p.z, 1.0);\n"
    "  vec2 s = u_proj.xy + u_proj.z * p.xy / z + vec2(u_proj.w, 0.0);\n"
    "  /* ponto escalado para a câmera: mesma posição na tela, mais perto (tolerância do passo de cor) */\n"
    "  float zd = z * (1.0 - u_depth.w);\n"
    "  vec3 r = p - u_lc.xyz;\n"
    "  vec3 lq = vec3(dot(r, u_lu), dot(r, u_lv), dot(r, u_ld)) * u_lc.w;\n"
    "  v_lpos = lq * 0.5 + 0.5;\n"
    "  if (u_spass != 0) gl_Position = vec4(lq, 1.0);\n"
    "  else gl_Position = vec4((s * u_ndc.xz + u_ndc.yw) * zd, u_depth.x * zd + u_depth.y, zd);\n"
    "}\n";

static const char* FS_BODY =
    "precision highp float;\n"
    "precision mediump sampler2DArray;\n"
    "uniform mediump sampler2DArray u_tex;\n"
    "uniform mediump sampler2DArray u_nmap;\n"
    "uniform float u_nmap_on; /* 1: mapas de normal das camadas */\n"
    "precision mediump sampler2DShadow;\n"
    "uniform mediump sampler2DShadow u_shadow;\n"
    "uniform float u_shadow_on;\n"
    "uniform int u_key;     /* luz que faz a sombra */\n"
    "uniform vec3 u_key_dir, u_key_col, u_fill_dir, u_fill_col, u_rim_dir, u_rim_col, u_sky, u_ground;\n"
    "uniform vec4 u_misc;   /* exposição, rugosidade, textura ligada, contraluz */\n"
    "uniform int u_stage;   /* 1: luzes do palco (GTE); 0: luz fixa na câmera da fase 1 */\n"
    "uniform vec3 u_ldir[3], u_lcol[3], u_bk;\n"
    "uniform int u_prepass; /* 1: só profundidade (o recorte por alfa vale igual) */\n"
    "uniform int u_semi;    /* 0: passos opacos (texel com STP de face semi sai), 1: passo 3 (só ele), -1: sem semi */\n"
    "/* expressão do rosto: casa da página (x0, y0, x1, y1 em texels) e deslocamento até a variante que o jogo\n"
    "   copiou para ela (du, dv, página, meio texel HD para não sangrar a borda) */\n"
    "uniform int u_nrm;\n"
    "uniform vec4 u_rm_rect[8], u_rm_off[8];\n"
    "in vec3 v_pos; in vec3 v_nrm; in vec3 v_base; in vec3 v_over; in vec2 v_uv; in float v_ao; in vec3 v_lpos;\n"
    "flat in int v_layer; flat in int v_mode;\n"
    "layout(location = 0) out vec4 frag;\n"
    "vec2 remap(vec2 t, int pg) {   /* t em texels da página; modo da face >> 3 = página */\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    if (i >= u_nrm) break;\n"
    "    vec4 r = u_rm_rect[i], o = u_rm_off[i];\n"
    "    if (int(o.z) == pg && all(greaterThanEqual(t, r.xy)) && all(lessThan(t, r.zw)))\n"
    "      return clamp(t, r.xy + o.w, r.zw - o.w) + o.xy;\n"
    "  }\n"
    "  return t;\n"
    "}\n"
    "float shadow_at() {   /* 5 amostras com comparação bilinear: borda macia de ~3 texels */\n"
    "  if (u_shadow_on < 0.5) return 1.0;\n"
    "  vec3 q = v_lpos; q.z -= 0.0025;\n"
    "  float t = 1.5 / 1024.0, s = texture(u_shadow, q) * 2.0;\n"
    "  s += texture(u_shadow, q + vec3(t, t, 0.0)) + texture(u_shadow, q + vec3(-t, t, 0.0));\n"
    "  s += texture(u_shadow, q + vec3(t, -t, 0.0)) + texture(u_shadow, q + vec3(-t, -t, 0.0));\n"
    "  return s / 6.0;\n"
    "}\n"
    "float D_ggx(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (3.14159 * d * d); }\n"
    "/* base tangente pelas derivadas da tela (Schüler): sem tangente por vértice, vale com UV espelhado */\n"
    "vec3 perturb(vec3 n, vec3 p, vec2 uv, vec3 t) {\n"
    "  vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 du1 = dFdx(uv), du2 = dFdy(uv);\n"
    "  vec3 dp2p = cross(dp2, n), dp1p = cross(n, dp1);\n"
    "  vec3 T = dp2p * du1.x + dp1p * du2.x, B = dp2p * du1.y + dp1p * du2.y;\n"
    "  float im = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));\n"
    "  return normalize(T * im * t.x + B * im * t.y + n * t.z);\n"
    "}\n"
    "vec3 light(vec3 n, vec3 v, vec3 l, vec3 col, vec3 alb, float wrap) {\n"
    "  float nl = dot(n, l);\n"
    "  float d = max((nl + wrap) / (1.0 + wrap), 0.0);\n"
    "  vec3 h = normalize(l + v);\n"
    "  float nh = max(dot(n, h), 0.0), nv = max(dot(n, v), 1e-3);\n"
    "  float F = 0.04 + 0.96 * pow(1.0 - max(dot(h, v), 0.0), 5.0);\n"
    "  float a = u_misc.y * u_misc.y;\n"
    "  float spec = D_ggx(nh, a) * F * 0.25 / max(nv, 0.2);\n"
    "  return col * (alb * d + spec * max(nl, 0.0));\n"
    "}\n"
    "void main() {\n"
    "  vec3 base = clamp(v_base, 0.0, 1.0);\n"
    "  int md = v_mode & 3;\n"
    "  float tex_a = 0.0;\n"
    "  vec2 gx = dFdx(v_uv), gy = dFdy(v_uv);   /* do UV sem remapear: o salto na borda da casa não muda o mip */\n"
    "  vec2 uv = u_nrm > 0 ? remap(v_uv * 256.0, v_mode >> 3) / 256.0 : v_uv;\n"
    "  if (md > 0 && v_layer >= 0 && u_misc.z > 0.5) {\n"
    "    vec4 t = textureGrad(u_tex, vec3(uv, float(v_layer)), gx, gy);\n"
    "    if (md == 2) { if (t.a < 0.5) discard; base = t.rgb * v_over; tex_a = 1.0; }\n"
    "    else {   /* decalque: com STP e semi (512), 0,5 cor base + 0,5 textura, como o GT3 semi do jogo sobre o G3 */\n"
    "      float k = t.a;\n"
    "      if ((v_mode & 512) != 0 && u_semi >= 0 && textureGrad(u_nmap, vec3(uv, float(v_layer)), gx, gy).a > 0.5) k *= 0.5;\n"
    "      base = mix(base, t.rgb * v_over, k); tex_a = t.a;\n"
    "    }\n"
    "  }\n"
    "  if (u_semi >= 0) {   /* semitransparência do PS1: só os texels com STP (sem textura: a face inteira) */\n"
    "    bool stp = (v_mode & 256) != 0 && (md == 0 || v_layer < 0 || u_misc.z < 0.5 ||\n"
    "               textureGrad(u_nmap, vec3(uv, float(v_layer)), gx, gy).a > 0.5);\n"
    "    if (stp != (u_semi == 1)) discard;\n"
    "  }\n"
    "  if (u_prepass != 0) { frag = vec4(0.0); return; }\n"
    "  vec3 alb = pow(clamp(base, 0.0, 1.0), vec3(2.2));\n"
    "  vec3 n = normalize(v_nrm), v = normalize(-v_pos);\n"
    "  if (dot(n, v) < 0.0) n = -n;\n"
    "  if (tex_a > 0.5 && u_nmap_on > 0.5 && (v_mode & 4) == 0) {\n"
    "    vec3 t = textureGrad(u_nmap, vec3(uv, float(v_layer)), gx, gy).xyz * 2.0 - 1.0;\n"
    "    n = perturb(n, v_pos, v_uv, t);\n"
    "  }\n"
    "  vec3 c;\n"
    "  float sh = shadow_at();\n"
    "  if (u_stage != 0) {\n"
    "    /* a soma do GTE (BK + cor * max(n.l, 0) por luz), por pixel e no espaço de gama como no PS1; luz de cor\n"
    "       positiva com um pouco de wrap; a negativa (o jogo usa uma para escurecer o lado de trás) sem */\n"
    "    vec3 I = u_bk * mix(0.6, 1.0, v_ao), sp = vec3(0.0);\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "      float nl = dot(n, u_ldir[i]);\n"
    "      vec3 pos = max(u_lcol[i], 0.0), neg = min(u_lcol[i], 0.0);\n"
    "      float s = i == u_key ? sh : 1.0;\n"
    "      I += pos * max((nl + 0.2) / 1.2, 0.0) * s + neg * max(nl, 0.0);\n"
    "      sp += light(n, v, u_ldir[i], pos, vec3(0.0), 0.0) * s;   /* só o especular */\n"
    "    }\n"
    "    c = alb * pow(max(I, 0.0), vec3(2.2)) + sp * 1.6;\n"
    "  } else {\n"
    "    c = light(n, v, u_key_dir, u_key_col, alb, 0.25) * sh;\n"
    "    c += light(n, v, u_fill_dir, u_fill_col, alb, 0.5);\n"
    "    c += mix(u_ground, u_sky, -n.y * 0.5 + 0.5) * alb * v_ao;   /* y da câmera do GTE aponta para baixo */\n"
    "  }\n"
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * max(dot(n, u_rim_dir), 0.0);\n"
    "  c += u_rim_col * rim * (0.3 + alb) * v_ao * u_misc.w;\n"
    "  c *= mix(0.55, 1.0, v_ao);\n"
    "  c *= u_misc.x;\n"
    "  c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);\n"
    "  frag = vec4(pow(c, vec3(1.0 / 2.2)), 0.0);   /* alfa = bit 15 do PS1 (máscara) desligado */\n"
    "}\n";

const char* rmk_gl_error(const RmkGL* g) { return g ? g->err : "sem contexto"; }

RmkGL* rmk_gl_create(RmkGetProc get_proc, int gles) {
    RmkGL* g = (RmkGL*)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->gles = gles;
    int ok = 1;
#define X(ret, name, args) \
    if (!(g->name = (ret(RGLAPI*) args)get_proc("gl" #name))) { ok = 0; snprintf(g->err, sizeof g->err, "sem gl%s", #name); }
    GL_FUNCS(X)
#undef X
    g->ClearDepthf = (void(RGLAPI*)(float))get_proc("glClearDepthf");
    g->ClearDepth = (void(RGLAPI*)(double))get_proc("glClearDepth");
    if (!ok) g->failed = 1;
    return g;
}

static GLuint_ compile(RmkGL* g, GLenum_ type, const char* body) {
    const char* src[2] = {g->gles ? "#version 300 es\n" : "#version 330 core\n", body};
    GLuint_ s = g->CreateShader(type);
    g->ShaderSource(s, 2, src, NULL);
    g->CompileShader(s);
    GLint_ ok = 0;
    g->GetShaderiv(s, R_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400];
        GLsizei_ n = 0;
        g->GetShaderInfoLog(s, sizeof log, &n, log);
        snprintf(g->err, sizeof g->err, "%s: %.*s", type == R_VERTEX_SHADER ? "VS" : "FS", (int)n, log);
        g->DeleteShader(s);
        return 0;
    }
    return s;
}

static int build_program(RmkGL* g) {
    GLuint_ vs = compile(g, R_VERTEX_SHADER, VS_BODY), fs = vs ? compile(g, R_FRAGMENT_SHADER, FS_BODY) : 0;
    if (!vs || !fs) return 0;
    g->prog = g->CreateProgram();
    g->AttachShader(g->prog, vs);
    g->AttachShader(g->prog, fs);
    g->LinkProgram(g->prog);
    g->DeleteShader(vs);
    g->DeleteShader(fs);
    GLint_ ok = 0;
    g->GetProgramiv(g->prog, R_LINK_STATUS, &ok);
    if (!ok) {
        char log[400];
        GLsizei_ n = 0;
        g->GetProgramInfoLog(g->prog, sizeof log, &n, log);
        snprintf(g->err, sizeof g->err, "link: %.*s", (int)n, log);
        return 0;
    }
#define U(name) g->name = g->GetUniformLocation(g->prog, #name)
    U(u_geo); U(u_geo_w); U(u_ndc); U(u_proj); U(u_depth); U(u_tex); U(u_nmap); U(u_nmap_on); U(u_misc); U(u_prepass);
    U(u_key_dir); U(u_key_col); U(u_fill_dir); U(u_fill_col); U(u_rim_dir); U(u_rim_col); U(u_sky); U(u_ground);
    U(u_stage); U(u_bk);
    U(u_lu); U(u_lv); U(u_ld); U(u_lc); U(u_spass); U(u_shadow); U(u_shadow_on); U(u_key); U(u_nrm); U(u_semi);
    g->u_ldir = g->GetUniformLocation(g->prog, "u_ldir[0]");
    g->u_lcol = g->GetUniformLocation(g->prog, "u_lcol[0]");
    g->u_rm_rect = g->GetUniformLocation(g->prog, "u_rm_rect[0]");
    g->u_rm_off = g->GetUniformLocation(g->prog, "u_rm_off[0]");
#undef U
    return 1;
}

/* Atributos por instância a partir da instância `first` do buffer (VAO e buffer de instâncias ligados). O passo 3
 * desenha a faixa das semi assim (sem baseInstance, que o GL 3.3 e o GLES 3.0 não têm). */
static void inst_attribs(RmkGL* g, uint32_t first) {
    const GLsizei_ st = (GLsizei_)sizeof(Inst);
    const size_t b = (size_t)first * sizeof(Inst);
    g->VertexAttribIPointer(1, 3, R_UNSIGNED_SHORT, st, (const void*)(b + offsetof(Inst, vi)));
    g->VertexAttribPointer(2, 4, R_FLOAT, 0, st, (const void*)(b + offsetof(Inst, uv)));
    g->VertexAttribPointer(3, 2, R_FLOAT, 0, st, (const void*)(b + offsetof(Inst, uv) + 16));
    for (int k = 0; k < 3; k++) {
        g->VertexAttribPointer(4 + k, 4, R_UNSIGNED_BYTE, 1, st, (const void*)(b + offsetof(Inst, base) + 4 * k));
        g->VertexAttribPointer(7 + k, 4, R_UNSIGNED_BYTE, 1, st, (const void*)(b + offsetof(Inst, over) + 4 * k));
    }
    g->VertexAttribPointer(10, 2, R_FLOAT, 0, st, (const void*)(b + offsetof(Inst, layer)));
}

/* Grade baricêntrica: ponto (i, j) = (1 - i/L - j/L, i/L, j/L); triângulos com a mesma orientação da face. */
static int gi(int i, int j) { return i * (RMK_TESS_L + 1) - i * (i - 1) / 2 + j; }

static int build_buffers(RmkGL* g, const RmkAsset* a) {
    float grid[GRID_PTS * 3];
    uint16_t idx[GRID_IDX];
    int n = 0, m = 0;
    const int L = RMK_TESS_L;
    for (int i = 0; i <= L; i++)
        for (int j = 0; j <= L - i; j++, n++) {
            grid[3 * n + 1] = (float)i / L; grid[3 * n + 2] = (float)j / L;
            grid[3 * n] = 1.0f - grid[3 * n + 1] - grid[3 * n + 2];
        }
    for (int i = 0; i < L; i++)
        for (int j = 0; j < L - i; j++) {
            idx[m++] = (uint16_t)gi(i, j); idx[m++] = (uint16_t)gi(i + 1, j); idx[m++] = (uint16_t)gi(i, j + 1);
            if (j < L - i - 1) {
                idx[m++] = (uint16_t)gi(i + 1, j); idx[m++] = (uint16_t)gi(i + 1, j + 1); idx[m++] = (uint16_t)gi(i, j + 1);
            }
        }
    /* Uma instância por face; a ordem no buffer é a de pintor do jogo, refeita a cada quadro (order_faces). */
    Inst* inst = g->inst = (Inst*)calloc(a->n_tris, sizeof(Inst));
    g->inst_order = (Inst*)calloc(2u * a->n_tris, sizeof(Inst));
    g->slot = (uint16_t*)calloc(a->n_tris, sizeof(uint16_t));
    if (!inst || !g->inst_order || !g->slot) return 0;
    for (uint32_t f = 0; f < a->n_tris; f++) {
        const RmkTri* t = &a->tris[f];
        Inst* o = &inst[f];
        for (int k = 0; k < 3; k++) {
            o->vi[k] = t->v[k];
            o->uv[2 * k] = t->uv[k][0]; o->uv[2 * k + 1] = t->uv[k][1];
            for (int c = 0; c < 3; c++) { o->base[k][c] = t->base[k][c]; o->over[k][c] = t->over[k][c]; }
            o->base[k][3] = o->over[k][3] = 255;
        }
        o->layer = (float)t->layer;
        /* modo | página << 3: a página da camada (expressão do rosto); 31 = nenhuma, nunca casa com um remapeamento */
        const unsigned page = t->layer >= 0 && a->layer_page[t->layer] != RMK_NO_PAGE ? a->layer_page[t->layer] : 31u;
        /* semi: 256 = face no passo 3 (só textura ou cor); 512 = decalque semi (mistura com a cor base no shader) */
        const unsigned semi = (t->mode & RMK_MODE_SEMI) ? ((t->mode & 3u) == 1u ? 512u : 256u) : 0u;
        o->mode = (float)((t->mode & 7u) | page << 3 | semi);
    }
    g->GenVertexArrays(1, &g->vao);
    g->BindVertexArray(g->vao);
    g->GenBuffers(1, &g->grid_vbo);
    g->BindBuffer(R_ARRAY_BUFFER, g->grid_vbo);
    g->BufferData(R_ARRAY_BUFFER, sizeof grid, grid, R_STATIC_DRAW);
    g->EnableVertexAttribArray(0);
    g->VertexAttribPointer(0, 3, R_FLOAT, 0, 12, (const void*)0);
    g->GenBuffers(1, &g->grid_ibo);
    g->BindBuffer(R_ELEMENT_ARRAY_BUFFER, g->grid_ibo);
    g->BufferData(R_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, R_STATIC_DRAW);
    g->GenBuffers(1, &g->inst_vbo);
    g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
    g->BufferData(R_ARRAY_BUFFER, (GLsizeiptr_)(2u * sizeof(Inst) * a->n_tris), NULL, R_STREAM_DRAW);
    for (int i = 1; i <= 10; i++) g->EnableVertexAttribArray((GLuint_)i);
    inst_attribs(g, 0);
    for (int i = 1; i <= 10; i++) g->VertexAttribDivisor((GLuint_)i, 1);
    g->BindVertexArray(0);
    g->BindBuffer(R_ARRAY_BUFFER, 0);
    /* geometria por quadro: 2 texels RGBA32F por vértice (posição + AO, normal) */
    const int texels = 2 * (int)a->n_verts;
    g->geo_w = texels < 1024 ? texels : 1024;
    g->geo_h = (texels + g->geo_w - 1) / g->geo_w;
    g->staging = (float*)calloc((size_t)g->geo_w * g->geo_h * 4u, sizeof(float));
    if (!g->staging) return 0;
    g->GenTextures(GEO_RING, g->geo);
    g->ActiveTexture(R_TEXTURE0 + 8);
    for (int r = 0; r < GEO_RING; r++) {
        g->BindTexture(R_TEXTURE_2D, g->geo[r]);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MIN_FILTER, R_NEAREST);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MAG_FILTER, R_NEAREST);
        g->TexImage2D(R_TEXTURE_2D, 0, R_RGBA32F, g->geo_w, g->geo_h, 0, R_RGBA, R_FLOAT, NULL);
    }
    g->BindTexture(R_TEXTURE_2D, 0);
    /* camadas HD: um texture array, enchido uma camada por chamada de rmk_gl_prepare */
    g->GenTextures(1, &g->tex);
    g->ActiveTexture(R_TEXTURE0 + 9);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->tex);
    g->TexImage3D(R_TEXTURE_2D_ARRAY, 0, R_RGBA8, (GLsizei_)a->tex_size, (GLsizei_)a->tex_size,
                  (GLsizei_)(a->n_layers ? a->n_layers : 1), 0, R_RGBA, R_UNSIGNED_BYTE, NULL);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MIN_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MAG_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
    /* mapas de normal (mesmo tamanho; se o asset não os tem, a textura fica 1x1 e o shader não lê) */
    g->GenTextures(1, &g->nmap);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->nmap);
    g->has_nmap = a->nmaps != NULL;
    const GLsizei_ ns = a->nmaps ? (GLsizei_)a->tex_size : 1;
    g->TexImage3D(R_TEXTURE_2D_ARRAY, 0, R_RGBA8, ns, ns, (GLsizei_)(a->n_layers ? a->n_layers : 1), 0, R_RGBA,
                  R_UNSIGNED_BYTE, NULL);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MIN_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MAG_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
    g->BindTexture(R_TEXTURE_2D_ARRAY, 0);
    /* mapa de sombra próprio: textura de profundidade com comparação (PCF bilinear do hardware) */
    g->GenTextures(1, &g->shadow_tex);
    g->ActiveTexture(R_TEXTURE0 + 11);
    g->BindTexture(R_TEXTURE_2D, g->shadow_tex);
    g->TexImage2D(R_TEXTURE_2D, 0, R_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, R_DEPTH_COMPONENT, R_UNSIGNED_INT, NULL);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MIN_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MAG_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_COMPARE_MODE, R_COMPARE_REF_TO_TEXTURE);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_COMPARE_FUNC, R_LEQUAL);
    g->BindTexture(R_TEXTURE_2D, 0);
    g->GenFramebuffers(1, &g->shadow_fbo);
    g->BindFramebuffer(R_FRAMEBUFFER, g->shadow_fbo);
    g->FramebufferTexture2D(R_FRAMEBUFFER, R_DEPTH_ATTACHMENT, R_TEXTURE_2D, g->shadow_tex, 0);
    const GLenum_ none = 0;
    g->DrawBuffers(1, &none);
    g->ReadBuffer(0);
    const int fbo_ok = g->CheckFramebufferStatus(R_FRAMEBUFFER) == R_FRAMEBUFFER_COMPLETE;
    g->BindFramebuffer(R_FRAMEBUFFER, 0);   /* só no preparo, fora do desenho do jogo: quem chama religa o seu */
    if (!fbo_ok) { snprintf(g->err, sizeof g->err, "mapa de sombra: FBO incompleto"); return 0; }
    return g->GetError() == 0;
}

static int build_floor(RmkGL* g);

int rmk_gl_prepare(RmkGL* g, const RmkAsset* a) {
    if (!g || g->failed) return 0;
    if (g->ready) return 1;
    while (g->GetError() != 0) {}
    g->PixelStorei(R_UNPACK_ALIGNMENT, 4);
    g->PixelStorei(R_UNPACK_ROW_LENGTH, 0);
    if (g->step == 0) {
        if (!build_program(g) || !build_buffers(g, a) || !build_floor(g)) {
            if (!g->err[0]) snprintf(g->err, sizeof g->err, "recursos GL: erro 0x%X", g->GetError());
            g->failed = 1;
            return 0;
        }
        g->step = 1;
        return 0;
    }
    /* passos: 1..n_layers camadas de cor, depois n_layers mapas de normal (se houver), depois mipmaps */
    g->ActiveTexture(R_TEXTURE0 + 9);
    const uint32_t k = (uint32_t)g->step - 1u, nm = a->nmaps ? a->n_layers : 0u;
    const size_t bytes = (size_t)a->tex_size * a->tex_size * 4u;
    if (k < a->n_layers + nm) {
        const int is_n = k >= a->n_layers;
        const uint32_t layer = is_n ? k - a->n_layers : k;
        g->BindTexture(R_TEXTURE_2D_ARRAY, is_n ? g->nmap : g->tex);
        g->TexSubImage3D(R_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint_)layer, (GLsizei_)a->tex_size, (GLsizei_)a->tex_size, 1,
                         R_RGBA, R_UNSIGNED_BYTE, (is_n ? a->nmaps : a->layers) + bytes * layer);
        g->step++;
    } else {
        float an = 0.0f;
        g->GetFloatv(R_MAX_TEXTURE_MAX_ANISOTROPY, &an);
        const int aniso = g->GetError() == 0 && an > 1.0f;
        for (int t = 0; t < (nm ? 2 : 1); t++) {
            g->BindTexture(R_TEXTURE_2D_ARRAY, t ? g->nmap : g->tex);
            g->GenerateMipmap(R_TEXTURE_2D_ARRAY);
            g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MIN_FILTER, R_LINEAR_MIPMAP_LINEAR);
            if (aniso) g->TexParameterf(R_TEXTURE_2D_ARRAY, R_TEXTURE_MAX_ANISOTROPY, an < 8.0f ? an : 8.0f);
        }
        while (g->GetError() != 0) {}
        g->ready = 1;
    }
    g->BindTexture(R_TEXTURE_2D_ARRAY, 0);
    if (g->GetError() != 0) { snprintf(g->err, sizeof g->err, "textura HD: erro GL"); g->failed = 1; return 0; }
    return g->ready;
}

static int ifloor(float f) { int i = (int)f; return (float)i > f ? i - 1 : i; }

/* Ordem de desenho do jogo (FUN_80036230 + OT reversa): slot (z0 + z1) >> 3 do maior para o menor; dentro
 * do slot, a face ligada por último (maior índice do asset) primeiro. Contagem por slot, estável. Só as
 * faces dos grupos ativos; devolve quantas. */
#define SLOTS 4200
#define SLOT_OFF 0xFFFFu
static uint32_t order_faces(RmkGL* g, const RmkAsset* a, const float* pos, uint32_t mask) {
    static uint32_t start[SLOTS + 1];
    memset(start, 0, sizeof start);
    uint32_t n = 0;
    for (uint32_t f = 0; f < a->n_tris; f++) {
        const RmkTri* t = &a->tris[f];
        if (!RMK_FACE_ON(t, mask)) { g->slot[f] = SLOT_OFF; continue; }
        int s = ((int)pos[3 * t->zp[0] + 2] + (int)pos[3 * t->zp[1] + 2]) >> 3;
        s = s < 0 ? 0 : s >= SLOTS ? SLOTS - 1 : s;
        g->slot[f] = (uint16_t)(SLOTS - 1 - s);           /* crescente = do mais longe ao mais perto */
        start[g->slot[f] + 1]++;
        n++;
    }
    for (int s = 0; s < SLOTS; s++) start[s + 1] += start[s];
    for (uint32_t k = a->n_tris; k-- > 0;)
        if (g->slot[k] != SLOT_OFF) g->inst_order[start[g->slot[k]]++] = g->inst[k];
    /* as semitransparentes de novo depois das n, na mesma ordem (passo 3) */
    g->n_semi = 0;
    for (uint32_t k = 0; k < n; k++)
        if ((int)g->inst_order[k].mode & 256) g->inst_order[n + g->n_semi++] = g->inst_order[k];
    return n;
}

/* Luz principal (maior cor positiva) e espaço ortográfico dela em volta do personagem: u, v perpendiculares à
 * direção da luz para a cena, origem no centro da caixa dos vértices ativos, escala 1/raio (com folga para o
 * Phong). Na luz fixa da fase 1 a principal é a chave dela. */
static void light_space(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos) {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (uint32_t gi = 0; gi <= a->n_groups; gi++) {
        if (gi && !(((fr->groups | 1u) >> gi) & 1u)) continue;
        const uint32_t i0 = gi ? a->groups[gi].v0 : 0, i1 = gi ? i0 + a->groups[gi].nv : a->n_body;
        for (uint32_t i = i0; i < i1; i++)
            for (int k = 0; k < 3; k++) {
                if (pos[3 * i + k] < lo[k]) lo[k] = pos[3 * i + k];
                if (pos[3 * i + k] > hi[k]) hi[k] = pos[3 * i + k];
            }
    }
    float d[3];
    g->key = 0;
    if (!(fr->flags & RMK_FLAG_FIXED_LIGHT)) {
        float best = -1e9f;
        for (int i = 0; i < 3; i++) {
            const float s = fr->lcol[i][0] + fr->lcol[i][1] + fr->lcol[i][2];
            if (s > best) { best = s; g->key = i; }
        }
        for (int k = 0; k < 3; k++) d[k] = -fr->ldir[g->key][k];
    } else {
        static const float kd[3] = {-0.45f, -0.80f, -0.55f};
        for (int k = 0; k < 3; k++) d[k] = -kd[k];
    }
    float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int k = 0; k < 3; k++) d[k] /= l;
    /* u = d x (0, 1, 0) (ou x se a luz vier de cima), v = u x d */
    float up[3] = {0.0f, 1.0f, 0.0f};
    if (fabsf(d[1]) > 0.95f) { up[0] = 1.0f; up[1] = 0.0f; }
    float u[3] = {d[1] * up[2] - d[2] * up[1], d[2] * up[0] - d[0] * up[2], d[0] * up[1] - d[1] * up[0]};
    l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (int k = 0; k < 3; k++) u[k] /= l;
    const float v[3] = {u[1] * d[2] - u[2] * d[1], u[2] * d[0] - u[0] * d[2], u[0] * d[1] - u[1] * d[0]};
    float r = 0.0f;
    for (int k = 0; k < 3; k++) {
        g->lcenter[k] = 0.5f * (lo[k] + hi[k]);
        r += (hi[k] - lo[k]) * (hi[k] - lo[k]);
    }
    r = 0.5f * sqrtf(r) * 1.06f + 1.0f;
    g->lcenter[3] = 1.0f / r;
    memcpy(g->lbasis[0], u, sizeof u); memcpy(g->lbasis[1], v, sizeof v); memcpy(g->lbasis[2], d, sizeof d);
}

/* Geometria, ordem de pintor e espaço da luz do quadro: uma vez por quadro (o espelho da tela larga e a sombra no
 * chão do mesmo quadro reaproveitam). */
static void frame_setup(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos) {
    if (g->have_serial && fr->serial == g->last_serial) return;
    g->ring = (g->ring + 1) % GEO_RING;
    rmk_pack_geometry(a, pos, fr->groups | 1u, g->staging);
    g->PixelStorei(R_UNPACK_ALIGNMENT, 4);
    g->PixelStorei(R_UNPACK_ROW_LENGTH, 0);
    g->ActiveTexture(R_TEXTURE0 + 8);
    g->BindTexture(R_TEXTURE_2D, g->geo[g->ring]);
    g->TexSubImage2D(R_TEXTURE_2D, 0, 0, 0, g->geo_w, g->geo_h, R_RGBA, R_FLOAT, g->staging);
    g->n_draw = order_faces(g, a, pos, fr->groups | 1u);
    light_space(g, a, fr, pos);
    g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
    g->BufferData(R_ARRAY_BUFFER, (GLsizeiptr_)(2u * sizeof(Inst) * a->n_tris), NULL, R_STREAM_DRAW);   /* órfão */
    g->BufferSubData(R_ARRAY_BUFFER, 0, (GLsizeiptr_)(sizeof(Inst) * (g->n_draw + g->n_semi)), g->inst_order);
    g->last_serial = fr->serial;
    g->have_serial = 1;
}

/* ---- sombra no chão (fase 3) ----------------------------------------------------------------------------- */
/* O jogo projeta uma cópia achatada do esqueleto no plano y = 0 do mundo, na direção da luz dele (FUN_8002A46C), e
 * a desenha por subtração no slot 4167 da OT. Aqui: mapa de sombra ortográfico próprio nessa mesma direção, em volta
 * do personagem, e um quadrilátero no plano do chão cobrindo a projeção dele; o fragment shader faz PCSS (busca de
 * bloqueadores, penumbra proporcional à distância entre o bloqueador e o chão: nítida no pé, macia na cabeça) e
 * escurece o palco já desenhado por blend (cor × (1 - a)), sem escrever o alfa. */
static const char* FLOOR_VS =
    "precision highp float;\n"
    "uniform vec4 u_ndc, u_proj;\n"
    "uniform vec3 u_quad[4];\n"
    "out vec3 v_pos;\n"
    "void main() {\n"
    "  vec3 p = u_quad[gl_VertexID];\n"
    "  v_pos = p;\n"
    "  float z = max(p.z, 1.0);\n"
    "  vec2 s = u_proj.xy + u_proj.z * p.xy / z + vec2(u_proj.w, 0.0);\n"
    "  gl_Position = vec4((s * u_ndc.xz + u_ndc.yw) * z, 0.0, z);\n"
    "}\n";

static const char* FLOOR_FS =
    "precision highp float;\n"
    "uniform highp sampler2D u_fdepth;\n"
    "uniform highp sampler2DShadow u_fshadow;\n"
    "uniform vec3 u_lu, u_lv, u_ld;\n"
    "uniform vec4 u_lc;\n"
    "uniform vec4 u_fl;   /* opacidade, penumbra por unidade de profundidade, penumbra mínima, raio da busca */\n"
    "in vec3 v_pos;\n"
    "layout(location = 0) out vec4 frag;\n"
    "const vec2 P[16] = vec2[16](vec2(-0.942, -0.399), vec2(0.946, -0.769), vec2(-0.094, -0.929), vec2(0.345, 0.294),\n"
    "  vec2(-0.916, 0.458), vec2(-0.815, -0.879), vec2(-0.383, 0.277), vec2(0.975, 0.756), vec2(0.443, -0.975),\n"
    "  vec2(0.537, -0.474), vec2(-0.265, -0.419), vec2(0.792, 0.191), vec2(-0.242, 0.997), vec2(-0.814, 0.914),\n"
    "  vec2(0.200, 0.786), vec2(0.144, -0.141));\n"
    "void main() {\n"
    "  vec3 r = v_pos - u_lc.xyz;\n"
    "  vec3 q = vec3(dot(r, u_lu), dot(r, u_lv), dot(r, u_ld)) * u_lc.w * 0.5 + 0.5;\n"
    "  float zr = min(q.z, 1.0) - 0.004;\n"
    "  float sum = 0.0, n = 0.0;\n"
    "  for (int i = 0; i < 16; i++) {\n"
    "    float z = texture(u_fdepth, q.xy + P[i] * u_fl.w).r;\n"
    "    if (z < zr) { sum += z; n += 1.0; }\n"
    "  }\n"
    "  if (n < 0.5) discard;\n"
    "  float pen = clamp((zr - sum / n) * u_fl.y, u_fl.z, u_fl.w);\n"
    "  float lit = 0.0;\n"
    "  for (int i = 0; i < 16; i++) lit += texture(u_fshadow, vec3(q.xy + P[i] * pen, zr));\n"
    "  float sh = 1.0 - lit / 16.0;\n"
    "  /* borda do mapa: some suave, sem corte reto */\n"
    "  vec2 e = min(q.xy, 1.0 - q.xy);\n"
    "  sh *= smoothstep(0.0, 0.04, min(e.x, e.y));\n"
    "  frag = vec4(0.0, 0.0, 0.0, sh * u_fl.x);\n"
    "}\n";

static int build_floor(RmkGL* g) {
    GLuint_ vs = compile(g, R_VERTEX_SHADER, FLOOR_VS), fs = vs ? compile(g, R_FRAGMENT_SHADER, FLOOR_FS) : 0;
    if (!vs || !fs) return 0;
    g->floor_prog = g->CreateProgram();
    g->AttachShader(g->floor_prog, vs);
    g->AttachShader(g->floor_prog, fs);
    g->LinkProgram(g->floor_prog);
    g->DeleteShader(vs);
    g->DeleteShader(fs);
    GLint_ ok = 0;
    g->GetProgramiv(g->floor_prog, R_LINK_STATUS, &ok);
    if (!ok) { snprintf(g->err, sizeof g->err, "link da sombra no chão"); return 0; }
    g->f_ndc = g->GetUniformLocation(g->floor_prog, "u_ndc");
    g->f_proj = g->GetUniformLocation(g->floor_prog, "u_proj");
    g->f_quad = g->GetUniformLocation(g->floor_prog, "u_quad[0]");
    g->f_depth = g->GetUniformLocation(g->floor_prog, "u_fdepth");
    g->f_shadow = g->GetUniformLocation(g->floor_prog, "u_fshadow");
    g->f_lu = g->GetUniformLocation(g->floor_prog, "u_lu");
    g->f_lv = g->GetUniformLocation(g->floor_prog, "u_lv");
    g->f_ld = g->GetUniformLocation(g->floor_prog, "u_ld");
    g->f_lc = g->GetUniformLocation(g->floor_prog, "u_lc");
    g->f_fl = g->GetUniformLocation(g->floor_prog, "u_fl");
    g->GenVertexArrays(1, &g->floor_vao);
    /* mapa de sombra do chão: profundidade lida crua (busca de bloqueadores) e com comparação (PCF), pela mesma
     * textura em duas unidades com objetos de amostragem diferentes */
    g->GenTextures(1, &g->floor_tex);
    g->ActiveTexture(R_TEXTURE0 + 12);
    g->BindTexture(R_TEXTURE_2D, g->floor_tex);
    g->TexImage2D(R_TEXTURE_2D, 0, R_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, R_DEPTH_COMPONENT, R_UNSIGNED_INT, NULL);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MIN_FILTER, R_NEAREST);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MAG_FILTER, R_NEAREST);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
    g->BindTexture(R_TEXTURE_2D, 0);
    g->GenSamplers(2, g->floor_smp);
    g->SamplerParameteri(g->floor_smp[0], R_TEXTURE_MIN_FILTER, R_NEAREST);
    g->SamplerParameteri(g->floor_smp[0], R_TEXTURE_MAG_FILTER, R_NEAREST);
    g->SamplerParameteri(g->floor_smp[0], R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->SamplerParameteri(g->floor_smp[0], R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
    g->SamplerParameteri(g->floor_smp[1], R_TEXTURE_MIN_FILTER, R_LINEAR);
    g->SamplerParameteri(g->floor_smp[1], R_TEXTURE_MAG_FILTER, R_LINEAR);
    g->SamplerParameteri(g->floor_smp[1], R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->SamplerParameteri(g->floor_smp[1], R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
    g->SamplerParameteri(g->floor_smp[1], R_TEXTURE_COMPARE_MODE, R_COMPARE_REF_TO_TEXTURE);
    g->SamplerParameteri(g->floor_smp[1], R_TEXTURE_COMPARE_FUNC, R_LEQUAL);
    g->GenFramebuffers(1, &g->floor_fbo);
    g->BindFramebuffer(R_FRAMEBUFFER, g->floor_fbo);
    g->FramebufferTexture2D(R_FRAMEBUFFER, R_DEPTH_ATTACHMENT, R_TEXTURE_2D, g->floor_tex, 0);
    const GLenum_ none = 0;
    g->DrawBuffers(1, &none);
    g->ReadBuffer(0);
    const int ok2 = g->CheckFramebufferStatus(R_FRAMEBUFFER) == R_FRAMEBUFFER_COMPLETE;
    g->BindFramebuffer(R_FRAMEBUFFER, 0);
    if (!ok2) { snprintf(g->err, sizeof g->err, "sombra no chão: FBO incompleto"); return 0; }
    return 1;
}

static float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void norm3(float* a) {
    const float l = sqrtf(dot3(a, a));
    if (l > 1e-12f) { a[0] /= l; a[1] /= l; a[2] /= l; }
}
static void cross3(const float* a, const float* b, float* o) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

/* Espaço ortográfico da sombra do chão (direção da projeção do jogo, para baixo) em volta dos vértices ativos e o
 * quadrilátero no chão que cobre a projeção deles (com folga para a penumbra). 0 se a luz rasa o chão. */
static int floor_setup(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos) {
    float n[3] = {fr->floor_n[0], fr->floor_n[1], fr->floor_n[2]}, d[3] = {fr->floor_l[0], fr->floor_l[1], fr->floor_l[2]};
    norm3(n); norm3(d);
    if (dot3(d, n) > 0.0f) { d[0] = -d[0]; d[1] = -d[1]; d[2] = -d[2]; }   /* para o chão */
    const float dn = dot3(d, n);
    if (dn > -0.15f) return 0;
    float up[3] = {0.0f, 1.0f, 0.0f};
    if (fabsf(d[1]) > 0.95f) { up[0] = 1.0f; up[1] = 0.0f; }
    float u[3], v[3], e1[3], e2[3];
    cross3(d, up, u); norm3(u);
    cross3(u, d, v);
    /* base do plano do chão para a caixa da projeção */
    float t0[3] = {1.0f, 0.0f, 0.0f};
    if (fabsf(n[0]) > 0.9f) { t0[0] = 0.0f; t0[2] = 1.0f; }
    cross3(n, t0, e1); norm3(e1);
    cross3(n, e1, e2);
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    float plo[2] = {1e30f, 1e30f}, phi[2] = {-1e30f, -1e30f};
    for (uint32_t gi = 0; gi <= a->n_groups; gi++) {
        if (gi && !(((fr->groups | 1u) >> gi) & 1u)) continue;
        const uint32_t i0 = gi ? a->groups[gi].v0 : 0, i1 = gi ? i0 + a->groups[gi].nv : a->n_body;
        for (uint32_t i = i0; i < i1; i++) {
            const float* p = &pos[3 * i];
            for (int k = 0; k < 3; k++) { if (p[k] < lo[k]) lo[k] = p[k]; if (p[k] > hi[k]) hi[k] = p[k]; }
            /* projeção no chão ao longo de d */
            const float r[3] = {p[0] - fr->floor_p[0], p[1] - fr->floor_p[1], p[2] - fr->floor_p[2]};
            const float s = dot3(r, n) / dn;
            const float q[3] = {r[0] - d[0] * s, r[1] - d[1] * s, r[2] - d[2] * s};
            const float c1 = dot3(q, e1), c2 = dot3(q, e2);
            if (c1 < plo[0]) plo[0] = c1;
            if (c1 > phi[0]) phi[0] = c1;
            if (c2 < plo[1]) plo[1] = c2;
            if (c2 > phi[1]) phi[1] = c2;
        }
    }
    float rad = 0.0f;
    for (int k = 0; k < 3; k++) { g->fcenter[k] = 0.5f * (lo[k] + hi[k]); rad += (hi[k] - lo[k]) * (hi[k] - lo[k]); }
    rad = 0.5f * sqrtf(rad) * 1.08f + 1.0f;
    g->fcenter[3] = 1.0f / rad;
    memcpy(g->fbasis[0], u, sizeof u); memcpy(g->fbasis[1], v, sizeof v); memcpy(g->fbasis[2], d, sizeof d);
    const float m = 0.15f * rad;
    const float c[4][2] = {{plo[0] - m, plo[1] - m}, {phi[0] + m, plo[1] - m}, {plo[0] - m, phi[1] + m}, {phi[0] + m, phi[1] + m}};
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < 3; j++) g->fquad[k][j] = fr->floor_p[j] + e1[j] * c[k][0] + e2[j] * c[k][1];
    return 1;
}

void rmk_gl_draw_floor(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos, const RmkTarget* t) {
    if (!g || !g->ready || !g->floor_prog || fr->n_verts != rmk_active_verts(a, fr->groups | 1u)) return;
    frame_setup(g, a, fr, pos);
    const int new_floor = !g->have_floor || g->floor_serial != fr->serial;
    if (new_floor) {
        g->floor_ok = floor_setup(g, a, fr, pos);
        g->floor_serial = fr->serial;
        g->have_floor = 1;
    }
    if (!g->floor_ok) return;
    const float n = fr->znear * (1.0f - fr->zbias), f = fr->zfar;
    const float dA = (f + n) / (f - n), dB = -2.0f * f * n / (f - n), alpha = (fr->flags & RMK_FLAG_FLAT) ? 0.0f : fr->alpha;
    g->Disable(R_BLEND);
    g->Disable(R_STENCIL_TEST);
    g->Disable(R_CULL_FACE);
    if (new_floor) {
        /* (0) profundidade do personagem na direção da sombra, no FBO próprio */
        g->UseProgram(g->prog);
        g->ActiveTexture(R_TEXTURE0 + 8);
        g->BindTexture(R_TEXTURE_2D, g->geo[g->ring]);
        g->ActiveTexture(R_TEXTURE0 + 9);
        g->BindTexture(R_TEXTURE_2D_ARRAY, g->tex);
        g->Uniform1i(g->u_geo, 8);
        g->Uniform1i(g->u_tex, 9);
        g->Uniform1i(g->u_geo_w, g->geo_w);
        g->Uniform4f(g->u_misc, 1.0f, 0.5f, (fr->flags & RMK_FLAG_NO_TEX) ? 0.0f : 1.0f, 0.0f);
        g->Uniform1i(g->u_nrm, 0);
        g->Uniform1i(g->u_semi, (fr->flags & RMK_FLAG_NO_SEMI) ? -1 : 0);
        g->ActiveTexture(R_TEXTURE0 + 10);
        g->BindTexture(R_TEXTURE_2D_ARRAY, g->nmap);
        g->Uniform1i(g->u_nmap, 10);
        g->Uniform3f(g->u_lu, g->fbasis[0][0], g->fbasis[0][1], g->fbasis[0][2]);
        g->Uniform3f(g->u_lv, g->fbasis[1][0], g->fbasis[1][1], g->fbasis[1][2]);
        g->Uniform3f(g->u_ld, g->fbasis[2][0], g->fbasis[2][1], g->fbasis[2][2]);
        g->Uniform4f(g->u_lc, g->fcenter[0], g->fcenter[1], g->fcenter[2], g->fcenter[3]);
        g->BindVertexArray(g->vao);
        g->BindFramebuffer(R_FRAMEBUFFER, g->floor_fbo);
        g->Viewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
        g->Disable(R_SCISSOR_TEST);
        g->ColorMask(0, 0, 0, 0);
        g->DepthMask(1);
        g->Clear(R_DEPTH_BUFFER_BIT);
        g->Enable(R_DEPTH_TEST);
        g->DepthFunc(R_LESS);
        g->Enable(R_POLYGON_OFFSET_FILL);
        g->PolygonOffset(1.5f, 3.0f);
        g->Uniform1i(g->u_spass, 1);
        g->Uniform1i(g->u_prepass, 1);
        g->Uniform4f(g->u_depth, dA, dB, alpha, 0.0f);
        g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)g->n_draw);
        g->Disable(R_POLYGON_OFFSET_FILL);
        g->Uniform1i(g->u_spass, 0);
        g->BindFramebuffer(R_FRAMEBUFFER, t->fbo);
        g->Viewport(t->viewport[0], t->viewport[1], t->viewport[2], t->viewport[3]);
    }
    /* (1) o quadrilátero no chão escurece o palco: cor × (1 - a), alfa intacto, sem profundidade */
    g->Disable(R_DEPTH_TEST);
    g->DepthMask(0);
    g->Enable(R_SCISSOR_TEST);
    g->Scissor(t->scissor[0], t->scissor[1], t->scissor[2], t->scissor[3]);
    g->ColorMask(1, 1, 1, 0);
    g->Enable(R_BLEND);
    g->BlendFunc(0 /* GL_ZERO */, 0x0303 /* GL_ONE_MINUS_SRC_ALPHA */);
    g->UseProgram(g->floor_prog);
    g->ActiveTexture(R_TEXTURE0 + 12);
    g->BindTexture(R_TEXTURE_2D, g->floor_tex);
    g->BindSampler(12, g->floor_smp[0]);
    g->ActiveTexture(R_TEXTURE0 + 13);
    g->BindTexture(R_TEXTURE_2D, g->floor_tex);
    g->BindSampler(13, g->floor_smp[1]);
    g->Uniform1i(g->f_depth, 12);
    g->Uniform1i(g->f_shadow, 13);
    g->Uniform4f(g->f_ndc, t->ndc[0], t->ndc[1], t->ndc[2], t->ndc[3]);
    g->Uniform4f(g->f_proj, fr->ofx, fr->ofy, fr->h, fr->shift_x);
    g->Uniform3fv(g->f_quad, 4, &g->fquad[0][0]);
    g->Uniform3f(g->f_lu, g->fbasis[0][0], g->fbasis[0][1], g->fbasis[0][2]);
    g->Uniform3f(g->f_lv, g->fbasis[1][0], g->fbasis[1][1], g->fbasis[1][2]);
    g->Uniform3f(g->f_ld, g->fbasis[2][0], g->fbasis[2][1], g->fbasis[2][2]);
    g->Uniform4f(g->f_lc, g->fcenter[0], g->fcenter[1], g->fcenter[2], g->fcenter[3]);
    /* penumbra em unidades do mapa: profundidade 0..1 = 2 raios; mínimo ~1,5 texel, máximo = raio da busca */
    g->Uniform4f(g->f_fl, fr->floor_alpha, 0.08f * fr->floor_soft, 1.5f / SHADOW_SIZE, 0.01f + 0.025f * fr->floor_soft);
    g->BindVertexArray(g->floor_vao);
    g->DrawArrays(0x0005 /* GL_TRIANGLE_STRIP */, 0, 4);
    g->BindVertexArray(0);
    g->BindSampler(12, 0);
    g->BindSampler(13, 0);
    g->Disable(R_BLEND);
    g->ColorMask(1, 1, 1, 1);
    g->DepthMask(1);
}

void rmk_gl_draw(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos, const RmkTarget* t) {
    if (!g || !g->ready || fr->n_verts != rmk_active_verts(a, fr->groups | 1u)) return;
    /* retângulo do desenho em pixels do alvo, dentro do recorte */
    const float nx0 = t->bbox[0] * t->ndc[0] + t->ndc[1], nx1 = t->bbox[2] * t->ndc[0] + t->ndc[1];
    const float ny0 = t->bbox[1] * t->ndc[2] + t->ndc[3], ny1 = t->bbox[3] * t->ndc[2] + t->ndc[3];
    int x0 = t->viewport[0] + ifloor((nx0 + 1.0f) * 0.5f * (float)t->viewport[2]);
    int x1 = t->viewport[0] + ifloor((nx1 + 1.0f) * 0.5f * (float)t->viewport[2]) + 1;
    int y0 = t->viewport[1] + ifloor((ny0 + 1.0f) * 0.5f * (float)t->viewport[3]);
    int y1 = t->viewport[1] + ifloor((ny1 + 1.0f) * 0.5f * (float)t->viewport[3]) + 1;
    if (x0 < t->scissor[0]) x0 = t->scissor[0];
    if (y0 < t->scissor[1]) y0 = t->scissor[1];
    if (x1 > t->scissor[0] + t->scissor[2]) x1 = t->scissor[0] + t->scissor[2];
    if (y1 > t->scissor[1] + t->scissor[3]) y1 = t->scissor[1] + t->scissor[3];
    if (x1 <= x0 || y1 <= y0) return;
    frame_setup(g, a, fr, pos);   /* o espelho da tela larga reaproveita geometria, ordem e sombra */
    const int new_frame = !g->have_shadow || fr->serial != g->shadow_serial;
    g->Enable(0x0C11 /* GL_SCISSOR_TEST */);
    g->Scissor(x0, y0, x1 - x0, y1 - y0);
    g->DepthMask(1);
    if (g->ClearDepthf) g->ClearDepthf(1.0f); else if (g->ClearDepth) g->ClearDepth(1.0);
    g->Clear(R_DEPTH_BUFFER_BIT);
    g->Enable(R_DEPTH_TEST);
    g->Enable(R_CULL_FACE);
    g->CullFace(R_BACK);
    g->FrontFace(R_CCW);   /* NCLIP > 0 do PS1 = anti-horário nas coordenadas da janela */
    g->Disable(R_BLEND);
    g->Disable(R_STENCIL_TEST);
    g->UseProgram(g->prog);
    g->ActiveTexture(R_TEXTURE0 + 8);
    g->BindTexture(R_TEXTURE_2D, g->geo[g->ring]);
    g->ActiveTexture(R_TEXTURE0 + 9);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->tex);
    g->ActiveTexture(R_TEXTURE0 + 10);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->nmap);
    g->Uniform1i(g->u_geo, 8);
    g->Uniform1i(g->u_tex, 9);
    g->Uniform1i(g->u_nmap, 10);
    g->Uniform1f(g->u_nmap_on, (g->has_nmap && !(fr->flags & RMK_FLAG_NO_NMAP)) ? 1.0f : 0.0f);
    g->Uniform1i(g->u_geo_w, g->geo_w);
    g->Uniform4f(g->u_ndc, t->ndc[0], t->ndc[1], t->ndc[2], t->ndc[3]);
    const int shadow = !(fr->flags & RMK_FLAG_NO_SHADOW);
    g->Uniform3f(g->u_lu, g->lbasis[0][0], g->lbasis[0][1], g->lbasis[0][2]);
    g->Uniform3f(g->u_lv, g->lbasis[1][0], g->lbasis[1][1], g->lbasis[1][2]);
    g->Uniform3f(g->u_ld, g->lbasis[2][0], g->lbasis[2][1], g->lbasis[2][2]);
    g->Uniform4f(g->u_lc, g->lcenter[0], g->lcenter[1], g->lcenter[2], g->lcenter[3]);
    g->Uniform1i(g->u_key, g->key);
    g->ActiveTexture(R_TEXTURE0 + 11);
    g->BindTexture(R_TEXTURE_2D, g->shadow_tex);
    g->Uniform1i(g->u_shadow, 11);
    g->Uniform1f(g->u_shadow_on, shadow ? 1.0f : 0.0f);
    const int semi = !(fr->flags & RMK_FLAG_NO_SEMI);
    g->Uniform1i(g->u_semi, semi ? 0 : -1);
    g->Uniform4f(g->u_proj, fr->ofx, fr->ofy, fr->h, fr->shift_x);
    /* expressão do rosto: casas remapeadas (texels da página); a borda recua meio texel HD */
    {
        float rect[4 * RMK_MAX_REMAP], off[4 * RMK_MAX_REMAP];
        const uint32_t nr = fr->n_remap < RMK_MAX_REMAP ? fr->n_remap : RMK_MAX_REMAP;
        const float inset = 128.0f / (float)a->tex_size;
        for (uint32_t i = 0; i < nr; i++) {
            const RmkRemap* r = &fr->remap[i];
            rect[4 * i] = r->x0; rect[4 * i + 1] = r->y0; rect[4 * i + 2] = r->x1; rect[4 * i + 3] = r->y1;
            off[4 * i] = r->du; off[4 * i + 1] = r->dv; off[4 * i + 2] = r->page; off[4 * i + 3] = inset;
        }
        g->Uniform1i(g->u_nrm, (GLint_)nr);
        if (nr) {
            g->Uniform4fv(g->u_rm_rect, (GLsizei_)nr, rect);
            g->Uniform4fv(g->u_rm_off, (GLsizei_)nr, off);
        }
    }
    /* o passo de cor puxa a profundidade até zbias: a faixa começa um pouco antes para caber */
    const float n = fr->znear * (1.0f - fr->zbias), f = fr->zfar;
    const float dA = (f + n) / (f - n), dB = -2.0f * f * n / (f - n), alpha = (fr->flags & RMK_FLAG_FLAT) ? 0.0f : fr->alpha;
    g->Uniform4f(g->u_misc, fr->expo, fr->rough, (fr->flags & RMK_FLAG_NO_TEX) ? 0.0f : 1.0f, fr->rim);
    /* luz fixa da fase 1 (flag RMK_FLAG_FIXED_LIGHT), na câmera (x direita, y para baixo, z para dentro):
     * principal quente de cima/esquerda/frente, preenchimento frio da direita, contraluz por trás; cores de
     * tools/remake/render.py. Sem a flag valem as luzes do palco (mais abaixo). */
    static const float kd[3] = {-0.45f, -0.80f, -0.55f}, fd[3] = {0.75f, -0.15f, -0.55f}, rd[3] = {0.25f, -0.45f, 1.0f};
    const float lk = 1.0f / sqrtf(kd[0] * kd[0] + kd[1] * kd[1] + kd[2] * kd[2]);
    const float lf = 1.0f / sqrtf(fd[0] * fd[0] + fd[1] * fd[1] + fd[2] * fd[2]);
    const float lr = 1.0f / sqrtf(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
    g->Uniform3f(g->u_key_dir, kd[0] * lk, kd[1] * lk, kd[2] * lk);
    g->Uniform3f(g->u_fill_dir, fd[0] * lf, fd[1] * lf, fd[2] * lf);
    g->Uniform3f(g->u_rim_dir, rd[0] * lr, rd[1] * lr, rd[2] * lr);
    g->Uniform3f(g->u_key_col, 1.9f * fr->key, 1.72f * fr->key, 1.5f * fr->key);
    g->Uniform3f(g->u_fill_col, 0.28f * fr->fill, 0.34f * fr->fill, 0.5f * fr->fill);
    g->Uniform3f(g->u_rim_col, 0.9f, 0.88f, 1.0f);
    g->Uniform3f(g->u_sky, 0.2f * fr->amb, 0.21f * fr->amb, 0.27f * fr->amb);
    g->Uniform3f(g->u_ground, 0.09f * fr->amb, 0.075f * fr->amb, 0.07f * fr->amb);
    const int stage = !(fr->flags & RMK_FLAG_FIXED_LIGHT);
    g->Uniform1i(g->u_stage, stage);
    if (stage) {
        /* luzes do palco; key/fill/amb do heat_light viram ganho da luz mais forte, das outras e do BK */
        float col[3][3], bk[3];
        int key = 0;
        float best = -1e9f;
        for (int i = 0; i < 3; i++) {
            const float s = fr->lcol[i][0] + fr->lcol[i][1] + fr->lcol[i][2];
            if (s > best) { best = s; key = i; }
        }
        for (int i = 0; i < 3; i++)
            for (int c = 0; c < 3; c++) col[i][c] = fr->lcol[i][c] * (i == key ? fr->key : fr->fill);
        for (int c = 0; c < 3; c++) bk[c] = fr->bk[c] * fr->amb;
        g->Uniform3fv(g->u_ldir, 3, &fr->ldir[0][0]);
        g->Uniform3fv(g->u_lcol, 3, &col[0][0]);
        g->Uniform3f(g->u_bk, bk[0], bk[1], bk[2]);
        /* contraluz: do lado oposto à luz principal, por trás (z para dentro), com a cor dela */
        float rd2[3] = {-fr->ldir[key][0], fr->ldir[key][1] * 0.5f - 0.3f, 0.9f};
        const float l2 = 1.0f / sqrtf(rd2[0] * rd2[0] + rd2[1] * rd2[1] + rd2[2] * rd2[2]);
        g->Uniform3f(g->u_rim_dir, rd2[0] * l2, rd2[1] * l2, rd2[2] * l2);
        const float kc = best > 1e-3f ? 2.7f / best : 1.0f;
        g->Uniform3f(g->u_rim_col, fr->lcol[key][0] * kc * 0.95f, fr->lcol[key][1] * kc * 0.95f, fr->lcol[key][2] * kc * 0.95f);
    }
    g->BindVertexArray(g->vao);
    if (shadow && new_frame) {
        /* (0) sombra: profundidade no espaço da luz, no FBO próprio; depois o alvo de quem chama de volta */
        g->BindFramebuffer(R_FRAMEBUFFER, g->shadow_fbo);
        g->Viewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
        g->Disable(R_SCISSOR_TEST);
        g->ColorMask(0, 0, 0, 0);
        g->DepthMask(1);
        g->Clear(R_DEPTH_BUFFER_BIT);
        g->DepthFunc(R_LESS);
        g->Disable(R_CULL_FACE);                /* pano fino: as duas faces fazem sombra */
        g->Enable(R_POLYGON_OFFSET_FILL);
        g->PolygonOffset(1.5f, 3.0f);
        g->Uniform1i(g->u_spass, 1);
        g->Uniform1i(g->u_prepass, 1);
        g->Uniform4f(g->u_depth, dA, dB, alpha, 0.0f);
        g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)g->n_draw);
        g->Disable(R_POLYGON_OFFSET_FILL);
        g->Enable(R_CULL_FACE);
        g->shadow_serial = fr->serial;
        g->have_shadow = 1;
        g->BindFramebuffer(R_FRAMEBUFFER, t->fbo);
        g->Viewport(t->viewport[0], t->viewport[1], t->viewport[2], t->viewport[3]);
        g->Enable(R_SCISSOR_TEST);
        g->Scissor(x0, y0, x1 - x0, y1 - y0);
    }
    g->Uniform1i(g->u_spass, 0);
    /* (1) profundidade verdadeira */
    g->ColorMask(0, 0, 0, 0);
    g->DepthMask(1);
    g->DepthFunc(R_LESS);
    g->Uniform4f(g->u_depth, dA, dB, alpha, 0.0f);
    g->Uniform1i(g->u_prepass, 1);
    g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)g->n_draw);
    /* (2) cor na ordem de pintor do jogo, dentro da tolerância */
    g->ColorMask(1, 1, 1, 1);
    g->DepthMask(0);
    g->DepthFunc(R_LEQUAL);
    g->Uniform4f(g->u_depth, dA, dB, alpha, fr->zbias);
    g->Uniform1i(g->u_prepass, 0);
    g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)g->n_draw);
    /* (3) texels com STP das faces semitransparentes: 0,5 B + 0,5 F (modo 0 do PS1), sem escrever o alfa */
    if (semi && g->n_semi) {
        g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
        inst_attribs(g, g->n_draw);
        g->Enable(R_BLEND);
        g->BlendEquation(0x8006 /* GL_FUNC_ADD */);
        g->BlendColor(0.5f, 0.5f, 0.5f, 0.5f);
        g->BlendFunc(0x8001 /* GL_CONSTANT_COLOR */, 0x8001);
        g->ColorMask(1, 1, 1, 0);
        g->Uniform1i(g->u_semi, 1);
        g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)g->n_semi);
        inst_attribs(g, 0);
        g->Disable(R_BLEND);
        g->ColorMask(1, 1, 1, 1);
        g->BindBuffer(R_ARRAY_BUFFER, 0);
    }
    g->BindVertexArray(0);
}
