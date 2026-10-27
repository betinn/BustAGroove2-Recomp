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
 * atrás continua escondido.
 *
 * Estado GL que rmk_gl_draw muda (quem chama restaura): programa, VAO, buffer de vértices, unidade ativa e
 * texturas das unidades 8 e 9, recorte (caixa), teste/função/máscara de profundidade, cull/front face,
 * blend e stencil (desligados), máscara de cor. A profundidade do FBO é limpa só no retângulo do desenho. */
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
#define R_UNPACK_ALIGNMENT 0x0CF5
#define R_UNPACK_ROW_LENGTH 0x0CF2

#define GL_FUNCS(X) \
    X(void, ActiveTexture, (GLenum_ t)) \
    X(void, AttachShader, (GLuint_ p, GLuint_ s)) \
    X(void, BindBuffer, (GLenum_ t, GLuint_ b)) \
    X(void, BindTexture, (GLenum_ t, GLuint_ x)) \
    X(void, BindVertexArray, (GLuint_ a)) \
    X(void, BufferData, (GLenum_ t, GLsizeiptr_ n, const void* d, GLenum_ u)) \
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
    X(void, Uniform4f, (GLint_ l, float x, float y, float z, float w)) \
    X(void, UseProgram, (GLuint_ p)) \
    X(void, VertexAttribDivisor, (GLuint_ i, GLuint_ d)) \
    X(void, VertexAttribIPointer, (GLuint_ i, GLint_ n, GLenum_ t, GLsizei_ s, const void* p)) \
    X(void, VertexAttribPointer, (GLuint_ i, GLint_ n, GLenum_ t, GLboolean_ nm, GLsizei_ s, const void* p))

#define GEO_RING 8                   /* texturas de geometria em rodízio (vários desenhos por quadro) */
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
    int gles, step, ready, failed;
    GLuint_ prog, vao, grid_vbo, grid_ibo, inst_vbo, tex, geo[GEO_RING];
    int geo_w, geo_h, ring, have_serial;
    uint32_t last_serial;
    float* staging;
    Inst* inst;                      /* por face, na ordem do asset */
    Inst* inst_order;                /* reordenado por quadro (ordem de pintor do jogo) */
    uint16_t* slot;                  /* slot da OT por face, por quadro */
    GLint_ u_geo, u_geo_w, u_ndc, u_proj, u_depth, u_tex, u_misc, u_prepass;
    GLint_ u_key_dir, u_key_col, u_fill_dir, u_fill_col, u_rim_dir, u_rim_col, u_sky, u_ground;
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
    "out vec3 v_pos; out vec3 v_nrm; out vec3 v_base; out vec3 v_over; out vec2 v_uv; out float v_ao;\n"
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
    "  gl_Position = vec4((s * u_ndc.xz + u_ndc.yw) * zd, u_depth.x * zd + u_depth.y, zd);\n"
    "}\n";

static const char* FS_BODY =
    "precision highp float;\n"
    "precision mediump sampler2DArray;\n"
    "uniform mediump sampler2DArray u_tex;\n"
    "uniform vec3 u_key_dir, u_key_col, u_fill_dir, u_fill_col, u_rim_dir, u_rim_col, u_sky, u_ground;\n"
    "uniform vec4 u_misc;   /* exposição, rugosidade, textura ligada, contraluz */\n"
    "uniform int u_prepass; /* 1: só profundidade (o recorte por alfa vale igual) */\n"
    "in vec3 v_pos; in vec3 v_nrm; in vec3 v_base; in vec3 v_over; in vec2 v_uv; in float v_ao;\n"
    "flat in int v_layer; flat in int v_mode;\n"
    "layout(location = 0) out vec4 frag;\n"
    "float D_ggx(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (3.14159 * d * d); }\n"
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
    "  if (v_mode > 0 && v_layer >= 0 && u_misc.z > 0.5) {\n"
    "    vec4 t = texture(u_tex, vec3(v_uv, float(v_layer)));\n"
    "    if (v_mode == 2) { if (t.a < 0.5) discard; base = t.rgb * v_over; }\n"
    "    else base = mix(base, t.rgb * v_over, t.a);\n"
    "  }\n"
    "  if (u_prepass != 0) { frag = vec4(0.0); return; }\n"
    "  vec3 alb = pow(clamp(base, 0.0, 1.0), vec3(2.2));\n"
    "  vec3 n = normalize(v_nrm), v = normalize(-v_pos);\n"
    "  if (dot(n, v) < 0.0) n = -n;\n"
    "  vec3 c = light(n, v, u_key_dir, u_key_col, alb, 0.25);\n"
    "  c += light(n, v, u_fill_dir, u_fill_col, alb, 0.5);\n"
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * max(dot(n, u_rim_dir), 0.0);\n"
    "  c += u_rim_col * rim * (0.3 + alb) * v_ao * u_misc.w;\n"
    "  c += mix(u_ground, u_sky, -n.y * 0.5 + 0.5) * alb * v_ao;   /* y da câmera do GTE aponta para baixo */\n"
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
    U(u_geo); U(u_geo_w); U(u_ndc); U(u_proj); U(u_depth); U(u_tex); U(u_misc); U(u_prepass);
    U(u_key_dir); U(u_key_col); U(u_fill_dir); U(u_fill_col); U(u_rim_dir); U(u_rim_col); U(u_sky); U(u_ground);
