/* Palco pelo caminho novo (núcleo portátil, rmk.h; fase 6 de docs/remake-plano.md).
 *
 * Asset: o HTR7 de sempre (rmk_asset.c: vértices de cada nó no espaço dele, divididos nos vincos; oclusão ambiente;
 * camadas HD com o STP no alfa) mais a tabela .stg (tools/remake/export_stage.py): para cada primitiva de cada nó, na
 * ordem do buffer de pacotes do jogo, as faces do asset e o slot do pacote de cada canto.
 *
 * Desenho: o adaptador passa cada sequência de pacotes do palco na ordem da OT (a ordem de pintor exata do jogo, com
 * os dançarinos e o resto do quadro entre as sequências). Para cada pacote vivo (cor, UV, CLUT/página: o que o jogo
 * montou neste quadro, então animação de UV ou de cor continua) saem as faces do asset, com a posição pela matriz do
 * nó. Sem profundidade (a ordem é a do jogo), um triângulo por face; as faces de superfícies curvas de verdade (fase 6b:
 * todos os cantos com a normal suave fora da normal da face, e uma aresta lisa com outra face curva) saem numa grade de
 * Phong Tessellation, com as arestas que dão para faces planas presas retas (sem fenda). Luz por
 * pixel sobre a cor do jogo, que já tem a luz do palco "pintada": a reiluminação pesa a diferença entre a luz das
 * luzes do palco (as dos dançarinos, GTE) na normal da geometria e uma luz média, o relevo do mapa de normal pesa a
 * diferença entre a normal perturbada e a da geometria, mais especular GGX e oclusão ambiente; a sombra dos dançarinos
 * (os mapas deles, RmkShadowView) escurece o palco atrás deles. Semitransparência do PS1 por pacote (os 4 modos),
 * só nos texels com STP quando há textura, como no jogo.
 *
 * Estado GL que rmk_stage_gl_draw muda (quem chama restaura): programa, VAO, buffer de vértices, unidade ativa e
 * texturas das unidades 8, 9, 10, 11, 12, 14 e 15 (com objetos de amostragem nas 14 e 15), profundidade (desligada), cull
 * (desligado), blend (função, equação e cor), stencil (desligado), máscara de cor, recorte (o do alvo). */
#include "rmk.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rmk_glfn.h"

#define GEO_RING 3

