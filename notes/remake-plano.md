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
vídeo fica para depois (fase 8). Primeiro no exe de PC; o port para o Android (Polygonia) vem depois e o PC
já é construído pensando nele (fase 7).

## Estado
| Fase | O que | Estado |
|---|---|---|
| 0 | Piloto offline + prova de conceito no jogo (HEAT por pacotes GT3) | **feito** (commits 6f0816a..d7cc0d4) |
| 1 | Atalho de render: malha direto no renderizador GL, shader moderno, núcleo portátil | **feito** (2026-10-27) |
| 2 | Detalhe nos personagens (mãos, orelhas, nariz, mapas de normal, sombra, luz do palco, IA 3D) | **feito** (2026-10-27, commits 5e51099..); IA 3D testada offline e descartada |
| 3 | Palco (luz, efeitos, geometria/luz pelo caminho novo) | **feito** (2026-10-27): pós-efeito, pulso, sombra no chão, dumps dos Stage 1; geometria do palco avaliada e adiada; render +0/+1 depois do patch do desenho de host |
| 4 | Dois jogadores (segundo personagem pelo mesmo pipeline) e depois os outros 16 | **feito** (2026-10-28): rota do 2 jogadores, COMET e os dois figurinos de 15 personagens; 3 recusados (outro esqueleto) |
| 5 | Memória do elenco: só quem está em cena fica carregado; lacunas da fase 4 conferidas | **feito** (2026-10-28): descarga, os 18 conferidos, asset v7 |
| 6 | Palcos: geometria e luz pelo caminho novo, pacote HD de todos os palcos | **feito** (2026-10-28): os 11 palcos da tela de palco pelo desenho de host, chão e fundos do overlay, pacote HD +92 texturas |
| 6b | Pendências da fase 6: render do 236, chão por RTPT em linha, segunda volta de dumps HD, e na segunda rodada tudo o que faltava | **feito** (2026-10-28 e 2026-10-29): espelho do 16:9 só nas laterais, RTPT/RTPS em linha do overlay (chão do 233, asfalto do 236), sombra macia no cenário, sem junta entre ladrilhos; Phong nas peças curvas, 241 e 245, texturas da VRAM, janela de textura, causa do HEAT × COMET; na terceira rodada, cantos saturados com a projeção exata do GTE e paletas animadas por camada (cobertura 80–98 %); 13 palcos a 60 fps, render até +2 |
| 6c | Os três itens que faltavam da fase 6: faces do 241, paletas animadas do 242/234, telão do 235; partida pelo teclado na build normal | **feito** (2026-10-30): 241 era um nó do modelo (prefixo `DR_TPAGE` nas listas 18..23/30/31, `packet_words`); paletas como variantes afins no shader (`.stg` v3: 242 58 camadas, 234 63, 0 texturas animadas com o jogo); telão do 235 = amostragem do buffer de tela, resolvido com a cópia do fbo no núcleo; 2 jogadores pelo teclado conferido |
| 7 | Port para o Polygonia (Android) | **próxima** |
| 8 | Abertura em vídeo ampliada por IA | futuro |

## Onde estamos (fim da fase 6c, 2026-10-30)
- Partida pelo teclado na build normal conferida (1P no 235, 2P HEAT × COMET no 233) com o computer-use; para o 2
  jogadores num teclado só: `p2_device = "keyboard"` e teclas próprias em `[player2]` (só na `build/`).
- 241: as faces sem par fora de `0x8008xxxx` eram o nó 5 do modelo, recusado porque o exportador não contava o
  `DR_TPAGE` de 2 palavras das listas semitransparentes sem textura (`PREFIX`); `geo_buf_mismatch` 2/OT → 0, sem par
  41,8 → 31,9 por OT (o que sobra é o 2D do overlay). 236 e 238 re-exportados com a mesma correção.
- Paletas animadas: variantes afins (`k·rgb + b` sobre uma camada âncora, `.stg` v3, tabela 2 × n_var no shader); 242 e
  234 sem textura animada com o jogo, com 58 e 63 camadas (eram 57 e 56).
