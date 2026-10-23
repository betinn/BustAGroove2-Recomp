# BustAGroove2Web — contexto para sessões do Claude

Bust A Groove 2 (USA, SLUS-01159, **PlayStation 1**) recompilado para .exe nativo de PC via PSXRecomp.
Projeto pessoal, uso local. A versão para navegador (web/) foi **abandonada** em 2026-10-23; foco 100 % no exe.
Responder e escrever docs/commits em pt-BR.

## Regras fixas
- **Nenhum dado do jogo entra no git**: .bin/.cue, XA/STR, `extracted/`, texturas extraídas/dumps, `saves/`,
  `releases/` (só `releases/README.md`). `tools/recomp/bag2/build/` é ignorado por `tools/recomp/bag2/.gitignore`.
- `releases/v1.0.0-estavel/` é backup: **nunca alterar**.
- Sem deploy/publicação. Commits com `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Edições de arquivo: ferramenta Edit ou Python com `encoding='utf-8'` (cp1252 corrompe acentos).
- Depois de qualquer teste: `Stop-Process -Name Bust_A_Groove_2_Recompiled -Force`.

## Onde está cada coisa
- Disco: `C:\Users\sem-m\OneDrive\Documentos\My Games\Bust a groove 2\Bust A Groove 2 (USA).cue` (+ .bin).
- Framework PSXRecomp (nativo): `C:\tools\psxrecomp` (docs em `docs/`: `WIDESCREEN.md`, `HD_TEXTURE_PACKS.md`,
  `MOD_PACKAGES.md`, `QUALITY_PRESETS.md`). Fork web (não usado mais): `C:\tools\psxrecomp-web`.
- Projeto do jogo: `tools/recomp/bag2/` — `CMakeLists.txt`, `game.toml` (config do runtime: `[video]`, `[widescreen]`),
  `settings.toml` (tela cheia), `Jogar.bat`, `src/bag2_mods.c` (plugins estáticos: widescreen),
  `mods/preloaded/packages/*/manifest.toml` (pacotes de mod ligados por padrão), `generated/` (C gerado + overlays AOT),
  `build/` (exe `Bust_A_Groove_2_Recompiled.exe`, `mods/bundled`, `mods/texture-packs/SLUS-01159/{dumps,replacements}`).
- Build: `powershell -File tools\recomp\build.ps1 -Step runtime` (só runtime/exe; `-Step all` regenera tudo).
  Ambiente: `tools/recomp/env.ps1` (GCC 16 WinLibs, CMake/Ninja). `game.toml`/`settings.toml` são copiados para
  `build/` no pós-build.
- Rodar: `tools\recomp\bag2\Jogar.bat` (acha o exe em `build\`) ou
  `Bust_A_Groove_2_Recompiled.exe --game game.toml --disc "<cue>"` dentro de `build/`. Log em stdout.
- Releases: `releases/vMAJOR.MINOR.PATCH-apelido/` com `VERSION.txt`; tabela e regras em `releases/README.md`.
  Atual: `v1.2.0-widescreen`. Para publicar uma versão nova: copiar `build/` (exe, `assets/`, `bios/`,
  `mods/bundled/`, `game.toml`, `settings.toml`, `keybinds.ini`, `input.ini`, `Jogar.bat`), escrever `VERSION.txt`,
  registrar em `releases/README.md` e no `README.md`.
- Notas técnicas: `notes/recomp.md` (§8 overlays AOT, §10 1080p, §11 widescreen/nitidez), `notes/formats.md`,
  `notes/model-format.md`, `notes/media-formats.md`. Logs e capturas: `tools/recomp/logs/`.

## Estado (2026-10-23)
- Jogo completo a 60 fps, 56 overlays compilados antecipadamente, 9x interno + PGXP + xBR, widescreen 16:9 nativo
  (menus/FMV em 4:3), tela cheia sem borda. Teclado em `keybinds.ini`; gamepad SDL automático.
- Nitidez da geometria no teto. O que resta borrado: texturas TIM (4/8 bpp) e sprites 2D do HUD.

## Próximo passo: v1.3.0-texturas-hd
Pacote de texturas HD com o mod embutido `psx.enhancement.hd-textures` (formato DuckStation, só OpenGL):
1. Ligar o mod com "Dump textures" (manifest em `mods/preloaded/packages/`, como o `bag2.enhancement.widescreen`,
   ou `build/mods/state.toml`); jogar abertura, menus, seleção e partidas em vários palcos para popular
   `build/mods/texture-packs/SLUS-01159/dumps`.
2. Ampliar os PNG (múltiplo inteiro, mesma proporção e nome) e colocar em `replacements/`.
3. "Load replacements" on, "Dump" off, comparar capturas antes/depois; manter 60 fps.
4. Script de upscale em `tools/`, notas §12, ignorar dumps/replacements no git, release `v1.3.0-texturas-hd`.

## Testes com captura de tela
`PrintWindow` e `CopyFromScreen` voltam preto na janela GL sem borda. Funciona: captura de tela do app
(computer-use screenshot). O jogo lançado em segundo plano fica atrás das outras janelas: clicar nele ou
`SetForegroundWindow`. A abertura (STR) dura ~1 min e não pula com Start; seleção de personagem: `x` confirma.
