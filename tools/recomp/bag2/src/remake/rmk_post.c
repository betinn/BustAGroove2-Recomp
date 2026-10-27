/* Pós-efeito do palco (núcleo portátil, rmk.h; notes/remake-visual.md, "Fase 3 no jogo").
 *
 * Roda no meio do quadro: o adaptador liga um pedido de desenho de host na OT do jogo depois de toda a cena 3D
 * e antes do HUD (slots < 95), então o que passa por aqui é só palco e personagens. Passos, todos em GL 3.3 core /
 * GLES 3.0 (sem extensão: alvos RGBA8 e RGB10_A2, BlitFramebuffer, gl_VertexID):
 *   1. cópia do retângulo do FBO de quem chama para uma textura (cena);
 *   2. bloom multiescala: passo de brilho em 1/4 (média 4x4, limiar suave no máximo do RGB, tons sem cor pesam
 *      menos para o chão branco não acender inteiro), descidas até 1/64 e subidas somando (filtro duplo),
 *      o que dá um halo largo e liso nos neons;
 *   3. composição de volta no retângulo: graduação (exposição, vibração, curva S, sombras frias e realces
 *      quentes, vinheta) e o bloom por "screen", com o alfa (bit 15 do PS1) intacto.
 * O pulso da música vem pronto no quadro (fase da batida); aqui só vira ganho do bloom e da exposição. */
#include "rmk.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define PGLAPI __stdcall
#else
#define PGLAPI
#endif

typedef uint32_t PEnum;
typedef uint32_t PUint;
typedef int32_t PInt;
typedef int32_t PSizei;
typedef uint8_t PBool;

#define P_TRIANGLES 0x0004
#define P_ONE 1
#define P_TEXTURE_2D 0x0DE1
#define P_TEXTURE0 0x84C0
#define P_RGBA 0x1908
#define P_RGBA8 0x8058
#define P_RGB10_A2 0x8059
#define P_UNSIGNED_BYTE 0x1401
#define P_UNSIGNED_INT_2_10_10_10_REV 0x8368
#define P_TEXTURE_MAG_FILTER 0x2800
#define P_TEXTURE_MIN_FILTER 0x2801
#define P_TEXTURE_WRAP_S 0x2802
#define P_TEXTURE_WRAP_T 0x2803
#define P_NEAREST 0x2600
#define P_LINEAR 0x2601
#define P_CLAMP_TO_EDGE 0x812F
#define P_FRAMEBUFFER 0x8D40
#define P_READ_FRAMEBUFFER 0x8CA8
#define P_DRAW_FRAMEBUFFER 0x8CA9
#define P_COLOR_ATTACHMENT0 0x8CE0
#define P_FRAMEBUFFER_COMPLETE 0x8CD5
#define P_COLOR_BUFFER_BIT 0x4000
#define P_FRAGMENT_SHADER 0x8B30
#define P_VERTEX_SHADER 0x8B31
#define P_COMPILE_STATUS 0x8B81
#define P_LINK_STATUS 0x8B82
#define P_BLEND 0x0BE2
#define P_DEPTH_TEST 0x0B71
#define P_STENCIL_TEST 0x0B90
#define P_SCISSOR_TEST 0x0C11
#define P_CULL_FACE 0x0B44
#define P_FUNC_ADD 0x8006