uint32_t rmk_fnv1a(const uint8_t* p, uint32_t n) {
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

void rmk_stage_free(RmkStage* s) {
    free(s->prims);
    free(s->keys);
    free(s->var);
    memset(s, 0, sizeof *s);
}

int rmk_stage_load(RmkStage* s, const char* path) {
    memset(s, 0, sizeof *s);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t magic = 0, h[8];
    int ok = fread(&magic, 4, 1, f) == 1 && magic == RMK_STAGE_TAB_MAGIC && fread(h, 4, 8, f) == 8 &&
             (h[0] >= 1u && h[0] <= 3u);
    if (ok) {
        s->version = h[0]; s->container = h[1]; s->n_nodes = h[2]; s->n_layers = h[3]; s->n_keys = h[4];
        s->model_bytes = h[5]; s->ov_hash = h[6]; s->model_hash = h[7];
        ok = s->n_nodes && s->n_nodes <= RMK_MAX_NODES && s->n_keys < 4096u;
    }
    for (uint32_t i = 0; ok && i < s->n_nodes; i++) {
        ok = fread(&s->nodes[i], sizeof(RmkStageNode), 1, f) == 1 && s->nodes[i].prim0 == s->n_prims &&
             s->nodes[i].n_prims < 65536u && s->nodes[i].mesh_off < s->model_bytes;
        s->n_prims += ok ? s->nodes[i].n_prims : 0u;
    }
    ok = ok && s->n_prims && s->n_prims < 65536u;
    if (ok) {
        s->prims = (RmkStagePrim*)malloc(sizeof(RmkStagePrim) * s->n_prims);
        s->keys = (RmkStageKey*)malloc(sizeof(RmkStageKey) * (s->n_keys ? s->n_keys : 1u));
        ok = s->prims && s->keys && fread(s->prims, sizeof(RmkStagePrim), s->n_prims, f) == s->n_prims &&
             fread(s->keys, sizeof(RmkStageKey), s->n_keys, f) == s->n_keys;
    }
    if (ok && s->version >= 2u) {    /* variantes de CLUT animada (v2: 8 bytes, só camada; v3: 20 bytes, com o tipo) */
        ok = fread(&s->n_var, 4, 1, f) == 1 && s->n_var < 4096u;
        if (ok && s->n_var) {
            s->var = (RmkStageVar*)calloc(s->n_var, sizeof(RmkStageVar));
            ok = s->var != NULL;
            for (uint32_t i = 0; ok && i < s->n_var; i++) {
                RmkStageVar* v = &s->var[i];
                if (s->version >= 3u) ok = fread(v, sizeof *v, 1, f) == 1;
                else {
                    uint8_t r8[8];
                    ok = fread(r8, 1, 8, f) == 8;
                    memcpy(&v->key, r8, 2); memcpy(&v->src_x, r8 + 2, 2); memcpy(&v->src_y, r8 + 4, 2);
                    v->layer = r8[6]; v->kind = RMK_STAGE_VAR_LAYER;
                    v->k[0] = v->k[1] = v->k[2] = 256; v->b[0] = v->b[1] = v->b[2] = 0;
                }
                ok = ok && v->key < s->n_keys && v->layer < s->n_layers && v->kind <= RMK_STAGE_VAR_AFFINE;
            }
            /* a variante afim viaja no pacote como camada n_layers + índice (u8; 0xE0.. são o telão, 0xFF nenhuma) */
            ok = ok && s->n_layers + s->n_var < RMK_STAGE_LAYER_FB;
        }
    }
    /* o buffer de cada nó tem exatamente as palavras das primitivas (o adaptador confere de novo na RAM) */
    for (uint32_t i = 0; ok && i < s->n_nodes; i++) {
        uint32_t w = 0;
        for (uint32_t p = 0; p < s->nodes[i].n_prims; p++) w += s->prims[s->nodes[i].prim0 + p].words;
        ok = 4u * w == s->nodes[i].buf_bytes;
    }
    fclose(f);
    if (!ok) rmk_stage_free(s);
    return ok;
}

/* ------------------------------------------------------------------ GL */
typedef struct {
    uint16_t vi[3], pad;
    float uv[6];
    uint8_t base[3][4], over[3][4];
    float layer, mode;
    uint32_t twin;                   /* janela de textura do pacote (GP0 E2h) */
} SInst;                             /* 68 bytes por face */
typedef struct { uint32_t key, slot; } EdgeRef;   /* aresta (vértice menor << 16 | maior), 3 * face + canto oposto */

struct RmkStageGL {
#define X(ret, name, args) ret(RGLAPI* name) args;
    GL_FUNCS(X)
#undef X
    int gles, step, ready, failed, has_nmap;
    GLuint_ prog, vao, tri_vbo, inst_vbo, tex, nmap, geo[GEO_RING], smp_shadow;
    GLuint_ vtab;                    /* variantes afins (.stg v3): 2 texels RGBA32F por variante, k e b (unidade 11) */
    uint32_t n_var;
    GLuint_ fb_tex, fb_fbo;          /* telão: a faixa 512 x 256 de um buffer de tela copiada do fbo (unidade 12) */
    int fb_k;                        /* pixels por texel da cópia (até 4) */
    uint32_t fb_serial;              /* quadro da última cópia */
    int fb_y;                        /* buffer copiado (0 ou 256) */
    GLuint_ vao_grid, grid_vbo, grid_ibo;   /* grade de Phong (RMK_TESS_L) para as faces curvas */
    uint8_t* tess;                   /* por face do asset: 128 curva (Phong) | arestas presas (bit k = a oposta ao canto k) */
    uint8_t* curv;                   /* por face: 1 curva (decidido com o nó dela em cena), 0 plana ou nó ainda não visto */
    EdgeRef* edges;                  /* as 3 arestas de cada face, ordenadas pela chave (uma vez por asset) */
    uint64_t nodes_done;             /* nós cujas faces já foram decididas */
    uint32_t n_tess;                 /* faces curvas (estatística) */
    int geo_w, geo_h, ring;
    uint32_t serial, have_frame, n_inst_cap, inst_used, run_key, run_first, run_n, n_free;
    float* pos;                      /* posições no espaço da câmera (3 floats por vértice) */
    float* staging;
    SInst* inst;
    uint8_t* seg;                    /* por instância da sequência: grupo de mistura (0 opaco, 1..4 modo semi + 1) | 128 Phong */
    float ofx, ofy, h, expo, rough;
    float ldir[3][3], lcol[3][3], bk[3];
    float relight, detail, spec, ao, shadow, pulse, floor_on, floor_n[3];
    uint32_t flags;
    GLint_ u_geo, u_geo_w, u_ndc, u_proj, u_tex, u_nmap, u_nmap_on, u_semi, u_ldir, u_lcol, u_bk, u_w, u_w2, u_floor;
    GLint_ u_sh[2], u_su[1], u_sv[1], u_sd[1], u_sc[1], u_nsh, u_phong, u_var, u_fb;
    uint64_t bytes;
    char err[512];
};

static const char* VS =
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp sampler2D u_geo;\n"
    "uniform int u_geo_w;\n"
    "uniform vec4 u_ndc;   /* NDC = tela * (x, z) + (y, w) */\n"
    "uniform vec4 u_proj;  /* OFX, OFY, H do GTE */\n"
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
    "layout(location = 11) in uint a_tess;   /* 128 face curva | arestas presas */\n"
    "layout(location = 12) in uint a_twin;   /* janela de textura (GP0 E2h) */\n"
    "flat out uint v_twin;\n"
    "uniform float u_phong;   /* alfa do Phong (0 = desligado) */\n"
    "out vec3 v_pos; out vec3 v_nrm; out vec3 v_base; out vec3 v_over; out vec2 v_uv; out float v_ao;\n"
    "flat out int v_layer; flat out int v_mode;\n"
    "flat out vec4 v_uvr;   /* retângulo de UV da face (centros dos texels dos cantos) */\n"
    "vec4 geo(uint i) { int k = int(i); return texelFetch(u_geo, ivec2(k % u_geo_w, k / u_geo_w), 0); }\n"
    "void main() {\n"
    "  vec3 b = a_bary;\n"
    "  vec4 p0 = geo(a_vi.x * 2u), p1 = geo(a_vi.y * 2u), p2 = geo(a_vi.z * 2u);\n"
    "  vec3 n0 = geo(a_vi.x * 2u + 1u).xyz, n1 = geo(a_vi.y * 2u + 1u).xyz, n2 = geo(a_vi.z * 2u + 1u).xyz;\n"
    "  vec3 c = p0.xyz * b.x + p1.xyz * b.y + p2.xyz * b.z;\n"
    "  vec4 p = vec4(c, 1.0);\n"
    "  if ((a_tess & 128u) != 0u && u_phong > 0.0) {   /* Phong Tessellation (Boubekeur & Alexa 2008) */\n"
    "    vec3 ph = b.x * (c - dot(c - p0.xyz, n0) * n0) + b.y * (c - dot(c - p1.xyz, n1) * n1)\n"
    "            + b.z * (c - dot(c - p2.xyz, n2) * n2);\n"
    "    /* aresta presa (a vizinha é plana ou não existe): reta nela, o Phong entra a um passo da grade */\n"
    "    float w = 1.0;\n"
    "    if ((a_tess & 1u) != 0u) w *= clamp(b.x * 4.0, 0.0, 1.0);\n"
    "    if ((a_tess & 2u) != 0u) w *= clamp(b.y * 4.0, 0.0, 1.0);\n"
    "    if ((a_tess & 4u) != 0u) w *= clamp(b.z * 4.0, 0.0, 1.0);\n"
    "    p.xyz = mix(c, ph, u_phong * w);\n"
    "  }\n"
    "  v_pos = p.xyz; v_ao = p0.w * b.x + p1.w * b.y + p2.w * b.z; v_nrm = n0 * b.x + n1 * b.y + n2 * b.z;\n"
    "  v_base = a_base0.rgb * b.x + a_base1.rgb * b.y + a_base2.rgb * b.z;\n"
    "  v_over = (a_over0.rgb * b.x + a_over1.rgb * b.y + a_over2.rgb * b.z) * (255.0 / 128.0);\n"
    "  v_uv = (a_uv01.xy * b.x + a_uv01.zw * b.y + a_uv2 * b.z + 0.5) / 256.0;\n"
    "  v_uvr = vec4(min(min(a_uv01.xy, a_uv01.zw), a_uv2) + 0.5, max(max(a_uv01.xy, a_uv01.zw), a_uv2) + 0.5) / 256.0;\n"
    "  v_layer = int(floor(a_lm.x + 0.5)); v_mode = int(floor(a_lm.y + 0.5)); v_twin = a_twin;\n"
    "  /* tela * z, sem dividir: o GL recorta em w = z > 0 (vértice atrás da câmera numa face que o jogo desenhou) */\n"
    "  vec2 sz = u_proj.xy * p.z + u_proj.z * p.xy;\n"
    "  gl_Position = vec4(sz * u_ndc.xz + u_ndc.yw * p.z, 0.0, p.z);\n"
    "}\n";

static const char* FS =
    "precision highp float;\n"
    "precision highp int;\n"
    "precision mediump sampler2DArray;\n"
    "precision mediump sampler2DShadow;\n"
    "uniform mediump sampler2DArray u_tex;\n"
    "uniform mediump sampler2DArray u_nmap;\n"
    "uniform highp sampler2D u_var;   /* variantes afins da paleta: texel (0, v) = k, (1, v) = b (texelFetch) */\n"
    "uniform mediump sampler2D u_fb;   /* telão: a faixa 512 x 256 do buffer de tela que o pacote lê */\n"
    "uniform float u_nmap_on;\n"
    "uniform int u_semi;    /* -1 face opaca; 0 face semi, texels sem STP; 1 face semi, só os texels com STP */\n"
    "uniform vec3 u_ldir[3], u_lcol[3], u_bk;\n"
    "uniform vec4 u_w;      /* reiluminação, relevo, especular, oclusão ambiente */\n"
    "uniform vec4 u_w2;     /* sombra dos dançarinos, pulso, exposição, rugosidade */\n"
    "uniform mediump sampler2DShadow u_sh0, u_sh1;\n"
    "uniform vec3 u_su[2], u_sv[2], u_sd[2];\n"
    "uniform vec4 u_sc[2];\n"
    "uniform int u_nsh;\n"
    "uniform vec4 u_floor;  /* normal do chão (câmera) e 1 = a sombra macia no chão já sai (o chão fica sem esta) */\n"
    "in vec3 v_pos; in vec3 v_nrm; in vec3 v_base; in vec3 v_over; in vec2 v_uv; in float v_ao;\n"
    "flat in int v_layer; flat in int v_mode;\n"
    "flat in vec4 v_uvr;\n"
    "flat in highp uint v_twin;   /* 20 bits: mediump não cabe */\n"
    "layout(location = 0) out vec4 frag;\n"
    "vec3 perturb(vec3 n, vec3 p, vec2 uv, vec3 t) {\n"
    "  vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 du1 = dFdx(uv), du2 = dFdy(uv);\n"
    "  vec3 dp2p = cross(dp2, n), dp1p = cross(n, dp1);\n"
    "  vec3 T = dp2p * du1.x + dp1p * du2.x, B = dp2p * du1.y + dp1p * du2.y;\n"
    "  float im = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));\n"
    "  return normalize(T * im * t.x + B * im * t.y + n * t.z);\n"
    "}\n"
    "/* janela de textura do PS1 (GP0 E2h), por eixo: texel = (texel & ~(máscara * 8)) | ((deslocamento & máscara) * 8);\n"
    "   a leitura fica na célula da janela (o menor bit da máscara), sem o filtro passar para a vizinha */\n"
    "vec2 twin_uv(vec2 uv) {\n"
    "  vec2 t = uv * 256.0;\n"
    "  uvec2 m = uvec2(v_twin & 31u, (v_twin >> 5) & 31u) * 8u;\n"
    "  uvec2 o = uvec2((v_twin >> 10) & 31u, (v_twin >> 15) & 31u) * 8u;\n"
    "  uvec2 w = (uvec2(max(floor(t), 0.0)) & 255u & ~m) | (o & m);\n"
    "  vec2 r = vec2(w) + fract(t);\n"
    "  uvec2 sz = m & (~m + 1u);\n"
    "  vec2 cs = vec2(w & ~(sz - 1u)), fs = vec2(sz);\n"
    "  vec2 rc = clamp(r, cs + 0.5, cs + fs - 0.5);\n"
    "  r = mix(r, rc, vec2(greaterThan(sz, uvec2(0u))));\n"
    "  return r / 256.0;\n"
    "}\n"
    "float wrapd(float nl) { return max((nl + 0.3) / 1.3, 0.0); }\n"
    "float D_ggx(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (3.14159 * d * d); }\n"
    "/* sombra de um dançarino: mapa ortográfico em volta dele; fora do mapa, aceso; atrás dele na direção da luz, a\n"
    "   coluna inteira (luz direcional) */\n"
    "float shadow_of(mediump sampler2DShadow sm, vec3 u, vec3 v, vec3 d, vec4 c) {\n"
    "  vec3 r = v_pos - c.xyz;\n"
    "  vec3 q = vec3(dot(r, u), dot(r, v), dot(r, d)) * c.w * 0.5 + 0.5;\n"
    "  if (q.z < 0.02 || any(lessThan(q.xy, vec2(0.0))) || any(greaterThan(q.xy, vec2(1.0)))) return 1.0;\n"
    "  /* 5 leituras com comparação bilinear (como a sombra própria dos dançarinos): borda macia de ~3 texels */\n"
    "  vec3 o = vec3(q.xy, min(q.z - 0.01, 0.995));\n"
    "  float k = 1.5 / 1024.0, s = texture(sm, o) * 2.0;\n"
    "  s += texture(sm, o + vec3(k, k, 0.0)) + texture(sm, o + vec3(-k, k, 0.0));\n"
    "  s += texture(sm, o + vec3(k, -k, 0.0)) + texture(sm, o + vec3(-k, -k, 0.0));\n"
    "  s /= 6.0;\n"
    "  vec2 e = min(q.xy, 1.0 - q.xy);\n"
    "  return mix(1.0, s, smoothstep(0.0, 0.05, min(e.x, e.y)));\n"
    "}\n"
    "void main() {\n"
    "  vec3 base = clamp(v_base, 0.0, 1.0);\n"
    "  int md = v_mode & 3;\n"
    "  float tex_a = 0.0, cover = 1.0;\n"
    "  bool stp = (v_mode & 256) != 0;\n"
    "  vec2 gx = dFdx(v_uv), gy = dFdy(v_uv);\n"
    "  vec4 nm = vec4(0.5, 0.5, 1.0, 1.0);   /* mapa de normal (xyz) e STP (a): uma leitura para os dois */\n"
    "  if (md > 0 && v_layer >= 0) {\n"
    "    /* o PS1 lê sem filtro: com o filtro da textura HD a borda da face puxaria os texels vizinhos da página (a\n"
    "       junta escura entre ladrilhos); a leitura fica no retângulo de UV da face */\n"
    "    vec3 uc = vec3(v_twin != 0u ? twin_uv(v_uv) : clamp(v_uv, v_uvr.xy, v_uvr.zw), float(v_layer));\n"
    "    vec4 t = textureGrad(u_tex, uc, gx, gy);\n"
    "    int vv = (v_mode >> 9) & 255;   /* paleta animada afim (bits 9+ do modo): rgb' = k * rgb + b na camada base */\n"
    "    if (vv > 0) t.rgb = clamp(t.rgb * texelFetch(u_var, ivec2(0, vv - 1), 0).rgb + texelFetch(u_var, ivec2(1, vv - 1), 0).rgb, 0.0, 1.0);\n"
    "    nm = textureGrad(u_nmap, uc, gx, gy);\n"
    "    stp = stp && nm.a > 0.5;\n"
    "    /* alfa = campo suave de cobertura (borda em 0,5): borda com antisserrilhado, misturada na ordem de pintor */\n"
    "    float fw = max(fwidth(t.a), 1e-3) * 0.75;\n"
    "    float cv = smoothstep(0.5 - fw, 0.5 + fw, t.a);\n"
    "    if (md == 2) { if (cv < 0.004) discard; base = t.rgb * v_over; tex_a = 1.0; cover = cv; }\n"
    "    else { base = mix(base, t.rgb * v_over, cv); tex_a = cv; }\n"
    "  } else if (md > 0 && v_layer == -2) {   /* telão: o quadro na VRAM, modulado pela cor do pacote; a página de 16\n"
    "       bpp tem 256 texels de largura a partir da coluna X = 64 * (bits 18..20 do modo) da faixa de 512 */\n"
    "    vec2 q = clamp(v_uv, v_uvr.xy, v_uvr.zw);\n"
    "    q.x = (float((v_mode >> 18) & 7) * 64.0 + q.x * 256.0) / 512.0;\n"
    "    base = texture(u_fb, q).rgb * v_over; tex_a = 1.0; stp = false;\n"
    "  }\n"
    "  if (u_semi >= 0 && stp != (u_semi == 1)) discard;\n"
    "  if ((v_mode & 131072) != 0) { frag = vec4(clamp(base, 0.0, 1.0), cover); return; }   /* emissivo: sem luz */\n"
    "  vec3 alb = pow(clamp(base, 0.0, 1.0), vec3(2.2));\n"
    "  vec3 v = normalize(-v_pos);\n"
    "  vec3 n0 = normalize(v_nrm);\n"
    "  if (dot(n0, v) < 0.0) n0 = -n0;\n"
    "  vec3 n = n0;\n"
    "  if (tex_a > 0.5 && u_nmap_on > 0.5 && (v_mode & 4) == 0) n = perturb(n0, v_pos, v_uv, nm.xyz * 2.0 - 1.0);\n"
    "  vec3 Ig = u_bk, In = u_bk, Ir = u_bk, sp = vec3(0.0);\n"
    "  float a = u_w2.w * u_w2.w;\n"
    "  for (int i = 0; i < 3; i++) {\n"
    "    vec3 c = max(u_lcol[i], 0.0), l = u_ldir[i];\n"
    "    Ig += c * wrapd(dot(n0, l)); In += c * wrapd(dot(n, l)); Ir += c * 0.5;\n"
    "    vec3 h = normalize(l + v);\n"
    "    float nl = max(dot(n, l), 0.0), nh = max(dot(n, h), 0.0);\n"
    "    float F = 0.04 + 0.96 * pow(1.0 - max(dot(h, v), 0.0), 5.0);\n"
    "    sp += c * D_ggx(nh, a) * F * 0.25 * nl / max(dot(n, v), 0.2);\n"
    "  }\n"
    "  vec3 ref = max(Ir, vec3(0.05));\n"
    "  vec3 macro = clamp(mix(vec3(1.0), Ig / ref, u_w.x), 0.45, 1.7);\n"
    "  vec3 detail = clamp(vec3(1.0) + u_w.y * (In - Ig) / ref, 0.5, 1.6);\n"
    "  float sh = 1.0;\n"
    "  if (u_nsh > 0) sh *= shadow_of(u_sh0, u_su[0], u_sv[0], u_sd[0], u_sc[0]);\n"
    "  if (u_nsh > 1) sh *= shadow_of(u_sh1, u_su[1], u_sv[1], u_sd[1], u_sc[1]);\n"
    "  if (u_floor.w > 0.5 && dot(n0, u_floor.xyz) > 0.85) sh = 1.0;\n"
    "  sh = mix(1.0, sh, u_w2.x);\n"
    "  float ao = mix(1.0, v_ao, u_w.w);\n"
    "  /* o especular segue a luz que o jogo pôs na cor do vértice (face escurecida pela névoa ou sombra pintada não brilha) */\n"
    "  vec3 vc = md == 0 ? v_base : v_over;\n"
    "  float lit = clamp(dot(vc, vec3(0.299, 0.587, 0.114)) * 1.6, 0.0, 1.0);\n"
    "  vec3 c = alb * macro * detail * ao * sh + sp * u_w.z * (1.0 + u_w2.y) * sh * ao * lit * lit;\n"
    "  c *= u_w2.z;\n"
    "  frag = vec4(pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)), cover);   /* alfa: cobertura da borda (não é escrito) */\n"
    "}\n";

const char* rmk_stage_gl_error(const RmkStageGL* g) { return g ? g->err : "sem contexto"; }
uint64_t rmk_stage_gl_bytes(const RmkStageGL* g) { return g ? g->bytes : 0u; }
uint32_t rmk_stage_gl_curved(const RmkStageGL* g) { return g ? g->n_tess : 0u; }

void rmk_stage_gl_destroy(RmkStageGL* g) {
    if (g && g->vtab) { g->DeleteTextures(1, &g->vtab); g->vtab = 0; }
    if (g && g->fb_fbo) { g->DeleteFramebuffers(1, &g->fb_fbo); g->fb_fbo = 0; }
    if (g && g->fb_tex) { g->DeleteTextures(1, &g->fb_tex); g->fb_tex = 0; }
    if (!g) return;
    if (g->DeleteTextures) {
        const GLuint_ tx[2] = {g->tex, g->nmap};
        g->DeleteTextures(2, tx);
        g->DeleteTextures(GEO_RING, g->geo);
        const GLuint_ bf[2] = {g->tri_vbo, g->inst_vbo};
        g->DeleteBuffers(2, bf);
        g->DeleteVertexArrays(1, &g->vao);
        g->DeleteVertexArrays(1, &g->vao_grid);
        const GLuint_ gb[2] = {g->grid_vbo, g->grid_ibo};
        g->DeleteBuffers(2, gb);
        g->DeleteSamplers(1, &g->smp_shadow);
        if (g->prog) g->DeleteProgram(g->prog);
    }
    free(g->pos);
    free(g->staging);
    free(g->inst);
    free(g->seg);
    free(g->tess);
    free(g->curv);
    free(g->edges);
    free(g);
}

RmkStageGL* rmk_stage_gl_create(RmkGetProc get_proc, int gles) {
    RmkStageGL* g = (RmkStageGL*)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->gles = gles;
    int ok = 1;
#define X(ret, name, args) \
    if (!(g->name = (ret(RGLAPI*) args)get_proc("gl" #name))) { ok = 0; snprintf(g->err, sizeof g->err, "sem gl%s", #name); }
    GL_FUNCS(X)
#undef X
    if (!ok) g->failed = 1;
    return g;
}

static GLuint_ compile(RmkStageGL* g, GLenum_ type, const char* body) {
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
        snprintf(g->err, sizeof g->err, "palco %s: %.*s", type == R_VERTEX_SHADER ? "VS" : "FS", (int)n, log);
        g->DeleteShader(s);
        return 0;
    }
    return s;
}

static void inst_attribs(RmkStageGL* g, uint32_t first) {
    const GLsizei_ st = (GLsizei_)sizeof(SInst);
    const size_t b = (size_t)first * sizeof(SInst);
    g->VertexAttribIPointer(1, 3, R_UNSIGNED_SHORT, st, (const void*)(b + offsetof(SInst, vi)));
    g->VertexAttribPointer(2, 4, R_FLOAT, 0, st, (const void*)(b + offsetof(SInst, uv)));
    g->VertexAttribPointer(3, 2, R_FLOAT, 0, st, (const void*)(b + offsetof(SInst, uv) + 16));
    for (int k = 0; k < 3; k++) {
        g->VertexAttribPointer(4 + k, 4, R_UNSIGNED_BYTE, 1, st, (const void*)(b + offsetof(SInst, base) + 4 * k));
        g->VertexAttribPointer(7 + k, 4, R_UNSIGNED_BYTE, 1, st, (const void*)(b + offsetof(SInst, over) + 4 * k));
    }
    g->VertexAttribPointer(10, 2, R_FLOAT, 0, st, (const void*)(b + offsetof(SInst, layer)));
    g->VertexAttribIPointer(11, 1, R_UNSIGNED_SHORT, st, (const void*)(b + offsetof(SInst, pad)));
    g->VertexAttribIPointer(12, 1, R_UNSIGNED_INT, st, (const void*)(b + offsetof(SInst, twin)));
}

/* Grade baricêntrica de Phong: ponto (i, j) = (1 - i/L - j/L, i/L, j/L) */
#define GRID_PTS ((RMK_TESS_L + 1) * (RMK_TESS_L + 2) / 2)
#define GRID_IDX (RMK_TESS_L * RMK_TESS_L * 3)
static int gi(int i, int j) { return i * (RMK_TESS_L + 1) - i * (i - 1) / 2 + j; }

/* Os atributos do VAO ligado: 0 por vértice (do buffer ligado agora), 1..12 por instância do inst_vbo. */
static void vao_attribs(RmkStageGL* g) {
    g->EnableVertexAttribArray(0);
    g->VertexAttribPointer(0, 3, R_FLOAT, 0, 12, (const void*)0);
    g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
    for (int i = 1; i <= 12; i++) g->EnableVertexAttribArray((GLuint_)i);
    inst_attribs(g, 0);
    for (int i = 1; i <= 12; i++) g->VertexAttribDivisor((GLuint_)i, 1);
}

/* Quais faces do asset saem pela grade de Phong. Cada face é decidida no primeiro quadro em que o nó dela está em cena,
 * no espaço da câmera: a matriz do nó traz a escala do modelo (no espaço do nó dono, o dos vértices do asset, as
 * arestas medem outra coisa: 233 266 faces curvas contra 612), e PHONG_EDGE foi medido offline nas posições do modelo
 * montado (`export_stage.py`, pose_positions). O ângulo entre normais de um mesmo nó não muda com a pose. A primeira
 * versão decidia tudo no primeiro quadro, e os nós ainda fora de cena (matriz de espera: identidade com
 * t[2] = RMK_STAGE_UNSEEN_Z, ou seja, no espaço do nó) davam outra conta a cada carga (232: 208 ou 512; 236: 48 a 81).
 * Curva: os três cantos com a normal suave fora da normal da face (3° a 45°) e a maior aresta até PHONG_EDGE (as peças;
 * os morros e paredões do fundo, com arestas de milhares de unidades, mudariam a silhueta contra o céu e abririam
 * frestas). Aresta presa: não é dividida só por faces curvas que usam os mesmos dois vértices (vizinha plana, borda ou
 * vinco: Phong de um lado só abriria uma fenda). Face curva com as três arestas presas fica plana (incharia sozinha). As
 * travas são refeitas a cada nó novo, numa passada pelas arestas (ordenadas uma vez por asset). */
#define PHONG_EDGE 1200.0f             /* unidades do GTE */
static int edge_cmp(const void* a, const void* b) {
    const uint32_t x = ((const EdgeRef*)a)->key, y = ((const EdgeRef*)b)->key;
    return x < y ? -1 : x > y;
}
static void classify_tess(RmkStageGL* g, const RmkAsset* a, uint64_t done) {
    const float lo = cosf(3.0f * 3.14159265f / 180.0f), hi = cosf(45.0f * 3.14159265f / 180.0f);
    const uint32_t ne = 3u * a->n_tris;
    if (!g->edges) {
        g->edges = (EdgeRef*)malloc(sizeof(EdgeRef) * (ne ? ne : 1u));
        if (!g->edges) return;
        for (uint32_t f = 0; f < a->n_tris; f++)
            for (uint32_t k = 0; k < 3u; k++) {   /* aresta oposta ao canto k */
                const uint32_t x = a->tris[f].v[(k + 1u) % 3u], y = a->tris[f].v[(k + 2u) % 3u];
                g->edges[3u * f + k].key = x < y ? (x << 16) | y : (y << 16) | x;
                g->edges[3u * f + k].slot = 3u * f + k;
            }
        qsort(g->edges, ne, sizeof *g->edges, edge_cmp);
    }
    const EdgeRef* e = g->edges;
    for (uint32_t f = 0; f < a->n_tris; f++) {
        const RmkTri* t = &a->tris[f];
        uint64_t own = 0;
        for (int k = 0; k < 3; k++) own |= 1ull << (a->verts[t->v[k]].owner & 63u);
        if ((own & done) != own || (own & g->nodes_done) == own) continue;   /* nó fora de cena ou já decidida */
        const float* s0 = &g->staging[8u * t->v[0]];
        const float* s1 = &g->staging[8u * t->v[1]];
        const float* s2 = &g->staging[8u * t->v[2]];
        const float u[3] = {s1[0] - s0[0], s1[1] - s0[1], s1[2] - s0[2]}, w[3] = {s2[0] - s0[0], s2[1] - s0[1], s2[2] - s0[2]};
        const float n[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
        const float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const float e2 = s2[0] - s1[0], e2y = s2[1] - s1[1], e2z = s2[2] - s1[2];
        const float m2 = fmaxf(fmaxf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2], w[0] * w[0] + w[1] * w[1] + w[2] * w[2]),
                               e2 * e2 + e2y * e2y + e2z * e2z);
        int curved = l > 1e-6f && m2 <= PHONG_EDGE * PHONG_EDGE;
        for (int k = 0; k < 3 && curved; k++) {
            const float* v = &g->staging[8u * t->v[k] + 4u];
            const float d = fabsf(v[0] * n[0] + v[1] * n[1] + v[2] * n[2]) / l;
            curved = d < lo && d > hi;
        }
        g->curv[f] = (uint8_t)curved;
    }
    for (uint32_t f = 0; f < a->n_tris; f++) g->tess[f] = g->curv[f] ? 128u : 0u;
    for (uint32_t i = 0; i < ne;) {
        uint32_t j = i + 1u;
        while (j < ne && e[j].key == e[i].key) j++;
        int all = j - i >= 2u;
        for (uint32_t k = i; k < j && all; k++) all = (g->tess[e[k].slot / 3u] & 128u) != 0;
        if (!all)
            for (uint32_t k = i; k < j; k++) g->tess[e[k].slot / 3u] |= (uint8_t)(1u << (e[k].slot % 3u));
        i = j;
    }
    uint32_t nt = 0;
    for (uint32_t f = 0; f < a->n_tris; f++) {
        if ((g->tess[f] & 7u) == 7u) g->tess[f] = 0;
        nt += (g->tess[f] & 128u) != 0;
    }
    g->n_tess = nt;
}

static int build(RmkStageGL* g, const RmkAsset* a, const RmkStage* s) {
    GLuint_ vs = compile(g, R_VERTEX_SHADER, VS), fs = vs ? compile(g, R_FRAGMENT_SHADER, FS) : 0;
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
        snprintf(g->err, sizeof g->err, "palco link: %.*s", (int)n, log);
        return 0;
    }
#define U(name) g->name = g->GetUniformLocation(g->prog, #name)
    U(u_geo); U(u_geo_w); U(u_ndc); U(u_proj); U(u_tex); U(u_nmap); U(u_nmap_on); U(u_semi); U(u_bk); U(u_w); U(u_w2);
    U(u_nsh); U(u_floor); U(u_phong); U(u_var); U(u_fb);
#undef U
    g->u_ldir = g->GetUniformLocation(g->prog, "u_ldir[0]");
    g->u_lcol = g->GetUniformLocation(g->prog, "u_lcol[0]");
    g->u_sh[0] = g->GetUniformLocation(g->prog, "u_sh0");
    g->u_sh[1] = g->GetUniformLocation(g->prog, "u_sh1");
    g->u_su[0] = g->GetUniformLocation(g->prog, "u_su[0]");
    g->u_sv[0] = g->GetUniformLocation(g->prog, "u_sv[0]");
    g->u_sd[0] = g->GetUniformLocation(g->prog, "u_sd[0]");
    g->u_sc[0] = g->GetUniformLocation(g->prog, "u_sc[0]");
    /* um triângulo por instância: os cantos são as baricêntricas (1,0,0), (0,1,0), (0,0,1) */
    static const float tri[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    g->n_inst_cap = 2u * a->n_tris + 64u;
    g->inst = (SInst*)calloc(g->n_inst_cap, sizeof(SInst));
    g->seg = (uint8_t*)calloc(g->n_inst_cap, 1);
    g->tess = (uint8_t*)calloc(a->n_tris ? a->n_tris : 1u, 1);
    g->curv = (uint8_t*)calloc(a->n_tris ? a->n_tris : 1u, 1);
    g->pos = (float*)calloc(3u * a->n_verts, sizeof(float));
    g->n_inst_cap += 2u * RMK_STAGE_MAX_FREE;
    const int texels = 2 * (int)(a->n_verts + RMK_STAGE_MAX_FREE);   /* asset + vértices livres do quadro */
    g->geo_w = texels < 1024 ? texels : 1024;
    g->geo_h = (texels + g->geo_w - 1) / g->geo_w;
    g->staging = (float*)calloc((size_t)g->geo_w * g->geo_h * 4u, sizeof(float));
    if (!g->inst || !g->seg || !g->pos || !g->staging || !g->tess || !g->curv) return 0;
    float grid[GRID_PTS * 3];
    uint16_t idx[GRID_IDX];
    {
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
    }
    g->GenBuffers(1, &g->inst_vbo);
    g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
    g->BufferData(R_ARRAY_BUFFER, (GLsizeiptr_)(sizeof(SInst) * g->n_inst_cap), NULL, R_STREAM_DRAW);
    g->GenVertexArrays(1, &g->vao);
    g->BindVertexArray(g->vao);
    g->GenBuffers(1, &g->tri_vbo);
    g->BindBuffer(R_ARRAY_BUFFER, g->tri_vbo);
    g->BufferData(R_ARRAY_BUFFER, sizeof tri, tri, R_STATIC_DRAW);
    vao_attribs(g);
    g->GenVertexArrays(1, &g->vao_grid);
    g->BindVertexArray(g->vao_grid);
    g->GenBuffers(1, &g->grid_vbo);
    g->BindBuffer(R_ARRAY_BUFFER, g->grid_vbo);
    g->BufferData(R_ARRAY_BUFFER, sizeof grid, grid, R_STATIC_DRAW);
    vao_attribs(g);
    g->GenBuffers(1, &g->grid_ibo);
    g->BindBuffer(R_ELEMENT_ARRAY_BUFFER, g->grid_ibo);   /* fica no VAO da grade */
    g->BufferData(R_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, R_STATIC_DRAW);
    g->BindVertexArray(0);
    g->BindBuffer(R_ARRAY_BUFFER, 0);
    g->GenTextures(GEO_RING, g->geo);
    g->ActiveTexture(R_TEXTURE0 + 8);
    for (int r = 0; r < GEO_RING; r++) {
        g->BindTexture(R_TEXTURE_2D, g->geo[r]);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MIN_FILTER, R_NEAREST);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MAG_FILTER, R_NEAREST);
        g->TexImage2D(R_TEXTURE_2D, 0, R_RGBA32F, g->geo_w, g->geo_h, 0, R_RGBA, R_FLOAT, NULL);
    }
    g->BindTexture(R_TEXTURE_2D, 0);
    g->GenTextures(1, &g->tex);
    g->ActiveTexture(R_TEXTURE0 + 9);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->tex);
    g->TexImage3D(R_TEXTURE_2D_ARRAY, 0, R_RGBA8, (GLsizei_)a->tex_size, (GLsizei_)a->tex_size,
                  (GLsizei_)(a->n_layers ? a->n_layers : 1), 0, R_RGBA, R_UNSIGNED_BYTE, NULL);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MIN_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_MAG_FILTER, R_LINEAR);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->TexParameteri(R_TEXTURE_2D_ARRAY, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
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
    /* sombra dos dançarinos: comparação de profundidade pelo objeto de amostragem (vale para o mapa do chão, que
     * não tem comparação na textura, e para o da sombra própria) */
    g->GenSamplers(1, &g->smp_shadow);
    g->SamplerParameteri(g->smp_shadow, R_TEXTURE_MIN_FILTER, R_LINEAR);
    g->SamplerParameteri(g->smp_shadow, R_TEXTURE_MAG_FILTER, R_LINEAR);
    g->SamplerParameteri(g->smp_shadow, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
    g->SamplerParameteri(g->smp_shadow, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
    g->SamplerParameteri(g->smp_shadow, R_TEXTURE_COMPARE_MODE, R_COMPARE_REF_TO_TEXTURE);
    g->SamplerParameteri(g->smp_shadow, R_TEXTURE_COMPARE_FUNC, R_LEQUAL);
    /* variantes afins da paleta (.stg v3): k e b de cada uma numa textura 2 x n_var RGBA32F, lida por texelFetch */
    {
        const uint32_t nv = s && s->n_var ? s->n_var : 1u;
        float* tab = (float*)calloc((size_t)nv * 8u, sizeof(float));
        if (!tab) return 0;
        for (uint32_t v = 0; v < nv; v++) {
            float* k = tab + 8u * v;
            k[0] = k[1] = k[2] = 1.0f; k[3] = 1.0f; k[7] = 1.0f;
            if (s && v < s->n_var)
                for (int c = 0; c < 3; c++) { k[c] = (float)s->var[v].k[c] / 256.0f; k[4 + c] = (float)s->var[v].b[c] / 4096.0f; }
        }
        g->n_var = s ? s->n_var : 0u;
        g->GenTextures(1, &g->vtab);
        g->ActiveTexture(R_TEXTURE0 + 11);
        g->BindTexture(R_TEXTURE_2D, g->vtab);
        g->TexImage2D(R_TEXTURE_2D, 0, R_RGBA32F, 2, (GLsizei_)nv, 0, R_RGBA, R_FLOAT, tab);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MIN_FILTER, R_NEAREST);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MAG_FILTER, R_NEAREST);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
        g->BindTexture(R_TEXTURE_2D, 0);
        free(tab);
    }
    const uint64_t layer = (uint64_t)a->tex_size * a->tex_size * 4u * (a->n_layers ? a->n_layers : 1u);
    g->bytes += layer * 4u / 3u + (uint64_t)ns * ns * 4u * (a->n_layers ? a->n_layers : 1u) * 4u / 3u +
                (uint64_t)GEO_RING * g->geo_w * g->geo_h * 16u + sizeof(SInst) * g->n_inst_cap;
    return g->GetError() == 0;
}

