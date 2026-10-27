# Plano do remake visual (personagens + palcos), decidido em 2026-10-26

Fonte de verdade do **plano** e do **estado** do remake. Detalhes técnicos do que já foi feito (formato,
números, testes) ficam em `notes/remake-visual.md`. Toda sessão que avançar uma fase atualiza:
1. a tabela de estado abaixo (e a fase correspondente);
2. a linha "Remake" do `CLAUDE.md` (seção Estado);
3. `notes/remake-visual.md` com o que aprendeu (achados de formato, medições, o que falhou);
4. a memória `project-bag2-hd-cache-patch` se mexer nos patches locais do PSXRecomp.

## Objetivo
Salto visual grande no jogo em si (palco + jogadores), sem artista humano, mantendo 60 fps estáveis com
folga e sem engasgo de áudio, com a mesma jogabilidade. Nada de mudança que quase não se nota. A abertura em
vídeo fica para depois (fase 6). Primeiro no exe de PC; o port para o Android (Polygonia) vem depois e o PC
já é construído pensando nele (fase 5).

## Estado
| Fase | O que | Estado |
|---|---|---|
| 0 | Piloto offline + prova de conceito no jogo (HEAT por pacotes GT3) | **feito** (commits 6f0816a..d7cc0d4) |
| 1 | Atalho de render: malha direto no renderizador GL, shader moderno, núcleo portátil | **feito** (2026-10-27) |
| 2 | Detalhe nos personagens (mãos, orelhas, nariz, mapas de normal, sombra, luz do palco, IA 3D) | **feito** (2026-10-27, commits 5e51099..); IA 3D testada offline e descartada |
| 3 | Palco (luz, efeitos, geometria/luz pelo caminho novo) | pendente |
| 4 | Dois jogadores (segundo personagem pelo mesmo pipeline) e depois os outros 16 | pendente |
| 5 | Port para o Polygonia (Android) | pendente |
| 6 | Abertura em vídeo ampliada por IA | futuro |

## Onde estamos (fase 2, 2026-10-27)
- HEAT com mãos de dedos por gesto (aberta, punho, apontando; o jogo troca a mão religando o irmão do antebraço e
  agora o remake acompanha, antes caía no HEAT original nesses quadros), orelhas de anime, nariz, mapas de normal
  gerados das texturas, luz do palco (a do GTE, por pixel) e sombra própria por shadow map no núcleo. Asset v4
  (grupos de faces por gesto). Detalhes, números e capturas em `notes/remake-visual.md` ("Fase 2 no jogo").
- Medido com tudo: 59,9 fps em todos os segundos, thread do jogo 40–43 % (original 46–53 %), render 21 % e 44 %
  (original 16 % e 48 %), modo 6 (4 HEATs) 59,9 com jogo 40 % e render 35 %; 0 underruns.
- Experimento de IA imagem→3D (Hunyuan3D-2mini, autorizado): offline, a roupa da IA ganha dobras mas perde a
  identidade e a estampa sai picada na dança; não entra no jogo. Detalhes em `notes/remake-visual.md`.
- Achado novo: o original troca a **expressão do rosto** (dentes, marcas) e o remake não (vem da fase 1).

## Onde estávamos (fim da fase 1)
- HEAT remake roda no exe pelo **desenho de host**: plugin `tools/recomp/bag2/src/bag2_heat.c` (hook de filtro
  em `FUN_80029D4C`) entrega a pose num marcador na OT do jogo; o renderizador GL chama o núcleo portátil
  `src/remake/` (Phong Tessellation por instancing, luz por pixel, texturas HD com mipmaps). Patch local novo
  `tools/recomp/patches/psxrecomp-host-draw.patch` (terceiro). Pacote `bag2.remake.heat` ligado por padrão.
  Asset v3 `mods/remake/heat.bin` (50 MB, RGBA8, fora do git) por `tools/remake/export_heat.py`. O caminho por
  pacotes da fase 0 continua como fallback (heat_mode flags bit 2).
- Desempenho medido: original 59,9 fps, thread do jogo 47–52 %, render 50–62 %; remake novo 59,9 sem nenhum segundo abaixo de 59,5 (mín. 59,7), jogo 40–44 %, render 33–48 %; modo 6 (4 HEATs) 59,9 (mín. 59,9), jogo 40 %, render 33 %; 0 underruns em todas as janelas de 60 s. Fase 0 por pacotes: jogo 60–61 %, render 47–52 %, e o modo 6 dela caía para 32,6 fps com 2204 underruns.
- Corrigido nesta fase: a ordem dos atributos dos quads (a estampa da calça, sapatos e mangas saía em cacos no
  remake e nos renders offline de 26/10).