- Telão do 235: não há cópia VRAM→VRAM; os painéis leem o buffer de tela (página de 16 bpp em x < 512) e o renderizador
  amostra a VRAM nativa, sem o desenho de host; o núcleo copia a faixa do fbo na primeira sequência do quadro que a lê e
  desenha os painéis com ela (emissivo). Números, A/B e desempenho em `notes/remake-visual.md`, "Fase 6c no jogo".
- Próxima: fase 7 (port para o Polygonia): levar junto a camada FB (no DuckStation a VRAM é uma textura só: o telão sai
  sozinho) e as variantes afins (só tabela e shader).

## Onde estávamos (fim da fase 6b, terceira rodada, 2026-10-29)
- Faces sem par: a tela dos vértices livres agora é a do GTE exata e os cantos saturados (tela em ±1024, vértice perto
  da câmera) são resolvidos pelo vizinho no espaço; sem par por OT 234 45 → 9, 243 38 → 3, 235 73 → 42. O que sobra é
  2D (painéis do fundo do 233, billboards de brilho, sprites): fica com o jogo.
- Paletas animadas por MoveImage: uma camada por linha de origem, tabela `.stg` v2, troca de camada a cada cópia; 232,
  233 e 235 sem textura animada com o jogo (232 sem camada 141 → 9 por OT). 242 e 234 não cabem nas 64 camadas.
- Cobertura 80–98 % dos pacotes do palco por quadro (antes 65–95 %). Desempenho nos 13 palcos: 59,9 fps em todos os
  segundos, 0 underruns, jogo −1 a −5, render −13 a +2.
- Travamento de 10–18 s depois de carregar savestate (desde a primeira volta da 6b): savestate salvo no meio da
  montagem do quadro + cursor da arena de marcadores do host = marcador apontando para si mesmo na OT (o DMA da GPU
  dava 262 mil voltas). Corrigido nos dois plugins: nenhum marcador novo no quadro da carga.
- Aberto: a partida na build normal pelo teclado. Detalhes em `notes/remake-visual.md`, "Terceira rodada" e "Limites
  e o que não deu (fase 6b)".

## Onde estávamos (fim da fase 6b, segunda rodada, 2026-10-29)
- Phong nas peças curvas do palco (barril, tronco, braço do monstro, helicóptero): faces com as normais dos cantos
  entre 3° e 45° e arestas até 1200, decididas por nó quando ele entra em cena; arestas com vizinha plana presas.
- 241 (heliporto) e 245 (estúdio "Dancing Heroes") no jogo pelo índice do palco (`0x8005FB56`); o 241 é uma partida
  normal, o 245 só mostra o palco por ~55 s. Os dois saem pelo caminho novo.
- Texturas das faces livres fora dos TIMs: lidas da VRAM e exportadas como camadas (234 42 → 56, 242 35 → 57);
  textura animada marcada por retângulo da página (234: 27 → 3 texturas com o jogo); janela de textura no shader (238).
- HEAT × COMET: a subida da render é a mesma a 9x e a 4x, então não é preenchimento de pixels; é do lado do driver.
- Desempenho nos 13 palcos: 59,9 fps em todos os segundos, 0 underruns, jogo −2 a −5, render −17 a +2. Memória: até
  1,72 GB de GPU no processo na partida, 692 MB no menu. Regressão sem mudança nos menus e no vídeo.
- Aberto: a partida na build normal pelo teclado (computer-use negado); a build normal foi recompilada e o palco
  carrega e descarrega nela (memória de GPU na demonstração automática). Cobertura: 65–95 % dos pacotes do palco por
  quadro saem pelo caminho novo; o resto fica com o jogo (pacote HD): texturas animadas, efeitos e faces que o overlay
  monta em 0x8008xxxx/0x800Dxxxx com vértices de uma projeção que o plugin não vê (até ~80 por OT no 233 e no 235;
  tabela em `notes/remake-visual.md`, "Limites e o que não deu (fase 6b)").

## Onde estávamos (fim da primeira volta da fase 6b, 2026-10-28)
- No 16:9 o palco desenhava duas vezes o centro da tela (superfície canônica e espelho da tela larga, cujo centro a
  apresentação cobre com a canônica): agora o espelho desenha só as laterais (patch do framework, `center_x`), sem
  mudar a imagem; mapa de normal lido uma vez por pixel. Com a folga, a sombra dos dançarinos no cenário voltou a ter
  borda macia (5 leituras).