int rmk_stage_gl_prepare(RmkStageGL* g, const RmkAsset* a, const RmkStage* s) {
    if (!g || g->failed) return 0;
    if (g->ready) return 1;
    while (g->GetError() != 0) {}
    g->PixelStorei(R_UNPACK_ALIGNMENT, 4);
    g->PixelStorei(R_UNPACK_ROW_LENGTH, 0);
    if (g->step == 0) {
        if (!build(g, a, s)) {
            if (!g->err[0]) snprintf(g->err, sizeof g->err, "palco: recursos GL, erro 0x%X", g->GetError());
            g->failed = 1;
            return 0;
        }
        g->step = 1;
        return 0;
    }
    g->ActiveTexture(R_TEXTURE0 + 9);
    const uint32_t k = (uint32_t)g->step - 1u, nm = a->nmaps ? a->n_layers : 0u;
    const size_t bytes = (size_t)a->tex_size * a->tex_size * 4u;
    if (k < a->n_layers + nm) {
        const int is_n = k >= a->n_layers;
        const uint32_t l = is_n ? k - a->n_layers : k;
        g->BindTexture(R_TEXTURE_2D_ARRAY, is_n ? g->nmap : g->tex);
        g->TexSubImage3D(R_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint_)l, (GLsizei_)a->tex_size, (GLsizei_)a->tex_size, 1,
                         R_RGBA, R_UNSIGNED_BYTE, (is_n ? a->nmaps : a->layers) + bytes * l);
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
    if (g->GetError() != 0) { snprintf(g->err, sizeof g->err, "palco: textura HD, erro GL"); g->failed = 1; return 0; }
    return g->ready;
}

