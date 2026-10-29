/* Remake do palco (fase 3, notes/remake-plano.md; técnica em notes/remake-visual.md, "Fase 3 no jogo").
 *
 * Pós-efeito no meio do quadro: a cada quadro de partida o plugin liga um marcador de desenho de host
 * (patch local psxrecomp-host-draw.patch) no slot 96 da OT, o último da cena; o HUD do jogo fica numa OT de 4
 * entradas pendurada na entrada 0 e sai depois, sem passar pelo efeito. Menus e FMV não têm marcador. O callback, na thread de render, chama o
 * núcleo portátil (src/remake/rmk_post.c): bloom multiescala e graduação de cor. Na tela larga o centro 4:3 só
 * chega à superfície larga na apresentação, então o callback no espelho monta a cena com o centro da superfície
 * canônica e as laterais da larga, e a composição escreve o centro na canônica e as laterais na larga (o bloom
 * atravessa a borda do 4:3 sem corte; rmk_post_apply_wide).
 *
 * Batida: a música é XA, mas o jogo conta o tempo em ticks (0x8005FBB4, 256 por batida; a batida inteira fica em
 * 0x8005FBD8). O ataque do som no SPU sai de +1 a +8 quadros depois da virada do tick conforme a música (medido com
 * tools/remake/stage_beat.py, stage_sync.py e stage_pulse_check.py), então a fase do pulso é a do tick atrasada
 * 4 quadros (erro máximo ~3 quadros).
 * Gancho: entrada de FUN_8002BEB4 (atualização e desenho de cada personagem, todo quadro de partida). Partida =
 * ponteiro em 0x8008A2C8 diferente de zero (zero nos menus e na seleção).
 * Comandos TCP (build-debug): stage_post, stage_stats. */
#include "mod_plugins.h"
#include "cpu_state.h"
#include "remake/rmk.h"
#include "gpu_timer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static double st_now_us(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }
#else
#include <pthread.h>
#include <time.h>
static double st_now_us(void) { struct timespec t; timespec_get(&t, TIME_UTC); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
#endif

#define CHAR_UPDATE_FN 0x8002BEB4u   /* FUN_8002BEB4: atualização/desenho de um personagem */
#define MATCH_PTR 0x8008A2C8u        /* != 0 só em partida */
#define MUSIC_TICK 0x8005FBB4u       /* ticks da música, 256 por batida */
#define FRAME_COUNTER 0x8006EE78u    /* +1 por quadro */
#define OT_PTR 0x8006EE88u
#define FRAME_PARITY 0x8006EE90u
#define POST_SLOT 96u                /* último slot da cena: a entrada 0 (slot 95) o jogo sobrescreve para pendurar
                                        a OT do HUD (4 entradas, ranks 4097..4100 do GP0) */
#define HOST_DRAW_POST 2
#define MARK_BYTES ((8u + (uint32_t)sizeof(PSXModHostDrawHeader) + (uint32_t)sizeof(RmkPostFrame) + 15u) & ~15u)
#define MARKS_PER_HALF 2u

static uint32_t s_arena, s_off, s_parity = 0xFFFFFFFFu;
static int s_on = 1;
static uint32_t s_flags;             /* RMK_POST_* (depuração) */
static float s_par[14] = {3.0f, 0.55f, 0.3f, 0.1f, 0.6f, 0.05f, 1.0f, 0.25f, 1.15f, 0.25f, 4.0f, 1.0f, 1.0f, 1.0f};
/* bloom, limiar, joelho, peso sem cor, pulso do bloom, pulso da exposição, exposição, curva S, saturação,
 * vinheta, atraso do som em quadros, cor fria das sombras (força), cor quente dos realces (força), adaptação */
static uint32_t s_last_fc = 0xFFFFFFFFu, s_serial;
static uint32_t s_tick_prev, s_tick_still;
static float s_rate = 9.4f;          /* ticks por quadro (média) */
static float s_beat = -1.0f, s_bar = -1.0f, s_env;
static uint32_t s_beat_tick;

/* thread de render */
static RmkPost* s_post;
static volatile int s_ready, s_failed;
static volatile uint32_t s_draws, s_prepares;
static volatile double s_us;
static uint32_t s_hr_fbo;
static RmkRect s_hr_rect;
static int s_hr_valid;
static volatile int s_band_w, s_band_h, s_band_changes;   /* tamanho da faixa (trocas = alvos refeitos) */
static GpuTimer s_gt;                /* tempo de GPU do pós-efeito por chamada */
static GpuTimer s_gt_geo[2];         /* ... da geometria do palco, na superfície canônica e no espelho da tela larga */
volatile int bag2_gpu_timers;        /* tempo de GPU por peça (gpu_timer.h); desligado por padrão */
static volatile int s_last_pass, s_last_scale, s_last_fbo, s_last_area[4], s_last_vp[4], s_pass_count[3];

static uint32_t rd32(uint32_t a) { return psx_mod_read_word(a); }

static void geo_activate(void);
static void stage_activate(void) {
    geo_activate();
    if (!s_arena) {
        s_arena = psx_mod_alloc_texture_packet_memory(2u * MARKS_PER_HALF * MARK_BYTES, 64u);
        if (!s_arena) psx_mod_counter_add("bag2_stage.arena_fail", 1);
    }
}

/* Reflexo dos dançarinos no chão (fase 6): força para o palco atual (0 = chão fosco ou geometria do palco desligada) e
 * a distância abaixo do chão em que o reflexo some. Chão escuro e liso: Sushi Planet (233) e discoteca (235). */
static int s_cur_container(void);
float bag2_stage_reflection(float* fade) {
    const int c = s_cur_container();
    *fade = 650.0f;
    return c == 233 ? 0.42f : c == 235 ? 0.30f : 0.0f;
}

/* Colunas do espelho da tela larga que a apresentação cobre com a superfície canônica (o desenho de host pode pular;
 * os dançarinos e o palco usam). Flag 2048 do stage_geo: desenha também o centro (comparação). */
static uint32_t s_geo_flags;
void bag2_stage_skip_x(const PSXModHostDrawTarget* t, int skip[2]) {
    const int on = t->pass == 1 && !(s_geo_flags & 2048u) && t->center_x[1] > t->center_x[0];
    skip[0] = on ? t->center_x[0] : 0;
    skip[1] = on ? t->center_x[1] : 0;
}

/* Pulso do quadro atual (0..1), para a luz do remake do HEAT acompanhar o palco. */
float bag2_stage_pulse(void) { return s_on && !(s_flags & RMK_POST_OFF) ? s_env : 0.0f; }

/* Fase da batida do quadro: o tick atrasado do tempo que o som leva para sair; sem música (tick parado por 10
 * quadros), -1. */
static void update_beat(uint32_t fc) {
    const uint32_t tick = rd32(MUSIC_TICK);
    static uint32_t fc_prev;
    const uint32_t dfc = fc - fc_prev;
    if (tick != s_tick_prev) {
        if (tick > s_tick_prev && dfc >= 1 && dfc < 8) {
            const float r = (float)(tick - s_tick_prev) / (float)(dfc + s_tick_still);
            if (r > 2.0f && r < 30.0f) s_rate += (r - s_rate) * 0.1f;
        }
        s_tick_still = 0;
    } else {
        s_tick_still += dfc;
    }
    s_tick_prev = tick;
    fc_prev = fc;
    if (!tick || s_tick_still > 10) { s_beat = s_bar = -1.0f; s_env = 0.0f; return; }
    const float t = (float)tick - s_par[10] * s_rate;   /* o som da batida sai s_par[10] quadros depois do tick */
    if (t < 0.0f) { s_beat = s_bar = -1.0f; s_env = 0.0f; return; }
    s_beat_tick = (uint32_t)t;
    s_beat = fmodf(t, 256.0f) / 256.0f;
    s_bar = fmodf(t, 1024.0f) / 1024.0f;
    s_env = rmk_post_envelope(s_beat, s_bar);
}

static void post_frame(RmkPostFrame* f) {
    memset(f, 0, sizeof *f);
    f->magic = RMK_POST_MAGIC;
    f->serial = ++s_serial;
    f->flags = s_flags | (psx_mod_widescreen_view_x_margin() > 0 ? 0x80000000u : 0u);
    f->beat = s_beat; f->bar = s_bar;
    f->bloom = s_par[0]; f->thr = s_par[1]; f->knee = s_par[2]; f->satw = s_par[3];
    f->pulse = s_par[4]; f->pulse_expo = s_par[5];
    f->expo = s_par[6]; f->contrast = s_par[7]; f->sat = s_par[8]; f->vignette = s_par[9];
    /* sombras frias (azul-violeta) e realces quentes, na força pedida */
    const float lo[3] = {0.94f, 0.96f, 1.08f}, hi[3] = {1.05f, 1.01f, 0.94f};
    f->adapt = s_par[13];
    for (int k = 0; k < 3; k++) {
        f->tint_lo[k] = 1.0f + (lo[k] - 1.0f) * s_par[11];
        f->tint_hi[k] = 1.0f + (hi[k] - 1.0f) * s_par[12];
    }
}

/* Marcador no slot POST_SLOT da OT do quadro: tag (1 palavra), NOP "HDR", cabeçalho, RmkPostFrame. */
static void emit_post(void) {
    static uint32_t buf[MARK_BYTES / 4u];
    if (!s_arena) return;
    const uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (parity != s_parity) { s_parity = parity; s_off = 0; }
    if (s_off + MARK_BYTES > MARKS_PER_HALF * MARK_BYTES) return;
    const uint32_t addr = s_arena + parity * MARKS_PER_HALF * MARK_BYTES + s_off;
    PSXModHostDrawHeader h;
    memset(&h, 0, sizeof h);
    h.magic = PSX_MOD_HOST_DRAW_MAGIC;
    h.id = HOST_DRAW_POST;
    h.bytes = (uint32_t)sizeof(RmkPostFrame);
    /* x passa das bordas do 4:3 (o renderizador chama também o espelho da tela larga); y só 1 linha: o callback
     * processa a área de desenho inteira, e o retângulo é o que o renderizador marca como sujo na VRAM (com o pacote
     * HD ativo marcar a tela toda pesa na thread de render: notes/remake-visual.md, Fase 3) */
    h.bbox[0] = -256; h.bbox[1] = 0; h.bbox[2] = 768; h.bbox[3] = 1;
    RmkPostFrame f;
    post_frame(&f);
    const uint32_t ot = rd32(OT_PTR) - 0x180u + 4u * POST_SLOT;
    buf[0] = 1u << 24 | (rd32(ot) & 0xFFFFFFu);
    buf[1] = PSX_MOD_HOST_DRAW_NOP;
    memcpy(&buf[2], &h, sizeof h);
    memcpy((uint8_t*)&buf[2] + sizeof h, &f, sizeof f);
    const uint32_t bytes = 8u + (uint32_t)sizeof h + (uint32_t)sizeof f;
    if (!psx_mod_write_gpu_dma(addr, buf, bytes))
        for (uint32_t i = 0; i < bytes / 4u; i++) psx_mod_write_word(addr + 4u * i, buf[i]);
    psx_mod_write_word(ot, addr & 0xFFFFFFu);
    s_off += MARK_BYTES;
    psx_mod_counter_add("bag2_stage.markers", 1);
}

static void stage_char_update(struct CPUState* cpu, uint32_t address) {
    (void)cpu; (void)address;
    if (!s_on || !rd32(MATCH_PTR)) return;
    const uint32_t fc = psx_mod_read_half(FRAME_COUNTER);
    if (fc == s_last_fc) return;     /* um marcador por quadro (a função roda uma vez por personagem) */
    s_last_fc = fc;
    update_beat(fc);
    emit_post();
}

/* ------------------------------------------------------------------ thread de render */
static void stage_host_draw(const PSXModHostDrawTarget* t, const void* payload, uint32_t bytes) {
    const double t0 = st_now_us();
    if (!s_post && !s_failed) {
        s_post = rmk_post_create(t->get_proc, 0);
        if (!s_post) s_failed = 1;
    }
    if (!s_ready && !s_failed) {
        s_prepares++;
        if (rmk_post_prepare(s_post)) s_ready = 1;
        else {
            fprintf(stdout, "bag2_stage: pós-efeito desligado: %s\n", rmk_post_error(s_post));
            fflush(stdout);
            s_failed = 1;
        }
        return;                       /* o primeiro marcador só compila (antes da música: partida carregando) */
    }
    const RmkPostFrame* f = (const RmkPostFrame*)payload;
    if (t->pass < 0 || !s_ready || !f || bytes < sizeof *f || f->magic != RMK_POST_MAGIC) return;
    const int wide = (f->flags & 0x80000000u) != 0;
    const int S = t->scale;
    gt_init(&s_gt, t->get_proc);
    s_last_pass = t->pass; s_last_scale = S; s_last_fbo = (int)t->fbo;
    for (int k = 0; k < 4; k++) { s_last_area[k] = t->area[k]; s_last_vp[k] = t->viewport[k]; }
    if (t->pass >= 0 && t->pass < 2) s_pass_count[t->pass]++;
    const int ax = t->area[0], ay = t->area[1], aw = t->area[2] - t->area[0] + 1, ah = t->area[3] - t->area[1] + 1;
    if (aw <= 0 || ah <= 0) return;
    if (t->pass == 0) {
        s_hr_fbo = t->fbo;
        s_hr_rect.x = ax * S; s_hr_rect.y = ay * S; s_hr_rect.w = aw * S; s_hr_rect.h = ah * S;
        s_hr_valid = 1;
        if (!wide) { gt_begin(&s_gt); rmk_post_apply(s_post, f, t->fbo, s_hr_rect, t->viewport); gt_end(&s_gt); }
    } else if (s_hr_valid) {
        /* superfície larga: x da tela = (x da VRAM + x_off) * escala; o centro ainda é o do quadro anterior */
        RmkRect c, band;
        c.x = (int)lrintf(((float)ax + t->x_off) * (float)S); c.y = ay * S; c.w = aw * S; c.h = ah * S;
        band.x = t->scissor[0]; band.w = t->scissor[2]; band.y = c.y; band.h = c.h;
        if (band.w < c.w) { band.x = 0; band.w = t->viewport[2]; }
        if (band.w != s_band_w || band.h != s_band_h) { s_band_w = band.w; s_band_h = band.h; s_band_changes++; }
        gt_begin(&s_gt);
        if (s_flags & 32u) {             /* depuração: o caminho antigo (emenda, processa e devolve), para A/B */
            rmk_post_blit(s_post, s_hr_fbo, s_hr_rect, t->fbo, c, t->fbo);
            rmk_post_apply(s_post, f, t->fbo, band, t->viewport);
            rmk_post_blit(s_post, t->fbo, c, s_hr_fbo, s_hr_rect, t->fbo);
        } else {
            rmk_post_apply_wide(s_post, f, t->fbo, band, c, s_hr_fbo, s_hr_rect, t->viewport);
        }
        gt_end(&s_gt);
        s_hr_valid = 0;
    }
    s_draws++;
    s_us += st_now_us() - t0;
}

/* ================================================================== geometria do palco (fase 6) */
/* O palco é um modelo no formato dos personagens no overlay do palco (containers 231..245, código em 0x801DB1B8): cada
 * nó com mesh é desenhado por FUN_80029588 (um pacote por primitiva no buffer do nó, a1; nó = a2 - 0x2C) e o resto
 * (fundo, chão e peças especiais de alguns palcos) pelo código do overlay. O asset de cada palco (mods/remake/palco<c>.bin
 * e .stg, tools/remake/export_stage.py) carrega numa thread quando o overlay dele aparece na RAM. Na entrada de
 * DrawOTagEnv (a OT do quadro pronta, com a do HUD) o plugin percorre a OT e troca cada sequência de pacotes dos nós
 * (as entradas vazias da OT no meio não contam) por um marcador de desenho de host com os pacotes vivos dela; o
 * renderizador chama stage_geo_draw na mesma posição da ordem de desenho. O primeiro marcador do quadro leva as
 * matrizes dos nós e a luz. Pacote que o plugin não conhece (linha, primitiva sem face, semitransparente sem textura)
 * fica com o jogo e corta a sequência. Depois de cada marcador com textura vai um GP0 E1h com a última página, para
 * o estado da GPU seguir como no original. A geometria do código do overlay entra como faces livres: os vértices que
 * ele transforma (funções da libgte e, desde a fase 6b, o RTPT/RTPS em linha) são reconhecidos nos pacotes. No espelho
 * da tela larga só as laterais são desenhadas (o centro vem da superfície canônica na apresentação). */
#define DRAW_OT_FN 0x800486ACu       /* libgpu DrawOTagEnv(ot, env), chamada pelo laço principal com a OT pronta */
#define NODE_FILL_FN 0x80029588u     /* FUN_80029588(mesh, buffer, nó + 0x2C, ...) */
#define STAGE_OVERLAY 0x801DB1B8u
#define HOST_DRAW_GEO 3
#define GEO_FLAG_PREPARE 1u
#define GEO_FLAG_FREE 2u
#define MAX_STAGES 15
#define GEO_RUNS_PER_HALF 96u
#define GEO_PKTS_PER_HALF 2400u
#define GEO_MARK_HDR (8u + (uint32_t)sizeof(PSXModHostDrawHeader) + (uint32_t)sizeof(RmkStageRun))
#define GEO_FREE_FACES 1024u
#define GEO_FRAME_BYTES ((uint32_t)(sizeof(RmkStageRun) + sizeof(RmkStageFrame) + sizeof(RmkStageFree)) + GEO_FREE_FACES + \
                         12u * RMK_STAGE_MAX_FREE)
