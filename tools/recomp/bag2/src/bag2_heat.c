/* Remake do HEAT dentro do jogo (notes/remake-visual.md, seção "No jogo"; plano em notes/remake-plano.md).
 *
 * Hook de filtro em FUN_80029D4C, que recebe em $a0 o nó raiz de um personagem e desenha a árvore de
 * ossos (FUN_80029C6C: filho em +0x94, irmão em +0x98; +0x6C = MATRIX câmera×osso já pronta para o
 * GTE, +0x8C = MeshHeader, +0xA0 = tipo). Quando a árvore é a do HEAT (conferido pelo MeshHeader de
 * cada osso: nº de vértices em +0x94 e índice-base de normais em +0x98), o plugin pula o desenho
 * original e desenha a malha nova (núcleo portátil em src/remake/, asset mods/remake/heat.bin v4 de
 * tools/remake/export_heat.py, fora do git). Mãos (fase 2): o jogo troca o mesh inteiro da mão em alguns
 * gestos (o script da dança religa o irmão do antebraço, nó 15/18 +0x98, para o nó 16/20/21 ou 19/22/23);
 * o nó na posição 16/19 da árvore diz o gesto, e o asset tem uma mão com dedos por gesto (grupos). Dois
 * caminhos:
 *   - desenho de host (padrão, fase 1): a pose vira um pacote marcador na arena de DMA, ligado no slot
 *     da OT da face mais próxima; quando a GPU emulada chega nele, o renderizador GL chama
 *     heat_host_draw com o FBO de alta resolução (patch local psxrecomp-host-draw.patch), e o shader do
 *     núcleo faz Phong Tessellation, luz por pixel e texturas HD com mipmaps;
 *   - pacotes GT3 (fase 0, fallback e comparação, heat_mode flags bit 2): milhares de triângulos com
 *     banco de textura HD e sufixo de precisão, ligados na OT com o slot de cada face.
 * Comandos TCP (build-debug): heat_mode, heat_light, heat_stats, heat_dump. */
#include "mod_plugins.h"
#include "cpu_state.h"
#include "remake/rmk.h"

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

#define HOST_DRAW_ID 1
#define HD_FLAG_PREPARE 1u           /* marcador só para criar os recursos GL aos poucos */
#define MARKS_PER_HALF 8u            /* marcadores por quadro (1 HEAT + 3 cópias + preparo dos 2 jogadores) */

typedef RmkVert HVert;
typedef RmkTri HTri;
typedef struct { float x, y, z; } V3;

static RmkAsset A;                   /* asset v3 (imutável depois da ativação: lido também pela thread de render) */
static int s_loaded;

static uint8_t* s_amask[64];         /* máscara de alfa por camada HD (caminho por pacotes) */
static uint32_t s_arena;             /* arena dos pacotes GT3 (2 metades) */
static uint32_t s_arena_off, s_last_parity = 0xFFFFFFFFu;
static uint32_t s_marks;             /* arena dos marcadores de desenho de host (2 metades) */
static uint32_t s_mark_bytes, s_mark_off, s_mark_parity = 0xFFFFFFFFu;
static int s_mode = 1;               /* 0 original, 1 remake, 2 os dois sobrepostos, 3 os dois lado a lado,
                                        4..6 mais 1..3 cópias do remake ao lado (teste de carga),
                                        7 original + pacotes (fase 0) + desenho de host lado a lado */
static float s_shift;                /* deslocamento x do remake nos modos 3..6 (pixels do PS1) */
static float s_last_shift;           /* o do último desenho (capturas) */
static int s_copy_xor;               /* modos 4..6: flags das cópias = s_flags ^ isto (comparação A/B no mesmo quadro) */
static int s_level = 4;              /* pacotes: máximo de divisões por aresta (4 -> até 16 triângulos por face) */
static float s_alpha = 0.6f;         /* Phong Tessellation */
/* bit 0 = pacotes sem cor base sob textura (depuração); bit 1 = pacotes agrupados por banco dentro do slot da
 * OT (corta as trocas de textura, ~79 % -> ~49 % da thread de render por HEAT); bit 2 = caminho por pacotes
 * (fase 0) em vez do desenho de host; bit 3 = desenho de host sem Phong (malha original); bit 4 = sem textura;
 * bit 5 = sem mapas de normal; bit 6 = luz fixa da fase 1 em vez das luzes do palco; bit 7 = sem sombra própria. */
static int s_flags = 2;
static float s_light[7] = {1.0f, 1.2f, 1.0f, 1.0f, 0.8f, 0.5f, 0.01f};   /* exposição, principal, preenchimento,
                                                         contraluz, ambiente, rugosidade, tolerância de profundidade */
static float s_zr[2];                /* faixa de Z do último remake (comando heat_mode) */
static volatile int s_dump_request;
static char s_dump_path[260] = "heat_dump.txt";
static float s_bbox[4];              /* último retângulo do remake na tela (pixels do PS1), para capturas */
static uint32_t s_last_mask;         /* grupos (gestos) do último HEAT desenhado */
static float s_hand_xy[2][3];        /* osso de cada mão na tela (pixels do PS1, sem deslocamento) e z */
static int s_force[2] = {-1, -1};    /* heat_force: gesto forçado em cada mão (teste), -1 = o do jogo */

/* thread de render (desenho de host): escritos só lá, lidos pelos comandos */
static RmkGL* s_gl;
static volatile int s_rt_ready, s_rt_failed;
static volatile double s_rt_us;
static volatile uint32_t s_rt_draws, s_rt_prepares;
static uint32_t s_serial;

static uint32_t rd32(uint32_t a) { return psx_mod_read_word(a); }
static int16_t rd16(uint32_t a) { return (int16_t)psx_mod_read_half(a); }
static void wr32(uint32_t a, uint32_t v) { psx_mod_write_word(a, v); }
static void write_arena(uint32_t addr, const uint32_t* w, uint32_t bytes) {
    if (!psx_mod_write_gpu_dma(addr, w, bytes))
        for (uint32_t i = 0; i < bytes / 4u; i++) wr32(addr + 4u * i, w[i]);
}