void rmk_stage_gl_frame(RmkStageGL* g, const RmkAsset* a, const RmkStageFrame* fr, const RmkStageFree* fv,
                        const uint8_t* face_n, const float* free_pos) {
    if (!g || !g->ready || (g->have_frame && fr->serial == g->serial)) return;
    const int nb = fr->n_bones < (uint32_t)a->n_bones ? (int)fr->n_bones : a->n_bones;
    rmk_skin(a, fr->bones, nb, 1u, g->pos);
    rmk_pack_geometry(a, g->pos, 1u, g->staging);
    uint64_t seen = 0;   /* nós em cena com matriz de verdade (não a de espera) */
    for (int k = 0; k < nb && k < 64; k++) {
        const float* m = fr->bones[k].m;
        const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                          m[2] * (m[3] * m[7] - m[4] * m[6]);
        if (fr->bones[k].t[2] < 0.5f * RMK_STAGE_UNSEEN_Z && fabsf(det) > 1e-9f) seen |= 1ull << k;
    }
    if (seen & ~g->nodes_done) {
        classify_tess(g, a, g->nodes_done | seen);
        g->nodes_done |= seen;
    }
    /* vértices livres depois dos do asset: posição, AO 1 e a normal da face (cada face tem os seus vértices) */
    g->n_free = 0;
    if (fv && face_n && free_pos && fv->n_free_verts <= RMK_STAGE_MAX_FREE) {
        uint32_t v = 0;
        for (uint32_t f = 0; f < fv->n_free_faces; f++) {
            const uint32_t nv = face_n[f] == 4 ? 4u : 3u;
            if (v + nv > fv->n_free_verts) break;
            const float* p0 = &free_pos[3 * v];
            const float* p1 = p0 + 3;
            const float* p2 = p0 + 6;
            const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            const float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (l > 1e-12f) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0.0f; n[1] = 0.0f; n[2] = -1.0f; }
            for (uint32_t k = 0; k < nv; k++, v++) {
                float* o = &g->staging[8u * (a->n_verts + v)];
                memcpy(o, &free_pos[3 * v], 12);
                o[3] = 1.0f;
                o[4] = n[0]; o[5] = n[1]; o[6] = n[2]; o[7] = 0.0f;
            }
        }
        g->n_free = v;
    }
    g->ring = (g->ring + 1) % GEO_RING;
    g->PixelStorei(R_UNPACK_ALIGNMENT, 4);
    g->PixelStorei(R_UNPACK_ROW_LENGTH, 0);
    g->ActiveTexture(R_TEXTURE0 + 8);
    g->BindTexture(R_TEXTURE_2D, g->geo[g->ring]);
    g->TexSubImage2D(R_TEXTURE_2D, 0, 0, 0, g->geo_w, g->geo_h, R_RGBA, R_FLOAT, g->staging);
    g->ofx = fr->ofx; g->ofy = fr->ofy; g->h = fr->h; g->expo = fr->expo; g->rough = fr->rough;
    memcpy(g->ldir, fr->ldir, sizeof g->ldir);
    memcpy(g->lcol, fr->lcol, sizeof g->lcol);
    memcpy(g->bk, fr->bk, sizeof g->bk);
    g->relight = fr->relight; g->detail = fr->detail; g->spec = fr->spec; g->ao = fr->ao;
    g->shadow = fr->shadow; g->pulse = fr->pulse; g->flags = fr->flags;
    g->floor_on = fr->floor_on;
    memcpy(g->floor_n, fr->floor_n, sizeof g->floor_n);
    if (fr->flags & (RMK_SFLAG_NO_RELIGHT | RMK_SFLAG_NO_LIGHTS | RMK_SFLAG_NODE_COLORS)) { g->relight = 0.0f; g->detail = 0.0f; g->spec = 0.0f; }
    if (fr->flags & RMK_SFLAG_NODE_COLORS) { g->ao = 0.0f; g->shadow = 0.0f; }
    if (fr->flags & RMK_SFLAG_NO_RELIGHT) g->ao = 0.0f;
    if (fr->flags & RMK_SFLAG_NO_SHADOW) g->shadow = 0.0f;
    g->serial = fr->serial;
    g->have_frame = 1;
    g->inst_used = 0;
    g->run_key = 0xFFFFFFFFu;
    g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
    g->BufferData(R_ARRAY_BUFFER, (GLsizeiptr_)(sizeof(SInst) * g->n_inst_cap), NULL, R_STREAM_DRAW);   /* órfão */
    g->BindBuffer(R_ARRAY_BUFFER, 0);
}