#define POST_FUNCS(X) \
    X(void, ActiveTexture, (PEnum t)) \
    X(void, AttachShader, (PUint p, PUint s)) \
    X(void, BindFramebuffer, (PEnum t, PUint f)) \
    X(void, BindTexture, (PEnum t, PUint x)) \
    X(void, BindVertexArray, (PUint a)) \
    X(void, BlendEquation, (PEnum m)) \
    X(void, BlendFunc, (PEnum s, PEnum d)) \
    X(void, BlitFramebuffer, (PInt sx0, PInt sy0, PInt sx1, PInt sy1, PInt dx0, PInt dy0, PInt dx1, PInt dy1, uint32_t m, PEnum f)) \
    X(PEnum, CheckFramebufferStatus, (PEnum t)) \
    X(void, ColorMask, (PBool r, PBool g, PBool b, PBool a)) \
    X(void, CompileShader, (PUint s)) \
    X(PUint, CreateProgram, (void)) \
    X(PUint, CreateShader, (PEnum t)) \
    X(void, DeleteFramebuffers, (PSizei n, const PUint* f)) \
    X(void, DeleteShader, (PUint s)) \
    X(void, DeleteTextures, (PSizei n, const PUint* t)) \
    X(void, Disable, (PEnum c)) \
    X(void, DrawArrays, (PEnum m, PInt f, PSizei n)) \
    X(void, Enable, (PEnum c)) \
    X(void, FramebufferTexture2D, (PEnum t, PEnum a, PEnum tt, PUint x, PInt l)) \
    X(void, GenFramebuffers, (PSizei n, PUint* f)) \
    X(void, GenTextures, (PSizei n, PUint* t)) \
    X(void, GenVertexArrays, (PSizei n, PUint* a)) \
    X(void, GetProgramInfoLog, (PUint p, PSizei n, PSizei* l, char* s)) \
    X(void, GetProgramiv, (PUint p, PEnum q, PInt* v)) \
    X(void, GetShaderInfoLog, (PUint s, PSizei n, PSizei* l, char* t)) \
    X(void, GetShaderiv, (PUint s, PEnum q, PInt* v)) \
    X(PInt, GetUniformLocation, (PUint p, const char* n)) \
    X(void, LinkProgram, (PUint p)) \
    X(void, Scissor, (PInt x, PInt y, PSizei w, PSizei h)) \
    X(void, ShaderSource, (PUint s, PSizei n, const char* const* t, const PInt* l)) \
    X(void, TexImage2D, (PEnum t, PInt l, PInt f, PSizei w, PSizei h, PInt b, PEnum pf, PEnum ty, const void* d)) \
    X(void, TexParameteri, (PEnum t, PEnum p, PInt v)) \
    X(void, Uniform1i, (PInt l, PInt v)) \
    X(void, Uniform2f, (PInt l, float x, float y)) \
    X(void, Uniform3f, (PInt l, float x, float y, float z)) \
    X(void, Uniform4f, (PInt l, float x, float y, float z, float w)) \
    X(void, UseProgram, (PUint p)) \
    X(void, Viewport, (PInt x, PInt y, PSizei w, PSizei h))

#define LEVELS 5                     /* 1/4, 1/8, 1/16, 1/32, 1/64 do retângulo */

enum { PG_BRIGHT, PG_DOWN, PG_UP, PG_COMP, PG_N };
typedef struct { PUint prog; PInt src, tx, a, b, c, d, lo, hi, scene, bloom, adapt; } Prog;

struct RmkPost {
#define X(ret, name, args) ret(PGLAPI* name) args;
    POST_FUNCS(X)
#undef X
    int gles, ready, failed;
    PUint vao;
    Prog pg[PG_N];
    PUint scene_tex, scene_fbo, lv_tex[LEVELS], lv_fbo[LEVELS];
    int sw, sh, lw[LEVELS], lh[LEVELS];
    char err[512];
};

static const char* VS =
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  v_uv = p;\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char* FS_HEAD =
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "layout(location = 0) out vec4 frag;\n"
    "uniform sampler2D u_src;\n"
    "uniform vec2 u_tx;   /* texel da origem */\n";

/* brilho: média 4x4 da cena (4 amostras bilineares); entra a cor inteira a partir do limiar (rampa do tamanho do
 * joelho) no máximo do RGB; o peso sobe com a saturação (neon saturado entra inteiro; pele, roupa clara e chão
 * branco quase nada, senão os personagens viram névoa) */
static const char* FS_BRIGHT =
    "uniform vec4 u_a;    /* limiar, joelho, peso dos tons sem cor, ganho */\n"
    "void main() {\n"
    "  vec3 c = 0.25 * (texture(u_src, v_uv + u_tx * vec2(-1.0, -1.0)).rgb + texture(u_src, v_uv + u_tx * vec2(1.0, -1.0)).rgb\n"
    "                 + texture(u_src, v_uv + u_tx * vec2(-1.0, 1.0)).rgb + texture(u_src, v_uv + u_tx * vec2(1.0, 1.0)).rgb);\n"
    "  float m = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));\n"
    "  float sat = m > 1e-4 ? (m - mn) / m : 0.0;\n"
    "  float w = smoothstep(u_a.x, u_a.x + max(u_a.y, 1e-3), m);\n"
    "  frag = vec4(c * w * mix(u_a.z, 1.0, smoothstep(0.35, 0.85, sat)) * u_a.w, 1.0);\n"
    "}\n";