#undef U
    return 1;
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
    g->inst_order = (Inst*)calloc(a->n_tris, sizeof(Inst));
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
        o->mode = (float)t->mode;
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
    g->BufferData(R_ARRAY_BUFFER, (GLsizeiptr_)(sizeof(Inst) * a->n_tris), inst, R_STREAM_DRAW);
    const GLsizei_ st = (GLsizei_)sizeof(Inst);
    g->EnableVertexAttribArray(1);
    g->VertexAttribIPointer(1, 3, R_UNSIGNED_SHORT, st, (const void*)offsetof(Inst, vi));
    g->EnableVertexAttribArray(2);
    g->VertexAttribPointer(2, 4, R_FLOAT, 0, st, (const void*)offsetof(Inst, uv));
    g->EnableVertexAttribArray(3);
    g->VertexAttribPointer(3, 2, R_FLOAT, 0, st, (const void*)(offsetof(Inst, uv) + 16));
    for (int k = 0; k < 3; k++) {
        g->EnableVertexAttribArray(4 + k);
        g->VertexAttribPointer(4 + k, 4, R_UNSIGNED_BYTE, 1, st, (const void*)(offsetof(Inst, base) + 4 * k));
        g->EnableVertexAttribArray(7 + k);
        g->VertexAttribPointer(7 + k, 4, R_UNSIGNED_BYTE, 1, st, (const void*)(offsetof(Inst, over) + 4 * k));
    }
    g->EnableVertexAttribArray(10);
    g->VertexAttribPointer(10, 2, R_FLOAT, 0, st, (const void*)offsetof(Inst, layer));
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
    g->BindTexture(R_TEXTURE_2D_ARRAY, 0);
    return g->GetError() == 0;
}

int rmk_gl_prepare(RmkGL* g, const RmkAsset* a) {
    if (!g || g->failed) return 0;
    if (g->ready) return 1;
    while (g->GetError() != 0) {}
    g->PixelStorei(R_UNPACK_ALIGNMENT, 4);
    g->PixelStorei(R_UNPACK_ROW_LENGTH, 0);
    if (g->step == 0) {
        if (!build_program(g) || !build_buffers(g, a)) {
            if (!g->err[0]) snprintf(g->err, sizeof g->err, "recursos GL: erro 0x%X", g->GetError());
            g->failed = 1;
            return 0;
        }
        g->step = 1;
        return 0;
    }
    g->ActiveTexture(R_TEXTURE0 + 9);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->tex);
    const uint32_t layer = (uint32_t)g->step - 1u;
    if (layer < a->n_layers) {
        const size_t bytes = (size_t)a->tex_size * a->tex_size * 4u;
        g->TexSubImage3D(R_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint_)layer, (GLsizei_)a->tex_size, (GLsizei_)a->tex_size, 1,
                         R_RGBA, R_UNSIGNED_BYTE, a->layers + bytes * layer);
        g->step++;
    } else {
        g->GenerateMipmap(R_TEXTURE_2D_ARRAY);
        g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MIN_FILTER, R_LINEAR_MIPMAP_LINEAR);
        float an = 0.0f;
        g->GetFloatv(R_MAX_TEXTURE_MAX_ANISOTROPY, &an);
        if (g->GetError() == 0 && an > 1.0f) g->TexParameterf(R_TEXTURE_2D_ARRAY, R_TEXTURE_MAX_ANISOTROPY, an < 8.0f ? an : 8.0f);
        while (g->GetError() != 0) {}
        g->ready = 1;
    }
    g->BindTexture(R_TEXTURE_2D_ARRAY, 0);
    if (g->GetError() != 0) { snprintf(g->err, sizeof g->err, "textura HD: erro GL"); g->failed = 1; return 0; }
    return g->ready;
}

