/* Remake do HEAT dentro do jogo (prova de conceito; notes/remake-visual.md, seção "No jogo").
 *
 * Hook de filtro em FUN_80029D4C, que recebe em $a0 o nó raiz de um personagem e desenha a árvore de
 * ossos (FUN_80029C6C: filho em +0x94, irmão em +0x98; +0x6C = MATRIX câmera×osso já pronta para o
 * GTE, +0x8C = MeshHeader, +0xA0 = tipo). Quando a árvore é a do HEAT (conferido pelo MeshHeader de
 * cada osso: nº de vértices em +0x94 e índice-base de normais em +0x98), o plugin pula o desenho
 * original e emite os próprios triângulos:
 *   - malha soldada exportada por tools/remake/export_heat.py (mods/remake/heat.bin, fora do git);
 *   - cada vértice no espaço da câmera = matriz do osso dono (a mesma que o jogo põe no GTE);
 *   - Phong Tessellation por face visível (culling pelo mesmo NCLIP > 0 do jogo), luz por vértice
 *     nova (principal + preenchimento + contraluz + oclusão ambiente pré-calculada);
 *   - pacotes GT3 numa arena de DMA do runtime (docs/RETAINED-TERRAIN-EXPERIMENT.md): banco de textura
 *     HD no byte alto de C1/C2 e sufixo com x/y em float e 1/z (perspectiva correta);
 *   - ligados na tabela de ordenação (OT) do jogo com o mesmo índice (z >> 2) que as primitivas
 *     originais usam, então o HEAT continua ordenado contra o palco e o outro dançarino.
 * Comandos TCP (build-debug): heat_mode 0|1|2 (original | remake | os dois), heat_dump <arquivo>. */