- O RTPT/RTPS em linha do código do overlay (20 endereços em `mod_instruction_sites`, overlays estáticos regenerados)
  entra pelas faces livres: o chão inteiro do Sushi Planet e o asfalto do 236 com luz, relevo e sombra novos.
- A textura HD não puxa mais os texels vizinhos da página na borda das faces (a junta escura entre ladrilhos).
- Desempenho nos 11 palcos: 59,9 fps em todos os segundos, 0 underruns, jogo −1 a −5, render −12 a +4 (236: +4 na
  ordem que antes deu +11). O palco custa menos de 1 ms de GPU por quadro (carimbos GL, `stage_gpu_ab.py`).
- Pacote HD: segunda volta de dumps com a música quase inteira em cada palco, 59 texturas novas (620 substituições).
- Abertos: pendência HEAT × COMET sem confirmação (o carimbo de GPU inclui espera), Phong nos adereços, partida na
  build normal pelo teclado, 241 e 245 (`notes/remake-visual.md`, "Limites e o que não deu (fase 6b)").

## Onde estávamos (fim da fase 6, 2026-10-28)
- Os palcos pelo caminho novo: o modelo de cada palco tem o formato dos personagens; o plugin troca na OT (entrada de
  `DrawOTagEnv`) as sequências de pacotes do palco pelo desenho de host, na ordem de pintor exata do jogo, com os pacotes
  vivos; chão, vigas e fundos do código do overlay entram como faces livres (vértices reconhecidos nos ganchos de
  `FUN_8001F278` e `RotTransPers*`). Luz por pixel sobre a cor do jogo, mapa de normal, especular, AO, sombra dos
  dançarinos no cenário, borda suave, reflexo no chão escuro (Sushi Planet, discoteca). Carga e descarga por palco.
- Os 11 palcos da tela de palco conferidos no 2 jogadores (capturas original × remake, cores por nó); 241 e 245 não
  aparecem nela (exportados, não conferidos).
- Desempenho: 59,9 fps em todos os segundos e 0 underruns nos 11; jogo 0 a −4 pontos em todos; render −13 a +3 em dez,
  e +11/−3 no 236 (duas sessões, média +4).
- Pacote HD: dumps dos 11 palcos, 92 texturas novas (564 substituições).
- Pendência da fase 5 (HEAT × COMET): render +9/+10 sem o palco novo; a CPU do plugin não explica (+0,1 ponto), é espera
  pela GPU; com o palco novo a render fica 6 pontos abaixo.

## Onde estávamos (fim da fase 5, 2026-10-28)
- Só quem está em cena fica carregado: numa sessão de 5 partidas a GPU dedicada fica em 1,06–1,22 GB nas partidas e
  0,77–0,99 GB nos menus (0 personagens carregados); sem a descarga chegava a 2,1 GB e subia a cada personagem novo.
- Os 18 personagens nos dois figurinos conferidos no jogo (partidas espelhadas do 2 jogadores, liberados só na RAM):
  0 quadros caindo no original em todos; ROBO-Z (cabeça), SUSHI BOY (mão) e CHICHI&SALLY (nó que some) corrigidos;
  PANDER com o figurino certo; CAPOEIRA não carrega mais à toa.
- Desempenho: 59,9 fps em todos os segundos e 0 underruns; HEAT × COMET com a render +12/+9 (fase 4: +6/+6; causa não
  achada no plugin), KITTY-N × HIRO −1/+2.

## Onde estávamos (fim da fase 4, 2026-10-28)
- Testes pelo **modo 2 jogadores** (`game_session.py` `to_match_2p`, `BAG2_MATCH`): os dois personagens e o palco
  escolhidos. A/B do ritmo na mesma música fechado (`notes/recomp.md` §13): vblank 564480 deriva +3,48 quadros por
  20 s, o 566121 do hardware +0,27.