static int ifloor(float f) { int i = (int)f; return (float)i > f ? i - 1 : i; }

/* Ordem de desenho do jogo (FUN_80036230 + OT reversa): slot (z0 + z1) >> 3 do maior para o menor; dentro
 * do slot, a face ligada por último (maior índice do asset) primeiro. Contagem por slot, estável. */
#define SLOTS 4200
static void order_faces(RmkGL* g, const RmkAsset* a, const float* pos) {
    static uint32_t start[SLOTS + 1];
    memset(start, 0, sizeof start);
    for (uint32_t f = 0; f < a->n_tris; f++) {
        const RmkTri* t = &a->tris[f];
        int s = ((int)pos[3 * t->zp[0] + 2] + (int)pos[3 * t->zp[1] + 2]) >> 3;
        s = s < 0 ? 0 : s >= SLOTS ? SLOTS - 1 : s;
        g->slot[f] = (uint16_t)(SLOTS - 1 - s);           /* crescente = do mais longe ao mais perto */
        start[g->slot[f] + 1]++;
    }
    for (int s = 0; s < SLOTS; s++) start[s + 1] += start[s];
    for (uint32_t k = a->n_tris; k-- > 0;) g->inst_order[start[g->slot[k]]++] = g->inst[k];
}

void rmk_gl_draw(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos, const RmkTarget* t) {
    if (!g || !g->ready || fr->n_verts != a->n_verts) return;
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
    if (!g->have_serial || fr->serial != g->last_serial) {   /* o espelho da tela larga reaproveita */
        g->ring = (g->ring + 1) % GEO_RING;
        rmk_pack_geometry(a, pos, g->staging);
        g->PixelStorei(R_UNPACK_ALIGNMENT, 4);
        g->PixelStorei(R_UNPACK_ROW_LENGTH, 0);
        g->ActiveTexture(R_TEXTURE0 + 8);
        g->BindTexture(R_TEXTURE_2D, g->geo[g->ring]);
        g->TexSubImage2D(R_TEXTURE_2D, 0, 0, 0, g->geo_w, g->geo_h, R_RGBA, R_FLOAT, g->staging);
        order_faces(g, a, pos);
        g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
        g->BufferData(R_ARRAY_BUFFER, (GLsizeiptr_)(sizeof(Inst) * a->n_tris), g->inst_order, R_STREAM_DRAW);
        g->last_serial = fr->serial;
        g->have_serial = 1;
    }
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
    g->Uniform1i(g->u_geo, 8);
    g->Uniform1i(g->u_tex, 9);
    g->Uniform1i(g->u_geo_w, g->geo_w);
    g->Uniform4f(g->u_ndc, t->ndc[0], t->ndc[1], t->ndc[2], t->ndc[3]);
    g->Uniform4f(g->u_proj, fr->ofx, fr->ofy, fr->h, fr->shift_x);
    /* o passo de cor puxa a profundidade até zbias: a faixa começa um pouco antes para caber */
    const float n = fr->znear * (1.0f - fr->zbias), f = fr->zfar;
    const float dA = (f + n) / (f - n), dB = -2.0f * f * n / (f - n), alpha = (fr->flags & RMK_FLAG_FLAT) ? 0.0f : fr->alpha;
    g->Uniform4f(g->u_misc, fr->expo, fr->rough, (fr->flags & RMK_FLAG_NO_TEX) ? 0.0f : 1.0f, fr->rim);
    /* luzes fixas na câmera (x direita, y para baixo, z para dentro): principal quente de cima/esquerda/
     * frente, preenchimento frio da direita, contraluz por trás; cores de tools/remake/render.py */
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
    g->BindVertexArray(g->vao);
    /* (1) profundidade verdadeira */
    g->ColorMask(0, 0, 0, 0);
    g->DepthMask(1);
    g->DepthFunc(R_LESS);
    g->Uniform4f(g->u_depth, dA, dB, alpha, 0.0f);
    g->Uniform1i(g->u_prepass, 1);
    g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)a->n_tris);
    /* (2) cor na ordem de pintor do jogo, dentro da tolerância */
    g->ColorMask(1, 1, 1, 1);
    g->DepthMask(0);
    g->DepthFunc(R_LEQUAL);
    g->Uniform4f(g->u_depth, dA, dB, alpha, fr->zbias);
    g->Uniform1i(g->u_prepass, 0);
    g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)a->n_tris);
    g->BindVertexArray(0);
}