#include "mod_plugins.h"
#include "cpu_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static double now_us(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }
#else
#include <time.h>
static double now_us(void) { struct timespec t; timespec_get(&t, TIME_UTC); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
#endif

#define DRAW_TREE_FN 0x80029D4Cu
#define OT_PTR 0x8006EE88u           /* DAT_8006EE88: OT do quadro (ClearOTagR(ot - 4, 0x1001)) */
#define FRAME_PARITY 0x8006EE90u     /* DAT_8006EE90: buffer duplo do quadro */
#define OT_MIN 95                    /* índices válidos de (ot - 0x180) + 4*i */
#define OT_MAX (95 + 4096)
#define MAX_NODES 32
#define PKT_BYTES 80u                /* 40 (tag + GT3) + 40 (sufixo de precisão) */
#define MAX_PKTS 40000u
#define BANKS_PER_LAYER 8
static const int k_bank_rows[BANKS_PER_LAYER] = {0, 64, 128, 192, 512, 576, 640, 704};

#pragma pack(push, 1)
typedef struct { int16_t x, y, z; uint8_t owner, ao; } HVert;
typedef struct {
    uint16_t v[3], zp[2]; uint8_t mode; int8_t layer;
    uint8_t uv[3][2]; uint8_t base[3][3]; uint8_t over[3][3];
} HTri;
#pragma pack(pop)

typedef struct { float x, y, z; } V3;

static struct {
    int loaded;
    uint32_t n_nodes, n_verts, n_tris, n_layers, hd_scale;
    uint16_t node_nverts[64]; uint32_t node_base[64];
    int mesh_nodes;                 /* nós com mesh (devem ser os primeiros, na ordem da árvore) */
    HVert* verts; HTri* tris;
    uint16_t white_bank;
} A;

static uint8_t* s_amask[64];         /* máscara de alfa 1024x1024 por camada HD (1 byte por texel) */
static uint32_t s_arena;             /* endereço guest da arena (2 metades) */
static uint32_t s_arena_off, s_last_parity = 0xFFFFFFFFu;
static int s_mode = 1;               /* 0 original, 1 remake, 2 os dois sobrepostos, 3 os dois lado a lado */
static float s_shift;                /* deslocamento x do remake no modo 3 (pixels do PS1) */
static int s_level = 4;              /* máximo de divisões por aresta (4 -> até 16 triângulos por face) */
static float s_alpha = 0.6f;         /* Phong Tessellation */
static int s_flags;                  /* bit 0 = sem cor base sob textura (depuração); bit 1 = agrupar por banco no slot */
static volatile int s_dump_request;
static char s_dump_path[260] = "heat_dump.txt";

static uint32_t rd32(uint32_t a) { return psx_mod_read_word(a); }
static int16_t rd16(uint32_t a) { return (int16_t)psx_mod_read_half(a); }
static void wr32(uint32_t a, uint32_t v) { psx_mod_write_word(a, v); }

/* ------------------------------------------------------------------ carga do pacote */
static int load_asset(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4];
    uint32_t hdr[5];
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "HTR2", 4) && fread(hdr, 4, 5, f) == 5;
    if (ok) {
        A.n_nodes = hdr[0]; A.n_verts = hdr[1]; A.n_tris = hdr[2]; A.n_layers = hdr[3]; A.hd_scale = hdr[4];
        ok = A.n_nodes <= 64 && A.n_verts < 65536;
    }
    for (uint32_t i = 0; ok && i < A.n_nodes; i++) {
        uint16_t nv, pad; uint32_t base;
        ok = fread(&nv, 2, 1, f) == 1 && fread(&pad, 2, 1, f) == 1 && fread(&base, 4, 1, f) == 1;
        A.node_nverts[i] = nv; A.node_base[i] = base;
    }
    A.mesh_nodes = 0;
    while (ok && A.mesh_nodes < (int)A.n_nodes && A.node_nverts[A.mesh_nodes]) A.mesh_nodes++;
    if (ok) {
        A.verts = (HVert*)malloc(sizeof(HVert) * A.n_verts);
        A.tris = (HTri*)malloc(sizeof(HTri) * A.n_tris);
        ok = A.verts && A.tris && fread(A.verts, sizeof(HVert), A.n_verts, f) == A.n_verts &&
             fread(A.tris, sizeof(HTri), A.n_tris, f) == A.n_tris;
    }
    /* camadas HD: 1024x1024 cada, cortadas em bancos de 1024x512 com início de linha em k_bank_rows,
     * para que qualquer janela de 256x256 alinhada a 64 caiba num banco (UV do GT3 tem 8 bits). */
    uint16_t* rows = NULL;
    uint16_t* band = (uint16_t*)malloc(1024u * 512u * 2u);
    for (uint32_t l = 0; ok && l < A.n_layers; l++) {
        uint16_t wh[2];
        ok = fread(wh, 2, 2, f) == 2 && wh[0] == 1024 && wh[1] == 1024;
        if (!ok) break;
        rows = (uint16_t*)realloc(rows, 1024u * 1024u * 2u);
        ok = rows && band && fread(rows, 2, 1024u * 1024u, f) == 1024u * 1024u;
        if (ok && l < 64 && (s_amask[l] = (uint8_t*)malloc(1024u * 1024u)) != NULL)
            for (uint32_t i = 0; i < 1024u * 1024u; i++) s_amask[l][i] = rows[i] != 0;
        for (int k = 0; ok && k < BANKS_PER_LAYER; k++) {
            memset(band, 0, 1024u * 512u * 2u);
            int r0 = k_bank_rows[k];
            int n = 1024 - r0 < 512 ? 1024 - r0 : 512;
            memcpy(band, rows + (size_t)r0 * 1024u, (size_t)n * 1024u * 2u);
            ok = psx_mod_define_texture_bank((uint16_t)(1 + l * BANKS_PER_LAYER + k), 1024, 512, band);
        }
    }
    if (ok) {   /* banco branco para as faces só de cor (GT3 com texel branco = cor pura) */
        for (int i = 0; i < 64 * 256; i++) band[i] = 0x7FFF;
        A.white_bank = (uint16_t)(1 + A.n_layers * BANKS_PER_LAYER);
        ok = psx_mod_define_texture_bank(A.white_bank, 64, 256, band);
    }
    free(rows); free(band); fclose(f);
    return ok;
}

/* A arena tem de ser alocada na ativação; os bancos de textura só existem depois que o renderer sobe
 * (a ativação roda antes da janela), então o pacote é carregado no primeiro desenho. */
static void heat_activate(void) {
    if (!s_arena) {
        s_arena = psx_mod_alloc_texture_packet_memory(2u * MAX_PKTS * PKT_BYTES, 64u);
        if (!s_arena) psx_mod_counter_add("bag2_heat.arena_fail", 1);
    }
}

static int ensure_loaded(void) {
    static int tried;
    if (A.loaded || tried) return A.loaded;
    if (!psx_mod_texture_banks_supported()) return 0;   /* tenta de novo no próximo quadro */
    tried = 1;
    A.loaded = load_asset("mods/remake/heat.bin");
    psx_mod_counter_add(A.loaded ? "bag2_heat.asset_ok" : "bag2_heat.asset_missing", 1);
    return A.loaded;
}

/* ------------------------------------------------------------------ árvore de ossos */
typedef struct { float m[9], t[3]; } NodeMat;

static int collect(uint32_t node, uint32_t* out, int n) {
    while (node && n < MAX_NODES) {
        if (rd32(node + 0xA0) == 1) out[n++] = node;
        n = collect(rd32(node + 0x94), out, n);
        node = rd32(node + 0x98);
    }
    return n;
}