- **Os outros personagens não passam por `FUN_80029D4C`**: o jogo chama uma função do overlay de cada um, que
  percorre a árvore e chama `FUN_80029588` por osso; gancho novo nela. Plugin genérico (`src/bag2_heat.c`): um asset
  por personagem e figurino (`tools/remake/export_char.py`, v6), carregado numa thread quando o jogo carrega o TIM
  dele, páginas na VRAM de cada jogador achadas nos pacotes do próprio personagem, expressão do rosto em coordenadas
  da VRAM, semitransparência do PS1 no núcleo. A "paleta do jogador 2" era o **figurino alternativo** (159/195 + 2k),
  usado pelo 2P numa partida espelhada; virou asset próprio.
- Medido HEAT × COMET com os dois remakes: 59,9 fps em todos os segundos, 0 underruns, jogo −10/−6 pontos e render
  +6/+6 contra a base (só o HEAT). Detalhes, elenco e limites em `notes/remake-visual.md` ("Fase 4 no jogo").

## Onde estávamos (fim da fase 3, 2026-10-27)
- Palco: bloom multiescala com adaptação e graduação de cor aplicados no meio do quadro (pedido de desenho de host no
  slot 96 da OT, depois da cena e antes da OT do HUD), pulso no tempo da música (tick do jogo em `0x8005FBB4`,
  atraso do som de 4 quadros) no bloom, na exposição e na luz do HEAT, e sombra macia do HEAT no chão (PCSS) no
  lugar da sombra do jogo. Tudo no núcleo portátil (`src/remake/rmk_post.c`, `rmk_gl.c`), adaptador
  `src/bag2_stage.c` + `bag2_heat.c`, pacote novo `bag2.remake.stage`. Framework sem patch novo.
- Medido com o pacote HD (a build-debug estava sem ele desde o rename da pasta; junção refeita): 59,9 fps em todos
  os segundos, 0 underruns. A thread de render ficava +7 a +11 pontos acima da base: cada desenho de host do
  framework fazia 13 `glGetIntegerv`, e com o driver da NVIDIA em modo de thread cada um esperava a fila do driver
  (1,5–1,9 ms por desenho). Corrigido no patch `psxrecomp-host-draw.patch` (save/restore sem ler de volta):
  **render tudo − base = +1 e 0 pontos**, jogo +3 e −2, render da base de 30–35 % para 25–27 %; modo 6 (4 HEATs) +
  palco 59,9 fps, render 29 %. GPU do remake medida por carimbos: pós 0,9 ms, chão 0,2 ms, corpo 0,2–0,3 ms por
  quadro do jogo. O remake do HEAT se desligava com o pacote HD (exigia bancos de textura); corrigido. Detalhes,
  capturas e limites em `notes/remake-visual.md` ("Fase 3 no jogo"); formato do palco em `notes/formats.md`.
- Ficou de fora: geometria e luz do palco pelo caminho novo (decisão com dados: formato do overlay do palco não
  mapeado, ganho menor que o das outras peças), palcos 2..8 do modo história nos dumps, afinar o atraso do som por
  música (±3 quadros).

## Onde estávamos (fim da fase 2, 2026-10-27)
- HEAT com mãos de dedos por gesto (aberta, punho, apontando; o jogo troca a mão religando o irmão do antebraço e
  agora o remake acompanha, antes caía no HEAT original nesses quadros), orelhas de anime, nariz, mapas de normal
  gerados das texturas, luz do palco (a do GTE, por pixel) e sombra própria por shadow map no núcleo. Asset v4
  (grupos de faces por gesto). Detalhes, números e capturas em `notes/remake-visual.md` ("Fase 2 no jogo").
- Medido com tudo: 59,9 fps em todos os segundos, thread do jogo 40–43 % (original 46–53 %), render 21 % e 44 %
  (original 16 % e 48 %), modo 6 (4 HEATs) 59,9 com jogo 40 % e render 35 %; 0 underruns.
- Experimento de IA imagem→3D (Hunyuan3D-2mini, autorizado): offline, a roupa da IA ganha dobras mas perde a
  identidade e a estampa sai picada na dança; não entra no jogo. Detalhes em `notes/remake-visual.md`.