/* Pior caso de vértices num quadro: corpo + o maior gesto de cada posição de mão. */
static uint32_t max_active_verts(void) {
    uint32_t n = A.n_body;
    for (uint32_t pos = 0; pos < RMK_MAX_NODES; pos++) {
        uint32_t m = 0;
        for (uint32_t g = 1; g <= A.n_groups; g++)
            if (A.groups[g].tree_pos == pos && A.groups[g].nv > m) m = A.groups[g].nv;
        n += m;
    }
    return n;
}

/* ------------------------------------------------------------------ ativação */
/* O asset é lido na ativação (antes do jogo rodar: nada de leitura de 48 MB no meio de uma música). As
 * arenas também têm de ser alocadas aqui. Os recursos GL só existem depois que o renderer sobe: o desenho
 * de host os cria aos poucos (marcadores de preparo), os bancos dos pacotes no primeiro uso. */
static void heat_activate(void) {
    if (!s_loaded) {
        s_loaded = rmk_asset_load(&A, "mods/remake/heat.bin");
        psx_mod_counter_add(s_loaded ? "bag2_heat.asset_ok" : "bag2_heat.asset_missing", 1);
        if (s_loaded) {
            const double t0 = now_us();   /* mapas de normal das camadas (fase 2): uma vez, antes do jogo rodar */
            if (rmk_asset_build_normals(&A, 16.0f)) psx_mod_counter_add("bag2_heat.us_nmaps", (uint32_t)(now_us() - t0));
            for (uint32_t l = 0; l < A.n_layers && l < 64; l++) {
                const size_t n = (size_t)A.tex_size * A.tex_size;
                if (!(s_amask[l] = (uint8_t*)malloc(n))) continue;
                const uint8_t* px = A.layers + n * 4u * l;
                for (size_t i = 0; i < n; i++) s_amask[l][i] = px[4 * i + 3] >= 128;
            }
        }
    }
    if (s_loaded && !s_marks) {
        s_mark_bytes = (uint32_t)(8u + sizeof(PSXModHostDrawHeader) + sizeof(RmkFrame) + 12u * max_active_verts() + 15u) & ~15u;
        s_marks = psx_mod_alloc_texture_packet_memory(2u * MARKS_PER_HALF * s_mark_bytes, 64u);
        if (!s_marks) psx_mod_counter_add("bag2_heat.marks_fail", 1);
    }
    if (!s_arena) {
        s_arena = psx_mod_alloc_texture_packet_memory(2u * MAX_PKTS * PKT_BYTES, 64u);
        if (!s_arena) psx_mod_counter_add("bag2_heat.arena_fail", 1);
    }
}

/* ------------------------------------------------------------------ árvore de ossos */
static int collect(uint32_t node, uint32_t* out, int n) {
    while (node && n < MAX_NODES) {
        if (rd32(node + 0xA0) == 1) out[n++] = node;
        n = collect(rd32(node + 0x94), out, n);
        node = rd32(node + 0x98);
    }
    return n;
}

/* 0 se a árvore não é a do HEAT; senão a máscara de grupos ativos (bit 0 = corpo, bit g = gesto g). A árvore
 * desenhada tem 20 ossos; nas posições das mãos (16, 19) o jogo liga o nó do gesto (16/20/21, 19/22/23),
 * reconhecido pelo MeshHeader. */
static uint32_t heat_groups(const uint32_t* nodes, int n) {
    if (!s_loaded || n < 16 || n > A.mesh_nodes) return 0;
    uint32_t mask = 1u;
    for (int i = 0; i < n; i++) {
        uint32_t mesh = rd32(nodes[i] + 0x8C);
        if (!mesh) return 0;
        const uint32_t nv = rd32(mesh + 0x94), base = rd32(mesh + 0x98);
        int hand = 0, found = 0;
        for (uint32_t g = 1; g <= A.n_groups; g++) {
            if (A.groups[g].tree_pos != (uint32_t)i) continue;
            hand = 1;
            if (nv == A.groups[g].sig_nverts && base == A.groups[g].sig_base) { mask |= 1u << g; found = 1; break; }
        }
        if (hand ? !found : (nv != A.node_nverts[i] || base != A.node_base[i])) return 0;
    }
    return mask;
}

/* quadros por gesto ativo (contadores bag2_heat.grupo<g>) */
static void count_groups(uint32_t mask) {
    static const char* names[RMK_MAX_GROUPS] = {"bag2_heat.grupo0", "bag2_heat.grupo1", "bag2_heat.grupo2",
        "bag2_heat.grupo3", "bag2_heat.grupo4", "bag2_heat.grupo5", "bag2_heat.grupo6", "bag2_heat.grupo7",
        "bag2_heat.grupo8", "bag2_heat.grupo9", "bag2_heat.grupo10", "bag2_heat.grupo11", "bag2_heat.grupo12",
        "bag2_heat.grupo13", "bag2_heat.grupo14", "bag2_heat.grupo15"};
    for (uint32_t g = 1; g <= A.n_groups; g++)
        if ((mask >> g) & 1u) psx_mod_counter_add(names[g], 1);
}

static void read_bones(const uint32_t* nodes, int n, RmkBone* b) {
    for (int i = 0; i < n; i++) {
        int16_t m[9];
        int32_t t[3];
        for (int k = 0; k < 9; k++) m[k] = rd16(nodes[i] + 0x6C + 2 * k);
        for (int k = 0; k < 3; k++) t[k] = (int32_t)rd32(nodes[i] + 0x80 + 4 * k);
        rmk_bone_from_gte(&b[i], m, t);
    }
}

/* Luzes do palco do quadro (fase 2): o GTE ilumina cada osso com a matriz de luz do nó (+0x4C, direções no
 * espaço do osso), a matriz de cor LCM (gte_ctrl 16..20, uma cor por luz, pode ser negativa) e o ambiente BK
 * (13..15). Com a MATRIX câmera×osso do mesmo nó (+0x6C) as direções vão para a câmera; todos os ossos dão a
 * mesma direção (conferido no heat_dump). Lido na entrada de FUN_80029D4C: o palco põe LCM e BK antes. */