static int is_heat(const uint32_t* nodes, int n) {
    /* a árvore desenhada tem só os ossos 0..19; os nós 20..23 do arquivo (mãos alternativas) ficam fora */
    if (!A.loaded || n < 16 || n > A.mesh_nodes) return 0;
    for (int i = 0; i < n; i++) {
        uint32_t mesh = rd32(nodes[i] + 0x8C);
        if (!mesh || rd32(mesh + 0x94) != A.node_nverts[i] || rd32(mesh + 0x98) != A.node_base[i]) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ emissão */
#define LMAX 6
#define GRID_PTS ((LMAX + 1) * (LMAX + 2) / 2)
#define KIDS_ALL 91                  /* soma de L² para L = 1..6 (cache de cobertura) */

typedef struct { float sx, sy, z; float u, v; float lit[3], base[3], over[3]; } GridPt;
typedef struct { uint32_t tri; uint16_t a, b, c; uint8_t kid, L; float z; } Child;
/* Pacote pronto na memória do host; ligado na OT depois de ordenado por (slot, cor/textura, banco). */
typedef struct { uint32_t w[20]; uint32_t key; } Pkt;

static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 norm(V3 a) { float l = sqrtf(dot(a, a)); return l > 1e-12f ? mul(a, 1.0f / l) : v3(0, 0, -1); }

/* posição do ponto (i, j) na grade de uma face dividida L vezes por aresta */
static int gi(int L, int i, int j) { return i * (L + 1) - i * (i - 1) / 2 + j; }
static int kid_off(int L) { return (L - 1) * L * (2 * L - 1) / 6; }   /* soma de l² para l < L */

static V3* s_P; static V3* s_N;            /* por vértice (espaço da câmera) */
static GridPt* s_grid;                     /* GRID_PTS por face */
static Child* s_kids;
static uint8_t* s_vis;
static Pkt* s_pk;
static uint32_t* s_order; static uint32_t* s_tmp;
static float s_tol = 0.12f;                /* desvio máximo da corda, em pixels do PS1 */
static float s_bbox[4];                    /* último retângulo do HEAT na tela (pixels do PS1), para capturas */
/* Cobertura do overlay por filho e por nível (UV é fixo): bit 0 = algum texel opaco, bit 1 = todos.
 * Filho todo opaco dispensa a cor base; filho sem texel dispensa o overlay. */
static uint8_t* s_cover;

static void for_each_kid(int L, void (*fn)(void*, int kid, int a, int b, int c), void* user) {
    int kid = 0;
    for (int i = 0; i < L; i++)
        for (int j = 0; j < L - i; j++) {
            fn(user, kid++, gi(L, i, j), gi(L, i + 1, j), gi(L, i, j + 1));
            if (j < L - i - 1) fn(user, kid++, gi(L, i + 1, j), gi(L, i + 1, j + 1), gi(L, i, j + 1));
        }
}

static void grid_bary(int L, int id, float bb[3]) {
    int i = 0;
    while (gi(L, i + 1, 0) <= id) i++;
    int j = id - gi(L, i, 0);
    bb[1] = (float)i / L; bb[2] = (float)j / L; bb[0] = 1.0f - bb[1] - bb[2];
}

typedef struct { const HTri* t; const uint8_t* am; int L; uint8_t* out; } CoverCtx;
static void cover_kid(void* user, int kid, int a, int b, int c) {
    CoverCtx* cx = (CoverCtx*)user;
    const int ids[3] = {a, b, c};
    float uv[3][2];
    for (int k = 0; k < 3; k++) {
        float bb[3]; grid_bary(cx->L, ids[k], bb);
        uv[k][0] = uv[k][1] = 0;
        for (int q = 0; q < 3; q++) {
            uv[k][0] += bb[q] * (cx->t->uv[q][0] * (float)A.hd_scale + A.hd_scale * 0.5f);
            uv[k][1] += bb[q] * (cx->t->uv[q][1] * (float)A.hd_scale + A.hd_scale * 0.5f);
        }
    }
    int any = 0, all = 1;
    const int S = 6;
    for (int i = 0; i <= S; i++)
        for (int j = 0; j <= S - i; j++) {
            float w1 = (float)i / S, w2 = (float)j / S, w0 = 1.0f - w1 - w2;
            int x = (int)(w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0]);
            int y = (int)(w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1]);
            x = x < 0 ? 0 : x > 1023 ? 1023 : x; y = y < 0 ? 0 : y > 1023 ? 1023 : y;
            if (cx->am[y * 1024 + x]) any = 1; else all = 0;
        }
    cx->out[kid] = (uint8_t)(any | all << 1);
}