- **Expressão do rosto** (feita em 2026-10-27, depois do fechamento da fase 2): o original troca olhos e boca
  copiando variantes de um atlas da página do rosto na VRAM com MoveImage; hooks em MoveImage/LoadImage mandam a
  tabela de casas no quadro e o shader do núcleo remapeia o UV dentro da casa (asset v5). Mesma expressão do
  original no mesmo quadro nas capturas; 59,9 fps em todos os segundos, jogo 39 %, render 32–36 %, 0 underruns.
  Detalhes em `notes/remake-visual.md` ("Expressão do rosto").

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
- Limites: orelhas um pouco abertas de frente; sombra discreta sob luz frontal. (A expressão do rosto, que não
  acompanhava no fechamento da fase 2, foi feita em seguida: ver "Onde estamos".)

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

## Fase 3 — Palco (feita em 2026-10-27; peça 4 adiada com dados)
**Resultado:** ver "Onde estamos". Peças feitas: (0) investigação (`notes/formats.md`, "Palco em partida"); (1)
pós-efeito; o `.pfx` do framework foi descartado porque a máscara 3D dele quase não marca o 3D deste jogo (PGXP não
prova as primitivas) e pegaria o HUD; (2) pulso; (3) sombra no chão; (5) dumps HD dos Stage 1 de quatro
personagens (158 dumps, 9 texturas novas ampliadas). Peça 4 (geometria/luz do palco pelo caminho novo) avaliada e
não feita: o brief pedia "só se valer e couber"; os dados de custo e ganho estão em `notes/remake-visual.md`
("Geometria e luz do palco pelo caminho novo: decisão").

Plano original:
- Luz e efeitos na tela: bloom forte nos neons, graduação de cor, luzes pulsando com a música. O framework
  já tem bloom e presets de pós-efeito `.pfx` (`C:/tools/psxrecomp/docs/POST_EFFECTS.md`), na GPU.
- Com o atalho: geometria e luz do palco pelo caminho novo (iluminação dinâmica, sombra dos personagens no
  chão, reflexo no piso se fizer sentido). Formato/container dos palcos ainda não mapeado.
- Texturas HD dos palcos já existem (pacote v1.3.0); cobrir mais palcos com dumps se faltar.

## Fase 4 — Dois jogadores e o resto do elenco (feita em 2026-10-28)
**Resultado** (técnica em `notes/remake-visual.md`, "Fase 4 no jogo"):
- Peça 0: rota do 2 jogadores nos scripts (`enter_match`, `to_match_2p`, `who()`), A/B do ritmo na mesma música.
- Peça 1: COMET pelo mesmo pipeline. Gancho em `FUN_80029588` (os overlays desenham os outros personagens), plugin
  com vários assets e carga sob demanda, páginas por jogador, figurino alternativo, semitransparência (passo 3 e
  decalque semi), mãos alternativas como grupos (pela solda ou observadas com `char_tree.py`).
- Peça 2: HEAT × COMET medido (60 s, 2 sessões, ordem trocada): 59,9 fps, 0 underruns, jogo abaixo da base, render
  +6.
- Peça 3: o elenco (`export_all.py`, `chars_tour.py`): ver a tabela em `notes/remake-visual.md`.

Plano original:
- Segundo personagem pelo mesmo pipeline (exportador genérico por personagem; assinatura dos ossos).
- Medir com `game_perf.py` numa partida inteira com os dois remakes reais.
- Depois os outros personagens (paleta do jogador 2 em `88 + 2k`; semitransparência ainda não implementada).

## Fase 5 — Memória do elenco e lacunas da fase 4 (feita em 2026-10-28)
Pedido do usuário (2026-10-28): deixar carregados todos os personagens, mesmo sem uso, não serve; e nada pode ficar
sem conferir.
- Carregar só quem entra na partida (nenhuma carga por impressão digital falsa: o CAPOEIRA carregava em toda
  partida).
- Descarregar quem saiu de cena: recursos GL (thread de render) e cópia de CPU, inclusive a do HEAT; voltar a carregar
  sozinho na próxima partida em que ele aparecer. Pausa não descarrega.
- Medir a memória (GPU dedicada do processo, privada, o que o plugin pediu) numa sessão com várias partidas, com e sem
  a descarga; nenhuma perda de fps nem underrun.
- Conferir as lacunas da fase 4: personagens bloqueados no cartão (liberados só na RAM), os três recusados, mãos
  alternativas sem posição, expressão do rosto de todos, figurino alternativo do CAPOEIRA, underruns do boot, build
  normal numa partida de verdade.