/* Instâncias da sequência, a partir dos pacotes vivos: as faces de cada primitiva, com cor/UV do slot do pacote de
 * cada canto. Devolve quantas. */
/* Camada do pacote: índice do asset ou, a partir de n_layers, uma variante da tabela; a afim devolve a camada base da
 * chave e o índice + 1 em *vv (vai nos bits 9+ do modo para o shader). -1 = sem textura. */
#define SMODE_EMISSIVE 131072u       /* bit 17 do modo: sem luz (o telão); bits 18..20: coluna da página (telão) */
static int pkt_layer(const RmkAsset* a, const RmkStage* s, int layer, unsigned* vv) {
    *vv = 0;
    if (layer < 0) return -1;
    if (RMK_STAGE_LAYER_IS_FB((unsigned)layer)) { *vv = ((unsigned)layer & 7u) << 9; return -2; }   /* telão: u_fb; coluna nos bits 18..20 */
    if (layer < (int)a->n_layers) return layer;
    const uint32_t v = (uint32_t)layer - a->n_layers;
    if (!s || v >= s->n_var || v >= 255u || s->var[v].kind != RMK_STAGE_VAR_AFFINE || s->var[v].layer >= a->n_layers) return -1;
    *vv = v + 1u;
    return (int)s->var[v].layer;
}

