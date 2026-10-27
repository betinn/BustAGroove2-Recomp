# Remake visual do HEAT (piloto offline 2026-10-25, prova de conceito no jogo 2026-10-26)

Pergunta: dá para modernizar o visual dos personagens **sem artista humano**, partindo das malhas
originais (suavizar, texturas HD, iluminação nova), mantendo a mesma animação? Feito só com o HEAT
(id 0, container `k = 3`), em duas etapas:
1. **Offline** (`tools/remake/heat_remake.py`): renders ORIGINAL x MODERNIZADO, turntable e GIF de dança.
2. **No jogo** (`tools/recomp/bag2/src/bag2_heat.c`): o exe troca o desenho do HEAT pela malha suavizada
   com texturas HD e luz nova, numa partida de verdade, a 60 fps estáveis (ver Desempenho). Os outros personagens e os palcos ficam
   originais.

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
- **Quads**: a ordem dos vértices na GPU é v0, v3, v1, v2; UV/normais/cores ficam em sequência v0..v3 e a
  diagonal é v1–v3. A leitura antiga (herdada do visualizador web) cruzava a diagonal: triângulos
  sobrepostos, buracos e texturas "em cacos" na calça, mangas, sapatos e óculos. O primeiro lote de renders
  do piloto (25/10) tinha esse defeito; os de 26/10 já estão certos.
- Slot de vértice compartilhado = nó × 256 + vértice local; índices de normal são globais no modelo.
- MeshHeader+0x98 = índice-base das normais (identifica cada osso também na RAM do jogo).
- HEAT: nós 20–23 são mãos alternativas, nós 24–37 um esqueleto de sombra; o jogo desenha só os nós 0–19.

Com os quads certos a malha do HEAT é **fechada e manifold** (2166 arestas, nenhuma aberta, nenhuma com
mais de duas faces). A conclusão do primeiro dia ("sopa de painéis abertos, Loop não funciona") estava
errada: vinha do bug dos quads. Hoje Loop e Phong dão resultados parecidos (`metodos_*.png`); o jogo usa
Phong porque é local por face e barato de recalcular a cada quadro.

## No jogo (`src/bag2_heat.c`)
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
palco) e `game_compare.py` (alterna variantes; os quadros ficam ~1 s um do outro). Capturas:
`tools/recomp/logs/remake/jogo/lado1_*.png` (Phong) e `lado2_*.png` (sem suavizar, para conferir a geometria).

## O que funcionou
- Formato, esqueleto, texturas e animação do disco até o quadro animado.
- No jogo: posição, pose, culling e ordem batem com o original; o HEAT novo dança a partida inteira com
  palco, sombra do jogo, HUD e o outro dançarino intactos, a 60 fps com o thread do jogo em ~61 %.
- Arredondamento de braços, pernas, sapatos e rosto; texturas HD nítidas; luz com volume.

## Limites reais (honesto)
- **Suavizar não cria detalhe.** Dedos continuam uma luva de poucos polígonos, a orelha é um cone, o nariz é
  a textura. Real-ESRGAN deixa as bordas nítidas mas é a mesma arte 4bpp; o que é desenhado na textura
  (olhos, sobrancelhas, traços) continua pintado.
- No jogo a luz é por vértice e fixa em relação à câmera; não usa as luzes do palco (o original usa as
  matrizes de luz do jogo, `node+0x4C`). Sem sombra projetada nova: fica a sombra simples do jogo.
- Pequenas diferenças de desenho nos decalques da calça e dos sapatos em relação ao original no jogo (o
  pacote de texturas HD do jogo e o filtro xBR tratam a mesma arte de outro jeito).
- Mãos alternativas (nós 20–23, trocadas pelo jogo em alguns gestos) ficam de fora: nesses momentos o remake
  mostra a mão padrão.
- HEAT como jogador 2 usa outra paleta (container 88+2k); o `heat.bin` tem só a do jogador 1, então um HEAT
  P2 sairia com as cores do P1. Não testado.
- O hook também pega o HEAT de outras telas que usam `FUN_80029D4C` (seleção, resultado); não conferido.
- Só testado na build-debug (dirigida por TCP). A build normal compila com o mesmo código.

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