**Resultado** (técnica e números em `notes/remake-visual.md`, "Fase 5 no jogo"):
- Descarga: estado 3 nos assets, decisão no vblank (`keep_loaded`), GL apagado pela thread de render num marcador
  (inclusive nos menus, por um marcador vazio no fim da OT), cópia de CPU solta depois; volta sozinha na próxima
  partida. Pausa não descarrega.
- CAPOEIRA: impressão digital de imagem uniforme batia com o vídeo de abertura e o palco; só conta `LoadImage` na área
  dos dançarinos com amostra não uniforme.
- Elenco: os 18 liberados só na RAM; CHICHI&SALLY, COLUMBO e SUSHI BOY exportados (não eram outro esqueleto);
  reconhecimento pelo mesh de cada nó e asset v7 (osso próprio por grupo, peças sem posição); ROBO-Z (cabeça) e SUSHI
  BOY (mão) com posição observada; figurino do PANDER corrigido.

## Fase 6 — Palcos: geometria, luz e o pacote HD de todos (antes do Polygonia)
Na fase 3 a geometria e a luz do palco pelo caminho novo foram avaliadas e adiadas (ganho menor que o custo naquele
momento; `notes/remake-visual.md`, "Geometria e luz do palco pelo caminho novo: decisão"). Decisão do usuário
(2026-10-28): os palcos têm de ficar no nível dos personagens **antes** do port para o Polygonia, e o port leva os dois
juntos. Tudo no núcleo portátil (`src/remake/`, C99, GL 3.3/GLES 3.0, sem geometry/tessellation shader nem SSBO).
- **Peça 0 — inventário**: todos os palcos que o jogo tem (overlays de palco nos containers 231..245, entrada 0, em
  `0x801DB1B8`; a tela de palco do 2 jogadores escolhe qualquer um), com captura, primitivas por quadro
  (`gpu_frame_dump`) e de onde vem cada parte (fundo e chão em `0x800D0000..`, céu/letreiros em `0x80080000..`,
  adereços 3D em `0x800B4000..0x800C3FFF` no Sushi Planet; `notes/formats.md`, "Palco em partida").
- **Peça 1 — pacote HD completo**: dumps de todos os palcos pelo 2 jogadores (`stage_dumps.py`, palco escolhido),
  Real-ESRGAN incremental (`tools/hd_textures.py --filter esrgan`), nenhum palco sem HD.
- **Peça 2 — formato e desenho**: mapear a malha dos adereços no overlay de cada palco e a função que os desenha
  (matrizes, luz); exportador offline por palco (malha soldada, texturas HD, mapas de normal), no mesmo formato de asset
  dos personagens quando couber.
- **Peça 3 — no jogo**: adereços 3D pelo desenho de host (Phong, luz por pixel com as luzes do palco, sombra dos
  dançarinos no cenário, não só no chão); planos grandes (fundo, chão) com mapa de normal e luz; reflexo dos dançarinos
  no chão escuro onde couber (Sushi Planet). Carga e descarga por palco, como o elenco da fase 5.
- **Peça 4 — medir e conferir**: por palco, no 2 jogadores, mesmos critérios (60 fps em todos os segundos, 0 underruns,
  jogo ≤ base + 3, render ≤ base + 10 pontos), capturas original × remake, regressão (HUD, menus, FMV, 16:9, ritmo).
  Junto: as pendências da fase 5 (render do par HEAT × COMET +12/+9; build normal numa partida jogada, que precisa do
  computer-use autorizado).

**Resultado** (técnica, números e limites em `notes/remake-visual.md`, "Fase 6 no jogo"; formato em
`notes/formats.md`, "Palcos: conteúdo e desenho"):
- Peça 0: `stage_inventory.py`; 11 palcos diferentes na tela de palco, modelo no formato dos personagens.
- Peça 1: dumps dos 11 palcos na mesma volta; Real-ESRGAN incremental, 92 texturas novas.
- Peça 2: `export_stage.py` (HTR7 + `.stg`, vincos, AO, alfa suave, texturas do overlay por `stage_keys.py`).
- Peça 3: `rmk_stage.c` + `bag2_stage.c` (troca na OT, faces livres, texturas animadas com o jogo, reflexo).
- Peça 4: `stage_perf.py`, `stage_geo_pause.py` (A/B por savestate), `stage_mem.py`, regressão `stage_check.py`.