static uint32_t build_run(RmkStageGL* g, const RmkAsset* a, const RmkStage* s, const RmkStagePkt* pk, uint32_t n) {
    uint32_t m = 0;
    SInst* out = g->inst + g->inst_used;
    uint8_t* sg = g->seg + g->inst_used;
    const uint32_t room = g->n_inst_cap - g->inst_used;
    for (uint32_t i = 0; i < n; i++) {
        const RmkStagePkt* p = &pk[i];
        if (p->prim & RMK_SPKT_FREE) {   /* face livre: os vértices do quadro na ordem dos slots do pacote */
            const uint32_t b = p->prim & 0x7FFFu, nv = (p->flags & RMK_SPKT_QUAD) ? 4u : 3u;
            static const uint8_t tri[2][3] = {{0, 1, 2}, {1, 3, 2}};
            if (b + nv > g->n_free) continue;
            const int tex = (p->flags & RMK_SPKT_TEX) != 0;
            unsigned vv = 0;
            const int layer = tex && p->layer != 0xFFu ? pkt_layer(a, s, p->layer, &vv) : -1;
            const int semi = (p->flags & RMK_SPKT_SEMI) != 0;
            for (uint32_t k = 0; k < nv - 2u && m < room; k++) {
                SInst* o = &out[m];
                for (int c = 0; c < 3; c++) {
                    const int sl = tri[k][c];
                    o->vi[c] = (uint16_t)(a->n_verts + b + (uint32_t)sl);
                    o->uv[2 * c] = p->uv[sl][0]; o->uv[2 * c + 1] = p->uv[sl][1];
                    for (int ch = 0; ch < 3; ch++) { o->base[c][ch] = p->col[sl][ch]; o->over[c][ch] = p->tcol[sl][ch]; }
                    if (g->flags & RMK_SFLAG_NODE_COLORS) { o->base[c][0] = 255; o->base[c][1] = 255; o->base[c][2] = 40; }
                    o->base[c][3] = o->over[c][3] = 255;
                }
                const int nc = (g->flags & RMK_SFLAG_NODE_COLORS) != 0;
                o->layer = (float)(nc ? -1 : layer);
                o->twin = p->twin;
                o->mode = (float)(nc || layer == -1 ? 0u : (2u | (semi ? 256u : 0u) | (vv << 9) | (layer == -2 ? SMODE_EMISSIVE : 0u)));
                sg[m] = (uint8_t)(semi && !nc ? 1u + RMK_SPKT_BLEND(p->flags) : 0u);
                m++;
            }
            continue;
        }
        if (p->prim >= s->n_prims) continue;
        const RmkStagePrim* pr = &s->prims[p->prim];
        for (uint32_t k = 0; k < pr->n_faces && m < room; k++) {
            const uint32_t f = pr->face0 + k;
            if (f >= a->n_tris) continue;
            const RmkTri* t = &a->tris[f];
            SInst* o = &out[m];
            const int tex = (p->flags & RMK_SPKT_TEX) != 0;
            for (int c = 0; c < 3; c++) {
                const int sl = pr->corner[k][c] & 3;
                o->vi[c] = t->v[c];
                o->uv[2 * c] = p->uv[sl][0]; o->uv[2 * c + 1] = p->uv[sl][1];
                for (int ch = 0; ch < 3; ch++) { o->base[c][ch] = p->col[sl][ch]; o->over[c][ch] = p->tcol[sl][ch]; }
                o->base[c][3] = o->over[c][3] = 255;
            }
            if (g->flags & RMK_SFLAG_NODE_COLORS) {   /* depuração: a cor do nó dono do 1º vértice */
                const unsigned k = a->verts[t->v[0]].owner;
                for (int c = 0; c < 3; c++) {
                    o->base[c][0] = (uint8_t)(64u + (k * 97u) % 192u);
                    o->base[c][1] = (uint8_t)(64u + (k * 59u + 80u) % 192u);
                    o->base[c][2] = (uint8_t)(64u + (k * 31u + 160u) % 192u);
                }
                o->layer = -1.0f; o->mode = 0.0f; sg[m] = 0; m++;
                continue;
            }
            unsigned vv = 0;
            int layer = pkt_layer(a, s, p->layer != 0xFFu ? p->layer : t->layer, &vv);
            unsigned md = t->mode & 3u;
            if (!tex) md = 0u;                      /* pacote sem textura: só a cor */
            else if (md == 0u) md = 2u;
            o->layer = (float)(tex ? layer : -1);
            const int semi = (p->flags & RMK_SPKT_SEMI) != 0;
            o->mode = (float)(md | (t->mode & 4u) | (semi ? 256u : 0u) | (tex && layer >= 0 ? vv << 9 : 0u) |
                              (tex && layer == -2 ? SMODE_EMISSIVE : 0u));
            o->pad = g->tess[f];
            o->twin = p->twin;
            sg[m] = (uint8_t)((semi ? 1u + RMK_SPKT_BLEND(p->flags) : 0u) | (g->tess[f] & 128u));
            m++;
        }
    }
    return m;
}

