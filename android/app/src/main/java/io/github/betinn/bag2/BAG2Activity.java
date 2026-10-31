package io.github.betinn.bag2;

import org.libsdl.app.SDLActivity;

/**
 * O jogo: SDLActivity do SDL3 carregando libBust_A_Groove_2_Recompiled.so (runtime do PSXRecomp com o SDL3
 * estático dentro; o JNI_OnLoad do SDL vem dela). SDL_main recebe os mesmos argumentos do exe do PC; os
 * caminhos (game.toml, disco, BIOS, mods) resolvem na pasta de arquivos do app
 * (SDL_GetAndroidExternalStoragePath, patch psxrecomp-android.patch), que a SetupActivity preencheu.
 */
public class BAG2Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "Bust_A_Groove_2_Recompiled" };
    }

    @Override
    protected String[] getArguments() {
        return new String[] { "--game", "game.toml" };
    }
}