## Fase 6b — Pendências da fase 6 (feita em 2026-10-28)
Pedido: render do 236 dentro do critério, priorizando a qualidade de imagem (o objetivo é o upgrade gráfico), e a
segunda volta de dumps HD. Técnica e números em `notes/remake-visual.md`, "Fase 6b no jogo".
- Render: o espelho da tela larga desenha só as laterais (`PSXModHostDrawTarget.center_x` no patch do desenho de host;
  `RmkTarget.skip_x` no núcleo), uma leitura do mapa de normal por pixel. Medição nova pelo tempo de GPU
  (`stage_geo s` com `gpu_timers 1`, `tools/remake/stage_gpu_ab.py`), porque o `render_busy` varia ±5 entre janelas.
- Qualidade: sombra dos dançarinos no cenário com 5 leituras; leitura da textura presa no retângulo de UV da face.
- Cobertura: RTPT/RTPS em linha nos overlays de palco por gancho de instrução (`game.toml`, `mod_instruction_sites`;
  overlays estáticos regenerados com `aot_overlay_pipeline.py static`); chaves de textura novas por `stage_keys.py`
  e os palcos re-exportados.
- Pacote HD: `stage_dump2.py` (música quase inteira por palco), 104 dumps, 59 texturas novas (620 substituições).
- Segunda rodada (2026-10-29), o que faltava: Phong nas peças curvas (`rmk_stage.c`, `classify_tess`, por nó); 241 e
  245 pelo índice do palco (`BAG2_STAGE_IDX`, `stage_hidden.py`); camadas pela VRAM para as texturas das faces livres
  (`stage_vram_keys.py` + `export_stage.py`); textura animada por retângulo e hash da CLUT na tabela `.stg`; janela de
  textura no shader; causa do HEAT × COMET (`heat_gpu_bound.py`). Conferido: medição dos 13 palcos (`perf6d`),
  capturas A/B, memória, regressão; build normal recompilada (partida pelo teclado não conferida: computer-use negado).

## Fase 6c — Os três itens que faltavam da fase 6 (feita em 2026-10-30)
Pedido: partida na build normal pelo teclado (computer-use), faces sem par do 241, paletas animadas do 242/234 fora
das 64 camadas, telão do 235. Técnica e números em `notes/remake-visual.md`, "Fase 6c no jogo"; formato em
`notes/formats.md` ("Palcos: conteúdo e desenho": prefixo `DR_TPAGE`, `.stg` v3, camada do telão).

## Fase 7 — Port para o Polygonia (Android)
O Polygonia é o DuckStation de 2021 (GPLv3), não o PSXRecomp: não tem plugins nem hook de função.
- Hook: detectar no CPU emulado a entrada de `0x80029D4C` e ler as matrizes da RAM.
- Palcos (fase 6): as entradas de `DrawOTagEnv` (0x800486AC, trocar as sequências na OT antes do DMA), de `FUN_80029588`
  (buffer e matriz do GTE de cada nó) e de `FUN_8001F278`/`RotTransPers*` (vértices do código do overlay), mais as 20
  instruções RTPT/RTPS em linha dos overlays de palco (fase 6b, endereços em `notes/formats.md`; conferir a palavra da
  instrução, porque o mesmo endereço é outra coisa em outro palco); o núcleo `rmk_stage.c` é o mesmo. O recorte do
  centro no espelho da tela larga (`RmkTarget.skip_x`) é do renderizador do PSXRecomp: no Polygonia, `skip_x` vazio. O
  adaptador marca os nós fora do quadro com a matriz de espera (`t[2] = RMK_STAGE_UNSEEN_Z`): o Phong de cada nó é
  decidido no primeiro quadro em que ele vem com matriz de verdade. Memória: 13–59 camadas 1024² por palco (55–250 MB de asset, até ~630 MB de GPU no 236): no
  celular, camadas menores ou comprimidas (ETC2, que o GLES 3.0 tem).
