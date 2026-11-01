# Projeto Android (Gradle) do Bust A Groove 2 Recompiled

Só empacota. A biblioteca nativa (`libBust_A_Groove_2_Recompiled.so`: runtime do PSXRecomp + jogo recompilado +
plugins do remake + SDL3 estático) é compilada fora do Gradle, e os dados do jogo (disco, BIOS, mods) entram
em `app/src/main/assets/bag2/` pelo staging. Tudo isso fica fora do git. Roteiro completo, decisões e medições:
[`docs/android-apk.md`](../docs/android-apk.md).

```bash
bash recomp/android/build.sh configure                 # uma vez: cmake + NDK em game/build-android
bash recomp/android/build.sh native stage apk install  # .so + APK pequeno (modo external) + adb install
bash recomp/android/build.sh push run                  # dados (4 GB, uma vez) e logcat
BAG2_STAGE_MODE=full bash recomp/android/build.sh stage apk   # APK completo, com os dados dentro
```

- `app/src/main/java/org/libsdl/app/`: classes Java do SDL 3.4.10 (`android-project` do SDL, licença zlib),
  com uma mudança: a thread do `SDL_main` nasce com 64 MiB de pilha (`SDLActivity.java`, como o exe do PC).
- `app/src/main/java/io/github/betinn/bag2/`: `SetupActivity` (extrai os assets na primeira execução ou pede
  os dados por `adb push` numa build `external`), `AssetInstaller`, `BAG2Activity` (SDLActivity do jogo).
- Assinatura: `C:\Users\sem-m\BAG2-keys\keystore.properties` (fora do repositório; caminho em
  `BAG2_KEYSTORE_PROPERTIES`). Sem o arquivo a release sai sem assinatura.
- `version.properties` é escrito pelo staging (`recomp/android/stage.py`): `versionCode` = nº de commits.
- Ícone: `python recomp/android/make_icon.py`.
- O APK leva disco e BIOS: uso próprio, nunca Play nem repositório público.
