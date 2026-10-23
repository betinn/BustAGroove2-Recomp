# Releases do Bust A Groove 2 recompilado (PC)

Pastas fora do git (65 MB cada); só este README é versionado. Esquema: `vMAJOR.MINOR.PATCH-apelido`.
- MAJOR: mudança grande de base (outro runtime/renderer, novo pipeline de código).
- MINOR: recurso visual/jogabilidade novo (resolução, widescreen, texturas HD, controles).
- PATCH: ajuste de config ou correção sem recurso novo.
Cada pasta contém `VERSION.txt` (versão, data, resumo), `Jogar.bat`, `game.toml`, `settings.toml`, `keybinds.ini`,
`bios/`, `mods/bundled/`, `assets/` e o exe. Para criar a próxima: copiar de `tools/recomp/bag2/build/`
(sem `cache/`, `overlay_captures*`, `*.json`), escrever `VERSION.txt`, registrar aqui e no README principal.

| Versão | Data | Conteúdo | Origem |
|---|---|---|---|
| `v1.0.0-estavel` | 2026-10-23 | Build nativa original, resolução 320x240, janela. **Backup: não alterar.** | commit `119ad2a` |
| `v1.1.0-1080p` | 2026-10-23 | 5x interno (1080p), PGXP, xBR, FXAA, sem dithering, tela cheia sem borda | commit `6f2228a` |
| `v1.2.0-widescreen` | 2026-10-23 | 9x interno (supersampling p/ 1080p), sem FXAA, **widescreen 16:9 nativo**, menus/FMV 4:3 | commit `2dded0a` |

Próxima prevista: `v1.3.0-texturas-hd` — pacote de texturas HD (`psx.enhancement.hd-textures`), ver `CLAUDE.md`
"Próximo passo" e `notes/recomp.md` §11.