- Desenho: adaptador para o renderizador do DuckStation (Vulkan e OpenGL ES), reaproveitando o núcleo
  portátil da fase 1 (asset, poses, shaders GLES 3.0). O DuckStation de 2021 tem buffer de profundidade PGXP.
- Assets gerados no PC a partir do disco do usuário e copiados para o celular; nada do jogo no APK nem no
  repositório público. Medir no aparelho. Estimativa: 4 a 8 sessões depois do PC estável.

## Fase 8 — Abertura (futuro)
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
- Expressão do rosto: troca de imagem na VRAM (MoveImage de um atlas na mesma página para as casas do rosto),
  não de UV/CLUT; o remake segue pelos hooks em MoveImage/LoadImage, com a tabela de casas em coordenadas da VRAM e a
  origem de cada página achada nos pacotes de cada personagem (fase 4).
- Desenho dos personagens (fase 4): só o HEAT passa por `FUN_80029D4C`; os outros, por funções do overlay de cada
  um (1P em `0x801CC510..`, 2P em `0x801D3B64..`), que chamam `FUN_80029588` por osso. Estados dos jogadores em
  `0x80060E30`/`0x80060E38`. Cada personagem tem dois figurinos (87/123 + 2k e 159/195 + 2k); o 2P de uma partida
  espelhada usa o alternativo. O buffer de pacotes do nó (+0xA8) não tem todas as primitivas (decalques saem de
  outro lugar): para saber o que o jogo desenha, `gpu_frame_dump` com o original ligado.
- Elenco (fase 5): reconhecer cada nó da árvore pelo mesh (nº de vértices + índice-base do MeshHeader), não pela
  posição: o jogo troca mãos e cabeça religando nós, pendura peças nas mãos e tira nós da árvore. Juntas sem mesh no
  meio da árvore não entram (só nós de tipo 1). Os TIMs dos dançarinos vão para x ≥ 704, y ≥ 256 na VRAM; impressão
  digital de amostra uniforme bate com qualquer coisa. Nos menus a OT do jogo é a mesma da partida (marcador de host
  funciona fora da partida); no vídeo de abertura não. Os 18 personagens se liberam só na RAM (CodeBreaker, 8 bytes em
  `0x8004F667`); o jogo grava o cartão depois das partidas (guardar e devolver).
- PSXRecomp: ativação de plugin roda antes do renderizador (ler arquivos ali; recursos GL só depois, aos
  poucos, por marcadores de preparo); `render_thread` + qualquer `GL_RT_SYNC` por primitiva derruba o fps;
  trocar textura fecha o lote do GL. Desenho de host (patch 3): marcador GP0 00h "HDR" na arena, payload por
  valor no fluxo da thread de render, callback com o FBO 9x e o espelho da tela larga.
- O jogo desenha cada dançarino a 30 Hz (a árvore de ossos sai 30 vezes por segundo por personagem; a tela
  roda a 60). A música de uma partida dura ~150 s: `game_perf.py` com no máximo 2 variantes de 60 s por sessão.
- OT: a entrada 0 (slot 95) o jogo sobrescreve para pendurar a OT do HUD; pedido de host para "depois da cena,
  antes do HUD" vai no slot 96. Ponteiros em variáveis globais: conferir no código se é ponteiro (a luz da sombra
  em `0x80060CB8` é).
- Música: tick em `0x8005FBB4` (256 por batida); áudio do SPU por `audio_wav` com `start` = índice da amostra (o anel
  guarda ~95 s; `start < 0` devolve o começo do anel). O oponente (e o Stage 1) é sorteado a cada partida.
- Ordem dentro do personagem: o modelo conta com a ordem de pintor da OT; no GL, z-buffer de dois passos
  (profundidade, depois cor na ordem do jogo com tolerância de 1 % de z), como o PGXP do framework.
- Editar arquivos do framework com o Edit: `sed -i` do Git Bash troca CRLF por LF no arquivo inteiro.
- Testes: build-debug (`-DPSX_DEBUG_TOOLS=ON`, TCP 4370), `tools/remake/game_session.py` (rota de menus por
  tempo), `game_sidebyside.py`, `game_compare.py`, `game_perf.py`. `screenshot_hires` (4608×2160) leva ~1 s.