static const char* FS_DOWN =
    "void main() {\n"
    "  vec3 s = texture(u_src, v_uv).rgb * 4.0;\n"
    "  s += texture(u_src, v_uv + u_tx * vec2(-1.0, -1.0)).rgb + texture(u_src, v_uv + u_tx * vec2(1.0, -1.0)).rgb;\n"
    "  s += texture(u_src, v_uv + u_tx * vec2(-1.0, 1.0)).rgb + texture(u_src, v_uv + u_tx * vec2(1.0, 1.0)).rgb;\n"
    "  frag = vec4(s / 8.0, 1.0);\n"
    "}\n";

/* subida (filtro duplo, 8 amostras), somada por blend ao nível de cima */
static const char* FS_UP =
    "uniform vec4 u_a;    /* ganho deste nível */\n"
    "void main() {\n"
    "  vec2 h = u_tx * 0.5;\n"
    "  vec3 s = texture(u_src, v_uv + vec2(-2.0 * h.x, 0.0)).rgb + texture(u_src, v_uv + vec2(2.0 * h.x, 0.0)).rgb;\n"
    "  s += texture(u_src, v_uv + vec2(0.0, -2.0 * h.y)).rgb + texture(u_src, v_uv + vec2(0.0, 2.0 * h.y)).rgb;\n"
    "  s += 2.0 * (texture(u_src, v_uv + vec2(-h.x, h.y)).rgb + texture(u_src, v_uv + vec2(h.x, h.y)).rgb);\n"
    "  s += 2.0 * (texture(u_src, v_uv + vec2(-h.x, -h.y)).rgb + texture(u_src, v_uv + vec2(h.x, -h.y)).rgb);\n"
    "  frag = vec4(s / 12.0 * u_a.x, 1.0);\n"
    "}\n";

/* composição: graduação da cena + bloom por screen; alfa (máscara do PS1) não é escrito (ColorMask) */
static const char* FS_COMP =
    "uniform sampler2D u_scene, u_bloom, u_adapt;\n"
    "uniform vec4 u_a;    /* origem do retângulo no FBO (x, y), aspecto, 0 */\n"
    "uniform vec4 u_b;    /* força do bloom, mostrar brilho, graduação ligada, vinheta */\n"
    "uniform vec4 u_c;    /* exposição, curva S, saturação, adaptação */\n"
    "uniform vec3 u_lo, u_hi;\n"
    "void main() {\n"
    "  vec3 c = texelFetch(u_scene, ivec2(gl_FragCoord.xy) - ivec2(u_a.xy), 0).rgb;\n"
    "  /* adaptação: o nível 1/64 do brilho (antes das subidas) diz quanto brilho há em volta; telão ou chão\n"
    "     claro inteiros atenuam o halo, neon fino em fundo escuro fica com ele inteiro */\n"
    "  float amb = dot(texture(u_adapt, v_uv).rgb, vec3(0.3333));\n"
    "  vec3 b = texture(u_bloom, v_uv).rgb * u_b.x / (1.0 + u_c.w * amb * 8.0);\n"
    "  if (u_b.y > 0.5) { frag = vec4(b, 0.0); return; }\n"
    "  if (u_b.z > 0.5) {\n"
    "    c *= u_c.x;\n"
    "    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));\n"
    "    float vib = mix(u_c.z, 1.0 + (u_c.z - 1.0) * 0.4, clamp((mx - mn) * 1.5, 0.0, 1.0));   /* mais nos tons pálidos */\n"
    "    c = max(mix(vec3(l), c, vib), 0.0);\n"
    "    c = clamp(c, 0.0, 1.0);\n"
    "    c = mix(c, c * c * (3.0 - 2.0 * c), u_c.y);\n"
    "    c *= mix(u_lo, u_hi, smoothstep(0.05, 0.85, l));\n"
    "    vec2 q = (v_uv - 0.5) * vec2(u_a.z, 1.0);\n"
    "    c *= 1.0 - u_b.w * smoothstep(0.15, 0.9, dot(q, q) * 2.0);\n"
    "  }\n"
    "  c = clamp(c, 0.0, 1.0);\n"
    "  b = clamp(b, 0.0, 1.0);\n"
    "  frag = vec4(1.0 - (1.0 - c) * (1.0 - b), 0.0);\n"
    "}\n";