static void compute_cover(void) {
    s_cover = (uint8_t*)calloc((size_t)A.n_tris * KIDS_ALL, 1);
    for (uint32_t f = 0; f < A.n_tris; f++) {
        const HTri* t = &A.tris[f];
        if (t->mode == 0 || t->layer < 0 || !s_amask[t->layer]) continue;
        for (int L = 1; L <= LMAX; L++) {
            CoverCtx cx = {t, s_amask[t->layer], L, s_cover + (size_t)f * KIDS_ALL + kid_off(L)};
            for_each_kid(L, cover_kid, &cx);
        }
    }
}

/* luz por vértice no espaço da câmera do GTE (x direita, y para baixo, z para dentro da tela):
 * principal quente de cima/esquerda/frente, preenchimento frio da direita, contraluz por trás */
static V3 s_lk, s_lf, s_lr;
static void shade(V3 n, V3 p, float ao, float out[3]) {
    static const float key[3] = {1.05f, 0.97f, 0.88f}, fill[3] = {0.30f, 0.36f, 0.50f};
    V3 vw = norm(mul(p, -1.0f));
    float dk = (dot(n, s_lk) + 0.25f) / 1.25f; if (dk < 0) dk = 0;
    float df = (dot(n, s_lf) + 0.5f) / 1.5f; if (df < 0) df = 0;
    float nv = dot(n, vw); if (nv < 0) nv = 0;
    float r1 = 1.0f - nv, rim = r1 * r1 * r1 * fmaxf(dot(n, s_lr) * 0.5f + 0.5f, 0.0f);
    float sh = fmaxf(dot(n, norm(add(s_lk, vw))), 0.0f);
    sh *= sh; sh *= sh; sh *= sh;                 /* ^8 */
    float spec = sh * sh * sh * 0.18f;            /* ^24 */
    float occ = 0.55f + 0.45f * ao;
    for (int i = 0; i < 3; i++) {
        float amb = (0.30f + 0.10f * (i == 2)) * occ;
        out[i] = amb + key[i] * dk * (0.75f + 0.25f * ao) + fill[i] * df + rim * 0.45f + spec;
    }
}

static uint32_t rgb(float r, float g, float b) {
    int R = (int)(r + 0.5f), G = (int)(g + 0.5f), B = (int)(b + 0.5f);
    R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
    return (uint32_t)R | (uint32_t)G << 8 | (uint32_t)B << 16;
}

/* GT3 com banco de textura e sufixo de precisão. Ordem: a mesma do jogo. Cada filho herda o slot da OT
 * da face original ((z0 + z1) >> 3 dos dois primeiros vértices da primitiva) e, dentro do slot, a ordem
 * de inserção do jogo (faces exportadas nessa ordem). O modelo tem camadas coplanares (decalques sobre a
 * calça e as mangas) que só ficam certas nessa ordem; ordenar pelo Z de cada triângulo embaralha as
 * camadas. Dentro de uma face: overlays ligados antes da cor base (o último ligado sai primeiro, então a
 * base é desenhada e a textura vai por cima, como o pacote G3+GT3 do jogo). Faces seguidas costumam usar
 * o mesmo banco, o que mantém os lotes do GL grandes. */