#define GEO_HALF_BYTES (GEO_RUNS_PER_HALF * (GEO_MARK_HDR + 8u + 48u) + GEO_MARK_HDR + GEO_FRAME_BYTES + \
                        GEO_PKTS_PER_HALF * (uint32_t)sizeof(RmkStagePkt) + 64u)
/* geometria do código do overlay: funções do executável que transformam vértices para os pacotes dele */
#define F278_FN 0x8001F278u          /* FUN_8001F278(SVECTOR* v, long* sxy, long* sz, int n): n vértices */
#define RTP_FN 0x800460ACu           /* libgte RotTransPers(v0, ...) */
#define RTP3_FN 0x800460DCu          /* libgte RotTransPers3(v0, v1, v2, ...) */
#define RTP4_FN 0x8004619Cu          /* libgte RotTransPers4(v0, v1, v2, v3, ...) */

typedef struct {
    char bin[48], stg[48];
    RmkStage t;                      /* tabelas (desde a ativação) */
    RmkAsset peek;                   /* cabeçalho do asset: assinatura de cada nó (rmk_asset_peek) */
    uint16_t* wmap[RMK_MAX_NODES];   /* por nó: primitiva local que começa em cada palavra do buffer (0xFFFF = nenhuma) */
    RmkAsset* full;                  /* asset (thread de carga); NULL até state 2 */
    volatile int state;              /* 0 só tabelas, 1 carregando, 2 carregado, 3 descarregando, -1 falhou */
    double load_ms, last_use_us;
    int marked;
    uint32_t loads, evictions;
    RmkStageGL* gl;                  /* thread de render */
    volatile int rt_ready, rt_failed, rt_freed;
    volatile uint32_t rt_prepares;
    volatile uint64_t gl_bytes;
} Stg;
static Stg s_st[MAX_STAGES];
static int s_nst;
static int s_cur = -1;               /* palco cujo overlay está na RAM numa partida */
static double s_seen_us;             /* último FUN_80029588 de um nó do palco atual (o jogo está desenhando o palco) */
static uint32_t s_model;             /* modelo do palco atual na RAM (0 = ainda não achado) */
static int s_geo_on = 1;
/* s_geo_flags (declarado antes de bag2_stage_skip_x): RMK_SFLAG_* | 64: confere a posição projetada contra o pacote (estatísticas);
                                        128: só a metade direita da tela (comparação no mesmo quadro);
                                        512: sem as faces livres (só os nós do modelo); 1024: sem o reflexo no chão;
                                        2048: o espelho da tela larga desenha também o centro (comparação) */
static float s_geo_par[8] = {0.55f, 0.9f, 0.35f, 0.6f, 0.55f, 1.0f, 0.45f, 0.6f};
/* reiluminação, relevo do mapa de normal, especular, oclusão ambiente, sombra dos dançarinos, exposição, rugosidade,
 * pulso (no especular) */