static struct { float ldir[3][3], lcol[3][3], bk[3]; } s_stage;
static void read_stage_light(const struct CPUState* cpu, uint32_t node0) {
    int16_t L[9], M[9], lcm[9];
    int32_t bk[3];
    for (int k = 0; k < 9; k++) { L[k] = rd16(node0 + 0x4C + 2 * k); M[k] = rd16(node0 + 0x6C + 2 * k); }
    const uint32_t c16 = cpu->gte_ctrl[16], c17 = cpu->gte_ctrl[17], c18 = cpu->gte_ctrl[18], c19 = cpu->gte_ctrl[19];
    lcm[0] = (int16_t)c16; lcm[1] = (int16_t)(c16 >> 16); lcm[2] = (int16_t)c17;
    lcm[3] = (int16_t)(c17 >> 16); lcm[4] = (int16_t)c18; lcm[5] = (int16_t)(c18 >> 16);
    lcm[6] = (int16_t)c19; lcm[7] = (int16_t)(c19 >> 16); lcm[8] = (int16_t)cpu->gte_ctrl[20];
    for (int k = 0; k < 3; k++) bk[k] = (int32_t)cpu->gte_ctrl[13 + k];
    rmk_stage_light(L, M, lcm, bk, s_stage.ldir, s_stage.lcol, s_stage.bk);
}

static RmkProj gte_proj(const struct CPUState* cpu) {
    RmkProj p;
    p.ofx = (float)(int32_t)cpu->gte_ctrl[24] / 65536.0f;
    p.ofy = (float)(int32_t)cpu->gte_ctrl[25] / 65536.0f;
    p.h = (float)(cpu->gte_ctrl[26] & 0xFFFFu);
    return p;
}

/* ================================================================== desenho de host (fase 1) */
/* Liga um marcador no slot `slot` da OT do quadro. Layout: tag (1 palavra a seguir), NOP com
 * PSX_MOD_HOST_DRAW_NOP, cabeçalho, RmkFrame + posições (fora do nó: a GPU emulada não lê). */
static int emit_marker(uint32_t slot, uint16_t flags, const RmkFrame* fr, const float* pos) {
    static uint32_t buf[(8u + sizeof(PSXModHostDrawHeader) + sizeof(RmkFrame)) / 4u + 3u * 65536u];
    /* pos: vetor completo; vai compacto (corpo + gestos ativos), fr->n_verts vértices */
    if (!s_marks) return 0;
    const uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (parity != s_mark_parity) { s_mark_parity = parity; s_mark_off = 0; }
    if (s_mark_off + s_mark_bytes > MARKS_PER_HALF * s_mark_bytes) return 0;
    const uint32_t addr = s_marks + parity * MARKS_PER_HALF * s_mark_bytes + s_mark_off;
    PSXModHostDrawHeader h;
    memset(&h, 0, sizeof h);
    h.magic = PSX_MOD_HOST_DRAW_MAGIC;
    h.id = HOST_DRAW_ID;
    h.flags = flags;
    h.bytes = fr ? (uint32_t)(sizeof(RmkFrame) + 12u * fr->n_verts) : 0u;
    if (fr) {
        h.bbox[0] = (int32_t)floorf(fr->bbox[0]); h.bbox[1] = (int32_t)floorf(fr->bbox[1]);
        h.bbox[2] = (int32_t)ceilf(fr->bbox[2]); h.bbox[3] = (int32_t)ceilf(fr->bbox[3]);
    } else {
        h.bbox[0] = 0; h.bbox[1] = 0; h.bbox[2] = -1; h.bbox[3] = -1;   /* nada visível: só o callback */
    }
    const uint32_t ot = rd32(OT_PTR) - 0x180u + 4u * slot;
    buf[0] = 1u << 24 | (rd32(ot) & 0xFFFFFFu);
    buf[1] = PSX_MOD_HOST_DRAW_NOP;
    memcpy(&buf[2], &h, sizeof h);
    uint32_t bytes = 8u + (uint32_t)sizeof h;
    if (fr) {
        memcpy((uint8_t*)buf + bytes, fr, sizeof *fr);
        rmk_compact(&A, fr->groups, pos, (float*)((uint8_t*)buf + bytes + sizeof *fr));
        bytes += h.bytes;
    }
    write_arena(addr, buf, bytes);
    wr32(ot, addr & 0xFFFFFFu);
    s_mark_off += s_mark_bytes;
    return 1;
}

static void host_frame(RmkFrame* fr, const RmkProj* pr, const RmkFrameInfo* fi, float shift, uint32_t mask) {
    memset(fr, 0, sizeof *fr);
    fr->magic = RMK_FRAME_MAGIC;
    fr->groups = mask;
    fr->n_verts = rmk_active_verts(&A, mask);
    fr->serial = ++s_serial;
    fr->flags = ((s_flags & 8) ? RMK_FLAG_FLAT : 0u) | ((s_flags & 16) ? RMK_FLAG_NO_TEX : 0u) |
                ((s_flags & 32) ? RMK_FLAG_NO_NMAP : 0u) | ((s_flags & 64) ? RMK_FLAG_FIXED_LIGHT : 0u) |
                ((s_flags & 128) ? RMK_FLAG_NO_SHADOW : 0u);
    memcpy(fr->ldir, s_stage.ldir, sizeof fr->ldir);
    memcpy(fr->lcol, s_stage.lcol, sizeof fr->lcol);
    memcpy(fr->bk, s_stage.bk, sizeof fr->bk);
    fr->ofx = pr->ofx; fr->ofy = pr->ofy; fr->h = pr->h; fr->shift_x = shift;
    float zn = fi->zmin * 0.9f - 16.0f, zf = fi->zmax * 1.1f + 16.0f;
    if (zn < 16.0f) zn = 16.0f;
    if (zf < zn + 64.0f) zf = zn + 64.0f;
    fr->znear = zn; fr->zfar = zf;
    fr->alpha = s_alpha;
    fr->expo = s_light[0]; fr->key = s_light[1]; fr->fill = s_light[2]; fr->rim = s_light[3];
    fr->amb = s_light[4]; fr->rough = s_light[5]; fr->zbias = s_light[6];
    memcpy(fr->bbox, fi->bbox, sizeof fr->bbox);
    s_zr[0] = fi->zmin; s_zr[1] = fi->zmax;
}