static void prepare(Pkt* pk, int oi, uint32_t face, const GridPt* g[3], int textured, uint16_t layer_bank0) {
    int x0 = 0, y0 = 0, ybit = 0; uint16_t bank = A.white_bank;
    if (textured) {
        float umin = fminf(fminf(g[0]->u, g[1]->u), g[2]->u), vmin = fminf(fminf(g[0]->v, g[1]->v), g[2]->v);
        x0 = ((int)umin / 64) * 64; if (x0 > 768) x0 = 768; if (x0 < 0) x0 = 0;
        y0 = ((int)vmin / 64) * 64; if (y0 > 960) y0 = 960; if (y0 < 0) y0 = 0;
        int o = (y0 % 512) < 256 ? y0 : y0 - 256;
        ybit = o != y0;
        int k = 0; while (k < BANKS_PER_LAYER - 1 && k_bank_rows[k] != o) k++;
        bank = (uint16_t)(layer_bank0 + k);
    }
    uint32_t* w = pk->w;
    const float sc = 128.0f / 255.0f;
    for (int i = 0; i < 3; i++) {
        int uu = textured ? (int)lrintf(g[i]->u) - x0 : 8, vv = textured ? (int)lrintf(g[i]->v) - y0 : 8;
        uint32_t u = (uint32_t)(uu < 0 ? 0 : uu > 255 ? 255 : uu), v = (uint32_t)(vv < 0 ? 0 : vv > 255 ? 255 : vv);
        const float* c = textured ? g[i]->over : g[i]->base;
        const float s = textured ? 1.0f : sc;
        w[1 + i * 3] = rgb(c[0] * s * g[i]->lit[0], c[1] * s * g[i]->lit[1], c[2] * s * g[i]->lit[2]);
        int x = (int)lrintf(g[i]->sx), y = (int)lrintf(g[i]->sy);
        w[2 + i * 3] = (uint32_t)(uint16_t)(int16_t)x | (uint32_t)(uint16_t)(int16_t)y << 16;
        w[3 + i * 3] = v << 8 | u;
    }
    w[1] |= 0x34u << 24;
    w[4] |= (uint32_t)(bank & 0xFF) << 24;
    w[7] |= (uint32_t)(bank >> 8) << 24;
    w[6] |= ((uint32_t)(x0 / 64) | (uint32_t)ybit << 4 | 2u << 7) << 16;   /* tpage: 15 bits, opaco */
    w[10] = 0x48545031u;
    float fl[9] = {1.0f / g[0]->z, 1.0f / g[1]->z, 1.0f / g[2]->z,
                   g[0]->sx, g[0]->sy, g[1]->sx, g[1]->sy, g[2]->sx, g[2]->sy};
    memcpy(&w[11], fl, sizeof fl);
    if (s_flags & 2)   /* dentro do slot: overlays agrupados por banco, depois as bases (menos trocas de textura) */
        pk->key = (uint32_t)oi << 16 | (uint32_t)(!textured) << 15 | bank;
    else               /* dentro do slot: ordem de inserção do jogo */
        pk->key = (uint32_t)oi << 16 | (face & 0x7FFFu) << 1 | (uint32_t)(!textured);
}

static void radix_sort(uint32_t n) {   /* s_order por s_pk[].key, duas passadas de 16 bits */
    static uint32_t cnt[65536];
    uint32_t* src = s_order; uint32_t* dst = s_tmp;
    for (int pass = 0; pass < 2; pass++) {
        const int sh = pass * 16;
        memset(cnt, 0, sizeof cnt);
        for (uint32_t i = 0; i < n; i++) cnt[(s_pk[src[i]].key >> sh) & 0xFFFF]++;
        uint32_t sum = 0;
        for (int i = 0; i < 65536; i++) { uint32_t c = cnt[i]; cnt[i] = sum; sum += c; }
        for (uint32_t i = 0; i < n; i++) dst[cnt[(s_pk[src[i]].key >> sh) & 0xFFFF]++] = src[i];
        uint32_t* t = src; src = dst; dst = t;
    }
}

typedef struct { uint32_t f; const GridPt* g; int L; uint32_t* nk; } KidCtx;
static void add_kid(void* user, int kid, int a, int b, int c) {
    KidCtx* cx = (KidCtx*)user;
    Child* ch = &s_kids[(*cx->nk)++];
    ch->tri = cx->f; ch->a = (uint16_t)a; ch->b = (uint16_t)b; ch->c = (uint16_t)c;
    ch->kid = (uint8_t)kid; ch->L = (uint8_t)cx->L;
    ch->z = (cx->g[a].z + cx->g[b].z + cx->g[c].z) / 3.0f;
}