const char* rmk_post_error(const RmkPost* p) { return p ? p->err : "sem contexto"; }

RmkPost* rmk_post_create(RmkGetProc get_proc, int gles) {
    RmkPost* p = (RmkPost*)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->gles = gles;
    int ok = 1;
#define X(ret, name, args) \
    if (!(p->name = (ret(PGLAPI*) args)get_proc("gl" #name))) { ok = 0; snprintf(p->err, sizeof p->err, "sem gl%s", #name); }
    POST_FUNCS(X)
#undef X
    if (!ok) p->failed = 1;
    return p;
}

static PUint compile(RmkPost* p, PEnum type, const char* a, const char* b) {
    const char* src[3] = {p->gles ? "#version 300 es\n" : "#version 330 core\n", a, b ? b : ""};
    PUint s = p->CreateShader(type);
    p->ShaderSource(s, 3, src, NULL);
    p->CompileShader(s);
    PInt ok = 0;
    p->GetShaderiv(s, P_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400];
        PSizei n = 0;
        p->GetShaderInfoLog(s, sizeof log, &n, log);
        snprintf(p->err, sizeof p->err, "%s: %.*s", type == P_VERTEX_SHADER ? "VS" : "FS", (int)n, log);
        p->DeleteShader(s);
        return 0;
    }
    return s;
}

static int link(RmkPost* p, Prog* g, const char* fs) {
    PUint v = compile(p, P_VERTEX_SHADER, VS, NULL), f = v ? compile(p, P_FRAGMENT_SHADER, FS_HEAD, fs) : 0;
    if (!v || !f) return 0;
    g->prog = p->CreateProgram();
    p->AttachShader(g->prog, v);
    p->AttachShader(g->prog, f);
    p->LinkProgram(g->prog);
    p->DeleteShader(v);
    p->DeleteShader(f);
    PInt ok = 0;
    p->GetProgramiv(g->prog, P_LINK_STATUS, &ok);
    if (!ok) {
        char log[400];
        PSizei n = 0;
        p->GetProgramInfoLog(g->prog, sizeof log, &n, log);
        snprintf(p->err, sizeof p->err, "link: %.*s", (int)n, log);
        return 0;
    }
#define U(f, name) g->f = p->GetUniformLocation(g->prog, name)
    U(src, "u_src"); U(tx, "u_tx"); U(a, "u_a"); U(b, "u_b"); U(c, "u_c"); U(d, "u_d"); U(lo, "u_lo"); U(hi, "u_hi");
    U(scene, "u_scene"); U(bloom, "u_bloom"); U(adapt, "u_adapt");
#undef U
    return 1;
}

int rmk_post_prepare(RmkPost* p) {
    if (!p || p->failed) return 0;
    if (p->ready) return 1;
    if (!link(p, &p->pg[PG_BRIGHT], FS_BRIGHT) || !link(p, &p->pg[PG_DOWN], FS_DOWN) ||
        !link(p, &p->pg[PG_UP], FS_UP) || !link(p, &p->pg[PG_COMP], FS_COMP)) {
        p->failed = 1;
        return 0;
    }
    p->GenVertexArrays(1, &p->vao);
    p->ready = 1;
    return 1;
}

static int target(RmkPost* p, PUint* tex, PUint* fbo, int w, int h, PEnum ifmt, PEnum fmt, PEnum type) {
    if (*tex) p->DeleteTextures(1, tex);
    if (*fbo) p->DeleteFramebuffers(1, fbo);
    p->GenTextures(1, tex);
    p->ActiveTexture(P_TEXTURE0 + 11);
    p->BindTexture(P_TEXTURE_2D, *tex);
    p->TexImage2D(P_TEXTURE_2D, 0, (PInt)ifmt, w, h, 0, fmt, type, NULL);
    p->TexParameteri(P_TEXTURE_2D, P_TEXTURE_MIN_FILTER, P_LINEAR);
    p->TexParameteri(P_TEXTURE_2D, P_TEXTURE_MAG_FILTER, P_LINEAR);
    p->TexParameteri(P_TEXTURE_2D, P_TEXTURE_WRAP_S, P_CLAMP_TO_EDGE);
    p->TexParameteri(P_TEXTURE_2D, P_TEXTURE_WRAP_T, P_CLAMP_TO_EDGE);
    p->GenFramebuffers(1, fbo);
    p->BindFramebuffer(P_FRAMEBUFFER, *fbo);
    p->FramebufferTexture2D(P_FRAMEBUFFER, P_COLOR_ATTACHMENT0, P_TEXTURE_2D, *tex, 0);
    return p->CheckFramebufferStatus(P_FRAMEBUFFER) == P_FRAMEBUFFER_COMPLETE;
}

/* Alvos no tamanho do retângulo (refeitos só quando ele muda: troca de tela larga ou de resolução). */
static int ensure_targets(RmkPost* p, int w, int h) {
    if (w == p->sw && h == p->sh) return 1;
    int ok = target(p, &p->scene_tex, &p->scene_fbo, w, h, P_RGBA8, P_RGBA, P_UNSIGNED_BYTE);
    for (int k = 0; k < LEVELS; k++) {
        p->lw[k] = (w >> (k + 2)) > 0 ? w >> (k + 2) : 1;
        p->lh[k] = (h >> (k + 2)) > 0 ? h >> (k + 2) : 1;
        ok = ok && target(p, &p->lv_tex[k], &p->lv_fbo[k], p->lw[k], p->lh[k], P_RGB10_A2, P_RGBA,
                          P_UNSIGNED_INT_2_10_10_10_REV);
    }
    if (!ok) { snprintf(p->err, sizeof p->err, "alvos do bloom incompletos (%dx%d)", w, h); p->failed = 1; return 0; }
    p->sw = w; p->sh = h;
    return 1;
}

float rmk_post_envelope(float beat, float bar) {
    if (beat < 0.0f) return 0.0f;
    const float down = bar >= 0.0f && bar < 0.25f ? 1.0f : 0.6f;   /* o 1 do compasso mais forte */
    return expf(-beat * 7.0f) * down;
}

void rmk_post_blit(RmkPost* p, uint32_t src, RmkRect rs, uint32_t dst, RmkRect rd, uint32_t bind) {
    if (!p || !p->ready) return;
    p->Disable(P_SCISSOR_TEST);
    p->BindFramebuffer(P_READ_FRAMEBUFFER, src);
    p->BindFramebuffer(P_DRAW_FRAMEBUFFER, dst);
    const int same = rs.w == rd.w && rs.h == rd.h;
    p->BlitFramebuffer(rs.x, rs.y, rs.x + rs.w, rs.y + rs.h, rd.x, rd.y, rd.x + rd.w, rd.y + rd.h, P_COLOR_BUFFER_BIT,
                       same ? P_NEAREST : P_LINEAR);
    p->BindFramebuffer(P_FRAMEBUFFER, bind);
}

static void pass(RmkPost* p, const Prog* g, PUint src, int sw, int sh, PUint fbo, int w, int h) {
    p->BindFramebuffer(P_FRAMEBUFFER, fbo);
    p->Viewport(0, 0, w, h);
    p->UseProgram(g->prog);
    p->ActiveTexture(P_TEXTURE0 + 8);
    p->BindTexture(P_TEXTURE_2D, src);
    p->Uniform1i(g->src, 8);
    p->Uniform2f(g->tx, 1.0f / (float)sw, 1.0f / (float)sh);
}

void rmk_post_apply(RmkPost* p, const RmkPostFrame* fr, uint32_t fbo, RmkRect r, const int vp[4]) {
    if (!p || !p->ready || p->failed || !fr || fr->magic != RMK_POST_MAGIC || (fr->flags & RMK_POST_OFF)) return;
    if (r.w < 64 || r.h < 64 || !ensure_targets(p, r.w, r.h)) return;
    p->Disable(P_BLEND);
    p->Disable(P_DEPTH_TEST);
    p->Disable(P_STENCIL_TEST);
    p->Disable(P_CULL_FACE);
    p->Disable(P_SCISSOR_TEST);
    p->ColorMask(1, 1, 1, 1);
    /* 1. cena */
    p->BindFramebuffer(P_READ_FRAMEBUFFER, fbo);
    p->BindFramebuffer(P_DRAW_FRAMEBUFFER, p->scene_fbo);
    p->BlitFramebuffer(r.x, r.y, r.x + r.w, r.y + r.h, 0, 0, r.w, r.h, P_COLOR_BUFFER_BIT, P_NEAREST);
    p->BindVertexArray(p->vao);
    const float env = rmk_post_envelope(fr->beat, fr->bar);
    const int bloom = !(fr->flags & RMK_POST_NO_BLOOM) && fr->bloom > 0.0f;
    if (bloom) {
        /* 2. brilho em 1/4, descidas, subidas somadas no nível de cima */
        const Prog* g = &p->pg[PG_BRIGHT];
        pass(p, g, p->scene_tex, r.w, r.h, p->lv_fbo[0], p->lw[0], p->lh[0]);
        p->Uniform4f(g->a, fr->thr, fr->knee, fr->satw, 1.0f);
        p->DrawArrays(P_TRIANGLES, 0, 3);
        g = &p->pg[PG_DOWN];
        for (int k = 1; k < LEVELS; k++) {
            pass(p, g, p->lv_tex[k - 1], p->lw[k - 1], p->lh[k - 1], p->lv_fbo[k], p->lw[k], p->lh[k]);
            p->DrawArrays(P_TRIANGLES, 0, 3);
        }
        g = &p->pg[PG_UP];
        p->Enable(P_BLEND);
        p->BlendEquation(P_FUNC_ADD);
        p->BlendFunc(P_ONE, P_ONE);
        for (int k = LEVELS - 2; k >= 0; k--) {
            pass(p, g, p->lv_tex[k + 1], p->lw[k + 1], p->lh[k + 1], p->lv_fbo[k], p->lw[k], p->lh[k]);
            p->Uniform4f(g->a, 1.0f, 0.0f, 0.0f, 0.0f);
            p->DrawArrays(P_TRIANGLES, 0, 3);
        }
        p->Disable(P_BLEND);
    }
    /* 3. composição no retângulo de quem chama */
    const Prog* g = &p->pg[PG_COMP];
    p->BindFramebuffer(P_FRAMEBUFFER, fbo);
    p->Viewport(r.x, r.y, r.w, r.h);
    p->ColorMask(1, 1, 1, 0);
    if (fr->flags & RMK_POST_SPLIT) {
        p->Enable(P_SCISSOR_TEST);
        p->Scissor(r.x + r.w / 2, r.y, r.w - r.w / 2, r.h);
    }
    p->UseProgram(g->prog);
    p->ActiveTexture(P_TEXTURE0 + 8);
    p->BindTexture(P_TEXTURE_2D, p->scene_tex);
    p->ActiveTexture(P_TEXTURE0 + 9);
    p->BindTexture(P_TEXTURE_2D, p->lv_tex[0]);
    p->Uniform1i(g->scene, 8);
    p->Uniform1i(g->bloom, 9);
    p->ActiveTexture(P_TEXTURE0 + 10);
    p->BindTexture(P_TEXTURE_2D, p->lv_tex[LEVELS - 1]);
    p->Uniform1i(g->adapt, 10);
    /* os níveis somados chegam a ~LEVELS vezes o brilho: a força é por nível */
    const float strength = bloom ? fr->bloom * (1.0f + fr->pulse * env) / (float)LEVELS : 0.0f;
    p->Uniform4f(g->a, (float)r.x, (float)r.y, (float)r.w / (float)r.h, 0.0f);
    p->Uniform4f(g->b, strength, (fr->flags & RMK_POST_SHOW_BRIGHT) ? 1.0f : 0.0f,
                 (fr->flags & RMK_POST_NO_GRADE) ? 0.0f : 1.0f, fr->vignette);
    p->Uniform4f(g->c, fr->expo * (1.0f + fr->pulse_expo * env), fr->contrast, fr->sat, fr->adapt);
    p->Uniform3f(g->lo, fr->tint_lo[0], fr->tint_lo[1], fr->tint_lo[2]);
    p->Uniform3f(g->hi, fr->tint_hi[0], fr->tint_hi[1], fr->tint_hi[2]);
    p->DrawArrays(P_TRIANGLES, 0, 3);
    p->ColorMask(1, 1, 1, 1);
    p->Disable(P_SCISSOR_TEST);
    p->BindVertexArray(0);
    p->Viewport(vp[0], vp[1], vp[2], vp[3]);
}