/* nós desenhados desde o último DrawOTagEnv (a OT que ele manda) */
typedef struct { uint32_t node, buf, lo, hi; int k; RmkBone bone; } Seen;   /* bone: RT/TR do GTE na chamada */
static Seen s_seen[RMK_MAX_NODES];
static int s_nseen;
static int8_t s_seen_k[RMK_MAX_NODES];   /* nó do palco -> índice em s_seen (-1) */
static uint32_t s_seen_gen;
static RmkProj s_proj;
static uint32_t s_neg[8], s_neg_i; /* meshes que não são do palco (cache para não testar de novo) */
static uint32_t s_geo_arena, s_geo_off, s_geo_parity = 0xFFFFFFFFu, s_geo_serial;
static struct { uint32_t ots, runs, pkts, kept, unknown, lines, nopr, semi_flat, overflow, key_miss, seen; double us;
                double err_sum; uint32_t err_n, err_b[3]; float err_max;
                float node_err[RMK_MAX_NODES]; uint32_t node_n[RMK_MAX_NODES], dup, overlap, size_bad;
                uint32_t free_faces, free_verts, free_nomatch, free_miss, free_twin, rtp; } s_gs;   /* estatísticas (stage_geo_stats) */
/* Vértices que as funções RTP do executável e o RTPT/RTPS em linha do overlay transformaram para a OT que está sendo
 * montada (o código do overlay monta os pacotes do chão e do fundo com eles): tela (como o GTE: OF + H x / z) ->
 * posição na câmera. Um pacote de fora dos nós cujos vértices batem todos (+-1 pixel) com estes vira face livre do
 * remake. */
typedef struct { int16_t sx, sy; uint16_t next, pad; float p[3]; } FreeV;
#define FV_MAX 8192u
static FreeV s_fv[FV_MAX];
static uint32_t s_nfv;
static uint16_t s_fh[4096];          /* cabeça de cada célula (índice + 1) */
static int s_fv_rec;                 /* registrando (palco pronto e ligado) */
static uint8_t s_ff_n[GEO_FREE_FACES];   /* faces livres do quadro: nº de vértices de cada uma */
static float s_ff_pos[3 * RMK_STAGE_MAX_FREE];
static uint32_t s_nff, s_nffv;
static int s_twin;                   /* janela de textura (GP0 E2h) ligada no ponto atual da OT */
static struct { uint16_t tpage, clut; uint32_t n; } s_miss[48];   /* texturas sem camada HD (stage_geo "k") */
static uint32_t s_nmiss;

/* thread de render */
static volatile uint32_t s_geo_draws, s_geo_frames, s_geo_free_n;
static volatile double s_geo_us;

int bag2_heat_stage_lights(float ldir[3][3], float lcol[3][3], float bk[3]);
int bag2_heat_shadow_views(RmkShadowView* out, int max);
int bag2_heat_ground(float n[3], int* floor_shadow);

static int s_cur_container(void) {
    return s_geo_on && s_cur >= 0 && s_st[s_cur].state == 2 && s_st[s_cur].rt_ready && !(s_geo_flags & 1024u)
           ? (int)s_st[s_cur].t.container : 0;
}

static void geo_activate(void) {
    if (!s_nst) {
        for (int c = 231; c < 231 + MAX_STAGES; c++) {
            Stg* s = &s_st[s_nst];
            snprintf(s->stg, sizeof s->stg, "mods/remake/palco%d.stg", c);
            snprintf(s->bin, sizeof s->bin, "mods/remake/palco%d.bin", c);
            if (!rmk_stage_load(&s->t, s->stg)) continue;
            if (!rmk_asset_peek(&s->peek, s->bin) || s->peek.n_nodes != s->t.n_nodes) { rmk_stage_free(&s->t); continue; }
            int ok = 1;
            for (uint32_t k = 0; k < s->t.n_nodes && ok; k++) {
                const RmkStageNode* nd = &s->t.nodes[k];
                const uint32_t nw = nd->buf_bytes / 4u;
                ok = (s->wmap[k] = (uint16_t*)malloc(2u * (nw ? nw : 1u))) != NULL;
                if (!ok) break;
                memset(s->wmap[k], 0xFF, 2u * (nw ? nw : 1u));
                uint32_t w = 0;
                for (uint32_t p = 0; p < nd->n_prims; p++) {
                    s->wmap[k][w] = (uint16_t)p;
                    w += s->t.prims[nd->prim0 + p].words;
                }
            }
            if (!ok) { for (uint32_t k = 0; k < RMK_MAX_NODES; k++) { free(s->wmap[k]); s->wmap[k] = NULL; } rmk_stage_free(&s->t); continue; }
            s_nst++;
        }
        psx_mod_counter_add(s_nst ? "bag2_stage.geo_assets" : "bag2_stage.geo_missing", (uint32_t)(s_nst ? s_nst : 1));
        memset(s_seen_k, -1, sizeof s_seen_k);
    }
    if (s_nst && !s_geo_arena) {
        s_geo_arena = psx_mod_alloc_texture_packet_memory(2u * GEO_HALF_BYTES, 64u);
        if (!s_geo_arena) psx_mod_counter_add("bag2_stage.geo_arena_fail", 1);
    }
}

/* ------------------------------------------------------------------ carga e descarga */
static void mem_fence(void) {
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
}

static void load_stage(Stg* s) {
    const double t0 = st_now_us();
    RmkAsset* a = (RmkAsset*)calloc(1, sizeof *a);
    int ok = a && rmk_asset_load(a, s->bin) && a->n_bones >= (int)s->t.n_nodes;
    if (ok) ok = rmk_asset_build_normals(a, 16.0f);
    s->load_ms = (st_now_us() - t0) / 1000.0;
    if (!ok) {
        if (a) { rmk_asset_free(a); free(a); }
        s->state = -1;
        return;
    }
    s->full = a;
    mem_fence();
    s->state = 2;
}
#ifdef _WIN32
static DWORD WINAPI stage_load_thread(LPVOID p) { load_stage((Stg*)p); return 0; }
#else
static void* stage_load_thread(void* p) { load_stage((Stg*)p); return NULL; }
#endif

static void request_stage(int i) {
    Stg* s = &s_st[i];
    s->last_use_us = st_now_us();
    if (s->state != 0) return;
    s->state = 1;
    s->marked = 0;
    s->loads++;
    psx_mod_counter_add("bag2_stage.geo_load", 1);
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, stage_load_thread, s, 0, NULL);
    if (h) CloseHandle(h); else load_stage(s);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, stage_load_thread, s) == 0) pthread_detach(th); else load_stage(s);
#endif
}

/* Overlay do palco na RAM: os 512 primeiros bytes em 0x801DB1B8 (a mesma conta do exportador). */
static int overlay_stage(void) {
    uint8_t b[512];
    for (uint32_t i = 0; i < 512u; i += 4u) {
        const uint32_t w = rd32(STAGE_OVERLAY + i);
        memcpy(&b[i], &w, 4);
    }
    const uint32_t h = rmk_fnv1a(b, 512u);
    for (int i = 0; i < s_nst; i++)
        if (s_st[i].t.ov_hash == h) return i;
    return -1;
}

/* VBlank (thread do jogo), 4 vezes por segundo: qual palco está na RAM numa partida (FUN_800326F8 instala o overlay e
 * liga o ponteiro da partida, que não volta a zero depois do QUIT); o asset carrega quando o jogo desenha os nós dele
 * (o voo da câmera antes do "Ready" cobre a carga e o preparo) e descarrega 3 s depois que ele para de desenhar
 * (menus, outro palco). */
static void geo_vblank(void) {
    if (!s_nst) return;
    static double last;
    const double now = st_now_us();
    if (now - last < 250e3) return;
    last = now;
    const int cur = rd32(MATCH_PTR) ? overlay_stage() : -1;
    if (cur != s_cur) { s_cur = cur; s_model = 0; s_neg_i = 0; memset(s_neg, 0, sizeof s_neg); }
    /* ligado ou não (stage_geo 0, comparação), o asset fica carregado enquanto o jogo desenha o palco */
    if (s_cur >= 0 && now - s_seen_us < 1e6) request_stage(s_cur);
    for (int i = 0; i < s_nst; i++) {
        Stg* s = &s_st[i];
        if (s->state == 2 && (i != s_cur || now - s_seen_us > 3e6) && now - s->last_use_us > 3e6) {
            s->rt_freed = 0;
            mem_fence();
            s->state = 3;
            s->evictions++;
            psx_mod_counter_add("bag2_stage.geo_evict", 1);
        }
        if (s->state == 3 && (!s->marked || s->rt_freed)) {
            if (s->full) { rmk_asset_free(s->full); free(s->full); s->full = NULL; }
            s->marked = 0;
            mem_fence();
            s->state = 0;
        }
    }
}

/* ------------------------------------------------------------------ nós do palco (FUN_80029588) */
static uint32_t ram_hash(uint32_t addr, uint32_t n) {
    uint8_t b[256];
    for (uint32_t i = 0; i < n; i += 4u) {
        const uint32_t w = rd32(addr + i);
        memcpy(&b[i], &w, 4);
    }
    return rmk_fnv1a(b, n);
}

/* Índice do nó do palco cujo MeshHeader é `mesh` (o modelo carregado + mesh_off), ou -1. Acha o modelo na primeira vez
 * pelo mesh de um nó com a mesma assinatura (nº de vértices e índice-base) e pela impressão digital do começo dele. */