static void draw_heat(struct CPUState* cpu, const uint32_t* nodes, int n) {
    const double t0 = now_us();
    NodeMat M[MAX_NODES];
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 9; k++) M[i].m[k] = rd16(nodes[i] + 0x6C + 2 * k) / 4096.0f;
        for (int k = 0; k < 3; k++) M[i].t[k] = (float)(int32_t)rd32(nodes[i] + 0x80 + 4 * k);
    }
    const float ofx = (float)(int32_t)cpu->gte_ctrl[24] / 65536.0f, ofy = (float)(int32_t)cpu->gte_ctrl[25] / 65536.0f;
    const float H = (float)(cpu->gte_ctrl[26] & 0xFFFFu);
    const int Lmax = s_level < 1 ? 1 : s_level > LMAX ? LMAX : s_level;
    if (!s_P) {
        s_P = (V3*)malloc(sizeof(V3) * A.n_verts); s_N = (V3*)malloc(sizeof(V3) * A.n_verts);
        s_grid = (GridPt*)malloc(sizeof(GridPt) * A.n_tris * GRID_PTS);
        s_kids = (Child*)malloc(sizeof(Child) * A.n_tris * LMAX * LMAX);
        s_vis = (uint8_t*)malloc(A.n_tris);
        s_pk = (Pkt*)malloc(sizeof(Pkt) * MAX_PKTS);
        s_order = (uint32_t*)malloc(sizeof(uint32_t) * MAX_PKTS);
        s_tmp = (uint32_t*)malloc(sizeof(uint32_t) * MAX_PKTS);
        s_lk = norm(v3(-0.45f, -0.80f, -0.55f)); s_lf = norm(v3(0.75f, -0.15f, -0.55f)); s_lr = norm(v3(0.25f, -0.45f, 1.0f));
        compute_cover();
    }
    for (uint32_t i = 0; i < A.n_verts; i++) {
        const HVert* hv = &A.verts[i];
        const NodeMat* m = &M[hv->owner < n ? hv->owner : 0];
        float x = hv->x, y = hv->y, z = hv->z;
        s_P[i] = v3(m->m[0] * x + m->m[1] * y + m->m[2] * z + m->t[0], m->m[3] * x + m->m[4] * y + m->m[5] * z + m->t[1],
                    m->m[6] * x + m->m[7] * y + m->m[8] * z + m->t[2]);
        s_N[i] = v3(0, 0, 0);
    }
    /* visibilidade (NCLIP > 0, como FUN_80036230) e normais por vértice */
    float orient = 0;
    for (uint32_t f = 0; f < A.n_tris; f++) {
        const HTri* t = &A.tris[f];
        V3 a = s_P[t->v[0]], b = s_P[t->v[1]], c = s_P[t->v[2]];
        V3 fn = cross(sub(b, a), sub(c, a));
        for (int k = 0; k < 3; k++) s_N[t->v[k]] = add(s_N[t->v[k]], fn);
        s_vis[f] = 0;
        if (a.z < 64 || b.z < 64 || c.z < 64) continue;
        float ax = a.x / a.z, ay = a.y / a.z, bx = b.x / b.z, by = b.y / b.z, cx = c.x / c.z, cy = c.y / c.z;
        if ((bx - ax) * (cy - ay) - (cx - ax) * (by - ay) > 0) { s_vis[f] = 1; orient += dot(fn, mul(add(add(a, b), c), -1.0f)); }
    }
    const float sgn = orient < 0 ? -1.0f : 1.0f;   /* normais da face visível apontam para a câmera */
    for (uint32_t i = 0; i < A.n_verts; i++) s_N[i] = norm(mul(s_N[i], sgn));
    /* grade de Phong por face visível, com nível adaptativo: divide só onde a curva de Phong se afasta da
     * corda mais que s_tol pixels (o desvio cai com L², daí a raiz). Arestas vizinhas com níveis diferentes
     * deixam uma junção em T com fresta menor que s_tol. */
    uint32_t nk = 0;
    float bb4[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    for (uint32_t f = 0; f < A.n_tris; f++) {
        if (!s_vis[f]) continue;
        const HTri* t = &A.tris[f];
        V3 P[3], N[3]; float ao[3];
        for (int k = 0; k < 3; k++) { P[k] = s_P[t->v[k]]; N[k] = s_N[t->v[k]]; ao[k] = A.verts[t->v[k]].ao / 255.0f; }
        float dev = 0;
        for (int e = 0; e < 4; e++) {   /* meios das 3 arestas e o centro */
            V3 m = e < 3 ? mul(add(P[e], P[(e + 1) % 3]), 0.5f) : mul(add(add(P[0], P[1]), P[2]), 1.0f / 3.0f);
            V3 ph = v3(0, 0, 0);
            const int cnt = e < 3 ? 2 : 3;
            for (int k = 0; k < cnt; k++) {
                const int vi = e < 3 ? (e + k) % 3 : k;
                ph = add(ph, sub(m, mul(N[vi], dot(sub(m, P[vi]), N[vi]))));
            }
            V3 d = mul(sub(mul(ph, 1.0f / cnt), m), s_alpha);
            float px = sqrtf(dot(d, d)) * H / (m.z > 64 ? m.z : 64);
            if (px > dev) dev = px;
        }
        int L = (int)ceilf(sqrtf(dev / s_tol));
        L = L < 1 ? 1 : L > Lmax ? Lmax : L;
        if (t->mode != 0 && t->layer >= 0) {
            /* o UV do GT3 tem 8 bits: cada pedaço tem de caber numa janela de 256 texels HD alinhada a 64,
             * ou seja, extensão <= 192; senão o UV satura e a textura sai em cacos */
            float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
            for (int k = 0; k < 3; k++) {
                umin = fminf(umin, t->uv[k][0]); umax = fmaxf(umax, t->uv[k][0]);
                vmin = fminf(vmin, t->uv[k][1]); vmax = fmaxf(vmax, t->uv[k][1]);
            }
            int Luv = (int)ceilf(fmaxf(umax - umin, vmax - vmin) * (float)A.hd_scale / 192.0f);
            if (Luv > L) L = Luv > LMAX ? LMAX : Luv;
        }
        GridPt* g = &s_grid[(size_t)f * GRID_PTS];
        int idx = 0;
        for (int i = 0; i <= L; i++)
            for (int j = 0; j <= L - i; j++, idx++) {
                float b1 = (float)i / L, b2 = (float)j / L, b0 = 1.0f - b1 - b2, bb[3] = {b0, b1, b2};
                V3 q = add(add(mul(P[0], b0), mul(P[1], b1)), mul(P[2], b2));
                V3 ph = v3(0, 0, 0);
                for (int k = 0; k < 3; k++) ph = add(ph, mul(sub(q, mul(N[k], dot(sub(q, P[k]), N[k]))), bb[k]));
                V3 p = add(mul(q, 1.0f - s_alpha), mul(ph, s_alpha));
                V3 nn = norm(add(add(mul(N[0], b0), mul(N[1], b1)), mul(N[2], b2)));
                if (p.z < 32) p.z = 32;
                GridPt* gp = &g[idx];
                gp->z = p.z; gp->sx = ofx + H * p.x / p.z + s_shift; gp->sy = ofy + H * p.y / p.z;
                if (gp->sx < bb4[0]) bb4[0] = gp->sx; if (gp->sy < bb4[1]) bb4[1] = gp->sy;
                if (gp->sx > bb4[2]) bb4[2] = gp->sx; if (gp->sy > bb4[3]) bb4[3] = gp->sy;
                gp->u = gp->v = 0;
                float a = 0;
                for (int q2 = 0; q2 < 3; q2++) gp->base[q2] = gp->over[q2] = 0;
                for (int k = 0; k < 3; k++) {
                    gp->u += bb[k] * (t->uv[k][0] * (float)A.hd_scale + A.hd_scale * 0.5f);
                    gp->v += bb[k] * (t->uv[k][1] * (float)A.hd_scale + A.hd_scale * 0.5f);
                    a += bb[k] * ao[k];
                    for (int q2 = 0; q2 < 3; q2++) { gp->base[q2] += bb[k] * t->base[k][q2]; gp->over[q2] += bb[k] * t->over[k][q2]; }
                }
                shade(nn, p, a, gp->lit);
            }
        KidCtx kc = {f, g, L, &nk};
        for_each_kid(L, add_kid, &kc);
    }
    memcpy(s_bbox, bb4, sizeof bb4);
    const double t1 = now_us();
    uint32_t np = 0;
    for (uint32_t k = 0; k < nk && np + 2 <= MAX_PKTS; k++) {
        const Child* ch = &s_kids[k];
        const HTri* t = &A.tris[ch->tri];
        const GridPt* gg = &s_grid[(size_t)ch->tri * GRID_PTS];
        const GridPt* g[3] = {&gg[ch->a], &gg[ch->b], &gg[ch->c]};
        int oi = ((int)s_P[t->zp[0]].z + (int)s_P[t->zp[1]].z) >> 3;   /* como FUN_80036230 */
        oi = oi < OT_MIN ? OT_MIN : oi > OT_MAX ? OT_MAX : oi;
        const uint8_t cov = (t->mode != 0 && t->layer >= 0) ? s_cover[(size_t)ch->tri * KIDS_ALL + kid_off(ch->L) + ch->kid] : 0;
        if (cov & 1) {                                        /* textura (sobre a cor base no modo 1) */
            prepare(&s_pk[np], oi, ch->tri, g, 1, (uint16_t)(1 + t->layer * BANKS_PER_LAYER)); s_order[np] = np; np++;
        }
        if (t->mode == 0 || (t->mode == 1 && !(cov & 2) && !(s_flags & 1))) {   /* cor do material: GT3 com texel branco */
            prepare(&s_pk[np], oi, ch->tri, g, 0, 0); s_order[np] = np; np++;
        }
    }
    radix_sort(np);
    const double t15 = now_us();
    uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (parity != s_last_parity) { s_last_parity = parity; s_arena_off = 0; }
    const uint32_t half = parity * MAX_PKTS * PKT_BYTES;
    const uint32_t ot_base = rd32(OT_PTR) - 0x180u;
    uint32_t emitted = 0;
    for (uint32_t k = 0; k < np && s_arena_off + PKT_BYTES <= MAX_PKTS * PKT_BYTES; k++) {
        Pkt* pk = &s_pk[s_order[k]];
        const uint32_t ot = ot_base + 4u * (pk->key >> 16), addr = s_arena + half + s_arena_off;
        pk->w[0] = 9u << 24 | (rd32(ot) & 0xFFFFFFu);
        for (int i = 0; i < 20; i++) wr32(addr + 4u * i, pk->w[i]);
        wr32(ot, addr & 0xFFFFFFu);
        s_arena_off += PKT_BYTES; emitted++;
    }
    const double t2 = now_us();
    psx_mod_counter_add("bag2_heat.us_geometry", (uint32_t)(t1 - t0));
    psx_mod_counter_add("bag2_heat.us_prepare", (uint32_t)(t15 - t1));
    psx_mod_counter_add("bag2_heat.us_emit", (uint32_t)(t2 - t15));
    psx_mod_counter_add("bag2_heat.packets", emitted);
    psx_mod_counter_add("bag2_heat.children", nk);
    psx_mod_counter_add("bag2_heat.frames", 1);
}

