# archive/ — o que não é mais usado

Guardado como registro; nada aqui entra no build do jogo nem nos testes. Os caminhos dentro destes arquivos foram
atualizados para a estrutura nova, mas eles não são mantidos.

| Pasta / arquivo | O que é | Por que parou |
|---|---|---|
| `web/` | A versão para navegador (Vite + TypeScript): visualizador de modelos, mídia STR/XA, EmulatorJS e a build WASM do PSXRecomp com apresentação WebGPU | Abandonada em 2026-10-23 em favor do exe nativo |
| `web-recomp/` | O lado de build da versão web: `build-web.ps1`, `game-web.toml`, `web/CMakeLists.txt`, `split_static_overlays.py`, os patches `psxrecomp-web-*` do fork `C:\tools\psxrecomp-web`, `setup_emulatorjs.py` | Idem |
| `ai/` | Geração de corpo 3D por IA (Hunyuan3D) para o remake: `ai_mesh`, `ai_input`, `ai_fit`, `ai_hybrid`, `ai_decimate` | Testada offline na fase 2 e descartada (`docs/remake-visual.md`) |
| `ghidra/` | Análise headless do `SLUS_011.59` com o Ghidra 12.1.4 + `ghidra_psx_ldr` (`run.sh`, `ExportDecomp.java`) | Serviu para mapear o executável; hoje o trabalho é no C gerado |
| `roadmap.md` | O roteiro original (emulação → recompilação → navegador) | Superado por `docs/remake-plano.md` |

O `heat_remake.py` ainda importa `ai_hybrid` quando chamado com o corpo da IA (`ai_body`), opção que não é usada.
