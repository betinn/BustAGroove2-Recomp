/* Plugins estáticos do Bust A Groove 2 recompilado. O runtime só aceita widescreen quando um plugin
 * confiável do título seleciona a proporção (docs/WIDESCREEN.md "mod-owned"); o pacote
 * mods/preloaded/packages/bag2.enhancement.widescreen liga este id por padrão. */
#include "mod_plugins.h"

static void bag2_widescreen_activate(void) {
    /* 16:9 fixo: a projeção do GTE é alargada (mais mundo nas laterais), o present abre a janela em
     * 16:9 e sprites/HUD/BIOS/FMV ficam em 4:3 pelo [widescreen] do game.toml. */
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    (void)psx_mod_set_widescreen_hud_size(0);   /* HUD no tamanho original */
}

PSX_MOD_CONSTRUCTOR(bag2_register_plugins) {
    (void)psx_mod_register_activation_plugin("bag2.widescreen", bag2_widescreen_activate);
}
