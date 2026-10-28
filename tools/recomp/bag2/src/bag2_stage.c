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
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static double st_now_us(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }
#else
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
volatile int bag2_gpu_timers;        /* tempo de GPU por peça (gpu_timer.h); desligado por padrão */
static volatile int s_last_pass, s_last_scale, s_last_fbo, s_last_area[4], s_last_vp[4], s_pass_count[3];

static uint32_t rd32(uint32_t a) { return psx_mod_read_word(a); }

static void stage_activate(void) {
    if (!s_arena) {
        s_arena = psx_mod_alloc_texture_packet_memory(2u * MARKS_PER_HALF * MARK_BYTES, 64u);
        if (!s_arena) psx_mod_counter_add("bag2_stage.arena_fail", 1);
    }
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
}