/* Faces opacas: a cobertura da borda (alfa do fragmento) mistura com o que já está desenhado; o alfa do FBO (bit 15 do
 * PS1) não é escrito. Por dentro a cobertura é 1 e o resultado é o mesmo de sem mistura. */
static void opaque_blend(RmkStageGL* g) {
    g->Enable(R_BLEND);
    g->BlendEquation(0x8006 /* GL_FUNC_ADD */);
    g->BlendFunc(0x0302 /* GL_SRC_ALPHA */, 0x0303 /* GL_ONE_MINUS_SRC_ALPHA */);
    g->ColorMask(1, 1, 1, 0);
}

static void set_blend(RmkStageGL* g, unsigned mode) {
    g->Enable(R_BLEND);
    switch (mode) {
    case 0:   /* 0,5 B + 0,5 F */
        g->BlendEquation(0x8006 /* GL_FUNC_ADD */);
        g->BlendColor(0.5f, 0.5f, 0.5f, 0.5f);
        g->BlendFunc(0x8001 /* GL_CONSTANT_COLOR */, 0x8001);
        break;
    case 1:   /* B + F */
        g->BlendEquation(0x8006);
        g->BlendFunc(1, 1);
        break;
    case 2:   /* B - F */
        g->BlendEquation(0x800B /* GL_FUNC_REVERSE_SUBTRACT */);
        g->BlendFunc(1, 1);
        break;
    default:  /* B + F / 4 */
        g->BlendEquation(0x8006);
        g->BlendColor(0.25f, 0.25f, 0.25f, 0.25f);
        g->BlendFunc(0x8001, 1);
        break;
    }
}

/* Telão (fase 6c): a faixa 512 x 256 de um buffer de tela (VRAM x 0..511, y fb_y..fb_y+255) copiada do fbo canônico
 * para uma textura, na primeira sequência do quadro que a lê: é o que as faces com página de 16 bpp dentro da tela
 * leem (no PS1, a textura é o próprio buffer de tela: no 235, o quadro anterior inteiro). Até 4 pixels por texel. */
static int run_fb_y(const RmkStagePkt* pk, uint32_t n) {   /* buffer de tela que a sequência lê (-1 = nenhum) */
    for (uint32_t i = 0; i < n; i++)
        if ((pk[i].flags & RMK_SPKT_TEX) && RMK_STAGE_LAYER_IS_FB(pk[i].layer)) return (pk[i].layer & 8u) ? 256 : 0;
    return -1;
}

