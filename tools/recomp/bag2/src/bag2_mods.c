/* Plugins estáticos do Bust A Groove 2 recompilado. O runtime só aceita widescreen quando um plugin
 * confiável do título seleciona a proporção (docs/WIDESCREEN.md "mod-owned"); o pacote
 * mods/preloaded/packages/bag2.enhancement.widescreen liga este id por padrão. */
#include "mod_plugins.h"
#include "psx_video_timing.h"

#include <stdio.h>

static void bag2_widescreen_activate(void) {
    /* 16:9 fixo: a projeção do GTE é alargada (mais mundo nas laterais), o present abre a janela em
     * 16:9 e sprites/HUD/BIOS/FMV ficam em 4:3 pelo [widescreen] do game.toml. */
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    (void)psx_mod_set_widescreen_hud_size(0);   /* HUD no tamanho original */
}

/* Ritmo da música (notes/remake-visual.md, "Ritmo da música"): o jogo anda o tick da música por quadro (VSync) e
 * a música XA anda pelo tempo do CD. O framework emula o vblank NTSC com 564480 ciclos (60,000 Hz redondo); no
 * PS1, NTSC sem entrelaçamento tem 263 linhas de 3412,5 ciclos de vídeo a 53,693 MHz = 59,826 Hz = 566121
 * ciclos de CPU. Com 60 Hz o tick corria 0,29 % na frente do som: +3,3 quadros a cada 20 s, quase uma batida
 * ao fim da música (tools/remake/audio_sync.py). O jogo é só NTSC e 240p; psx_video_timing_set_pal(0) não
 * reescreve o período (só troca quando o padrão muda). */
#define BAG2_VBLANK_NTSC_240P 566121u

static void bag2_set_vblank(uint32_t cycles) {
    g_psx_vblank_cycles = cycles;
    g_psx_hblank_cycles = cycles / PSX_LINES_PER_FRAME_NTSC;   /* clock HBlank do Timer 1 */
}

static int bag2_vblank_cmd(const char* args, char* reply, int cap) {
    unsigned v = 0;
    if (args && sscanf(args, "%u", &v) == 1 && v >= 500000u && v <= 700000u) bag2_set_vblank(v);
    snprintf(reply, cap, "vblank=%u hblank=%u (hardware NTSC 240p %u, framework %u)", g_psx_vblank_cycles,
             g_psx_hblank_cycles, BAG2_VBLANK_NTSC_240P, PSX_VBLANK_CYCLES_NTSC);
    return 0;
}

PSX_MOD_CONSTRUCTOR(bag2_register_plugins) {
    (void)psx_mod_register_activation_plugin("bag2.widescreen", bag2_widescreen_activate);
    if (!psx_video_timing_is_pal()) bag2_set_vblank(BAG2_VBLANK_NTSC_240P);
    (void)psx_mod_register_command("vblank_ntsc", bag2_vblank_cmd);
}