/* Um HEAT pelo desenho de host (shift = deslocamento na tela, para comparação e carga). */
static void host_heat(const RmkProj* pr, const float* pos, uint32_t mask, float shift, int copy) {
    const double t0 = now_us();
    RmkFrameInfo fi;
    rmk_frame_info(&A, pos, mask, pr, shift, &fi);
    if (!fi.visible) return;
    int slot = fi.ot_near;
    slot = slot < OT_MIN ? OT_MIN : slot > OT_MAX ? OT_MAX : slot;
    RmkFrame fr;
    const int saved = s_flags;
    if (copy) s_flags ^= s_copy_xor;
    host_frame(&fr, pr, &fi, shift, mask);
    s_flags = saved;
    const double t1 = now_us();
    if (emit_marker((uint32_t)slot, 0, &fr, pos)) psx_mod_counter_add("bag2_heat.markers", 1);
    memcpy(s_bbox, fi.bbox, sizeof s_bbox);
    psx_mod_counter_add("bag2_heat.us_geometry", (uint32_t)(t1 - t0));
    psx_mod_counter_add("bag2_heat.us_emit", (uint32_t)(now_us() - t1));
    psx_mod_counter_add("bag2_heat.frames", 1);
}

/* Thread que tem o contexto GL (render, ou a do jogo num ponto de sincronia). */
static void heat_host_draw(const PSXModHostDrawTarget* t, const void* payload, uint32_t bytes) {
    const double t0 = now_us();
    if (!s_gl && !s_rt_failed) {
        s_gl = rmk_gl_create(t->get_proc, 0);
        if (!s_gl) s_rt_failed = 1;
    }
    if (!s_rt_ready && !s_rt_failed) {
        s_rt_prepares++;
        if (rmk_gl_prepare(s_gl, &A)) s_rt_ready = 1;
        else if (*rmk_gl_error(s_gl)) {
            fprintf(stdout, "bag2_heat: desenho de host desligado: %s\n", rmk_gl_error(s_gl));
            fflush(stdout);
            s_rt_failed = 1;
        }
    }
    if ((t->flags & HD_FLAG_PREPARE) || t->pass < 0 || !s_rt_ready) return;
    const RmkFrame* fr = (const RmkFrame*)payload;
    static float* full;              /* posições no vetor completo do asset (só a thread de render) */
    if (!full && !(full = (float*)calloc(3u * A.n_verts, sizeof(float)))) return;
    if (!fr || bytes < sizeof *fr || fr->magic != RMK_FRAME_MAGIC || (fr->groups & ~((2u << A.n_groups) - 1u)) ||
        fr->n_verts != rmk_active_verts(&A, fr->groups) || bytes < sizeof *fr + 12u * fr->n_verts) return;
    rmk_expand(&A, fr->groups, (const float*)(fr + 1), full);
    RmkTarget rt;
    rt.fbo = t->fbo;
    rt.ndc[0] = 1.0f / t->x_half;
    rt.ndc[1] = ((float)t->draw_off_x + t->shift + t->x_off) / t->x_half - 1.0f;
    rt.ndc[2] = 1.0f / t->y_half;
    rt.ndc[3] = ((float)t->draw_off_y + t->shift) / t->y_half - 1.0f;
    memcpy(rt.viewport, t->viewport, sizeof rt.viewport);
    memcpy(rt.scissor, t->scissor, sizeof rt.scissor);
    memcpy(rt.bbox, fr->bbox, sizeof rt.bbox);
    rmk_gl_draw(s_gl, &A, fr, full, &rt);
    s_rt_draws++;
    s_rt_us += now_us() - t0;
}

/* ================================================================== pacotes GT3 (fase 0, fallback) */
static uint16_t s_white_bank;
static int s_banks_state;            /* 0 ainda não, 1 prontos, -1 falhou */

/* camadas HD em 15 bits, cortadas em bancos de 1024x512 com início de linha em k_bank_rows, para que
 * qualquer janela de 256x256 alinhada a 64 caiba num banco (UV do GT3 tem 8 bits). */
static int ensure_banks(void) {
    if (s_banks_state) return s_banks_state > 0;
    if (!psx_mod_texture_banks_supported()) return 0;   /* tenta de novo no próximo quadro */
    int ok = A.tex_size == 1024;
    uint16_t* band = (uint16_t*)malloc(1024u * 512u * 2u);
    uint16_t* rows = (uint16_t*)malloc(1024u * 1024u * 2u);
    ok = ok && band && rows;
    for (uint32_t l = 0; ok && l < A.n_layers; l++) {
        const uint8_t* px = A.layers + (size_t)1024u * 1024u * 4u * l;
        for (uint32_t i = 0; i < 1024u * 1024u; i++) {
            const uint8_t* p = &px[4 * i];
            uint32_t v = (uint32_t)(p[0] >> 3) | (uint32_t)(p[1] >> 3) << 5 | (uint32_t)(p[2] >> 3) << 10;
            if (!v) v = 0x8000u;              /* preto opaco: bit STP, senão 0x0000 vira transparente */
            rows[i] = (uint16_t)(p[3] >= 128 ? v : 0u);
        }
        for (int k = 0; ok && k < BANKS_PER_LAYER; k++) {
            memset(band, 0, 1024u * 512u * 2u);
            const int r0 = k_bank_rows[k], n = 1024 - r0 < 512 ? 1024 - r0 : 512;
            memcpy(band, rows + (size_t)r0 * 1024u, (size_t)n * 1024u * 2u);
            ok = psx_mod_define_texture_bank((uint16_t)(1 + l * BANKS_PER_LAYER + k), 1024, 512, band);
        }
    }
    if (ok) {   /* banco branco para as faces só de cor (GT3 com texel branco = cor pura) */
        for (int i = 0; i < 64 * 256; i++) band[i] = 0x7FFF;
        s_white_bank = (uint16_t)(1 + A.n_layers * BANKS_PER_LAYER);
        ok = psx_mod_define_texture_bank(s_white_bank, 64, 256, band);
    }
    free(rows); free(band);
    s_banks_state = ok ? 1 : -1;
    psx_mod_counter_add(ok ? "bag2_heat.banks_ok" : "bag2_heat.banks_fail", 1);
    return ok;
}

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
static uint32_t* s_out;                    /* pacotes na ordem final, copiados de uma vez para a arena */
static int iround(float f) { return (int)(f >= 0 ? f + 0.5f : f - 0.5f); }   /* sem chamada à libm */
static uint32_t* s_order; static uint32_t* s_tmp;
static float s_tol = 0.25f;                /* desvio máximo da corda, em pixels do PS1 */
static uint32_t s_budget = 3300;           /* máximo de triângulos (filhos) por desenho do HEAT */
static float s_tol_used;                   /* tolerância efetiva do último quadro (sobe em closes) */
static float* s_dev; static uint8_t* s_luv;
/* Cobertura do overlay por filho e por nível (UV é fixo): bit 0 = algum texel opaco, bit 1 = todos.
 * Filho todo opaco dispensa a cor base; filho sem texel dispensa o overlay. */
