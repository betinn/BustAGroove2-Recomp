# Remake visual do HEAT (piloto offline 2026-10-25, no jogo 2026-10-26, desenho de host 2026-10-27)

Pergunta: dá para modernizar o visual dos personagens **sem artista humano**, partindo das malhas
originais (suavizar, texturas HD, iluminação nova), mantendo a mesma animação? Feito só com o HEAT
(id 0, container `k = 3`), em etapas:
1. **Offline** (`tools/remake/heat_remake.py`): renders ORIGINAL x MODERNIZADO, turntable e GIF de dança.
2. **No jogo, fase 0** (`tools/recomp/bag2/src/bag2_heat.c`, caminho por pacotes): o exe troca o desenho do
   HEAT pela malha suavizada com texturas HD e luz por vértice nova, por pacotes GT3 na GPU emulada.
3. **No jogo, fase 1** (padrão): a malha vai direto ao renderizador GL por um callback de host, com shader
   próprio (Phong na GPU, luz por pixel), a 60 fps em todos os segundos e mais barato que o HEAT original.
   Os outros personagens e os palcos ficam originais.

## Como reproduzir
```
python -m venv tools/recomp/logs/remake/venv
tools/recomp/logs/remake/venv/Scripts/python -m pip install -r tools/remake/requirements.txt
tools/recomp/logs/remake/venv/Scripts/python tools/remake/heat_remake.py      # renders offline (~1 min, GPU)
tools/recomp/logs/remake/venv/Scripts/python tools/remake/export_heat.py      # pacote do jogo (heat.bin)
```
`export_heat.py` grava `mods/remake/heat.bin` (25 MB, derivado do disco, fora do git) em `build/` e
`build-debug/`. Depois é só compilar o runtime e jogar (`Jogar.bat`). Sem o `heat.bin`, o plugin fica inerte
e o HEAT original aparece.

Saída offline em `tools/recomp/logs/remake/heat/` (ignorado pelo git; tem render de assets do jogo):
`compare_a14_f25_y25.png` (frente), `_y205.png` (costas), `compare_rosto_a14_f25.png`,
`metodos_{corpo,rosto,pe}.png` (só a geometria muda), `turntable.gif`, `danca_a14.gif`, `textures/` (cache do
Real-ESRGAN). Capturas do jogo em `tools/recomp/logs/remake/jogo/`, ver "Testes no jogo".

## Pipeline offline (`tools/remake/`)
1. `bag2model.py`: lê BUST2.BIN do disco, containers `123 + 2k` (modelo + animações) e `87 + 2k` (TIMs).
   Porta em Python de `web/src/bag2/model.ts`/`anim.ts`, com as correções de formato abaixo.
2. `rig.py`: **malha soldada**. Cada osso do PS1 é um mesh rígido; vértices de junta são copiados entre
   ossos por um buffer global. Cada vértice recebe um id único (dono = osso que o define), então o corpo
   vira uma superfície só e a pose de cada frame é "matriz do osso dono × posição local".
3. Suavização por frame: **Phong Tessellation** (Boubekeur & Alexa 2008, α = 0,6) por padrão; Loop com vincos
   (`--method loop`) como alternativa. UV, cores e camada de textura são interpolados dentro da face
   original, então a textura não escorrega.
4. Texturas: cada par (TIM, linha de CLUT) usado vira uma camada 256×256 (HEAT usa 12). Real-ESRGAN
   `realesrgan-x4plus-anime` 4x no RGB (pixels transparentes dilatados antes), alfa por xBR.
5. `render.py` (moderngl): ORIGINAL (imitação PS1: luz por vértice, textura afim sem filtro, 15 bits com
   dithering), MODERNIZADO (luz por pixel, GGX, sombra, oclusão ambiente, ACES, MSAA) e cel-shading.