/* ------------------------------------------------------------------ hook */
static void dump_tree(struct CPUState* cpu) {
    FILE* f = fopen(s_dump_path, "a");
    if (!f) return;
    uint32_t nodes[MAX_NODES];
    int n = collect(cpu->gpr[4], nodes, 0);
    fprintf(f, "=== draw_tree root=%08X ra=%08X s2=%08X s2+4=%02X heat=%d\ngte_ctrl:", cpu->gpr[4], cpu->gpr[31],
            cpu->gpr[18], psx_mod_read_byte(cpu->gpr[18] + 4), is_heat(nodes, n));
    for (int i = 0; i < 32; i++) fprintf(f, " %d:%08X", i, cpu->gte_ctrl[i]);
    fprintf(f, "\n");
    for (int i = 0; i < n; i++) {
        uint32_t mesh = rd32(nodes[i] + 0x8C);
        fprintf(f, "node %d addr=%08X mesh=%08X nverts=%u base=%u\n  M=", i, nodes[i], mesh, rd32(mesh + 0x94), rd32(mesh + 0x98));
        for (int k = 0; k < 9; k++) fprintf(f, " %d", rd16(nodes[i] + 0x6C + k * 2));
        fprintf(f, " T= %d %d %d\n", (int32_t)rd32(nodes[i] + 0x80), (int32_t)rd32(nodes[i] + 0x84), (int32_t)rd32(nodes[i] + 0x88));
    }
    fclose(f);
}