static uint8_t* s_cover;
static const float s_hd_scale = 4.0f;      /* texels HD por texel do PS1 */

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
            uv[k][0] += bb[q] * (cx->t->uv[q][0] * s_hd_scale + s_hd_scale * 0.5f);
            uv[k][1] += bb[q] * (cx->t->uv[q][1] * s_hd_scale + s_hd_scale * 0.5f);
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
        if ((t->mode & 3) == 0 || t->layer < 0 || !s_amask[t->layer]) continue;
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
    int x0 = 0, y0 = 0, ybit = 0; uint16_t bank = s_white_bank;
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
        int uu = textured ? iround(g[i]->u) - x0 : 8, vv = textured ? iround(g[i]->v) - y0 : 8;
        uint32_t u = (uint32_t)(uu < 0 ? 0 : uu > 255 ? 255 : uu), v = (uint32_t)(vv < 0 ? 0 : vv > 255 ? 255 : vv);
        const float* c = textured ? g[i]->over : g[i]->base;
        const float s = textured ? 1.0f : sc;
        w[1 + i * 3] = rgb(c[0] * s * g[i]->lit[0], c[1] * s * g[i]->lit[1], c[2] * s * g[i]->lit[2]);
        int x = iround(g[i]->sx), y = iround(g[i]->sy);
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