## Correções no formato (em `notes/model-format.md`)
- **Quads**: a diagonal é v1–v3 (a leitura herdada do visualizador web cruzava a diagonal: triângulos
  sobrepostos e buracos). **Atributos (UV, normal, cor) por canto: v0 usa o de índice 2, v1 o 1, v2 o 0, v3 o
  3** (`QUAD_ATTR`), medido na GPU em 27/10 (ver `notes/model-format.md`). A leitura de 26/10 ("em
  sequência v0..v3") trocava v0 e v2: a estampa da calça, dos sapatos e das mangas saía em cacos no remake
  do jogo e também nos renders offline de 26/10 (`compare_*.png` antigos); só o rosto e o peito, quase
  todos triângulos, pareciam certos. Como foi achado: `gpu_frame_dump` (comandos GP0 do quadro, com UV e
  posição por slot) + `heat_dump` do mesmo quadro; cada GT4/FT4 do HEAT casa com a primitiva do arquivo
  pelo conjunto de UV e, pela posição projetada (~1 px), mostra qual vértice foi para qual slot. Antes
  disso foram descartados: CLUT (a da VRAM é igual à do disco), página de textura (TIM = página 704,256,
  100 %), animação de UV (as primitivas na RAM são iguais às do disco) e profundidade.
- Slot de vértice compartilhado = nó × 256 + vértice local; índices de normal são globais no modelo.
- MeshHeader+0x98 = índice-base das normais (identifica cada osso também na RAM do jogo).
- HEAT: nós 20–23 são mãos alternativas, nós 24–37 um esqueleto de sombra; o jogo desenha só os nós 0–19.

Com os quads certos a malha do HEAT é **fechada e manifold** (2166 arestas, nenhuma aberta, nenhuma com
mais de duas faces). A conclusão do primeiro dia ("sopa de painéis abertos, Loop não funciona") estava
errada: vinha do bug dos quads. Hoje Loop e Phong dão resultados parecidos (`metodos_*.png`); o jogo usa
Phong porque é local por face e barato de recalcular a cada quadro.

## Fase 1 no jogo: desenho de host (padrão desde 2026-10-27)
O HEAT não passa mais pela GPU emulada. Três peças:
1. **Núcleo portátil** `tools/recomp/bag2/src/remake/` (C99, sem PSXRecomp nem SDL; é o que vai para o
   Polygonia): `rmk_asset.c` (asset v3 "HTR3": malha soldada, atributos por canto, AO por vértice, 12 camadas
   1024² RGBA8), `rmk_pose.c` (MATRIX câmera×osso do GTE → vértices no espaço da câmera, retângulo na tela,
   faixa de Z, slot da OT da face visível mais próxima, normais por vértice), `rmk_gl.c` (desenho). Shaders no
   subconjunto comum de GL 3.3 core e GLES 3.0 (`#version` escolhido em `rmk_gl_create`; funções GL pelo
   carregador de quem chama; instancing, atributos inteiros, `texelFetch`, texture array, nada de
   tessellation/geometry shader nem SSBO).
2. **Phong Tessellation na GPU por instancing**: os vértices são uma grade baricêntrica fixa L = 4 (15 pontos,
   16 triângulos), cada instância é uma face (3 índices de vértice, UV, cores, camada, modo); o vertex shader
   lê posição/normal dos 3 cantos de uma textura RGBA32F (906 vértices × 2 texels, subida por quadro) e faz a
   interpolação de Phong (α = 0,6). Fragment shader: luz por pixel no espaço da câmera do GTE como
   `render.py` "modern" (principal com wrap, preenchimento, GGX, contraluz, ambiente de hemisfério, AO, ACES),
   textura HD com mipmaps e anisotrópico 8x, cor base + textura misturadas pelo alfa (borda filtrada), modo
   "só textura" com descarte por alfa. Saída com alfa 0 (é o bit 15 do PS1 no FBO do framework).
3. **Patch local `psxrecomp-host-draw.patch`** (terceiro, depois dos dois de antes): um pacote marcador na
   arena de DMA (tag de 1 palavra, GP0 00h com "HDR" = NOP para o PS1, depois cabeçalho e payload fora do
   nó) é ligado na OT do jogo; quando a GPU emulada chega nele, `gl_renderer_host_draw` grava `RTH_HOSTDRAW`
   no fluxo da thread de render com o payload copiado por valor (a pose: 906 posições em float + projeção do
   GTE, ~11 KB). No replay, o callback do plugin desenha com o FBO de alta resolução ligado (superfície
   canônica 9x) e, se o retângulo sai do quadro 4:3, de novo no espelho da tela larga (fila de espelhos
   esvaziada antes, para manter a ordem). Estado GL salvo/restaurado em volta. Sem ponteiro de host na RAM:
   savestate salvo e carregado no meio da partida com o remake ligado, desenho seguiu normal.

**Recursos GL sem engasgo:** o asset (50 MB) é lido na ativação, antes do jogo rodar. Os recursos GL são
criados aos poucos por marcadores de preparo (uma camada de textura por marcador, 14 passos) que qualquer
personagem liga enquanto não ficam prontos; até lá o HEAT original aparece. Na prática ficam prontos na
abertura, bem antes da partida.

**Composição e ordem:** o marcador vai no slot da OT da face visível mais próxima, então o palco atrás sai
antes e o que está na frente (HUD, faixas translúcidas do palco, "Ready") sai depois, como nas capturas. A
sombra do jogo continua. Por dentro do HEAT, z-buffer próprio (profundidade limpa só no retângulo dele) em
**dois passos**, como o buffer de profundidade PGXP do framework: (1) só profundidade; (2) cor na ordem de
pintor do jogo (slot `(z0 + z1) >> 3` do mais longe ao mais perto, dentro do slot a face exportada depois
primeiro, pois a OT é reversa), com a profundidade puxada para a câmera em 1 % de z (`zbias`) e sem escrever
profundidade. Motivo: o modelo foi feito para a ordem de pintor (chamas e enfeites ficam um pouco à frente da
roupa); num z-buffer comum o Phong da roupa atravessaria. (Na prática o "cacos" que vi primeiro era o bug de
atributo dos quads, ver acima; o z-buffer simples e o de dois passos dão quase o mesmo com o asset certo, e o de
dois passos ficou por segurança.)

**Desempenho (fase 1)** (build-debug, RTX 3070 Ti, 9x interno + 16:9, `tools/remake/game_perf.py`, 60 s por
janela, 2 janelas por partida, ordem trocada entre sessões para não favorecer um trecho da música):

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| A / 1ª | HEAT original | 59,9 (mín. 58,8; 1 s abaixo de 59,5) | 52 % (máx. 56) | 62 % (máx. 74) | 0 |
| A / 2ª | remake novo | 59,9 (mín. 59,8; nenhum abaixo) | 40 % (máx. 45) | 33 % (máx. 51) | 0 |
| B / 1ª | remake novo | 59,9 (mín. 59,7; nenhum abaixo) | 44 % (máx. 52) | 48 % (máx. 69) | 0 |
| B / 2ª | HEAT original | 59,9 (mín. 59,9) | 48 % (máx. 54) | 52 % (máx. 82) | 0 |
| C / 1ª | modo 6: remake + 3 cópias (4 HEATs) | 59,9 (mín. 59,9; nenhum abaixo) | 40 % (máx. 42) | 33 % (máx. 39) | 0 |
| C / 2ª | HEAT original | 59,9 (mín. 59,8) | 47 % (máx. 50) | 50 % (máx. 65) | 0 |
| D / 1ª e 2ª | fase 0, pacotes (fallback) | 59,9 (mín. 59,9) | 61 % e 60 % (máx. 64) | 52 % e 47 % (máx. 59) | 0 |
| fase 0, antes desta sessão | modo 6 por pacotes | 32,6 (mín. 26,3; 59 de 59 s abaixo) | 66 % (máx. 99) | 91 % (máx. 98) | 2204 |

Critérios da fase 1 atendidos: thread do jogo e de render abaixo da base (o pedido era até base + 3 e + 10
pontos); modo 6 a 60 em todos os segundos e 0 underruns. A sessão D mediu pacotes nas duas janelas porque
`heat_mode "1"` não zera as flags (ficou o 6 da janela anterior).

O remake novo custa menos que o HEAT original nas duas threads (no mesmo trecho: jogo −8 pontos, render −14 a
−19), porque o original passa ~700 primitivas pela GPU emulada e pelo pacote de texturas HD a cada desenho, e
o novo só manda um marcador de 1 palavra. Plugin na thread do jogo: ~22 µs por HEAT (skinning de 906 vértices
e resumo do quadro). Thread de render: 60–70 µs de CPU por desenho (normais, ordem de pintor, subida de
11 KB + 92 KB, duas chamadas instanciadas); o espelho da tela larga entrou em 260 de 1786 desenhos na sessão A.
O jogo desenha o HEAT 30 vezes por segundo (cada dançarino a 30 Hz, a tela a 60).

**Comparação e fallback:** `heat_mode` continua (0 original, 1 remake, 2 sobrepostos, 3 lado a lado, 4..6
cópias de carga); flags bit 2 = caminho por pacotes da fase 0 (o mesmo asset v3: os bancos de 15 bits são
gerados na hora), bit 3 = sem Phong, bit 4 = sem textura. `heat_light "<exposição> <principal> <preench.>
<contraluz> <ambiente> <rugosidade> <zbias>"` ajusta a luz ao vivo (padrão 1,0 1 1 1 1 0,5 0,01).
`heat_stats` mostra desenhos e µs de CPU na thread de render e os contadores do renderizador (desenhos,
espelhos na tela larga, marcadores sem nada visível).

## Fase 0 no jogo: pacotes GT3 (`src/bag2_heat.c`, hoje fallback)
**Hook.** `FUN_80029D4C` recebe em `$a0` o nó raiz de um personagem e desenha a árvore de ossos
(`FUN_80029C6C`: filho em +0x94, irmão em +0x98; +0x6C = MATRIX câmera×osso já pronta para o GTE, +0x8C =
MeshHeader, +0xA0 = tipo). O `game.toml` lista a função em `[recompiler] mod_function_entry_funcs`
(o recompilador põe a chamada do hook no começo dela; regenerar com `psxrecomp-game --config game.toml`,
só um shard muda). O plugin `bag2.heat_remake` (pacote `mods/preloaded/packages/bag2.remake.heat`, ligado
por padrão) é um *filtro*: quando a árvore é a do HEAT (nº de vértices e índice-base de normais de cada
osso batem com o `heat.bin`), desenha a malha nova e devolve 1, o que pula o desenho original. Qualquer
outra árvore segue o caminho original.

**Por quadro**, em C, no thread do jogo:
1. posição de cada vértice no espaço da câmera = matriz do osso dono lida da RAM (a mesma do GTE);
2. culling pelo mesmo NCLIP > 0 do jogo, normais por vértice;
3. Phong Tessellation com nível **adaptativo**: cada face é dividida só o necessário para a curva de Phong
   ficar a menos de 0,25 pixel do PS1 da corda (até 4 divisões por aresta; orçamento de 3300 triângulos por
   quadro), e o bastante para o UV de cada pedaço caber numa janela de 256 texels HD (o UV do GT3 tem 8 bits);
4. luz por vértice nova no espaço da câmera (principal quente, preenchimento frio, contraluz, um brilho e
   oclusão ambiente pré-calculada por vértice);
5. pacotes GT3 numa **arena de DMA do runtime** (`psx_mod_alloc_texture_packet_memory`, 0x80800000) com o
   **banco de textura** HD no byte alto de C1/C2 e o **sufixo de precisão** (x/y em float e 1/z):
   `docs/RETAINED-TERRAIN-EXPERIMENT.md` do PSXRecomp. As 12 camadas 1024×1024 viram 96 bancos de
   1024×512 (8 por camada, com início de linha 0/64/128/192/512/576/640/704, para que toda janela de 256
   alinhada a 64 caiba num banco), mais um banco branco para as faces só de cor;
6. ligação na **OT do próprio jogo** (`DAT_8006EE88`, OT reversa de 4097 entradas): cada triângulo herda o
   slot da face original (`(z0 + z1) >> 3`, como `FUN_80036230`) e, dentro do slot, a ordem de inserção do
   jogo (faces exportadas na ordem nó → lista na ordem de `FUN_80029588` → primitiva). Assim o HEAT
   continua ordenado contra o palco, o outro dançarino e os efeitos, e as camadas coplanares do modelo
   (decalques) ficam como no original. Face com textura sobre cor: base e textura no mesmo slot, a base
   desenhada antes; pedaços com textura toda opaca dispensam a base, pedaços sem texel dispensam a textura.

**Desempenho** (build-debug, partida de 2 personagens, RTX 3070 Ti; `tools/remake/game_perf.py`):

| | fps por segundo | thread do jogo | underruns de áudio |
|---|---|---|---|
| HEAT original | 59,9 (mín. 59,9) | 48 % | 0 |
| remake, 1ª versão, `render_thread = true` | 31 (mín. 27) | 82 % | 528 em 30 s |
| remake, 1ª versão, `render_thread = false` | 58,0 (mín. 49,7) | 96 % (máx. 100) | — |
| **remake atual, `render_thread = true`, 150 s** | **59,9 (mín. 59,9, nenhum segundo abaixo)** | **61 % (máx. 64)** | **0** |

Underrun é o áudio sem amostras na hora de tocar, o que se ouve como engasgo; com o jogo a 60 o tempo
emulado anda junto com o real e a música mantém o ritmo. O que mudou da 1ª versão para a atual:
1. **Patch local no PSXRecomp** (`tools/recomp/patches/psxrecomp-texture-bank-render-thread.patch`, aplicar
   depois do `psxrecomp-hd-cache-caps.patch`): a seleção de banco de textura passa a ser gravada no fluxo de
   comandos da thread de render (`RTH_BANK`) em vez de devolver o contexto GL e copiar a VRAM a cada pacote;
   a volta ao banco 0 depois de cada pacote só é gravada quando outro desenho vier; SEMI repetido não é
   gravado; o sufixo de precisão é lido direto da arena; pacote com sufixo pula as buscas do PGXP; nova
   função `psx_mod_write_gpu_dma` copia os pacotes para a arena de uma vez. Com isso `render_thread` volta a
   `true` (o jogo base também ganha: 48 % do thread do jogo contra 72 % sem a thread de render).
2. Plugin de 3,8 ms para 0,6 ms: cópia única para a arena, OT lida e escrita uma vez por slot, sem `lrintf`.
3. **Menos pacotes**: tolerância de Phong 0,25 pixel do PS1 (era 0,12) e orçamento de 3300 triângulos por
   desenho do HEAT; num close, em vez de multiplicar os pacotes, a tolerância sobe sozinha. Ficam ~3,4 mil
   pacotes por quadro. A GPU emulada gasta ~0,2 us por palavra de comando no DMA (10 palavras por pacote),
   e era isso que travava o thread do jogo.

**Carga de mais personagens** (modo de teste 4..6: o plugin desenha 1..3 cópias extras do HEAT remake ao
lado, para simular outros personagens com remake; mesmo método, 25 s cada):

| cópias do remake | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|
| 0 (original) | 59,9 | 48 % | 62 % | 0 |
| 1 | 59,9 | 60 % | ~50 % | 0 |
| 2 (dois jogadores) | 60,0, mín. 57, 3 s abaixo de 59,5 | 82 % (máx. 91) | 66 % (máx. 78) | 0 |
| 3 (dois jogadores + algo do tamanho de um palco) | 58,1, mín. 51 | 99 % | 84 % | 107 |
| 4 | 48,8 | 100 % | 81 % | 573 |

Com a ordem padrão do jogo dentro do slot (sem agrupar por banco) já 2 cópias caíam para 54 fps com a thread
de render em 95 %; por isso o agrupamento por banco virou o padrão (`s_flags = 2`). Conclusão: este caminho
(cada triângulo passa pelo DMA e pela GPU emulados, ~0,2 us por palavra) aguenta dois personagens no
limite, sem folga para um palco. Para mais, o PSXRecomp precisaria de uma submissão direta de malha ao
renderizador GL (a doc do próprio framework diz que ainda não existe) e de bancos num texture array.

**Comandos de depuração** (só na build-debug, TCP 4370): `mod_cmd heat_mode "<modo> <nível> <α> <tol>
<flags> <orçamento>"` com modo 0 original, 1 remake, 2 os dois sobrepostos, 3 os dois lado a lado (o remake ao lado do
original, no mesmo quadro); `heat_dump <arquivo>` despeja nós, matrizes e registradores do GTE.
`mod_counters` mostra `bag2_heat.*` (pacotes, filhos, microssegundos por etapa).

### Testes no jogo
`tools/remake/game_session.py` (abre a build-debug, passa a abertura e os menus por TCP e entra numa
partida com o HEAT), `game_sidebyside.py` (modo 3: ORIGINAL e REMAKE no mesmo quadro, mesma pose e luz do
palco; modo 7: ORIGINAL, FASE 0 e FASE 1 no mesmo quadro) e `game_compare.py` (alterna variantes; os quadros
ficam ~1 s um do outro). Capturas: `tools/recomp/logs/remake/jogo/tres_*.png` (modo 7, 27/10, asset
corrigido), `q4_1_pair.png` (modo 3, fase 1), `lado1_*.png`/`lado2_*.png` (fase 0, 26/10, com o bug dos
quads). Renders offline de 27/10 em `tools/recomp/logs/remake/heat/compare_*.png` (os de 26/10, com o bug,
em `antes_quad_fix/`). O modo 7 desenha três HEATs mais as capturas de 1 s: o fps cai durante ele (só
diagnóstico).

## O que funcionou
- Formato, esqueleto, texturas e animação do disco até o quadro animado.
- No jogo: posição, pose, culling e ordem batem com o original; o HEAT novo dança a partida inteira com
  palco, sombra do jogo, HUD e o outro dançarino intactos. Fase 1: 60 fps em todos os segundos, mais barato que
  o HEAT original nas duas threads, com folga para 4 HEATs.
- Arredondamento de braços, pernas, sapatos e rosto; texturas HD nítidas; luz com volume.

## Limites reais (honesto)
- **Suavizar não cria detalhe.** Dedos continuam uma luva de poucos polígonos, a orelha é um cone, o nariz é
  a textura. Real-ESRGAN deixa as bordas nítidas mas é a mesma arte 4bpp; o que é desenhado na textura
  (olhos, sobrancelhas, traços) continua pintado.
- A luz (por pixel desde a fase 1) é fixa em relação à câmera; não usa as luzes do palco (o original usa as
  matrizes de luz do jogo, `node+0x4C`), então não muda de cor com os efeitos do palco. Sem sombra própria
  (nem auto-sombra): fica a sombra simples do jogo. Os renders offline têm shadow map; o jogo ainda não.
- Composição: o HEAT entra inteiro num slot da OT (o da face visível mais próxima). Contra o palco e o HUD fica
  igual ao original nas capturas; se o outro dançarino se sobrepuser ao HEAT na mesma profundidade, as faces
  dele à frente desse slot cobrem o HEAT e as de trás ficam por baixo (visto com a cópia deslocada do modo 7,
  `tres_2.png`). Em partida normal os dois ficam lado a lado; não vi isso num quadro real. Oclusão por pixel
  pediria o `pgxp_depth_buffer` do framework (desligado no `game.toml`).
- Caminho GLES 3.0 do núcleo escrito, mas só testado em GL 3.3 core (NVIDIA). Rewind não testado (savestate sim).
- Mãos alternativas (nós 20–23, trocadas pelo jogo em alguns gestos) ficam de fora: nesses momentos o remake
  mostra a mão padrão.
- HEAT como jogador 2 usa outra paleta (container 88+2k); o `heat.bin` tem só a do jogador 1, então um HEAT
  P2 sairia com as cores do P1. Não testado.
- O hook também pega o HEAT de outras telas que usam `FUN_80029D4C` (seleção, resultado); não conferido.
- Só testado na build-debug (dirigida por TCP). A build normal (`build/`) compila com o mesmo código e o
  `heat.bin` v3 está nas duas; não joguei uma partida nela.

## Plano
Plano completo, fases e estado: `notes/remake-plano.md` (decidido em 2026-10-26).

## Esforço estimado para seguir
- **Release com só o HEAT**: empacotar `heat.bin` e o exe como `v1.4.0` (meia sessão), depois de jogar
  algumas partidas e decidir se fica `default_enabled`.
- **Melhorias do HEAT no jogo** (1–2 sessões): usar as matrizes de luz do jogo, paleta do P2, mãos
  alternativas.
- **Outros 17 personagens**: o pipeline é genérico (exporte por personagem + assinatura dos ossos);
  1–2 sessões para todos, mais ajustes nos que fugirem do esqueleto de 19 ossos ou usarem primitivas que o
  HEAT não usa (semitransparência ainda não implementada).
- **Palcos**: container e formato ainda não mapeados para este pipeline; várias sessões. O ganho visual
  esperado é menor (geometria reta não melhora com suavização; o caminho seria luz e pós-efeitos).