static int stage_node_of(const Stg* s, uint32_t mesh) {
    if (!s_model) {
        for (uint32_t i = 0; i < 8u; i++) if (s_neg[i] == mesh) return -1;
        const uint32_t nv = rd32(mesh + 0x94u), base = rd32(mesh + 0x98u);
        const RmkAsset* t = &s->peek;
        for (uint32_t k = 0; k < s->t.n_nodes && !s_model; k++) {
            if (t->node_nverts[k] != nv || t->node_base[k] != base || mesh < 0x80000000u + s->t.nodes[k].mesh_off) continue;
            const uint32_t cand = mesh - s->t.nodes[k].mesh_off;
            if ((cand & 3u) == 0 && cand >= 0x80000000u && cand + 256u < 0x80200000u && ram_hash(cand, 256u) == s->t.model_hash)
                s_model = cand;
        }
        if (!s_model) { s_neg[s_neg_i++ & 7u] = mesh; return -1; }
        psx_mod_counter_add("bag2_stage.geo_model_found", 1);
    }
    if (mesh < s_model || mesh >= s_model + s->t.model_bytes) return -1;
    const uint32_t off = mesh - s_model;
    int lo = 0, hi = (int)s->t.n_nodes - 1;
    while (lo <= hi) {   /* mesh_off cresce com o nó (ordem do arquivo) */
        const int m = (lo + hi) / 2;
        if (s->t.nodes[m].mesh_off == off) return m;
        if (s->t.nodes[m].mesh_off < off) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

static void geo_node_fill(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (s_cur < 0) return;
    Stg* s = &s_st[s_cur];
    const uint32_t mesh = cpu->gpr[4];
    if (mesh < 0x80000000u || mesh >= 0x80200000u) return;
    const int k = stage_node_of(s, mesh);
    if (k < 0) return;
    s_seen_us = st_now_us();
    s->last_use_us = s_seen_us;
    if (s->state != 2 || !s_geo_on) return;
    const uint32_t buf = cpu->gpr[5] & 0x00FFFFFFu, node = cpu->gpr[6] - 0x2Cu;
    if (rd32(node + 0xA4u) != s->t.nodes[k].buf_bytes) { psx_mod_counter_add("bag2_stage.geo_buf_mismatch", 1); return; }
    int i = s_seen_k[k];
    if (i >= 0) s_gs.dup++;          /* o mesmo mesh de novo nesta OT (outra instância): fica a última */
    if (i < 0) { if (s_nseen >= RMK_MAX_NODES) return; i = s_nseen++; s_seen_k[k] = (int8_t)i; }
    s_seen[i].node = node; s_seen[i].buf = buf; s_seen[i].k = k;
    s_seen[i].lo = buf; s_seen[i].hi = buf + s->t.nodes[k].buf_bytes;
    /* FUN_80029588 transforma os vértices com a matriz que já está no GTE (RT em gte_ctrl 0..4, TR em 5..7): para os
     * personagens é a do nó (+0x6C), mas o overlay do palco põe outra em parte dos nós */
    int16_t m[9];
    int32_t t[3];
    for (int r = 0; r < 4; r++) { m[2 * r] = (int16_t)cpu->gte_ctrl[r]; m[2 * r + 1] = (int16_t)(cpu->gte_ctrl[r] >> 16); }
    m[8] = (int16_t)cpu->gte_ctrl[4];
    for (int r = 0; r < 3; r++) t[r] = (int32_t)cpu->gte_ctrl[5 + r];
    rmk_bone_from_gte(&s_seen[i].bone, m, t);
    s_proj.ofx = (float)(int32_t)cpu->gte_ctrl[24] / 65536.0f;
    s_proj.ofy = (float)(int32_t)cpu->gte_ctrl[25] / 65536.0f;
    s_proj.h = (float)(cpu->gte_ctrl[26] & 0xFFFFu);
}

/* ------------------------------------------------------------------ vértices do código do overlay */
static uint32_t fv_cell(int x, int y) { return ((uint32_t)(x >> 1) & 63u) | (((uint32_t)(y >> 1) & 63u) << 6); }

static void fv_reset(void) {
    if (s_nfv) memset(s_fh, 0, sizeof s_fh);
    s_nfv = 0;
}

/* Um vértice pela matriz que está no GTE, como RTPS faria; guarda posição e tela. */
static void fv_add_xyz(const struct CPUState* cpu, float x, float y, float z) {
    if (s_nfv >= FV_MAX) return;
    float m[9];
    for (int r = 0; r < 4; r++) { m[2 * r] = (float)(int16_t)cpu->gte_ctrl[r]; m[2 * r + 1] = (float)(int16_t)(cpu->gte_ctrl[r] >> 16); }
    m[8] = (float)(int16_t)cpu->gte_ctrl[4];
    FreeV* f = &s_fv[s_nfv];
    for (int r = 0; r < 3; r++)
        f->p[r] = (m[3 * r] * x + m[3 * r + 1] * y + m[3 * r + 2] * z) / 4096.0f + (float)(int32_t)cpu->gte_ctrl[5 + r];
    if (f->p[2] < 16.0f) return;
    const float ofx = (float)(int32_t)cpu->gte_ctrl[24] / 65536.0f, ofy = (float)(int32_t)cpu->gte_ctrl[25] / 65536.0f;
    const float h = (float)(cpu->gte_ctrl[26] & 0xFFFFu);
    const float sx = ofx + h * f->p[0] / f->p[2], sy = ofy + h * f->p[1] / f->p[2];
    if (sx < -1024.0f || sx > 1023.0f || sy < -1024.0f || sy > 1023.0f) return;   /* o GTE satura: não bate */
    f->sx = (int16_t)lrintf(sx); f->sy = (int16_t)lrintf(sy);
    const uint32_t c = fv_cell(f->sx, f->sy);
    f->next = s_fh[c];
    s_fh[c] = (uint16_t)(s_nfv + 1u);
    s_nfv++;
    s_gs.rtp++;
}

/* ... de um SVECTOR na RAM (argumento das funções da libgte) */
static void fv_add(const struct CPUState* cpu, uint32_t v) {
    if (v < 0x80000000u || v >= 0x801FFFF8u) return;
    fv_add_xyz(cpu, (float)(int16_t)psx_mod_read_half(v), (float)(int16_t)psx_mod_read_half(v + 2u),
               (float)(int16_t)psx_mod_read_half(v + 4u));
}

static int fv_find(int x, int y, float out[3]) {
    int best = -1, bd = 3;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            for (uint16_t i = s_fh[fv_cell(x + 2 * dx, y + 2 * dy)]; i; i = s_fv[i - 1u].next) {
                const FreeV* f = &s_fv[i - 1u];
                const int d = abs(f->sx - x) + abs(f->sy - y);
                if (d < bd) { bd = d; best = (int)i - 1; }
            }
    if (best < 0) return 0;
    memcpy(out, s_fv[best].p, 12);
    return 1;
}

static void geo_rtp(struct CPUState* cpu, uint32_t address) {
    if (!s_fv_rec) return;
    if (address == F278_FN) {
        const uint32_t n = cpu->gpr[7] < 1024u ? cpu->gpr[7] : 1024u;
        for (uint32_t i = 0; i < n; i++) fv_add(cpu, cpu->gpr[4] + 8u * i);
    } else {
        const int n = address == RTP_FN ? 1 : address == RTP3_FN ? 3 : 4;
        for (int i = 0; i < n; i++) fv_add(cpu, cpu->gpr[4 + i]);
    }
}

/* RTPT/RTPS em linha no código do overlay do palco (o chão do 233 e peças de outros palcos não passam pelas funções da
 * libgte): gancho antes da instrução ([recompiler] mod_instruction_sites; o runtime confere a palavra na RAM, então
 * os endereços dos outros overlays de palco não disparam), com os vértices em V0..V2 do GTE. */
#define RTPT_WORD 0x4A280030u
#define RTPS_WORD 0x4A180001u
static const uint32_t RTPT_SITES[] = {0x801E1E0Cu, 0x801E1E64u, 0x801E1ED0u, 0x801E3440u, 0x801E3498u, 0x801DB2FCu,
                                      0x801DB354u, 0x801DB348u, 0x801DB3A0u, 0x801DB40Cu, 0x801DB2B0u, 0x801DB304u,
                                      0x801E2868u, 0x801E28C0u, 0x801DB31Cu, 0x801DB374u, 0x801E0304u, 0x801E035Cu};
static const uint32_t RTPS_SITES[] = {0x801DB3BCu, 0x801DB3DCu};
static void geo_gte_rtp(struct CPUState* cpu, uint32_t address) {
    if (!s_fv_rec) return;
    const int n = psx_mod_read_word(address) == RTPT_WORD ? 3 : 1;
    for (int i = 0; i < n; i++) {
        const uint32_t xy = cpu->gte_data[2 * i], z = cpu->gte_data[2 * i + 1];
        fv_add_xyz(cpu, (float)(int16_t)xy, (float)(int16_t)(xy >> 16), (float)(int16_t)z);
    }
}

static void note_miss(uint16_t tpage, uint16_t clut) {
    for (uint32_t i = 0; i < s_nmiss; i++)
        if (s_miss[i].tpage == (tpage & 0x19Fu) && s_miss[i].clut == clut) { s_miss[i].n++; return; }
    if (s_nmiss < 48u) { s_miss[s_nmiss].tpage = tpage & 0x19Fu; s_miss[s_nmiss].clut = clut; s_miss[s_nmiss].n = 1; s_nmiss++; }
}

/* ------------------------------------------------------------------ troca na OT (DrawOTagEnv) */
static int find_seen(uint32_t p) {
    for (int i = 0; i < s_nseen; i++)
        if (p >= s_seen[i].lo && p < s_seen[i].hi) return i;
    return -1;
}

static uint32_t geo_slot(uint32_t bytes) {
    if (!s_geo_arena) return 0;
    const uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (parity != s_geo_parity) { s_geo_parity = parity; s_geo_off = 0; }
    bytes = (bytes + 15u) & ~15u;
    if (s_geo_off + bytes > GEO_HALF_BYTES) return 0;
    const uint32_t a = s_geo_arena + parity * GEO_HALF_BYTES + s_geo_off;
    s_geo_off += bytes;
    return a;
}

/* Marcador na arena com o link para `next`; com `tpage` != 0 vai depois dele um FT3 degenerado fora da tela com essa
 * página (bit 16 ligado = há página): não desenha nada e deixa a página da GPU (modo de mistura e profundidade da
 * textura, que sprites e polígonos sem textura seguintes usam) como os pacotes trocados deixariam. Um GP0 E1h faria o
 * mesmo, mas também mexe no dithering. Devolve o endereço do marcador (0 = sem espaço). */
static uint32_t geo_marker(uint16_t flags, const int32_t bbox[4], const void* payload, uint32_t bytes, uint32_t next,
                           uint32_t tpage) {
    static uint32_t buf[(GEO_MARK_HDR + sizeof(RmkStageFrame) + GEO_PKTS_PER_HALF * sizeof(RmkStagePkt) + 16u) / 4u];
    const uint32_t total = 8u + (uint32_t)sizeof(PSXModHostDrawHeader) + bytes;
    if (total > sizeof buf) return 0;
    const uint32_t addr = geo_slot(((total + 15u) & ~15u) + (tpage ? 32u : 0u));
    if (!addr) return 0;
    PSXModHostDrawHeader h;
    memset(&h, 0, sizeof h);
    h.magic = PSX_MOD_HOST_DRAW_MAGIC;
    h.id = HOST_DRAW_GEO;
    h.flags = flags;
    h.bytes = bytes;
    memcpy(h.bbox, bbox, sizeof h.bbox);
    const uint32_t latch = addr + ((total + 15u) & ~15u);
    buf[0] = 1u << 24 | ((tpage ? latch : next) & 0xFFFFFFu);
    buf[1] = PSX_MOD_HOST_DRAW_NOP;
    memcpy(&buf[2], &h, sizeof h);
    if (bytes) memcpy((uint8_t*)buf + 8u + sizeof h, payload, bytes);
    if (!psx_mod_write_gpu_dma(addr, buf, total))
        for (uint32_t i = 0; i < total / 4u; i++) psx_mod_write_word(addr + 4u * i, buf[i]);
    if (tpage) {
        const uint32_t off = 0xFC00FC00u;   /* (-1024, -1024): fora da área de desenho */
        const uint32_t w[8] = {7u << 24 | (next & 0xFFFFFFu), 0x24808080u, off, 0u, off, (tpage & 0xFFFFu) << 16, off, 0u};
        if (!psx_mod_write_gpu_dma(latch, w, 32u))
            for (uint32_t i = 0; i < 8u; i++) psx_mod_write_word(latch + 4u * i, w[i]);
    }
    return addr;
}

/* Marcador sem nada visível logo depois do 1º pacote da OT (preparo e descarga fora da troca). */
static void geo_side_marker(uint32_t head, uint16_t flags) {
    if (!head) return;
    const uint32_t tag = rd32(0x80000000u | head);
    const int32_t bb[4] = {0, 0, -1, -1};
    const uint32_t m = geo_marker(flags, bb, NULL, 0, tag & 0xFFFFFFu, 0);
    if (m) psx_mod_write_word(0x80000000u | head, (tag & 0xFF000000u) | (m & 0xFFFFFFu));
}

/* Texturas que o jogo muda durante a partida (LoadImage/MoveImage na CLUT ou na página: luzes que piscam pela CLUT,
 * telão): a camada HD do asset é a do disco, então os pacotes delas ficam com o jogo. Uma marca por chave do palco
 * atual; zera quando o palco fica pronto (as cargas da tela de carregamento vêm antes). */
static uint8_t s_dyn[4096];
static uint32_t s_dyn_n;
static int geo_key_layer(const RmkStage* t, uint16_t tpage, uint16_t clut) {
    const uint16_t tp = tpage & 0x19Fu;
    for (uint32_t i = 0; i < t->n_keys; i++)
        if (t->keys[i].tpage == tp && t->keys[i].clut == clut) return s_dyn[i] ? -1 : t->keys[i].layer;
    return -1;
}

static void dyn_rect(int x, int y, int w, int h) {
    if (s_cur < 0 || s_st[s_cur].state != 2 || !s_st[s_cur].rt_ready || w <= 0 || h <= 0) return;
    const RmkStage* t = &s_st[s_cur].t;
    for (uint32_t i = 0; i < t->n_keys && i < 4096u; i++) {
        if (s_dyn[i]) continue;
        const int cx = (t->keys[i].clut & 0x3F) * 16, cy = (t->keys[i].clut >> 6) & 0x1FF;
        const int tx = (t->keys[i].tpage & 0xF) * 64, ty = ((t->keys[i].tpage >> 4) & 1) * 256;
        const int tw = ((t->keys[i].tpage >> 7) & 3) ? 128 : 64;
        const int clut_hit = x < cx + 16 && cx < x + w && y <= cy && cy < y + h;
        const int page_hit = x < tx + tw && tx < x + w && y < ty + 256 && ty < y + h;
        if (clut_hit || page_hit) { s_dyn[i] = 1; s_dyn_n++; }
    }
}

static void geo_load_image(struct CPUState* cpu, uint32_t address) {
    (void)address;
    const uint32_t r = cpu->gpr[4];
    if (r < 0x80000000u || r >= 0x801FFFF8u) return;
    dyn_rect((int16_t)psx_mod_read_half(r), (int16_t)psx_mod_read_half(r + 2u), (int16_t)psx_mod_read_half(r + 4u),
             (int16_t)psx_mod_read_half(r + 6u));
}

static void geo_move_image(struct CPUState* cpu, uint32_t address) {
    (void)address;
    const uint32_t r = cpu->gpr[4];
    if (r < 0x80000000u || r >= 0x801FFFF8u) return;
    dyn_rect((int)cpu->gpr[5], (int)cpu->gpr[6], (int16_t)psx_mod_read_half(r + 4u), (int16_t)psx_mod_read_half(r + 6u));
}

/* Lê um pacote de polígono do jogo para `e` (o 2º pacote de um decalque completa o 1º). 0 = não serve. */
static int read_packet(uint32_t a, uint32_t len, RmkStagePkt* e, int second, uint32_t* tpage_out, int16_t bb[4],
                       int16_t xy_out[4][2], int* nv_out) {
    const uint32_t w1 = rd32(a + 4u), op = w1 >> 24;
    if ((op & 0xE0u) != 0x20u) return 0;
    const int g = (op & 0x10u) != 0, quad = (op & 0x08u) != 0, tex = (op & 0x04u) != 0, semi = (op & 0x02u) != 0;
    const int raw = (op & 0x01u) != 0, nv = quad ? 4 : 3;
    const uint32_t need = (uint32_t)((g ? nv : 1) + nv + (tex ? nv : 0));
    if (len != need) return 0;
    if (semi && !tex) return -2;
    uint32_t q = a + 4u, col = w1;
    uint8_t c[4][3];
    uint8_t uv[4][2] = {{0}};
    uint16_t clut = 0, tpage = 0;
    for (int v = 0; v < nv; v++) {
        if (g && v) col = rd32(q);
        if (g || !v) q += 4u;
        for (int k = 0; k < 3; k++) c[v][k] = (uint8_t)(col >> (8 * k));
        const uint32_t xy = rd32(q); q += 4u;
        const int16_t x = (int16_t)(xy & 0xFFFFu), y = (int16_t)(xy >> 16);
        if (xy_out) { xy_out[v][0] = x; xy_out[v][1] = y; }
        if (x < bb[0]) bb[0] = x;
        if (y < bb[1]) bb[1] = y;
        if (x > bb[2]) bb[2] = x;
        if (y > bb[3]) bb[3] = y;
        if (tex) {
            const uint32_t t = rd32(q); q += 4u;
            uv[v][0] = (uint8_t)t; uv[v][1] = (uint8_t)(t >> 8);
            if (v == 0) clut = (uint16_t)(t >> 16);
            if (v == 1) tpage = (uint16_t)(t >> 16);
        }
    }
    if (tex) {
        for (int v = 0; v < nv; v++)
            for (int k = 0; k < 3; k++) e->tcol[v][k] = raw ? 128u : c[v][k];
        memcpy(e->uv, uv, sizeof uv);
        e->flags |= RMK_SPKT_TEX;
        if (semi) e->flags = (uint8_t)((e->flags & 0x0Fu) | RMK_SPKT_SEMI | ((tpage >> 5) & 3u) << 4);
        const int l = geo_key_layer(&s_st[s_cur].t, tpage, clut);
        if (l < 0) { s_gs.key_miss++; note_miss(tpage, clut); }
        e->layer = (uint8_t)(l < 0 ? 0xFF : l);
        *tpage_out = tpage;
        if (!second) memcpy(e->col, e->tcol, sizeof e->col);
    } else {
        memcpy(e->col, c, sizeof c);
        if (!second) memcpy(e->tcol, c, sizeof c);
    }
    if (nv_out) *nv_out = nv;
    return 1;
}

/* Pacote de fora dos nós: face livre se todos os vértices batem com os transformados desta OT, sem janela de textura
 * e com a camada HD conhecida (ou sem textura). 1 = em `e`, com os vértices no fim de s_ff_pos. */
static int try_free(uint32_t a, uint32_t len, RmkStagePkt* e, uint32_t* tp, int16_t pb[4]) {
    const uint32_t op = rd32(a + 4u) >> 24;
    if ((op & 0xE0u) != 0x20u || !s_nfv) return 0;
    if (s_twin) { s_gs.free_twin++; return 0; }
    if (s_nff >= GEO_FREE_FACES || s_nffv + 4u > RMK_STAGE_MAX_FREE) return 0;
    int16_t xy[4][2];
    int nv = 0;
    memset(e, 0, sizeof *e);
    if (read_packet(a, len, e, 0, tp, pb, xy, &nv) != 1) return 0;
    if ((e->flags & RMK_SPKT_TEX) && e->layer == 0xFFu) { s_gs.free_miss++; return 0; }
    float* out = &s_ff_pos[3u * s_nffv];
    for (int v = 0; v < nv; v++)
        if (!fv_find(xy[v][0], xy[v][1], &out[3 * v])) { s_gs.free_nomatch++; return 0; }
    e->prim = (uint16_t)(RMK_SPKT_FREE | s_nffv);
    if (nv == 4) e->flags |= RMK_SPKT_QUAD;
    s_ff_n[s_nff++] = (uint8_t)nv;
    s_nffv += (uint32_t)nv;
    s_gs.free_faces++;
    s_gs.free_verts += (uint32_t)nv;
    return 1;
}

/* GP0 E2h (janela de textura) num pacote de comandos de estado (DR_ENV, DR_TWIN): as faces livres sob janela ligada
 * ficam com o jogo (a camada HD não repete a janela). */
static void track_env(uint32_t a, uint32_t len) {
    if (len > 12u) return;
    for (uint32_t i = 1; i <= len; i++) {
        const uint32_t w = rd32(a + 4u * i);
        if ((w >> 24) == 0xE2u) s_twin = (w & 0xFFFFFu) != 0;
        else if ((w >> 24) < 0xE1u || (w >> 24) > 0xE6u) return;
    }
}

/* Confere (depuração, flag 64) a posição projetada do 1º canto de cada face contra o vértice do pacote. */
static void check_projection(const Stg* s, const RmkStageFrame* fr, uint32_t a, const RmkStagePkt* e) {
    const RmkStagePrim* pr = &s->t.prims[e->prim];
    if (!pr->n_faces || !s->full) return;
    const RmkTri* t = &s->full->tris[pr->face0];
    const uint32_t op = rd32(a + 4u) >> 24;
    const int g = (op & 0x10u) != 0, tex = (op & 0x04u) != 0;
    for (int c = 0; c < 3; c++) {
        const RmkVert* v = &s->full->verts[t->v[c]];
        const RmkBone* b = &fr->bones[v->owner];
        const float x = b->m[0] * v->x + b->m[1] * v->y + b->m[2] * v->z + b->t[0];
        const float y = b->m[3] * v->x + b->m[4] * v->y + b->m[5] * v->z + b->t[1];
        const float z = b->m[6] * v->x + b->m[7] * v->y + b->m[8] * v->z + b->t[2];
        if (z < 256.0f) continue;   /* perto da câmera o GTE satura a tela; o desenho novo recorta em 3D */
        const int sl = pr->corner[0][c] & 3;
        const uint32_t per = 1u + (g ? 1u : 0u) + (tex ? 1u : 0u);
        const uint32_t xyw = rd32(a + 4u + 4u * ((g ? 0u : 1u) + (uint32_t)sl * per + (g ? 1u : 0u)));
        const float px = (float)(int16_t)(xyw & 0xFFFFu), py = (float)(int16_t)(xyw >> 16);
        const float sx = fr->ofx + fr->h * x / z, sy = fr->ofy + fr->h * y / z;
        const float d = sqrtf((sx - px) * (sx - px) + (sy - py) * (sy - py));
        s_gs.err_sum += d; s_gs.err_n++;
        s_gs.err_b[d < 1.0f ? 0 : d < 4.0f ? 1 : 2]++;
        s_gs.node_err[v->owner] += d < 1000.0f ? d : 1000.0f; s_gs.node_n[v->owner]++;
        if (d > s_gs.err_max) s_gs.err_max = d;
    }
}

static void geo_frame(RmkStageFrame* fr, const Stg* s) {
    memset(fr, 0, sizeof *fr);
    fr->magic = RMK_STAGE_FRAME_MAGIC;
    fr->serial = ++s_geo_serial;
    fr->flags = s_geo_flags & (15u | RMK_SFLAG_NODE_COLORS);
    fr->n_bones = s->t.n_nodes;
    fr->ofx = s_proj.ofx; fr->ofy = s_proj.ofy; fr->h = s_proj.h;
    fr->relight = s_geo_par[0]; fr->detail = s_geo_par[1]; fr->spec = s_geo_par[2]; fr->ao = s_geo_par[3];
    fr->shadow = s_geo_par[4]; fr->expo = s_geo_par[5]; fr->rough = s_geo_par[6];
    fr->pulse = bag2_stage_pulse() * s_geo_par[7];
    if (!bag2_heat_stage_lights(fr->ldir, fr->lcol, fr->bk)) fr->flags |= RMK_SFLAG_NO_LIGHTS;
    int fs = 0;
    if (bag2_heat_ground(fr->floor_n, &fs)) fr->floor_on = fs ? 1.0f : 0.0f;
    for (uint32_t k = 0; k < RMK_MAX_NODES; k++) {
        RmkBone* b = &fr->bones[k];
        memset(b, 0, sizeof *b);
        b->m[0] = b->m[4] = b->m[8] = 1.0f;
        b->t[2] = 1e6f;
    }
    for (int i = 0; i < s_nseen; i++) fr->bones[s_seen[i].k] = s_seen[i].bone;
}

/* Uma sequência pronta (s_pk[0..n)): o elemento antes dela (run_prev) passa a apontar para o marcador, e o marcador
 * para `next`. Devolve o marcador (0 = sem espaço: a sequência fica com o jogo). */
#define GEO_RUN_CAP 1500u            /* pacotes por marcador (o payload do desenho de host vai até 64 KB) */
static RmkStagePkt s_pk[GEO_PKTS_PER_HALF];
static uint32_t close_run(Stg* s, uint32_t run_prev, uint32_t next, uint32_t n, const RmkStageFrame* fr, int first,
                          const int16_t bb[4], uint32_t tpage) {
    static uint8_t payload[sizeof(RmkStageRun) + GEO_RUN_CAP * sizeof(RmkStagePkt)];
    (void)first;
    if (!run_prev || !n) return 0;
    RmkStageRun* r = (RmkStageRun*)payload;
    r->magic = RMK_STAGE_RUN_MAGIC; r->serial = fr->serial; r->n = n; r->has_frame = 0;
    uint32_t bytes = (uint32_t)sizeof *r;
    memcpy(payload + bytes, s_pk, sizeof(RmkStagePkt) * n);
    bytes += (uint32_t)sizeof(RmkStagePkt) * n;
    const int32_t b4[4] = {bb[0], bb[1], bb[2], bb[3]};
    const uint32_t m = geo_marker((uint16_t)(s_cur << 8), b4, payload, bytes, next, tpage);
    if (!m) { s_gs.overflow++; return 0; }
    const uint32_t pa = 0x80000000u | run_prev, pt = rd32(pa);
    psx_mod_write_word(pa, (pt & 0xFF000000u) | (m & 0xFFFFFFu));
    s_gs.runs++;
    s_gs.pkts += n;
    s->marked = 1;
    return m;
}

static void geo_draw_ot(struct CPUState* cpu, uint32_t address) {
    (void)address;
    const double t0 = st_now_us();
    const uint32_t head = cpu->gpr[4] & 0x00FFFFFFu;
    const int nseen = s_nseen;
    for (int i = 0; i < s_nseen; i++) s_seen_k[s_seen[i].k] = -1;   /* os nós vistos valem só para esta OT */
    s_nseen = 0;
    if (!s_nst || !head) return;
    /* recursos GL aos poucos e descarga: marcadores sem nada visível no começo da OT */
    for (int i = 0; i < s_nst; i++) {
        Stg* s = &s_st[i];
        if (s->state == 2 && !s->rt_ready && !s->rt_failed) { geo_side_marker(head, (uint16_t)(GEO_FLAG_PREPARE | i << 8)); s->marked = 1; }
        if (s->state == 3 && s->marked && !s->rt_freed) geo_side_marker(head, (uint16_t)(GEO_FLAG_FREE | i << 8));
    }
    /* os vértices transformados (geo_rtp) valem para esta OT; registra para a próxima só com o palco pronto */
    const int ready = s_geo_on && s_cur >= 0 && s_st[s_cur].state == 2 && s_st[s_cur].rt_ready;
    static int ready_prev;
    if (!(s_cur >= 0 && s_st[s_cur].state == 2 && s_st[s_cur].rt_ready)) { if (ready_prev) memset(s_dyn, 0, sizeof s_dyn); ready_prev = 0; }
    else ready_prev = 1;
    if (!ready || !nseen) { fv_reset(); s_fv_rec = ready; return; }
    Stg* s = &s_st[s_cur];
    s->last_use_us = t0;
    RmkStageFrame fr;
    s_nseen = nseen;                 /* geo_frame e find_seen leem os nós desta OT */
    for (int i = 0; i < s_nseen; i++)
        for (int j = i + 1; j < s_nseen; j++)
            if (s_seen[i].lo < s_seen[j].hi && s_seen[j].lo < s_seen[i].hi) s_gs.overlap++;
    geo_frame(&fr, s);
    s_nff = 0; s_nffv = 0; s_twin = 0;
    int first = 1;
    uint32_t p = head, prev = 0, run_prev = 0, n = 0, steps = 0, run_tpage = 0, used = 0;
    int in_run = 0;
    int16_t bb[4] = {32767, 32767, -32768, -32768};
    s_gs.ots++;
    s_gs.seen += (uint32_t)nseen;
    while (p != 0xFFFFFFu && steps++ < 40000u) {
        const uint32_t a = 0x80000000u | p;
        const uint32_t tag = rd32(a), len = tag >> 24, next = tag & 0xFFFFFFu;
        int cls = 0;                 /* 0 = de fora, 1 = do palco (em s_pk), 2 = vazio ou sem nada a desenhar */
        if (!len) cls = 2;
        else {
            const int i = find_seen(p);
            if (i < 0) {             /* de fora dos nós: geometria do overlay (face livre) ou estado da GPU */
                const uint32_t op = rd32(a + 4u) >> 24;
                if (op >= 0xE1u && op <= 0xE6u) track_env(a, len);
                else if (!(s_geo_flags & 512u) && (op & 0xE0u) == 0x20u && (in_run || used + n < GEO_PKTS_PER_HALF)) {
                    if (in_run && n >= GEO_RUN_CAP) {
                        const uint32_t m = close_run(s, run_prev, p, n, &fr, first, bb, 0u);
                        if (m) { first = 0; used += n; run_prev = m; }
                        else in_run = 0;
                        n = 0; run_tpage = 0;
                        bb[0] = bb[1] = 32767; bb[2] = bb[3] = -32768;
                    }
                    RmkStagePkt e;
                    uint32_t tp = 0;
                    int16_t pb[4] = {32767, 32767, -32768, -32768};
                    if (try_free(a, len, &e, &tp, pb)) {
                        if ((s_geo_flags & 128u) && pb[0] + pb[2] < 512) {   /* metade esquerda (A/B): fica o original */
                            s_nffv -= s_ff_n[--s_nff];
                            s_gs.free_faces--;
                        } else {
                            for (int q = 0; q < 2; q++) { if (pb[q] < bb[q]) bb[q] = pb[q]; if (pb[q + 2] > bb[q + 2]) bb[q + 2] = pb[q + 2]; }
                            cls = 1;
                            s_pk[n++] = e;
                            if (e.flags & RMK_SPKT_TEX) run_tpage = tp | 0x10000u;
                        }
                    }
                }
            } else {
                const Seen* sn = &s_seen[i];
                uint16_t lp = (p - sn->buf) & 3u ? 0xFFFFu : s->wmap[sn->k][(p - sn->buf) / 4u];
                if (lp == 0xFFFFu && !((p - sn->buf) & 3u)) {
                    /* o 2º pacote de uma primitiva com decalque (o mapa marca só o começo dela): a primitiva começa no
                     * máximo 13 palavras antes, e o 1º pacote dela termina aqui */
                    const uint32_t w = (p - sn->buf) / 4u;
                    for (uint32_t b = 1; b <= 13u && b <= w; b++) {
                        const uint16_t q = s->wmap[sn->k][w - b];
                        if (q == 0xFFFFu) continue;
                        const RmkStagePrim* pq = &s->t.prims[s->t.nodes[sn->k].prim0 + q];
                        if (pq->n_pk == 2 && (rd32(0x80000000u | (sn->buf + 4u * (w - b))) >> 24) + 1u == b) lp = q;
                        break;
                    }
                }
                if (lp == 0xFFFFu) s_gs.unknown++;
                else {
                    const uint32_t gp = s->t.nodes[sn->k].prim0 + lp;
                    const int second = in_run && n && s_pk[n - 1].prim == gp;
                    const RmkStagePrim* pr = &s->t.prims[gp];
                    if (pr->n_pk == 1 && len + 1u != pr->words) s_gs.size_bad++;   /* não é o pacote desta primitiva */
                    else if (!pr->n_faces) { cls = 2; s_gs.nopr++; }   /* degenerada: o jogo não desenha nada */
                    else if (!second && used + n >= GEO_PKTS_PER_HALF) s_gs.overflow++;
                    else {
                        if (!second && n >= GEO_RUN_CAP) {   /* sequência longa: fecha e continua a partir do marcador */
                            const uint32_t m = close_run(s, run_prev, p, n, &fr, first, bb, 0u);
                            if (m) { first = 0; used += n; run_prev = m; }
                            else in_run = 0;
                            n = 0; run_tpage = 0;
                            bb[0] = bb[1] = 32767; bb[2] = bb[3] = -32768;
                        }
                        RmkStagePkt e;
                        if (second) e = s_pk[n - 1];
                        else { memset(&e, 0, sizeof e); e.prim = (uint16_t)gp; }
                        uint32_t tp = 0;
                        int16_t pb[4] = {32767, 32767, -32768, -32768};
                        int r = read_packet(a, len, &e, second, &tp, pb, NULL, NULL);
                        if (r == 1 && (s_geo_flags & 128u) && pb[0] + pb[2] < 512) r = 0;   /* metade esquerda: original */
                        if (r == 1) {
                            for (int q = 0; q < 2; q++) { if (pb[q] < bb[q]) bb[q] = pb[q]; if (pb[q + 2] > bb[q + 2]) bb[q + 2] = pb[q + 2]; }
                            cls = 1;
                            if (second) s_pk[n - 1] = e; else s_pk[n++] = e;
                            if (e.flags & RMK_SPKT_TEX) run_tpage = tp | 0x10000u;
                            if (s_geo_flags & 64u) check_projection(s, &fr, a, &e);
                        } else if (r == -2) s_gs.semi_flat++;
                        else if (!(s_geo_flags & 128u)) s_gs.lines++;
                    }
                }
            }
        }
        if (cls == 1 && !in_run) { in_run = 1; run_prev = prev; }
        if (cls == 0) {
            if (in_run) {   /* fecha a sequência antes deste pacote */
                if (close_run(s, run_prev, p, n, &fr, first, bb, run_tpage)) { first = 0; used += n; }
                in_run = 0; n = 0; run_tpage = 0;
                bb[0] = bb[1] = 32767; bb[2] = bb[3] = -32768;
            }
            s_gs.kept++;
            prev = p;
        } else if (cls == 2 && !in_run) prev = p;
        p = next;
    }
    if (in_run) psx_mod_counter_add("bag2_stage.geo_run_at_end", 1);   /* a OT termina num pacote do palco: fica com o jogo */
    /* o quadro (matrizes, luz, vértices das faces livres) num marcador logo depois do 1º elemento da OT: é o primeiro
     * desenho do palco na ordem; as sequências vêm depois */
    if (!first) {
        static uint8_t fp[GEO_FRAME_BYTES];
        RmkStageRun* r = (RmkStageRun*)fp;
        r->magic = RMK_STAGE_RUN_MAGIC; r->serial = fr.serial; r->n = 0; r->has_frame = 1;
        uint32_t b = (uint32_t)sizeof *r;
        memcpy(fp + b, &fr, sizeof fr); b += (uint32_t)sizeof fr;
        RmkStageFree fv = {s_nff, s_nffv};
        memcpy(fp + b, &fv, sizeof fv); b += (uint32_t)sizeof fv;
        memcpy(fp + b, s_ff_n, s_nff); b += (s_nff + 3u) & ~3u;
        memcpy(fp + b, s_ff_pos, 12u * s_nffv); b += 12u * s_nffv;
        const uint32_t ht = rd32(0x80000000u | head);
        const int32_t bb0[4] = {0, 0, -1, -1};   /* nada visível: o callback roda com pass = -1 */
        const uint32_t m = geo_marker((uint16_t)(s_cur << 8), bb0, fp, b, ht & 0xFFFFFFu, 0);
        if (m) psx_mod_write_word(0x80000000u | head, (ht & 0xFF000000u) | (m & 0xFFFFFFu));
        else s_gs.overflow++;
    }
    s_nseen = 0;
    fv_reset();
    s_fv_rec = 1;
    s_gs.us += st_now_us() - t0;
}

/* ------------------------------------------------------------------ thread de render */
static void geo_collect(void) {
    for (int i = 0; i < s_nst; i++) {
        Stg* s = &s_st[i];
        if (s->state != 3 || s->rt_freed) continue;
        if (s->gl) { rmk_stage_gl_destroy(s->gl); s->gl = NULL; s_geo_free_n++; }
        s->rt_ready = 0; s->rt_failed = 0; s->rt_prepares = 0; s->gl_bytes = 0;
        mem_fence();
        s->rt_freed = 1;
    }
}

static void geo_host_draw(const PSXModHostDrawTarget* t, const void* payload, uint32_t bytes) {
    const double t0 = st_now_us();
    geo_collect();
    if (t->flags & GEO_FLAG_FREE) return;
    const int i = (t->flags >> 8) & 31;
    if (i >= s_nst) return;
    Stg* s = &s_st[i];
    if (s->state != 2) return;
    if (!s->gl && !s->rt_failed) {
        s->gl = rmk_stage_gl_create(t->get_proc, 0);
        if (!s->gl) s->rt_failed = 1;
    }
    if (!s->rt_ready && !s->rt_failed) {
        s->rt_prepares++;
        if (rmk_stage_gl_prepare(s->gl, s->full)) {
            s->rt_ready = 1;
            s->gl_bytes = rmk_stage_gl_bytes(s->gl);
            RmkAsset* a = s->full;   /* as camadas estão na GPU: a cópia de CPU sai (malha e faces ficam) */
            free(a->layers); a->layers = NULL;
            free(a->nmaps); a->nmaps = NULL;
            free(a->stp); a->stp = NULL;
        } else if (*rmk_stage_gl_error(s->gl)) {
            fprintf(stdout, "bag2_stage: geometria do palco desligada: %s\n", rmk_stage_gl_error(s->gl));
            fflush(stdout);
            s->rt_failed = 1;
        }
    }
    if ((t->flags & GEO_FLAG_PREPARE) || !s->rt_ready || !payload) return;
    const RmkStageRun* r = (const RmkStageRun*)payload;
    if (bytes < sizeof *r || r->magic != RMK_STAGE_RUN_MAGIC) return;
    const uint8_t* q = (const uint8_t*)(r + 1);
    if (r->has_frame) {   /* o quadro: matrizes, luz e as faces livres; nada a desenhar */
        if (bytes < sizeof *r + sizeof(RmkStageFrame) + sizeof(RmkStageFree)) return;
        const RmkStageFrame* fr = (const RmkStageFrame*)q;
        const RmkStageFree* fv = (const RmkStageFree*)(fr + 1);
        const uint8_t* fn = (const uint8_t*)(fv + 1);
        const float* fp = (const float*)(fn + ((fv->n_free_faces + 3u) & ~3u));
        if (fr->magic != RMK_STAGE_FRAME_MAGIC || fv->n_free_verts > RMK_STAGE_MAX_FREE ||
            (const uint8_t*)(fp + 3u * fv->n_free_verts) > (const uint8_t*)payload + bytes) return;
        rmk_stage_gl_frame(s->gl, s->full, fr, fv, fn, fp);
        s_geo_frames++;
        return;
    }
    if (t->pass < 0 || bytes < (uint32_t)sizeof *r + r->n * (uint32_t)sizeof(RmkStagePkt)) return;
    RmkTarget rt;
    rt.fbo = t->fbo;
    rt.ndc[0] = 1.0f / t->x_half;
    rt.ndc[1] = ((float)t->draw_off_x + t->shift + t->x_off) / t->x_half - 1.0f;
    rt.ndc[2] = 1.0f / t->y_half;
    rt.ndc[3] = ((float)t->draw_off_y + t->shift) / t->y_half - 1.0f;
    memcpy(rt.viewport, t->viewport, sizeof rt.viewport);
    memcpy(rt.scissor, t->scissor, sizeof rt.scissor);
    rt.bbox[0] = rt.bbox[1] = 0.0f; rt.bbox[2] = rt.bbox[3] = 0.0f;
    bag2_stage_skip_x(t, rt.skip_x);
    RmkShadowView sh[2];
    const int nsh = bag2_heat_shadow_views(sh, 2);
    /* a mesma sequência no espelho da tela larga: a chave (endereço do payload no fluxo não serve) é o primeiro pacote */
    const RmkStagePkt* pk = (const RmkStagePkt*)q;
    const uint32_t key = r->serial * 4096u + (r->n ? pk[0].prim : 0u) * 3u + r->n;
    GpuTimer* gt = &s_gt_geo[t->pass == 1];
    gt_init(gt, t->get_proc);
    gt_begin(gt);
    rmk_stage_gl_draw(s->gl, s->full, &s->t, pk, r->n, key, &rt, sh, nsh);
    gt_end(gt);
    s_geo_draws++;
    s_geo_us += st_now_us() - t0;
}

/* ------------------------------------------------------------------ comandos */
static int stage_geo_stats_cmd(const char* args, char* reply, int cap) {
    (void)args;
    int n = snprintf(reply, cap, "ots=%u corridas=%u pacotes=%u fora=%u desconhecidos=%u linhas=%u sem_face=%u "
                     "semi_sem_textura=%u sem_espaco=%u chave_falha=%u nos_vistos=%u us_por_ot=%.1f erro_px=%.2f "
                     "(max %.1f, n %u: <1 %u, <4 %u, >=4 %u) | rt desenhos=%u quadros=%u us_por_desenho=%.1f gl_apagados=%u |",
                     s_gs.ots, s_gs.runs, s_gs.pkts, s_gs.kept, s_gs.unknown, s_gs.lines, s_gs.nopr, s_gs.semi_flat,
                     s_gs.overflow, s_gs.key_miss, s_gs.seen, s_gs.ots ? s_gs.us / s_gs.ots : 0.0,
                     s_gs.err_n ? s_gs.err_sum / s_gs.err_n : 0.0, s_gs.err_max, s_gs.err_n, s_gs.err_b[0], s_gs.err_b[1],
                     s_gs.err_b[2], s_geo_draws, s_geo_frames,
                     s_geo_draws ? s_geo_us / s_geo_draws : 0.0, s_geo_free_n);
    n += snprintf(reply + n, cap - n, " livres: faces=%u vértices=%u sem_par=%u sem_camada=%u janela=%u rtp=%u |",
                  s_gs.free_faces, s_gs.free_verts, s_gs.free_nomatch, s_gs.free_miss, s_gs.free_twin, s_gs.rtp);
    n += snprintf(reply + n, cap - n, " texturas_animadas=%u", s_dyn_n);
    {   /* tempo de GPU (com gpu_timers 1): por desenho e no total da janela, canônica e espelho */
        static uint64_t ns0[2], n0[2];
        for (int k = 0; k < 2; k++) {
            uint64_t c = 0;
            const uint64_t a = s_gt_geo[k].ns - ns0[k];
            const double us = gt_take(&s_gt_geo[k], &ns0[k], &n0[k], &c);
            n += snprintf(reply + n, cap - n, " gpu%d_us=%.1f gpu%d_n=%llu gpu%d_ms_total=%.0f", k, us, k,
                          (unsigned long long)c, k, (double)a / 1e6);
        }
    }
    n += snprintf(reply + n, cap - n, " dup=%u sobrepostos=%u tamanho_errado=%u nós com erro:", s_gs.dup, s_gs.overlap,
                  s_gs.size_bad);
    for (int k = 0; k < RMK_MAX_NODES && n < cap - 160; k++)
        if (s_gs.node_n[k] && s_gs.node_err[k] / s_gs.node_n[k] > 2.0f)
            n += snprintf(reply + n, cap - n, " %d:%.0f/%u", k, s_gs.node_err[k] / s_gs.node_n[k], s_gs.node_n[k]);
    for (int i = 0; i < s_nst && n < cap - 96; i++) {
        const Stg* s = &s_st[i];
        if (!s->state && !s->loads) continue;
        n += snprintf(reply + n, cap - n, " %s: estado=%d cargas=%u descargas=%u carga_ms=%.0f gl=%d/%d gpu_mb=%.0f;",
                      s->bin + 12, s->state, s->loads, s->evictions, s->load_ms, s->rt_ready, s->rt_failed,
                      (double)s->gl_bytes / 1048576.0);
    }
    if (args && args[0] == 'z') { memset(&s_gs, 0, sizeof s_gs); s_geo_draws = 0; s_geo_frames = 0; s_geo_us = 0; }
    return 0;
}

/* stage_geo "<on> <flags> <reiluminação> <relevo> <especular> <AO> <sombra> <exposição> <rugosidade> <pulso>";
 * stage_geo "s" = estatísticas ("sz" zera depois; com gpu_timers 1, também o tempo de GPU na superfície canônica e no
 * espelho da tela larga). Um comando só: o framework guarda até 16. */
static int stage_geo_cmd(const char* args, char* reply, int cap) {
    if (args && args[0] == 's') return stage_geo_stats_cmd(args + 1, reply, cap);
    if (args && args[0] == 'k') {   /* texturas das faces livres sem camada HD: tpage/clut/vezes */
        int n = snprintf(reply, cap, "faltas=%u:", s_nmiss);
        for (uint32_t i = 0; i < s_nmiss && n < cap - 32; i++)
            n += snprintf(reply + n, cap - n, " %03X/%04X/%u", s_miss[i].tpage, s_miss[i].clut, s_miss[i].n);
        if (args[1] == 'z') s_nmiss = 0;
        return 0;
    }
    float v[8];
    int on = -1, fl = -1;
    for (int i = 0; i < 8; i++) v[i] = -1.0f;
    if (args) sscanf(args, "%d %d %f %f %f %f %f %f %f %f", &on, &fl, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    if (on >= 0) s_geo_on = on;
    if (fl >= 0) s_geo_flags = (uint32_t)fl;
    for (int i = 0; i < 8; i++) if (v[i] >= 0.0f) s_geo_par[i] = v[i];
    snprintf(reply, cap, "on=%d flags=%u relight=%.2f detail=%.2f spec=%.2f ao=%.2f shadow=%.2f expo=%.2f rough=%.2f "
             "pulse=%.2f | palcos=%d atual=%d (%s) modelo=%08X", s_geo_on, s_geo_flags, s_geo_par[0], s_geo_par[1],
             s_geo_par[2], s_geo_par[3], s_geo_par[4], s_geo_par[5], s_geo_par[6], s_geo_par[7], s_nst, s_cur,
             s_cur >= 0 ? s_st[s_cur].bin : "-", s_model);
    return 0;
}

/* ------------------------------------------------------------------ comandos */
static int stage_post_cmd(const char* args, char* reply, int cap) {
    float v[14];
    int on = -1, fl = -1;
    for (int i = 0; i < 14; i++) v[i] = -1.0f;
    if (args)
        sscanf(args, "%d %d %f %f %f %f %f %f %f %f %f %f %f %f %f %f", &on, &fl, &v[0], &v[1], &v[2], &v[3], &v[4],
               &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13]);
    if (on >= 0) s_on = on;
    if (fl >= 0) s_flags = (uint32_t)fl;
    for (int i = 0; i < 14; i++) if (v[i] >= 0.0f) s_par[i] = v[i];
    snprintf(reply, cap, "on=%d flags=%u bloom=%.2f thr=%.2f knee=%.2f satw=%.2f pulse=%.2f pexpo=%.3f expo=%.2f "
             "curva=%.2f sat=%.2f vinheta=%.2f atraso=%.1f frio=%.2f quente=%.2f adapt=%.2f | tick=%u rate=%.2f beat=%.3f bar=%.3f "
             "env=%.2f wide=%d ready=%d failed=%d", s_on, s_flags, s_par[0], s_par[1], s_par[2], s_par[3], s_par[4],
             s_par[5], s_par[6], s_par[7], s_par[8], s_par[9], s_par[10], s_par[11], s_par[12], s_par[13], s_tick_prev, s_rate,
             s_beat, s_bar, s_env, psx_mod_widescreen_view_x_margin(), s_ready, s_failed);
    return 0;
}

static int gpu_timers_cmd(const char* args, char* reply, int cap) {
    int on = -1;
    if (args) sscanf(args, "%d", &on);
    if (on >= 0) bag2_gpu_timers = on;
    snprintf(reply, cap, "gpu_timers=%d", bag2_gpu_timers);
    return 0;
}

static int stage_stats_cmd(const char* args, char* reply, int cap) {
    (void)args;
    static uint32_t d0;
    static double u0;
    const uint32_t d = s_draws;
    const double u = s_us;
    static uint64_t g_ns0, g_n0;
    uint64_t g_cnt = 0;
    const double g_us = gt_take(&s_gt, &g_ns0, &g_n0, &g_cnt);
    snprintf(reply, cap, "gpu_us=%.0f gpu_n=%llu draws=%u us_per_draw=%.1f prepares=%u ready=%d failed=%d err=%s | ultimo passo=%d escala=%d "
             "fbo=%d area=%d,%d,%d,%d vp=%d,%d,%d,%d passos0=%d passos1=%d faixa=%dx%d trocas=%d", g_us, (unsigned long long)g_cnt, d - d0,
             d > d0 ? (u - u0) / (double)(d - d0) : 0.0, s_prepares, s_ready, s_failed, s_post ? rmk_post_error(s_post) : "",
             s_last_pass, s_last_scale, s_last_fbo, s_last_area[0], s_last_area[1], s_last_area[2], s_last_area[3],
             s_last_vp[0], s_last_vp[1], s_last_vp[2], s_last_vp[3], s_pass_count[0], s_pass_count[1], s_band_w, s_band_h,
             s_band_changes);
    d0 = d; u0 = u;
    return 0;
}

PSX_MOD_CONSTRUCTOR(bag2_stage_register) {
    (void)psx_mod_register_activation_plugin("bag2.stage_remake", stage_activate);
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", CHAR_UPDATE_FN, stage_char_update);
    (void)psx_mod_register_host_draw(HOST_DRAW_POST, stage_host_draw);
    (void)psx_mod_register_command("stage_post", stage_post_cmd);
    (void)psx_mod_register_command("stage_stats", stage_stats_cmd);
    (void)psx_mod_register_command("gpu_timers", gpu_timers_cmd);
    (void)psx_mod_register_command("stage_geo", stage_geo_cmd);
    (void)psx_mod_register_vblank_plugin("bag2.stage_remake", geo_vblank);
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", NODE_FILL_FN, geo_node_fill);
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", DRAW_OT_FN, geo_draw_ot);
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", F278_FN, geo_rtp);
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", 0x80048234u, geo_load_image);   /* LoadImage */
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", 0x800482F4u, geo_move_image);   /* MoveImage */
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", RTP_FN, geo_rtp);
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", RTP3_FN, geo_rtp);
    (void)psx_mod_register_function_entry_plugin("bag2.stage_remake", RTP4_FN, geo_rtp);
    for (size_t i = 0; i < sizeof RTPT_SITES / sizeof RTPT_SITES[0]; i++)
        (void)psx_mod_register_instruction_plugin("bag2.stage_remake", RTPT_SITES[i], RTPT_WORD, geo_gte_rtp);
    for (size_t i = 0; i < sizeof RTPS_SITES / sizeof RTPS_SITES[0]; i++)
        (void)psx_mod_register_instruction_plugin("bag2.stage_remake", RTPS_SITES[i], RTPS_WORD, geo_gte_rtp);
    (void)psx_mod_register_host_draw(HOST_DRAW_GEO, geo_host_draw);
}
