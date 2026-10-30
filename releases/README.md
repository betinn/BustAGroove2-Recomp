# Releases do Bust A Groove 2 recompilado (PC)

Pastas fora do git (65 MB cada); só este README é versionado. Esquema: `vMAJOR.MINOR.PATCH-apelido`.
- MAJOR: mudança grande de base (outro runtime/renderer, novo pipeline de código).
- MINOR: recurso visual/jogabilidade novo (resolução, widescreen, texturas HD, controles).
- PATCH: ajuste de config ou correção sem recurso novo.
Cada pasta contém `VERSION.txt` (versão, data, resumo), `Jogar.bat`, `game.toml`, `settings.toml`, `keybinds.ini`,
`bios/`, `mods/bundled/` (+ `mods/texture-packs/` desde a v1.3.0), `assets/` e o exe. Para criar a próxima: copiar de `tools/recomp/bag2/build/`
(sem `cache/`, `overlay_captures*`, `*.json`), escrever `VERSION.txt`, registrar aqui e no README principal.

| Versão | Data | Conteúdo | Origem |
|---|---|---|---|
| `v1.0.0-estavel` | 2026-10-23 | Build nativa original, resolução 320x240, janela. **Backup: não alterar.** | commit `119ad2a` |
| `v1.1.0-1080p` | 2026-10-23 | 5x interno (1080p), PGXP, xBR, FXAA, sem dithering, tela cheia sem borda | commit `6f2228a` |
| `v1.2.0-widescreen` | 2026-10-23 | 9x interno (supersampling p/ 1080p), sem FXAA, **widescreen 16:9 nativo**, menus/FMV 4:3 | commit `2dded0a` |
| `v1.3.0-texturas-hd` | 2026-10-23 | v1.2.0 + **pacote de texturas HD** (472 texturas 4x via Real-ESRGAN em `mods/texture-packs/SLUS-01159/replacements`, mod `psx.enhancement.hd-textures` ligado por padrão; exe com o patch dos caches HD, 60 fps) | commits `25a830f`, `deb9b81` |
| `v2.0.0-remake` | 2026-10-30 | **Remake visual completo** (fases 0–6c): 18 personagens nos dois figurinos, geometria e pós-efeito dos 15 palcos, 620 texturas HD, ritmo da música corrigido. **Auto-suficiente**: abre com dois cliques no exe (sem `.bat`), disco em `disc/`, tudo ligado por padrão; ~4 GB | commit da release (depois de `10268b8`) |

A partir da v1.3.0 a pasta também leva `mods/texture-packs/SLUS-01159/replacements/` (~97 MB de PNG; `dumps/` fica
só na build). Regerar o pack: capturar com `dump = "true"` em `build/mods/state.toml`, jogar, depois
`python tools/hd_textures.py --filter esrgan` (`notes/recomp.md` §12 e §12.1). O exe precisa do patch
`tools/recomp/patches/psxrecomp-hd-cache-caps.patch` no framework, senão a partida cai para 13–35 fps.

A partir da v2.0.0 a pasta é auto-suficiente e não tem `Jogar.bat`: o exe lê o `game.toml` da própria pasta, e o
`game.toml` da release aponta o disco por caminho relativo (`disc = "disc/Bust A Groove 2 (USA).cue"`, resolvido a
partir da pasta do exe). Além do que está acima, leva `mods/remake/` (36 assets dos personagens e 15 palcos, 3,3 GB),
`input.ini` e `disc/` (`.cue` + `.bin`). O exe fixa a pasta de trabalho na própria pasta (os plugins do remake abrem
`mods/remake/` por caminho relativo), então funciona por atalho ou de outra pasta.