- Visual: ainda sem sombra própria (fica a do jogo) e com luz fixa na câmera (não usa as luzes do palco).

## Fase 1 — Atalho de render (só PC, preparando o Polygonia) — feita em 2026-10-27
**Resultado:** o HEAT é desenhado pelo renderizador GL por um callback de host (patch local
`psxrecomp-host-draw.patch`), com Phong Tessellation por instancing e luz por pixel no shader do núcleo
portátil `tools/recomp/bag2/src/remake/`. Detalhes, números e capturas em `notes/remake-visual.md` ("Fase 1 no
jogo"). Resumo da medição: original 59,9 fps, thread do jogo 47–52 %, render 50–62 %; remake novo 59,9 sem nenhum segundo abaixo de 59,5 (mín. 59,7), jogo 40–44 %, render 33–48 %; modo 6 (4 HEATs) 59,9 (mín. 59,9), jogo 40 %, render 33 %; 0 underruns em todas as janelas de 60 s. Fase 0 por pacotes: jogo 60–61 %, render 47–52 %, e o modo 6 dela caía para 32,6 fps com 2204 underruns.

**Meta:** o HEAT desenhado pelo renderizador GL direto da malha de host, com shader próprio, sem passar
pelo DMA/GP0 emulado. Critérios de pronto:
- Visual: luz por pixel (difusa com wrap, especular GGX, contraluz, ambiente de hemisfério, oclusão
  ambiente), texturas HD RGBA filtradas com mipmaps, Phong Tessellation na GPU; comparação lado a lado no
  mesmo quadro (`game_sidebyside.py`) visivelmente acima da versão por pacotes e perto dos renders offline.
- Desempenho (build-debug, `game_perf.py`, ≥ 60 s): 1 remake com thread do jogo ≤ base + 3 pontos e thread
  de render ≤ base + 10 pontos; teste de carga com 3 cópias extras (modo 6) a 60 fps em todos os segundos e
  0 underruns.
- Composição com o palco certa (o HEAT na frente/atrás do cenário e do outro dançarino como no original).
- Núcleo do remake portátil (ver abaixo); o caminho por pacotes continua como fallback/comparação.

**Desenho proposto** (ajustar se a investigação mostrar algo melhor):
1. **Núcleo portátil** (`tools/recomp/bag2/src/remake/`, C99, sem incluir nada do PSXRecomp):
   carga do asset (formato novo v3, versionado), matrizes dos ossos (MATRIX do GTE → float), dados por quadro,
   shaders GLSL escritos no subconjunto comum a GL 3.3 core e GLES 3.0 (`#version` injetado pelo adaptador;
   nada de tessellation shader, geometry shader nem SSBO). Phong Tessellation por instancing: um padrão fixo de
   grade baricêntrica (L = 4) como vértices, os 3 cantos da face (posição/normal/UV/cores) como atributos por
   instância; o vertex shader faz a interpolação de Phong. Deformação: a CPU aplica as 20 matrizes nos 906
   vértices (barato) e sobe um buffer pequeno por quadro, ou o shader faz o skinning lendo as matrizes
   (escolher o que for mais simples e portátil).
2. **Adaptador PC** (`bag2_heat.c`): o hook continua o mesmo (assinatura dos ossos pelo MeshHeader +0x94/+0x98,
   matrizes em node+0x6C). Em vez de milhares de GT3, entrega ao renderizador um pedido de desenho com uma
   cópia dos dados do quadro.
3. **Patch novo no PSXRecomp** (`tools/recomp/patches/`, terceiro patch local): API de desenho de host para
   plugins confiáveis. Ideia: um pacote marcador na arena (reconhecido como os GT3 de banco), ligado no slot
   da OT do personagem; quando a GPU emulada chega nele, grava um comando `RTH_HOSTDRAW` no fluxo da thread de
   render com o payload do quadro; no replay, o renderizador chama o callback registrado com o FBO de alta
   resolução ligado e os parâmetros de tela (escala 9x, deslocamento de desenho do buffer duplo, offset do
   widescreen nativo, área de recorte) e restaura o estado GL depois. Não escrever ponteiros de host na RAM do
   jogo; savestate/rewind não podem quebrar.
4. **Composição**: começar pelo marcador na OT (o HEAT entra inteiro na ordem de desenho do palco, com
   z-buffer próprio por dentro). Avaliar o `pgxp_depth_buffer` do framework para oclusão por pixel contra o
   palco. Sombra: manter a do jogo nesta fase.
5. **Texturas**: as 12 camadas HD como texturas GL normais (sem os 96 bancos 1024×512 e sem a limitação de
   UV de 8 bits).
6. Manter `heat_mode` (0 original, 1 remake, 2/3 comparação, 4..6 carga) e os contadores `bag2_heat.*`.

## Fase 2 — Detalhe nos personagens — feita em 2026-10-27
**Resultado** (técnica em `notes/remake-visual.md`, "Fase 2 no jogo"):
- Mãos: `tools/remake/hands.py` gera por SDF uma mão com dedos por gesto (aberta, punho, apontando; ~720 faces),
  com a textura da luva por projeção das costas da luva original na pose aberta. O jogo troca a mão pelo script
  da dança (comandos 901..903/1000+ no overlay religam o irmão do antebraço, nó 15/18 +0x98, para o nó
  16/20/21 ou 19/22/23); o plugin reconhece o nó na posição 16/19 da árvore. `heat_force` força o gesto para teste.
- Orelhas: `tools/remake/ears.py`, folha com hélice e concha cobrindo a placa original (achatada).
- Nariz: cone pequeno com a pele do rosto embaixo (o rosto do HEAT é côncavo). Sem mapa de normal (rosto pintado).
- Mapas de normal: gerados das texturas HD (estampa em relevo, traço preto como sulco, dobras pintadas como
  ondulação), mesma conta offline e no núcleo (na ativação, 0,9 s); base tangente por derivadas no shader.
- Luzes do palco: a matriz de luz do osso (+0x4C), LCM e BK do GTE, somadas por pixel como o PS1 faz por vértice.
  Feita antes da sombra porque a sombra usa a luz principal do palco.
- Sombra própria: shadow map 1024² no núcleo, dentro do callback, da luz principal; a do chão continua.
- Medido: ver "Onde estamos". Critérios atendidos (jogo abaixo do original; render +5 e −4 pontos; modo 6 a 60).
- IA imagem→3D: Hunyuan3D-2mini (uma vista) + encaixe no esqueleto + textura por face original mais próxima. Pior
  que o remake atual (identidade perdida, estampa picada, buracos nas juntas); fica só offline (`ai_*.py`).
- Limites: expressão do rosto não acompanha (próximo passo sugerido); orelhas um pouco abertas de frente; sombra
  discreta sob luz frontal.

Plano original da fase 2 (mantido para referência):
- **Mãos com dedos** modeladas por código, presas ao osso da mão; uma versão por gesto (mão padrão nos nós
  16/19 e as mãos alternativas dos nós 20–23, que o jogo troca em alguns gestos; hoje o remake ignora essas).
  O jogo não tem ossos de dedo: cada gesto é uma pose fixa. Chance boa.
- **Orelhas** de anime (borda e concha) encaixadas nos vértices da cabeça. Chance boa.
- **Nariz**: relevo pequeno + mapa de normal; o estilo é quase só desenho, ganho sutil por natureza.
- **Rosto** (olhos, boca, sobrancelhas) continua pintado na textura: é o estilo original.
- **Mapas de normal** gerados das texturas/dobras (roupa, costuras, chamas) com a luz por pixel.
- **Experimento com IA de imagem→3D** (malha nova a partir de renders do HEAT de vários ângulos, depois
  encaixe no esqueleto de 19 ossos usando a malha original como guia). Único caminho que cria detalhe novo de
  verdade; risco alto (rosto/dedos borrados, deformação na dança). **Pedir autorização ao usuário antes de
  qualquer download** (modelos de vários GB), dizendo nome, origem e tamanho.
- Toda peça nova aparece primeiro nos renders offline (`tools/remake/heat_remake.py`) lado a lado com o
  original para o usuário aprovar, e só depois vai para o jogo.

## Fase 3 — Palco
- Luz e efeitos na tela: bloom forte nos neons, graduação de cor, luzes pulsando com a música. O framework
  já tem bloom e presets de pós-efeito `.pfx` (`C:/tools/psxrecomp/docs/POST_EFFECTS.md`), na GPU.
- Com o atalho: geometria e luz do palco pelo caminho novo (iluminação dinâmica, sombra dos personagens no
  chão, reflexo no piso se fizer sentido). Formato/container dos palcos ainda não mapeado.
- Texturas HD dos palcos já existem (pacote v1.3.0); cobrir mais palcos com dumps se faltar.

## Fase 4 — Dois jogadores e o resto do elenco
- Segundo personagem pelo mesmo pipeline (exportador genérico por personagem; assinatura dos ossos).
- Medir com `game_perf.py` numa partida inteira com os dois remakes reais.
- Depois os outros personagens (paleta do jogador 2 em `88 + 2k`; semitransparência ainda não implementada).

## Fase 5 — Port para o Polygonia (Android)
O Polygonia é o DuckStation de 2021 (GPLv3), não o PSXRecomp: não tem plugins nem hook de função.
- Hook: detectar no CPU emulado a entrada de `0x80029D4C` e ler as matrizes da RAM.
- Desenho: adaptador para o renderizador do DuckStation (Vulkan e OpenGL ES), reaproveitando o núcleo
  portátil da fase 1 (asset, poses, shaders GLES 3.0). O DuckStation de 2021 tem buffer de profundidade PGXP.
- Assets gerados no PC a partir do disco do usuário e copiados para o celular; nada do jogo no APK nem no
  repositório público. Medir no aparelho. Estimativa: 4 a 8 sessões depois do PC estável.

## Fase 6 — Abertura (futuro)
Ampliar a abertura (STR) por IA offline; falta investigar como trocar o vídeo no runtime.

## Aprendizados que valem para todas as fases (resumo; detalhes em `notes/remake-visual.md`)
- Formato: quads com diagonal v1–v3 e atributos (UV, normal, cor) trocados entre v0 e v2: v0 usa o de índice 2,
  v1 o 1, v2 o 0, v3 o 3 (`QUAD_ATTR`, medido na GPU em 27/10; antes a estampa da calça/sapatos/mangas saía
  em cacos); slot de vértice compartilhado = nó × 256 + local; índices de normal globais; MeshHeader+0x98 =
  base de normais. Para conferir formato contra o jogo: `gpu_frame_dump` + `heat_dump` do mesmo quadro.
- Jogo: árvore de ossos em `FUN_80029C6C` (filho +0x94, irmão +0x98, MATRIX câmera×osso em +0x6C, T em
  +0x80, mesh em +0x8C, tipo em +0xA0); o HEAT desenhado tem só os nós 0–19. Tela: `OFX + H·X/Z` com OFX/OFY
  em gte_ctrl[24]/[25] (16.16), H em gte_ctrl[26]; a tela nativa larga tem 512 colunas (OFX 256).
- Ordem: culling NCLIP > 0; slot da OT `(z0 + z1) >> 3` (v0, v1 em triângulos; v0, v2 em quads); OT em
  `DAT_8006EE88 - 0x180`, entradas 95..4191, reversa (o último ligado sai primeiro); paridade do quadro em
  `0x8006EE90`. O modelo tem camadas coplanares (decalques): a ordem dentro do slot importa.
- PSXRecomp: ativação de plugin roda antes do renderizador (ler arquivos ali; recursos GL só depois, aos
  poucos, por marcadores de preparo); `render_thread` + qualquer `GL_RT_SYNC` por primitiva derruba o fps;
  trocar textura fecha o lote do GL. Desenho de host (patch 3): marcador GP0 00h "HDR" na arena, payload por
  valor no fluxo da thread de render, callback com o FBO 9x e o espelho da tela larga.
- O jogo desenha cada dançarino a 30 Hz (a árvore de ossos sai 30 vezes por segundo por personagem; a tela
  roda a 60). A música de uma partida dura ~150 s: `game_perf.py` com no máximo 2 variantes de 60 s por sessão.
- Ordem dentro do personagem: o modelo conta com a ordem de pintor da OT; no GL, z-buffer de dois passos
  (profundidade, depois cor na ordem do jogo com tolerância de 1 % de z), como o PGXP do framework.
- Editar arquivos do framework com o Edit: `sed -i` do Git Bash troca CRLF por LF no arquivo inteiro.
- Testes: build-debug (`-DPSX_DEBUG_TOOLS=ON`, TCP 4370), `tools/remake/game_session.py` (rota de menus por
  tempo), `game_sidebyside.py`, `game_compare.py`, `game_perf.py`. `screenshot_hires` (4608×2160) leva ~1 s.