static void draw_heat_packets(const struct CPUState* cpu, const uint32_t* nodes, int n, uint32_t mask) {
    const double t0 = now_us();
    RmkBone M[MAX_NODES];
    read_bones(nodes, n, M);
    const RmkProj pr = gte_proj(cpu);
    const float ofx = pr.ofx, ofy = pr.ofy, H = pr.h;
    const int Lmax = s_level < 1 ? 1 : s_level > LMAX ? LMAX : s_level;
    if (!s_P) {
        s_P = (V3*)malloc(sizeof(V3) * A.n_verts); s_N = (V3*)malloc(sizeof(V3) * A.n_verts);
        s_grid = (GridPt*)malloc(sizeof(GridPt) * A.n_tris * GRID_PTS);
        s_kids = (Child*)malloc(sizeof(Child) * A.n_tris * LMAX * LMAX);
        s_vis = (uint8_t*)malloc(A.n_tris);
        s_dev = (float*)malloc(sizeof(float) * A.n_tris);
        s_luv = (uint8_t*)malloc(A.n_tris);
        s_pk = (Pkt*)malloc(sizeof(Pkt) * MAX_PKTS);
        s_order = (uint32_t*)malloc(sizeof(uint32_t) * MAX_PKTS);
        s_tmp = (uint32_t*)malloc(sizeof(uint32_t) * MAX_PKTS);
        s_out = (uint32_t*)malloc(PKT_BYTES * MAX_PKTS);
        s_lk = norm(v3(-0.45f, -0.80f, -0.55f)); s_lf = norm(v3(0.75f, -0.15f, -0.55f)); s_lr = norm(v3(0.25f, -0.45f, 1.0f));
        compute_cover();
    }
    rmk_skin(&A, M, n, mask, (float*)s_P);
    for (uint32_t i = 0; i < A.n_verts; i++) s_N[i] = v3(0, 0, 0);
    /* visibilidade (NCLIP > 0, como FUN_80036230) e normais por vértice */
    float orient = 0;
    for (uint32_t f = 0; f < A.n_tris; f++) {
        const HTri* t = &A.tris[f];
        s_vis[f] = 0;
        if (!RMK_FACE_ON(t, mask)) continue;
        V3 a = s_P[t->v[0]], b = s_P[t->v[1]], c = s_P[t->v[2]];
        V3 fn = cross(sub(b, a), sub(c, a));
        for (int k = 0; k < 3; k++) s_N[t->v[k]] = add(s_N[t->v[k]], fn);
        if (a.z < 64 || b.z < 64 || c.z < 64) continue;
        float ax = a.x / a.z, ay = a.y / a.z, bx = b.x / b.z, by = b.y / b.z, cx = c.x / c.z, cy = c.y / c.z;
        if ((bx - ax) * (cy - ay) - (cx - ax) * (by - ay) > 0) { s_vis[f] = 1; orient += dot(fn, mul(add(add(a, b), c), -1.0f)); }
    }
    const float sgn = orient < 0 ? -1.0f : 1.0f;   /* normais da face visível apontam para a câmera */
    for (uint32_t i = 0; i < A.n_verts; i++) s_N[i] = norm(mul(s_N[i], sgn));
    /* grade de Phong por face visível, com nível adaptativo: divide só onde a curva de Phong se afasta da
     * corda mais que s_tol pixels (o desvio cai com L², daí a raiz). Arestas vizinhas com níveis diferentes
     * deixam uma junção em T com fresta menor que s_tol. */
    /* Passo A: desvio de Phong (pixels do PS1) e mínimo de divisões pelo UV de cada face visível. */
    uint32_t n_uvforced = 0, n_vis = 0, n_tex = 0, n_base = 0;
    for (uint32_t f = 0; f < A.n_tris; f++) {
        if (!s_vis[f]) continue;
        n_vis++;
        const HTri* t = &A.tris[f];
        V3 P[3], N[3];
        for (int k = 0; k < 3; k++) { P[k] = s_P[t->v[k]]; N[k] = s_N[t->v[k]]; }
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
        s_dev[f] = dev;
        s_luv[f] = 1;
        if ((t->mode & 3) != 0 && t->layer >= 0) {
            /* o UV do GT3 tem 8 bits: cada pedaço tem de caber numa janela de 256 texels HD alinhada a 64,
             * ou seja, extensão <= 192; senão o UV satura e a textura sai em cacos */
            float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
            for (int k = 0; k < 3; k++) {
                umin = fminf(umin, t->uv[k][0]); umax = fmaxf(umax, t->uv[k][0]);
                vmin = fminf(vmin, t->uv[k][1]); vmax = fmaxf(vmax, t->uv[k][1]);
            }
            int Luv = (int)ceilf(fmaxf(umax - umin, vmax - vmin) * s_hd_scale / 192.0f);
            s_luv[f] = (uint8_t)(Luv < 1 ? 1 : Luv > LMAX ? LMAX : Luv);
        }
    }
    /* Orçamento: o custo do quadro cresce com o nº de pacotes (a GPU emulada gasta ~0,2 us por palavra de
     * comando), e num close o mesmo s_tol pediria muito mais divisões. Se a soma de L² passar do orçamento,
     * a tolerância sobe (L² ~ desvio/tol) até caber. */
    float tol = s_tol;
    for (int it = 0; it < 6; it++) {
        uint32_t total = 0;
        for (uint32_t f = 0; f < A.n_tris; f++) {
            if (!s_vis[f]) continue;
            int L = (int)ceilf(sqrtf(s_dev[f] / tol));
            L = L < 1 ? 1 : L > Lmax ? Lmax : L;
            if (s_luv[f] > L) L = s_luv[f];
            total += (uint32_t)(L * L);
        }
        if (total <= s_budget) break;
        tol *= (float)total / (float)s_budget * 1.05f;
    }
    s_tol_used = tol;
    uint32_t nk = 0;
    float bb4[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    for (uint32_t f = 0; f < A.n_tris; f++) {
        if (!s_vis[f]) continue;
        const HTri* t = &A.tris[f];
        V3 P[3], N[3]; float ao[3];
        for (int k = 0; k < 3; k++) { P[k] = s_P[t->v[k]]; N[k] = s_N[t->v[k]]; ao[k] = A.verts[t->v[k]].ao / 255.0f; }
        int L = (int)ceilf(sqrtf(s_dev[f] / tol));
        L = L < 1 ? 1 : L > Lmax ? Lmax : L;
        if (s_luv[f] > L) { L = s_luv[f]; n_uvforced++; }
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
                    gp->u += bb[k] * (t->uv[k][0] * s_hd_scale + s_hd_scale * 0.5f);
                    gp->v += bb[k] * (t->uv[k][1] * s_hd_scale + s_hd_scale * 0.5f);
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
        const uint8_t cov = ((t->mode & 3) != 0 && t->layer >= 0) ? s_cover[(size_t)ch->tri * KIDS_ALL + kid_off(ch->L) + ch->kid] : 0;
        if (cov & 1) {                                        /* textura (sobre a cor base no modo 1) */
            prepare(&s_pk[np], oi, ch->tri, g, 1, (uint16_t)(1 + t->layer * BANKS_PER_LAYER)); s_order[np] = np; np++; n_tex++;
        }
        if ((t->mode & 3) == 0 || ((t->mode & 3) == 1 && !(cov & 2) && !(s_flags & 1))) {   /* cor do material: GT3 com texel branco */
            prepare(&s_pk[np], oi, ch->tri, g, 0, 0); s_order[np] = np; np++; n_base++;
        }
    }
    radix_sort(np);
    const double t15 = now_us();
    uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (parity != s_last_parity) { s_last_parity = parity; s_arena_off = 0; }
    const uint32_t half = parity * MAX_PKTS * PKT_BYTES;
    const uint32_t ot_base = rd32(OT_PTR) - 0x180u;
    /* Liga na ordem ordenada: pacotes do mesmo slot ficam seguidos, então a corrente de cada slot é montada
     * no host e a OT do jogo é lida e escrita uma vez por slot; os pacotes vão para a arena numa cópia só
     * (psx_mod_write_gpu_dma, patch local do framework), em vez de 20 stores por pacote. */
    uint32_t emitted = (MAX_PKTS * PKT_BYTES - s_arena_off) / PKT_BYTES;
    if (emitted > np) emitted = np;
    const uint32_t base_addr = s_arena + half + s_arena_off;
    for (uint32_t k = 0; k < emitted;) {
        const uint32_t slot = s_pk[s_order[k]].key >> 16, ot = ot_base + 4u * slot;
        uint32_t prev = rd32(ot) & 0xFFFFFFu;
        for (; k < emitted && (s_pk[s_order[k]].key >> 16) == slot; k++) {
            uint32_t* w = &s_out[(size_t)k * (PKT_BYTES / 4u)];
            memcpy(w, s_pk[s_order[k]].w, PKT_BYTES);
            w[0] = 9u << 24 | prev;
            prev = (base_addr + k * PKT_BYTES) & 0xFFFFFFu;
        }
        wr32(ot, prev);
    }
    if (emitted) write_arena(base_addr, s_out, emitted * PKT_BYTES);
    s_arena_off += emitted * PKT_BYTES;
    const double t2 = now_us();
    psx_mod_counter_add("bag2_heat.us_geometry", (uint32_t)(t1 - t0));
    psx_mod_counter_add("bag2_heat.us_prepare", (uint32_t)(t15 - t1));
    psx_mod_counter_add("bag2_heat.us_emit", (uint32_t)(t2 - t15));
    psx_mod_counter_add("bag2_heat.packets", emitted);
    psx_mod_counter_add("bag2_heat.children", nk);
    psx_mod_counter_add("bag2_heat.faces_visible", n_vis);
    psx_mod_counter_add("bag2_heat.faces_uvforced", n_uvforced);
    psx_mod_counter_add("bag2_heat.pk_textured", n_tex);
    psx_mod_counter_add("bag2_heat.pk_base", n_base);
    psx_mod_counter_add("bag2_heat.frames", 1);
}

/* ------------------------------------------------------------------ hook */
static void dump_tree(struct CPUState* cpu) {
    FILE* f = fopen(s_dump_path, "a");
    if (!f) return;
    uint32_t nodes[MAX_NODES];
    int n = collect(cpu->gpr[4], nodes, 0);
    fprintf(f, "=== draw_tree root=%08X ra=%08X s2=%08X s2+4=%02X heat=%u\ngte_ctrl:", cpu->gpr[4], cpu->gpr[31],
            cpu->gpr[18], psx_mod_read_byte(cpu->gpr[18] + 4), heat_groups(nodes, n));
    for (int i = 0; i < 32; i++) fprintf(f, " %d:%08X", i, cpu->gte_ctrl[i]);
    fprintf(f, "\n");
    for (int i = 0; i < n; i++) {
        uint32_t mesh = rd32(nodes[i] + 0x8C);
        fprintf(f, "node %d addr=%08X mesh=%08X nverts=%u base=%u\n  M=", i, nodes[i], mesh, rd32(mesh + 0x94), rd32(mesh + 0x98));
        for (int k = 0; k < 9; k++) fprintf(f, " %d", rd16(nodes[i] + 0x6C + k * 2));
        fprintf(f, " T= %d %d %d\n  L=", (int32_t)rd32(nodes[i] + 0x80), (int32_t)rd32(nodes[i] + 0x84), (int32_t)rd32(nodes[i] + 0x88));
        for (int k = 0; k < 9; k++) fprintf(f, " %d", rd16(nodes[i] + 0x4C + k * 2));   /* matriz de luz do nó */
        fprintf(f, "\n");
    }
    fclose(f);
}

/* Teste: faz o que o script da dança faz num gesto (liga o nó da mão do gesto como irmão do antebraço,
 * nó 15/18 +0x98), na thread do jogo, antes de desenhar. Os nós do HEAT ficam num vetor de 0xB0 bytes a
 * partir da raiz (nó 0). Só na árvore do HEAT (MeshHeader da raiz e do nó 16 conferidos). */
static void force_gesture(uint32_t root) {
    static const int fore[2] = {15, 18}, hand[2][3] = {{16, 20, 21}, {19, 22, 23}};
    const uint32_t m0 = rd32(root + 0x8C), m16 = rd32(root + 16u * 0xB0u + 0x8C);
    if (!m0 || rd32(m0 + 0x98) != A.node_base[0] || !m16 || rd32(m16 + 0x98) != A.node_base[16]) return;
    for (int s = 0; s < 2; s++) {
        if (s_force[s] < 0 || s_force[s] > 2) continue;
        wr32(root + (uint32_t)fore[s] * 0xB0u + 0x98u, root + (uint32_t)hand[s][s_force[s]] * 0xB0u);
    }
}

static int use_host_path(void) { return !(s_flags & 4) && s_marks && !s_rt_failed && psx_mod_texture_banks_supported(); }

static int heat_filter(struct CPUState* cpu, uint32_t address) {
    (void)address;
    psx_mod_counter_add("bag2_heat.draw_tree", 1);
    if (s_dump_request) { dump_tree(cpu); s_dump_request--; }
    if (s_mode == 0 || !s_loaded) return 0;
    const int host = use_host_path();
    /* Recursos GL do desenho de host: criados aos poucos, por marcadores de preparo ligados por qualquer
     * personagem (o primeiro aparece bem antes de uma partida: abertura, seleção). Até ficarem prontos, o
     * HEAT original continua. */
    if (host && !s_rt_ready) {
        if (emit_marker(OT_MAX, HD_FLAG_PREPARE, NULL, NULL)) psx_mod_counter_add("bag2_heat.prepare_marks", 1);
        return 0;
    }
    if (s_force[0] >= 0 || s_force[1] >= 0) force_gesture(cpu->gpr[4]);
    uint32_t nodes[MAX_NODES];
    int n = collect(cpu->gpr[4], nodes, 0);
    const uint32_t mask = heat_groups(nodes, n);
    if (!mask) return 0;
    count_groups(mask);
    read_stage_light(cpu, nodes[0]);
    s_last_mask = mask;
    if ((!host || s_mode == 7) && (!s_arena || !ensure_banks())) return 0;
    const float w = (s_bbox[2] > s_bbox[0] ? s_bbox[2] - s_bbox[0] : 60.0f) * 1.05f;
    if (s_mode == 7) {   /* comparação de 3: original, pacotes (fase 0) à direita, desenho de host mais à direita */
        static float* pos7;
        if (!pos7 && !(pos7 = (float*)malloc(12u * A.n_verts))) return 0;
        RmkBone M[MAX_NODES];
        read_bones(nodes, n, M);
        rmk_skin(&A, M, n, mask, pos7);
        const RmkProj pr = gte_proj(cpu);
        s_shift = w;
        draw_heat_packets(cpu, nodes, n, mask);
        s_shift = 0;
        if (host) host_heat(&pr, pos7, mask, 2.0f * w, 0);   /* por último: s_bbox fica com o retângulo deste */
        return 0;
    }
    const int copies = s_mode >= 4 ? s_mode - 3 : 0;   /* teste de carga: cópias do remake ao lado */
    s_shift = s_mode == 3 ? w : 0.0f;
    s_last_shift = s_shift;
    if (host) {
        static float* pos;
        if (!pos && !(pos = (float*)malloc(12u * A.n_verts))) return 0;
        RmkBone M[MAX_NODES];
        read_bones(nodes, n, M);
        rmk_skin(&A, M, n, mask, pos);
        const RmkProj pr = gte_proj(cpu);
        for (int k = 0; k < 2; k++) {   /* para recortar as mãos nas capturas (heat_mode) */
            const RmkBone* b = &M[k ? 19 : 16];
            const float z = b->t[2] > 32.0f ? b->t[2] : 32.0f;
            s_hand_xy[k][0] = pr.ofx + pr.h * b->t[0] / z; s_hand_xy[k][1] = pr.ofy + pr.h * b->t[1] / z;
            s_hand_xy[k][2] = z;
        }
        host_heat(&pr, pos, mask, s_shift, 0);
        float bb[4];
        memcpy(bb, s_bbox, sizeof bb);
        for (int c = 1; c <= copies; c++) host_heat(&pr, pos, mask, w * c, 1);
        if (copies) memcpy(s_bbox, bb, sizeof bb);
    } else {
        draw_heat_packets(cpu, nodes, n, mask);
        for (int c = 1; c <= copies; c++) { s_shift = w * c; draw_heat_packets(cpu, nodes, n, mask); }
    }
    s_shift = 0;
    return s_mode == 1 || s_mode >= 4;   /* 1 e 4+ pulam o original; 2 e 3 desenham os dois (comparar) */
}

static int heat_dump_cmd(const char* args, char* reply, int cap) {
    if (args && *args) snprintf(s_dump_path, sizeof s_dump_path, "%s", args);
    s_dump_request = 2;
    snprintf(reply, cap, "dump armado -> %s", s_dump_path);
    return 0;
}

static int heat_mode_cmd(const char* args, char* reply, int cap) {
    int m = -1, lv = -1, fl = -1, bud = -1, cx = -1; float al = -1, tol = -1;
    if (args) sscanf(args, "%d %d %f %f %d %d %d", &m, &lv, &al, &tol, &fl, &bud, &cx);
    if (bud > 0) s_budget = (uint32_t)bud;
    if (cx >= 0) s_copy_xor = cx;
    if (fl >= 0) s_flags = fl;
    if (tol > 0) s_tol = tol;
    if (m >= 0 && m <= 7) s_mode = m;
    if (lv >= 1 && lv <= 6) s_level = lv;
    if (al >= 0 && al <= 1) s_alpha = al;
    snprintf(reply, cap, "mode=%d level=%d alpha=%.2f tol=%.3f (efetiva %.3f) budget=%u flags=%d loaded=%d host=%d "
             "rt_ready=%d rt_failed=%d arena=%08X z=%.0f..%.0f bbox=%.1f,%.1f,%.1f,%.1f mask=%X maos=%.1f,%.1f,%.1f,%.1f,%.1f,%.1f shift=%.1f", s_mode, s_level, s_alpha, s_tol,
             s_tol_used, s_budget, s_flags, s_loaded, use_host_path(), s_rt_ready, s_rt_failed, s_arena, s_zr[0], s_zr[1],
             s_bbox[0], s_bbox[1], s_bbox[2], s_bbox[3], s_last_mask, s_hand_xy[0][0], s_hand_xy[0][1], s_hand_xy[0][2],
             s_hand_xy[1][0], s_hand_xy[1][1], s_hand_xy[1][2], s_last_shift);
    return 0;
}

static int heat_force_cmd(const char* args, char* reply, int cap) {
    int l = -1, r = -1;
    if (args) sscanf(args, "%d %d", &l, &r);
    s_force[0] = l; s_force[1] = r;
    snprintf(reply, cap, "gesto forçado: esquerda %d direita %d (-1 = o do jogo)", l, r);
    return 0;
}

static int heat_light_cmd(const char* args, char* reply, int cap) {
    float v[7] = {-1, -1, -1, -1, -1, -1, -1};
    if (args) sscanf(args, "%f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
    for (int i = 0; i < 7; i++) if (v[i] >= 0) s_light[i] = v[i];
    snprintf(reply, cap, "expo=%.2f key=%.2f fill=%.2f rim=%.2f amb=%.2f rough=%.2f zbias=%.4f | palco: l0=%.2f,%.2f,%.2f "
             "c0=%.2f,%.2f,%.2f l1=%.2f,%.2f,%.2f c1=%.2f,%.2f,%.2f l2=%.2f,%.2f,%.2f c2=%.2f,%.2f,%.2f bk=%.2f,%.2f,%.2f",
             s_light[0], s_light[1], s_light[2], s_light[3], s_light[4], s_light[5], s_light[6],
             s_stage.ldir[0][0], s_stage.ldir[0][1], s_stage.ldir[0][2], s_stage.lcol[0][0], s_stage.lcol[0][1], s_stage.lcol[0][2],
             s_stage.ldir[1][0], s_stage.ldir[1][1], s_stage.ldir[1][2], s_stage.lcol[1][0], s_stage.lcol[1][1], s_stage.lcol[1][2],
             s_stage.ldir[2][0], s_stage.ldir[2][1], s_stage.ldir[2][2], s_stage.lcol[2][0], s_stage.lcol[2][1], s_stage.lcol[2][2],
             s_stage.bk[0], s_stage.bk[1], s_stage.bk[2]);
    return 0;
}

/* Desde a última chamada: desenhos de host e tempo médio de CPU na thread de render; do renderizador
 * (patch local), desenhos com alvo, espelhos na superfície larga e marcadores sem nada visível. */
extern uint64_t gl_renderer_host_draw_stats(uint64_t* mirrors, uint64_t* skipped);
static int heat_stats_cmd(const char* args, char* reply, int cap) {
    (void)args;
    static uint32_t d0;
    static double u0;
    static uint64_t c0, m0, k0;
    const uint32_t d = s_rt_draws;
    const double u = s_rt_us;
    uint64_t m = 0, k = 0;
    const uint64_t c = gl_renderer_host_draw_stats(&m, &k);
    snprintf(reply, cap, "rt_draws=%u rt_us_per_draw=%.1f gl_calls=%llu gl_mirrors=%llu gl_skipped=%llu rt_prepares=%u "
             "rt_ready=%d rt_failed=%d err=%s", d - d0, d > d0 ? (u - u0) / (double)(d - d0) : 0.0,
             (unsigned long long)(c - c0), (unsigned long long)(m - m0), (unsigned long long)(k - k0), s_rt_prepares,
             s_rt_ready, s_rt_failed, s_gl ? rmk_gl_error(s_gl) : "");
    d0 = d; u0 = u; c0 = c; m0 = m; k0 = k;
    return 0;
}

PSX_MOD_CONSTRUCTOR(bag2_heat_register) {
    (void)psx_mod_register_activation_plugin("bag2.heat_remake", heat_activate);
    (void)psx_mod_register_function_filter_plugin("bag2.heat_remake", DRAW_TREE_FN, heat_filter);
    (void)psx_mod_register_host_draw(HOST_DRAW_ID, heat_host_draw);
    (void)psx_mod_register_command("heat_dump", heat_dump_cmd);
    (void)psx_mod_register_command("heat_mode", heat_mode_cmd);
    (void)psx_mod_register_command("heat_light", heat_light_cmd);
    (void)psx_mod_register_command("heat_force", heat_force_cmd);
    (void)psx_mod_register_command("heat_stats", heat_stats_cmd);
}