static void fb_capture(RmkStageGL* g, const RmkTarget* t, int fb_y) {
    const int S = t->scale, k = S >= 4 ? 4 : S, W = 512 * k, H = 256 * k;
    if (!g->fb_tex || g->fb_k != k) {
        if (g->fb_fbo) { g->DeleteFramebuffers(1, &g->fb_fbo); g->fb_fbo = 0; }
        if (g->fb_tex) { g->DeleteTextures(1, &g->fb_tex); g->fb_tex = 0; }
        g->GenTextures(1, &g->fb_tex);
        g->ActiveTexture(R_TEXTURE0 + 12);
        g->BindTexture(R_TEXTURE_2D, g->fb_tex);
        g->TexImage2D(R_TEXTURE_2D, 0, R_RGBA8, W, H, 0, R_RGBA, R_UNSIGNED_BYTE, NULL);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MIN_FILTER, R_LINEAR);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_MAG_FILTER, R_LINEAR);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_S, R_CLAMP_TO_EDGE);
        g->TexParameteri(R_TEXTURE_2D, R_TEXTURE_WRAP_T, R_CLAMP_TO_EDGE);
        g->GenFramebuffers(1, &g->fb_fbo);
        g->BindFramebuffer(R_FRAMEBUFFER, g->fb_fbo);
        g->FramebufferTexture2D(R_FRAMEBUFFER, R_COLOR_ATTACHMENT0, R_TEXTURE_2D, g->fb_tex, 0);
        const int ok = g->CheckFramebufferStatus(R_FRAMEBUFFER) == R_FRAMEBUFFER_COMPLETE;
        g->BindFramebuffer(R_FRAMEBUFFER, t->fbo);
        if (!ok) { g->DeleteFramebuffers(1, &g->fb_fbo); g->DeleteTextures(1, &g->fb_tex); g->fb_fbo = g->fb_tex = 0; g->fb_serial = g->serial; return; }
        g->fb_k = k;
        g->bytes += (uint64_t)W * H * 4u;
    }
    const int y0 = fb_y * S;
    g->Disable(R_SCISSOR_TEST);
    g->BindFramebuffer(R_READ_FRAMEBUFFER, t->fbo);
    g->BindFramebuffer(R_DRAW_FRAMEBUFFER, g->fb_fbo);
    g->BlitFramebuffer(0, y0, 512 * S, y0 + 256 * S, 0, 0, W, H, R_COLOR_BUFFER_BIT, R_LINEAR);
    g->BindFramebuffer(R_FRAMEBUFFER, t->fbo);
    g->fb_serial = g->serial;
    g->fb_y = fb_y;
}

void rmk_stage_gl_draw(RmkStageGL* g, const RmkAsset* a, const RmkStage* s, const RmkStagePkt* pk, uint32_t n,
                       uint32_t key, const RmkTarget* t, const RmkShadowView* sh, int n_sh) {
    if (!g || !g->ready || !g->have_frame || !n) return;
    /* telão: a cópia do buffer de tela na primeira sequência do quadro que o lê (o que a VRAM tem neste ponto da OT) */
    if (t->pass == 0 && t->scale > 0 && g->fb_serial != g->serial) {
        const int fy = run_fb_y(pk, n);
        if (fy >= 0) fb_capture(g, t, fy);
    }
    g->BindBuffer(R_ARRAY_BUFFER, g->inst_vbo);
    if (key != g->run_key) {   /* sequência nova; o espelho da tela larga repete a anterior */
        const uint32_t m = build_run(g, a, s, pk, n);
        if (!m) { g->BindBuffer(R_ARRAY_BUFFER, 0); return; }
        g->BufferSubData(R_ARRAY_BUFFER, (GLsizeiptr_)(sizeof(SInst) * g->inst_used), (GLsizeiptr_)(sizeof(SInst) * m),
                         g->inst + g->inst_used);
        g->run_key = key;
        g->run_first = g->inst_used;
        g->run_n = m;
        g->inst_used += m;
    }
    g->Disable(R_DEPTH_TEST);
    g->DepthMask(0);
    g->Disable(R_CULL_FACE);
    g->Disable(R_STENCIL_TEST);
    g->Disable(R_BLEND);
    g->Enable(R_SCISSOR_TEST);
    g->Scissor(t->scissor[0], t->scissor[1], t->scissor[2], t->scissor[3]);
    g->ColorMask(1, 1, 1, 1);
    g->UseProgram(g->prog);
    g->ActiveTexture(R_TEXTURE0 + 8);
    g->BindTexture(R_TEXTURE_2D, g->geo[g->ring]);
    g->ActiveTexture(R_TEXTURE0 + 9);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->tex);
    g->ActiveTexture(R_TEXTURE0 + 10);
    g->BindTexture(R_TEXTURE_2D_ARRAY, g->nmap);
    g->ActiveTexture(R_TEXTURE0 + 11);
    g->BindTexture(R_TEXTURE_2D, g->vtab);
    g->ActiveTexture(R_TEXTURE0 + 12);
    g->BindTexture(R_TEXTURE_2D, g->fb_tex);
    g->Uniform1i(g->u_geo, 8);
    g->Uniform1i(g->u_tex, 9);
    g->Uniform1i(g->u_nmap, 10);
    g->Uniform1i(g->u_var, 11);
    g->Uniform1i(g->u_fb, 12);
    g->Uniform1i(g->u_geo_w, g->geo_w);
    g->Uniform1f(g->u_nmap_on, (g->has_nmap && !(g->flags & RMK_SFLAG_NO_NMAP)) ? 1.0f : 0.0f);
    g->Uniform4f(g->u_ndc, t->ndc[0], t->ndc[1], t->ndc[2], t->ndc[3]);
    g->Uniform4f(g->u_proj, g->ofx, g->ofy, g->h, 0.0f);
    g->Uniform3fv(g->u_ldir, 3, &g->ldir[0][0]);
    g->Uniform3fv(g->u_lcol, 3, &g->lcol[0][0]);
    g->Uniform3f(g->u_bk, g->bk[0], g->bk[1], g->bk[2]);
    g->Uniform4f(g->u_w, g->relight, g->detail, g->spec, g->ao);
    g->Uniform4f(g->u_w2, g->shadow, g->pulse, g->expo, g->rough);
    g->Uniform4f(g->u_floor, g->floor_n[0], g->floor_n[1], g->floor_n[2], g->floor_on);
    int ns = g->shadow > 0.0f ? (n_sh < 2 ? n_sh : 2) : 0;
    for (int i = 0; i < 2; i++) {
        g->ActiveTexture(R_TEXTURE0 + 14 + i);
        g->BindTexture(R_TEXTURE_2D, i < ns ? sh[i].tex : 0u);
        g->BindSampler(14 + i, g->smp_shadow);
        g->Uniform1i(g->u_sh[i], 14 + i);
    }
    if (ns) {
        float u[6], v[6], d[6], c[8];
        for (int i = 0; i < ns; i++) {
            memcpy(&u[3 * i], sh[i].u, 12); memcpy(&v[3 * i], sh[i].v, 12); memcpy(&d[3 * i], sh[i].d, 12);
            memcpy(&c[4 * i], sh[i].c, 16);
        }
        g->Uniform3fv(g->u_su[0], ns, u);
        g->Uniform3fv(g->u_sv[0], ns, v);
        g->Uniform3fv(g->u_sd[0], ns, d);
        g->Uniform4fv(g->u_sc[0], ns, c);
    }
    g->Uniform1i(g->u_nsh, ns);
    g->Uniform1f(g->u_phong, (g->flags & RMK_SFLAG_NO_PHONG) ? 0.0f : 0.6f);
    /* grupos seguidos com a mesma mistura, na ordem; a face semi sai em dois passos (texels opacos, depois os com STP).
     * No espelho da tela larga só as laterais (o centro vem da superfície canônica na apresentação). */
    const uint8_t* sg = g->seg + g->run_first;
    int rc[2][4];
    const int nrc = rmk_target_rects(t, t->scissor, rc);
    for (int q = 0; q < nrc; q++) {
        g->Scissor(rc[q][0], rc[q][1], rc[q][2], rc[q][3]);
        uint32_t i = 0;
        while (i < g->run_n) {
            uint32_t j = i + 1;
            while (j < g->run_n && sg[j] == sg[i]) j++;
            const int grid = (sg[i] & 128u) != 0;
            const unsigned bl = sg[i] & 127u;
            g->BindVertexArray(grid ? g->vao_grid : g->vao);
            inst_attribs(g, g->run_first + i);
            for (int pass = 0; pass < (bl ? 2 : 1); pass++) {   /* semi: texels opacos, depois os com STP */
                if (pass == 0) { opaque_blend(g); g->Uniform1i(g->u_semi, bl ? 0 : -1); }
                else { set_blend(g, bl - 1u); g->ColorMask(1, 1, 1, 0); g->Uniform1i(g->u_semi, 1); }
                if (grid) g->DrawElementsInstanced(R_TRIANGLES, GRID_IDX, R_UNSIGNED_SHORT, (const void*)0, (GLsizei_)(j - i));
                else g->DrawArraysInstanced(R_TRIANGLES, 0, 3, (GLsizei_)(j - i));
            }
            inst_attribs(g, 0);
            i = j;
        }
    }
    g->Disable(R_BLEND);
    g->BlendEquation(0x8006);
    g->ColorMask(1, 1, 1, 1);
    for (int k = 0; k < 2; k++) g->BindSampler(14 + k, 0);
    g->BindVertexArray(0);
    g->BindBuffer(R_ARRAY_BUFFER, 0);
}