static int heat_filter(struct CPUState* cpu, uint32_t address) {
    (void)address;
    psx_mod_counter_add("bag2_heat.draw_tree", 1);
    if (s_dump_request) { dump_tree(cpu); s_dump_request--; }
    if (s_mode == 0 || !s_arena || !ensure_loaded()) return 0;
    uint32_t nodes[MAX_NODES];
    int n = collect(cpu->gpr[4], nodes, 0);
    if (!is_heat(nodes, n)) return 0;
    s_shift = 0;
    if (s_mode == 3) s_shift = (s_bbox[2] > s_bbox[0] ? s_bbox[2] - s_bbox[0] : 60.0f) * 1.05f;
    draw_heat(cpu, nodes, n);
    return s_mode == 1;   /* 1: pula o desenho original; 2 e 3: desenha os dois (conferir alinhamento/comparar) */
}

static int heat_dump_cmd(const char* args, char* reply, int cap) {
    if (args && *args) snprintf(s_dump_path, sizeof s_dump_path, "%s", args);
    s_dump_request = 2;
    snprintf(reply, cap, "dump armado -> %s", s_dump_path);
    return 0;
}

static int heat_mode_cmd(const char* args, char* reply, int cap) {
    int m = -1, lv = -1, fl = -1; float al = -1, tol = -1;
    if (args) sscanf(args, "%d %d %f %f %d", &m, &lv, &al, &tol, &fl);
    if (fl >= 0) s_flags = fl;
    if (tol > 0) s_tol = tol;
    if (m >= 0 && m <= 3) s_mode = m;
    if (lv >= 1 && lv <= 6) s_level = lv;
    if (al >= 0 && al <= 1) s_alpha = al;
    snprintf(reply, cap, "mode=%d level=%d alpha=%.2f tol=%.3f loaded=%d arena=%08X bbox=%.1f,%.1f,%.1f,%.1f", s_mode,
             s_level, s_alpha, s_tol, A.loaded, s_arena, s_bbox[0], s_bbox[1], s_bbox[2], s_bbox[3]);
    return 0;
}

PSX_MOD_CONSTRUCTOR(bag2_heat_register) {
    (void)psx_mod_register_activation_plugin("bag2.heat_remake", heat_activate);
    (void)psx_mod_register_function_filter_plugin("bag2.heat_remake", DRAW_TREE_FN, heat_filter);
    (void)psx_mod_register_command("heat_dump", heat_dump_cmd);
    (void)psx_mod_register_command("heat_mode", heat_mode_cmd);
}
