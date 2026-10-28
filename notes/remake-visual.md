# Remake visual dos personagens e do palco (piloto offline 2026-10-25, no jogo 2026-10-26, desenho de host, detalhe e palco 2026-10-27, o elenco 2026-10-28)

Pergunta: dá para modernizar o visual dos personagens **sem artista humano**, partindo das malhas
originais (suavizar, texturas HD, iluminação nova), mantendo a mesma animação? Feito só com o HEAT
(id 0, container `k = 3`), em etapas:
1. **Offline** (`tools/remake/heat_remake.py`): renders ORIGINAL x MODERNIZADO, turntable e GIF de dança.
2. **No jogo, fase 0** (`tools/recomp/bag2/src/bag2_heat.c`, caminho por pacotes): o exe troca o desenho do
   HEAT pela malha suavizada com texturas HD e luz por vértice nova, por pacotes GT3 na GPU emulada.
3. **No jogo, fase 1** (padrão): a malha vai direto ao renderizador GL por um callback de host, com shader
   próprio (Phong na GPU, luz por pixel), a 60 fps em todos os segundos e mais barato que o HEAT original.
   Os outros personagens e os palcos ficam originais.
4. **No jogo, fase 2**: mãos com dedos por gesto, orelhas, nariz, mapas de normal, luz do palco, sombra própria e a
   expressão do rosto do original (seção "Fase 2 no jogo"), ainda a 60 fps em todos os segundos.
5. **No jogo, fase 3** (palco): bloom e graduação depois da cena e antes do HUD, pulso no tempo da música e sombra
   macia do HEAT no chão (seção "Fase 3 no jogo").
6. **No jogo, fase 4** (os dois jogadores e o elenco): o mesmo pipeline para os outros personagens e os dois
   figurinos de cada um, com semitransparência (seção "Fase 4 no jogo").

## Como reproduzir
```
python -m venv tools/recomp/logs/remake/venv
tools/recomp/logs/remake/venv/Scripts/python -m pip install -r tools/remake/requirements.txt
tools/recomp/logs/remake/venv/Scripts/python tools/remake/heat_remake.py      # renders offline (~1 min, GPU)
tools/recomp/logs/remake/venv/Scripts/python tools/remake/export_heat.py      # pacote do jogo (heat.bin)
tools/recomp/logs/remake/venv/Scripts/python tools/remake/export_char.py COMET  # outro personagem (comet.bin);
                                                     # "COMET*" = figurino alternativo (comet_alt.bin)
python tools/remake/export_all.py                    # o elenco todo, nos dois figurinos (fase 4)
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

## Fase 2 no jogo: detalhe no HEAT (2026-10-27)
Seis peças, cada uma primeiro nos renders offline (`heat_remake.py`, `--only maos` e as variantes abaixo), depois
no jogo lado a lado e medida. Asset novo **v4** ("HTR4", `export_heat.py`): grupos de faces por gesto das mãos,
vértices exclusivos de cada grupo contíguos depois dos do corpo, bit 4 no modo da face (sem mapa de normal). Depois
**v5** ("HTR5"): página de textura de cada camada e a origem dela na VRAM (ver "Expressão do rosto"); o núcleo lê os
dois.
O quadro leva a máscara de grupos e só os vértices ativos (corpo + as duas mãos do gesto: ~1,4 mil posições,
~16 KB; o limite do desenho de host é 64 KB).

**Convenção de enrolamento (vale para toda geometria nova):** nas faces do arquivo do jogo, a normal pela regra da
mão direita (`cross(b - a, c - a)` no espaço do osso) aponta para **dentro** (corpo: 0 % das faces para fora;
é o NCLIP > 0 com o y da tela para baixo). A malha gerada sai com a normal para fora, então é invertida antes de
entrar no asset; sem isso o culling do jogo some com o lado de perto e mostra o de dentro (a primeira versão das
mãos no jogo aparecia girada 180° em torno do dedo, palma no lugar das costas). Os renders offline não fazem
culling e não mostram esse erro.

### Mãos com dedos (`tools/remake/hands.py`)
- **Como o jogo escolhe a mão:** o script da dança (interpretador de comandos no overlay do palco, função em
  ~0x801CF5xx, chamada de `0x800306A4`; comandos 901..903 e 1000+) religa o **irmão** do antebraço: na RAM a árvore
  de ossos é uma lista de irmãos, e o nó 15 (+0x98) passa a apontar para o nó 16 (aberta), 20 (punho) ou 21
  (apontando); na direita o nó 18 para 19/22/23. O gesto também fica em `estado + 2026`. Achado com o write trace
  do servidor de debug (`wtrace_arm` em `nó15 + 0x98`): escritas em `0x801CF6A0` (volta ao 16) e `0x801CF710`
  (liga o 20), endereço do nó = base + índice × 0xB0. O ponteiro do mesh (+0x8C) do nó 16 nunca muda. O jogo
  calcula a matriz do nó ligado normalmente (o punho original sai certo quando o gesto é forçado).
- Antes da fase 2, nesses quadros a árvore não batia com a assinatura e o **HEAT original** era desenhado.
  Agora `heat_groups` reconhece o gesto pelo MeshHeader do nó nas posições 16 e 19 da árvore e liga o grupo.
- Mão nova por gesto: SDF de palma (caixa arredondada), 4 dedos de 3 falanges e polegar (cones arredondados com
  mínimo suave), ângulos por gesto, no espaço do osso da mão do jogo (dedos para -y, costas para +x, polegar da
  esquerda para +z; a direita é o espelho em z). Surface nets (célula 95) + projeção na superfície + Laplaciano
  leve, decimação QEM até ~720 faces (~360 vértices), cache em `logs/remake/heat/maos/`.
- Textura: a mesma página da luva (camada 11). O UV vem de uma função linear por partes de (y, z) tirada das costas
  da luva original aberta (faces com |normal x| > 0,3; dentro da projeção vale a de maior x), avaliada na pose
  aberta de cada ponto (o ponto vai para a pose aberta pela falange mais próxima), então a placa branca com os nós
  pretos fica nas costas e cada linha branca fica no seu dedo, também com o punho fechado. Palma e polegar:
  vermelho liso (mediana da textura). Um primeiro ajuste por plano único (mínimos quadrados) errava a placa em
  ~10 texels. O punho cinza original de cada gesto (22 faces de cor com os 6 vértices do pulso) continua.
- Comando novo `heat_force "<esq> <dir>"` (0 aberta, 1 punho, 2 apontando, -1 = o do jogo): faz o que o script
  faz (liga o nó do gesto como irmão do antebraço) para testar; `tools/remake/game_hands.py` força cada gesto e
  recorta as mãos do original e do remake no mesmo quadro (heat_mode devolve `maos=` e `shift=`). Contadores
  `bag2_heat.grupo1..6` contam quadros por gesto.

### Orelhas (`tools/remake/ears.py`)
A orelha original é uma placa de 16 faces de pele (cor 230,156,102) no osso 13, com a borda de trás soldada ao
cabelo quase até a ponta (8 dos 13 vértices são compartilhados com o resto da cabeça), não um cone com anel de
base: só "encolher a orelha velha" deixava a placa inteira atrás da nova. Agora: os 5 vértices só dela vão para o
plano da placa (PCA), e a orelha nova cobre a placa toda, do pé até a ponta original (×1,08): folha pontuda com
hélice levantada (mais forte na borda de trás), concha funda escurecida pela cor do vértice, costas
arredondadas, raiz enterrada 260 unidades na cabeça, ponta afastada 220. ~280 faces por orelha.

### Nariz (`ears.attach_nose`)
O rosto do HEAT é côncavo: queixo (z 3818) e testa (z 3772) ficam à frente da ponta do nariz da malha (z 3639),
e com a suavização o nariz quase some. Nariz novo: cone arredondado da ponte (y 1620, enterrada) até a ponta
(y 1120, z 3640), raios 60..110, ~160 faces; pele, camada e cores copiadas da face do rosto que fica embaixo
(projeção em z), sem mapa de normal como o resto do rosto. Duas tentativas erraram a altura (o eixo dos olhos do
osso da cabeça fica em y ~1900; a primeira pôs o nariz na testa, a segunda comprido demais entre os olhos).

### Mapas de normal (`tools/remake/detail.py` e `rmk_asset_build_normals`)
Gerados das camadas HD, a mesma conta em Python (offline) e em C (no jogo, na ativação; diferença máxima de 1 LSB):
altura = 1,0 × estampa (saturação > 0,25, caixa de raio 3 duas vezes) − 0,8 × traço preto (luminância < 0,3,
raio 2) + 1,2 × luminância (raio 8, o sombreado pintado das dobras); normal por diferenças centrais com k = 16.
Shader: base tangente pelas derivadas da tela (Schüler, sem tangente por vértice; funciona com UV espelhado),
segunda texture array na unidade 10. O rosto pintado (faces de cor com desenho por cima no osso 13) fica sem
relevo: com relevo o traço dos olhos parecia gravado. Primeira tentativa (só o traço, desfoque de 2 texels, k = 6)
quase não aparecia; o chanfro precisa de 6–16 texels HD para ser visto na tela. Custo: 0,9 s de CPU na ativação
(antes do jogo rodar) e 50 MB de RAM/VRAM a mais; sem custo medível por quadro. Flag 32 desliga.

### Luzes do palco
O original é iluminado pelo GTE: matriz de luz de cada osso em `nó + 0x4C` (linhas = direção de cada luz no espaço
do osso, 4.12), cores na LCM (`gte_ctrl` 16..20, coluna j = cor da luz j, pode ser negativa) e ambiente BK (13..15).
Medido no Stage 1 do HEAT (constante em 60 s): luz 1 quase branca (0,75; 0,71; 0,75) da frente-direita-cima, luz 2
= −luz 1 com cor **negativa** (−0,56; −0,59; −0,52), que escurece o lado de trás, luz 3 laranja fraca (0,38; 0,31;
0,06) de trás-esquerda, BK (0,55; 0,53; 0,59). Direção na câmera = MATRIX câmera×osso (+0x6C) × linha de L; todos
os ossos dão a mesma (conferido). A luz fixa da fase 1 vinha de cima-esquerda, do lado oposto ao do palco.
Agora o plugin lê L e a MATRIX do nó 0 e LCM/BK na entrada de `FUN_80029D4C`, o núcleo (`rmk_stage_light`) leva
para a câmera e o shader faz a soma do GTE por pixel, em gama como no PS1: `I = BK·AO + Σ cor·max(n·l, 0)` (wrap
0,2 nas luzes positivas), `cor = albedo·I^2,2 + especular` das luzes positivas e contraluz do lado oposto à luz
principal com a cor dela. `heat_light` padrão agora 1,0 / principal 1,2 / ambiente 0,8 (mais contraste para a
sombra e o relevo aparecerem); a resposta mostra as luzes do palco. Flag 64 volta à luz fixa. Offline:
`render.STAGE_LIGHT` (os valores medidos) com `render_set(..., stage=...)`, e o ORIGINAL com a soma do GTE por
vértice.

### Sombra própria
Shadow map no núcleo, dentro do callback: textura de profundidade 1024² própria com comparação (PCF bilinear do
hardware, 5 amostras), projeção ortográfica da luz principal do palco (maior cor positiva) em volta da caixa dos
vértices ativos, passo de profundidade com as duas faces e deslocamento de polígono (1,5; 3), antes dos dois passos
de sempre; o FBO e o viewport de quem chama são religados depois (`RmkTarget.fbo`). Refeita só quando o quadro
muda (o espelho da tela larga reaproveita). Escurece só a luz principal. Com a luz frontal do Stage 1 a sombra é
discreta (cabelo atrás da cabeça, pescoço sob o queixo, coxa sob a mão); com ambiente baixo (`heat_light` amb
0,1) fica forte. Flag 128 desliga. A sombra do jogo no chão continua.

### Experimento de IA imagem→3D (Hunyuan3D-2mini): não entra no jogo
Autorizado pelo usuário em 2026-10-27. Modelo `tencent/Hunyuan3D-2mini`, subpasta `hunyuan3d-dit-v2-mini`
(Hugging Face; `model.fp16.safetensors` de 3,8 GB; o repositório trouxe também uma cópia `.ckpt` de 3,8 GB em
pickle, apagada), código oficial `Tencent-Hunyuan/Hunyuan3D-2` (GitHub), licença Tencent Hunyuan Community (uso
não comercial; não vale na UE, Reino Unido e Coreia do Sul). Tudo em `tools/recomp/logs/remake/ai/` (fora do git):
venv próprio com PyTorch 2.11 + CUDA 12.8, `transformers<5` (a série 5 renomeou as camadas do DINOv2 e o
checkpoint não carrega) e `diffusers<0.36` (compatível com o `huggingface_hub` dessa série).
Fluxo (scripts em `tools/remake/`):
1. `ai_input.py`: o HEAT em pose em A (braços a 450/4096 de volta dos nós 14 e 17; na pose de descanso os braços
   colam no corpo), texturas HD, fundo transparente, 4 vistas 1024².
2. `ai_mesh.py` (venv da IA): só a vista de frente (o mini é de uma imagem). Octree 256, 50 passos: 75 mil
   vértices, 150 mil faces, fechada, 20 s de geração, 4,3 GB de VRAM.
3. `ai_decimate.py` (pymeshlab, quádricas) para 12 mil faces; `ai_fit.py`: escala/centro pela caixa, orientação
   por Chamfer, ICP de similaridade; erro final 2,3 % da altura.
4. `ai_hybrid.py` (`Character(ai_body=...)`): roupa da IA (quadril, tronco, braços, pernas; 7,3 mil faces) e o
   resto como está; vértice rígido no osso do vértice original mais próximo (o esquema do PS1), faces só entre
   ossos vizinhos, textura da face original mais próxima.
Resultado (`logs/remake/ai/malha_frente_256_compara.png`, `..._hibrido.png`, `hibrido_zoom.png`): a forma da IA
tem dobras de pano bonitas (punhos, barra do casaco, gola, barra da calça), mas **perde a identidade** (cabeça
genérica sem os óculos, sem a peça grande das costas, sapato de caixa, luva fechada) e o híbrido **piora a roupa**
na dança: a estampa sai picada (cada face pega uma face original diferente e as chamas se partem), há buracos e
bordas serrilhadas onde as faces entre ossos não vizinhos saem (sem isso, membranas esticadas entre as pernas e um
fio entre os pés). O remake atual (malha original suavizada + mapas de normal) fica com a estampa limpa. Não foi
para o jogo nem foi medido. Para valer a pena faltaria: modelo de várias vistas (`tencent/Hunyuan3D-2mv`, outro
download), textura assada num atlas novo (UV de 16 bits no asset; hoje o UV é u8 por página de 256) e pesos de
pele suaves nas juntas (o núcleo só tem um osso por vértice).

### Expressão do rosto (`tools/remake/faces.py`, asset v5)
O original troca de expressão (dentes cerrados, piscada, olhos semicerrados e fechados) e até aqui o remake ficava
sempre com o rosto do disco. Como o jogo faz, medido na build-debug:
- **Não é RAM**: 60 s amostrando a cada ~25 ms o MeshHeader do nó 13 e as listas de primitivas (1 versão só) e os
  dois buffers de pacotes do nó (nó +0xA8/+0xAC, tamanho em +0xA4, montados uma vez e com só xy/cor mudando a cada
  quadro): UV, CLUT e tpage nunca mudam.
- **É VRAM**: a página do rosto (TIM 1, na VRAM em 768,256, tpage 0x1C) é um atlas. Além das **casas** que as faces
  da cabeça amostram, tem as variantes ao lado: olho A (casa 0,32, 48×24 texels) e olho B (casa 0,92, 48×28) com
  aberto/semicerrado/fechado em x 48/96/144; boca (casa 8,148, 40×20) fechada em 48,52 e com dentes em 88,52. O
  código do personagem chama **MoveImage** (libgpu, `0x800482F4`; `LoadImage` é `0x80048234`, nomes pelas strings de
  depuração da libgpu em `0x80011600..`) e copia a variante para a casa. A cópia sai na hora (fila da libgpu com a
  GPU ociosa, pacote avulso 0x04FFFFFF, fora da OT); no anel GP0 ela aparece depois dos polígonos do rosto de um
  quadro e antes do próximo. A CLUT e a página 704,256 não mudam. A piscada é aberto → semicerrado → fechado →
  semicerrado → aberto, e cada olho pode trocar sozinho (piscada de um olho).
- Ferramentas da investigação: `read_ram` dos ponteiros do MeshHeader (`+0x04` vértices, `+0x08..+0x10` normais,
  refs e uses, `+0x14 + 4k` lista k, de `FUN_80021E24`), `vram_peek` da página, `gpu_frame_dump` (opcode 0x80 com o
  campo `bld` = pilha de retorno), disassembly (capstone) do exe e do dump de RAM.

O remake **não lê a VRAM** (ler a VRAM no GL força a descida do FBO, caro). Três peças:
1. **Hooks** em MoveImage e LoadImage (`[recompiler] mod_function_entry_funcs` do `game.toml`; regenerar com
   `psxrecomp-game --config game.toml`, só o shard 18 muda). O plugin mantém a tabela de casas remapeadas: cópia
   com origem e destino na mesma página vira (página, casa em texels, deslocamento até a origem); origem dentro de
   outra casa remapeada compõe o deslocamento; cópia de fora da página, `LoadImage` por cima ou savestate carregado
   tiram a casa da tabela (volta ao conteúdo do disco). Contadores `bag2_heat.face_moves`, `face_move_after`,
   `face_move_before`, `face_move_unknown`.
2. **Quadro**: o `RmkFrame` ("RMK3") leva a tabela (até 8 casas) no fim. Medido: **todas** as trocas acontecem
   depois de o HEAT ligar o marcador do quadro e antes de a GPU chegar nele (`face_move_after` 133 de 133), então o
   plugin regrava o bloco da tabela nos marcadores do HEAT já ligados na metade atual da arena (marcador já
   desenhado: a escrita não faz nada, o payload foi copiado por valor).
3. **Núcleo**: asset v5 ("HTR5") com a página (TIM) de cada camada e a origem dela na VRAM (`faces.PAGE_VRAM`,
   medida para o HEAT jogador 1); o modo da instância leva a página (`modo | página << 3`). No fragment shader, UV
   dentro de uma casa da mesma página soma o deslocamento e lê a variante na camada HD (o Real-ESRGAN ampliou o
   atlas inteiro); `textureGrad` com as derivadas do UV sem remapear (o salto na borda da casa não muda o mip) e
   recuo de meio texel HD na borda da origem (sem sangrar a variante vizinha). Também no mapa de normal. Remapear
   por face não serviria: das 61 primitivas da cabeça que tocam uma casa, 38 atravessam a borda dela.

Offline primeiro: `heat_remake.py --only rosto` grava `expressoes.png` (ORIGINAL com a casa copiada nos índices da
página, como o jogo faz na VRAM, × REMAKE com o UV remapeado): disco, olhos abertos/semicerrados/fechados, boca
fechada e dentes, todos iguais. No jogo: `tools/remake/game_face.py` (modo 3) lê `heat_face` (tabela, posição da
cabeça na tela) a cada ~15 ms e captura o rosto do ORIGINAL e do REMAKE no mesmo quadro assim que aparece um
estado novo (`rosto2_*.png`, 16 capturas): a mesma expressão nos dois em todas, inclusive nas transições (quando
a tabela já trocou para o quadro seguinte, os dois ainda mostram o estado anterior, juntos).

Desempenho (build-debug, `game_perf.py`, 60 s por janela, 2 sessões com a ordem trocada):

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| A / 1ª | remake com expressão | 59,9 (mín. 59,9) | 39 % (máx. 41) | 36 % (máx. 41) | 0 |
| A / 2ª | HEAT original | 59,9 (mín. 59,9) | 46 % (máx. 48) | 49 % (máx. 60) | 0 |
| B / 1ª | HEAT original | 59,9 (mín. 59,9) | 47 % (máx. 49) | 54 % (máx. 62) | 0 |
| B / 2ª | remake com expressão | 59,9 (mín. 59,9) | 39 % (máx. 41) | 32 % (máx. 41) | 0 |

Plugin 56–57 µs por HEAT na thread do jogo (igual à fase 2); thread de render 147–148 µs de CPU por desenho
(fase 2: 109–168). Limites: a origem da página na VRAM é a do HEAT jogador 1 (constante medida); o caminho por
pacotes da fase 0 não acompanha a expressão.

### Comparação A/B no mesmo quadro
`heat_mode` ganhou um 7º número: nos modos 4..6 as cópias usam `flags ^ isto`. `game_sidebyside.py` com modo 4
recorta o remake e a cópia (ex.: `"4 0.6 0.25 2 3300 128"` = com e sem sombra). `HEAT_LIGHT="..."` no ambiente
aplica um `heat_light` antes das capturas.

### Medições da fase 2
Build-debug, RTX 3070 Ti, 9x + 16:9, `game_perf.py`, 60 s por janela, 2 janelas por partida (ordem trocada entre
sessões). Por peça (cada uma com as anteriores):

| peça | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|
| mãos (mesma sessão: original 59,9, jogo 47 %, render 53 %) | 59,9 (mín. 59,9) | 39 % (máx. 41) | 33 % (máx. 39) | 0 |
| + orelhas | 59,9 (mín. 59,9) | 44 % (máx. 47) | 31 % (máx. 46) | 0 |
| + mapas de normal e nariz | 59,9 (mín. 59,9) | 44 % (máx. 47) | 29 % (máx. 42) | 0 |
| (a mesma sem mapas de normal, flag 32) | 59,9 (mín. 59,8) | 39 % (máx. 45) | 32 % (máx. 51) | 0 |
| + luzes do palco | 59,9 (mín. 59,9) | 39 % (máx. 40) | 34 % (máx. 43) | 0 |

Final, todas as peças (com a sombra):

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| A / 1ª | HEAT original | 59,9 (mín. 59,9) | 53 % (máx. 57) | 16 % (máx. 25) | 0 |
| A / 2ª | remake fase 2 | 59,9 (mín. 59,9; nenhum abaixo de 59,5) | 40 % (máx. 46) | 21 % (máx. 40) | 0 |
| B / 1ª | remake fase 2 | 59,9 (mín. 59,9) | 43 % (máx. 46) | 44 % (máx. 48) | 0 |
| B / 2ª | HEAT original | 59,9 (mín. 59,7) | 46 % (máx. 53) | 48 % (máx. 69) | 0 |
| C / 1ª | modo 6: 4 HEATs da fase 2 | 59,9 (mín. 59,9) | 40 % (máx. 42) | 35 % (máx. 42) | 0 |
| C / 2ª | HEAT original | 59,9 (mín. 59,9) | 47 % (máx. 48) | 50 % (máx. 61) | 0 |

Critérios: thread do jogo abaixo do original em todas as sessões (pedido: até +3 pontos); render +5 pontos na
sessão A (21 contra 16 %, num trecho em que o original ficou bem abaixo das outras medições) e −4 na B (pedido:
até +10); modo 6 a 59,9 em todos os segundos. Plugin na thread do jogo: 57 µs por HEAT (fase 1: 22 µs; o skinning
agora tem ~1,4 mil vértices ativos e lê as luzes). Thread de render: 109–168 µs de CPU por desenho (fase 1: 60–70 µs),
133 µs no modo 6.

### Capturas
Offline (`tools/recomp/logs/remake/heat/`): `maos_{aberta,punho,aponta}.png`, `orelhas_close.png`,
`normal_{corpo,rosto,perna}.png`, `luz_palco.png`, `expressoes.png`. Jogo (`tools/recomp/logs/remake/jogo/`):
`maos_{0,1}_{gesto}.png` (close das mãos, original x remake), `orelhas_*.png`, `nmap_*.png`, `luz_*.png` (modo 3),
`ab_sombra*_*.png` (modo 4, com e sem sombra), `fimA..C.log` (medição final), `rosto2_*.png` e `rosto2_grade.png`
(expressão do rosto, original x remake no mesmo quadro), `rostoA.log`/`rostoB.log` (medição com a expressão).

### Limites e o que não deu (fase 2)
- (Resolvido depois, mesma data: **expressão do rosto**. A hipótese desta lista, UV/CLUT mudados na RAM, estava
  errada: é cópia de VRAM com MoveImage dentro da página do rosto; ver "Expressão do rosto".)
- Orelhas: de frente abrem um pouco mais que as originais (mesmo com a ponta a 220).
- Sombra própria discreta sob a luz frontal do palco; só a luz principal faz sombra.
- As luzes saem do nó 0; se um palco mudar LCM/BK com os efeitos, o remake acompanha, mas só medi o Stage 1.
- Mapas de normal: 0,9 s de CPU na ativação; para o Polygonia (CPU mais lenta) vale gravar os mapas no asset.
- Experimento de IA imagem→3D feito offline (Hunyuan3D-2mini): resultado pior que o remake atual, não entra no jogo
  (ver a seção do experimento).

## Fase 3 no jogo: palco (2026-10-27)
Formato do palco, OT, sombra do jogo e variáveis da música em `notes/formats.md` ("Palco em partida"). Três peças
feitas (pós-efeito, pulso, sombra no chão); geometria do palco e dumps HD dos outros palcos não (ver "Limites").

### Pós-efeito no meio do quadro (`src/remake/rmk_post.c`, adaptador `src/bag2_stage.c`)
- **Por que não o `.pfx` do framework**: a máscara 3D dele (stencil bit 1) só marca primitivas "provadas" pelo PGXP e
  as aditivas; neste jogo quase nada do 3D sai provado (captura com `postfx/debug_mask.pfx`: só as linhas aditivas
  do laser ficaram marcadas, chão, cenário e KITTY-N não). E marcar tudo custaria um passo de stencil extra por
  lote. Sem máscara, o bloom e a graduação pegariam o HUD.
- **Como ficou**: a cada quadro de partida o plugin `bag2.stage_remake` (pacote `bag2.remake.stage`, ligado por
  padrão) liga um pedido de desenho de host (id 2) no **slot 96** da OT, o último da cena: o HUD está numa OT de 4
  entradas pendurada na entrada 0 (slot 95, que o jogo sobrescreve; o primeiro teste no 95 sumia) e sai depois, sem
  passar pelo efeito. Menus e FMV não têm marcador (partida = `0x8008A2C8` != 0; 0 marcadores antes da partida).
  Gancho: entrada de `FUN_8002BEB4`, um marcador por quadro do jogo (`0x8006EE78`).
- **Callback** (thread de render, núcleo portátil GL 3.3/GLES 3.0, sem extensão): cópia do retângulo para uma
  textura, brilho em 1/4 (média 4x4, rampa no máximo do RGB entre `thr` e `thr + joelho`, peso pela saturação para
  pele, roupa clara e chão branco não virarem névoa), descidas até 1/64 e subidas somadas (filtro duplo, halo largo e
  liso), **adaptação** (o nível 1/64 diz quanto brilho há em volta: telão e chão claros atenuam, neon fino em fundo
  escuro fica inteiro) e composição: exposição, vibração, curva S, sombras frias e realces quentes, vinheta, bloom por
  "screen", alfa (bit 15 do PS1) intacto. Alvos RGBA8 e RGB10_A2.
- **Tela larga**: o centro 4:3 só chega à superfície larga na apresentação (`blit_wide_center_from_canonical`).
  No passo do espelho o callback monta a cena com o centro da superfície canônica e as laterais da larga, e a
  composição escreve o centro de volta na canônica e as laterais na larga (`rmk_post_apply_wide`; o halo atravessa
  a borda do 4:3 sem corte). A 1ª versão emendava o centro na larga, processava a faixa e devolvia o centro: duas
  cópias de resolução cheia a mais, 1,06–1,22 ms de GPU por chamada contra 0,86–0,93 ms agora (flag 32 do
  `stage_post` volta ao caminho antigo para A/B). Em 4:3 processa no passo 0. A área de desenho do jogo tem
  **512 px** de largura (não 320): na 9x a faixa larga é 6138×2160 (13 Mpx) e o centro 4608×2160.
- Comando `stage_post "<on> <flags> <bloom> <limiar> <joelho> <peso sem cor> <pulso> <pulso exp.> <exposição>
  <curva> <saturação> <vinheta> <atraso> <frio> <quente> <adaptação>"` (flags: 2 sem bloom, 4 sem graduação,
  8 **metade direita** = A/B no mesmo quadro, 16 mostra o brilho); `stage_stats`. Padrão `3 0,55 0,3 0,1 0,6 0,05 1
  0,25 1,15 0,25 4 1 1 1`. Afinação: bloom 5 / limiar 0,42 estourava o letreiro em branco e acendia o guarda-sol;
  sem o peso pela saturação os personagens viravam névoa; sem a adaptação o telão e o tapete do estúdio estouravam.

### Pulso no tempo da música
- Fase da batida = `(tick - atraso × ticks por quadro) mod 256 / 256`, tick em `0x8005FBB4` (256 por batida),
  ticks por quadro em média móvel; sem música (tick parado 10 quadros) sem pulso. Envelope `exp(-7 fase)`, 1 do
  compasso mais forte (0,6 nos outros). Vai no quadro e vira ganho do bloom (+60 %) e da exposição (+5 %); a luz do
  remake do HEAT usa o mesmo envelope (principal +35 %, contraluz +52 %, `heat_floor` 3º número, flag 512 desliga).
- **Atraso do som**: medido com o áudio do SPU (`audio_wav` por índice de amostra; o anel guarda ~95 s e
  `start < 0` devolve o começo do anel, não o fim) contra o tick do mesmo quadro. Pelo pico de energia (caixa e
  chimbal) a média sincronizada dava +7 quadros; pela fase da fundamental do ataque dos transientes (4 amostras por
  quadro) dá +1 a +2 quadros na música do estúdio (começo e meio) e +3 a +8 na outra; no começo da música o
  primeiro som forte sai +17 quadros depois do tick 0 (o primeiro tempo pode não ser o 1). Escolhido **4 quadros**
  (erro máximo ~3 quadros, ~50 ms). Um primeiro código somava o atraso em vez de subtrair (o pulso vinha antes do
  tick); `tools/remake/stage_pulse_check.py` mostrou e mede: pico do envelope 7 quadros depois da virada do tick
  com atraso 7, como projetado, e correlação com os transientes do som com pico em -4 quadros naquela música, o que
  levou ao atraso 4.

### Sombra macia do HEAT no chão (`rmk_gl_draw_floor`)
- O plugin pula `FUN_8002A46C` para a raiz de sombra do HEAT (nó do mesmo vetor de nós) e, nesse mesmo momento,
  liga um segundo pedido (flag `RMK_FLAG_FLOOR`) no slot 4167, o da sombra original, com a pose do quadro (a sombra é
  chamada depois do corpo em 904 de 904 quadros). O outro dançarino fica com a sombra do jogo.
- Chão: plano y = 0 do mundo pela câmera `0x8005E4D0` (normal = 2ª coluna da rotação, ponto = translação),
  virado para o quadril; conferido: pé a −5..−11 unidades do plano, quadril a ~545. Direção: a luz da sombra do jogo
  (ponteiro em `0x80060CB8`; a 1ª versão leu o ponteiro como vetor e a sombra saía espelhada).
- Núcleo: mapa de sombra 1024² próprio, ortográfico nessa direção em volta do personagem; quadrilátero no chão
  cobrindo a projeção dos vértices; fragment shader PCSS (16 amostras de busca de bloqueador na textura crua e 16 de
  PCF na mesma textura com outro objeto de amostragem, `glBindSampler`, GL 3.3/GLES 3.0), penumbra proporcional à
  distância bloqueador-chão (nítida no pé, macia na cabeça), borda do mapa suave, `cor × (1 − a)` sem alfa.
  Padrão `heat_floor "0,85 0,4"` (opacidade, maciez); 0,6/1,0 deixava braços e cabeça sumirem no borrão.
  Flag 256 do `heat_mode` volta à sombra do jogo. Geometria e ordem do quadro são feitas uma vez e servem à sombra
  no chão, ao corpo e ao espelho da tela larga (`frame_setup`).

### Regressão (`tools/remake/stage_check.py`)
- Abertura (FMV), título, menu e seleção: 0 marcadores do palco, captura 1920×1080 (tela cheia) igual ao original.
- Partida, fim da música e "CONTINUE?": efeito na cena, HUD e textos por cima intactos. De volta ao título: o
  contador para (0 marcadores novos). A **demonstração do attract** (partida automática KITTY-N × COMET no Sushi
  Planet) recebe o efeito: é uma partida para o jogo (`0x8008A2C8` != 0). Capturas `chk_*.png`, `chk_grade.png`,
  `neon_final_ab.png` (Sushi Planet com os parâmetros finais, original × fase 3, ~0,1 s entre as duas).
- Build normal (`build/`, sem TCP): sobe com o pacote HD ligado (477 substituições) e sem erro no log; não foi
  conferida por tela nesta sessão (sem acesso de computer-use ao exe).

### Custo do palco hoje (antes do remake dele)
- Thread do jogo: `dma_hold_stats` em 10 s de partida: 356 caminhadas da OT (o jogo monta a OT a 30 Hz), 2,9 ms
  e 13,4 mil palavras GP0 por caminhada (~0,22 µs por palavra), 10 % da thread do jogo. Divisão das palavras por
  origem (3 quadros, `gpu_frame_dump`): outro dançarino 37–39 %, adereços 3D 29 %, sombras 15–17 %, fundo/chão 7 %,
  outros 5–6 %, HUD 3 %, céu/letreiros 2 %. O palco (adereços + fundo + céu + outros) fica em ~43 % das palavras:
  ~1,2 ms por OT, ~4 % da thread do jogo.
- Thread de render: não separável por origem com as ferramentas atuais; pela mesma divisão de primitivas o palco é
  ~45 % do que a GPU emulada desenha.

### Geometria e luz do palco pelo caminho novo: decisão (peça 4)
Não feita, com base nos dados acima:
- Ganho: ~45 % do palco são planos grandes de fundo, chão e céu (FT4 com textura já HD; luz por pixel e Phong não
  mudam um plano texturizado), o resto são adereços pequenos de cor lisa (G3/G4) longe da câmera. O que mais muda o
  visual (halo dos neons, cor, sombra macia, pulso) já está nas peças 1–3.
- Custo de fazer: o formato de malha do overlay do palco (containers 231..245) não está mapeado e seria um por
  palco; os adereços não passam por `FUN_80029D4C`. Custo de rodar não é problema (o palco é ~4 % da thread do jogo).
- O que valeria tentar depois (fase 4 ou 6): reflexo dos dançarinos no chão escuro do Sushi Planet pelo caminho do
  remake (precisa dos dois dançarinos pelo pipeline, senão só o HEAT refletiria).

### Pacote HD na build-debug e remake do HEAT com o pacote HD
- A junção `build-debug/mods/texture-packs/SLUS-01159/replacements` apontava para a pasta antiga `BustAGroove2Web`
  (renomeada em 2026-10-24): o log de 103 execuções da build-debug tem "HD textures: filesystem error: cannot create
  directories: File exists", ou seja, **o pacote HD estava desligado na build-debug** (palco e outro dançarino com as
  texturas originais) em todas as medições e capturas desde então (fases 1–3). Refeita para
  `build/mods/texture-packs/SLUS-01159/replacements` em 2026-10-27.
- Com o pacote HD ligado, o renderizador assume a VRAM nativa (`s_hd_native_authority`) e
  `psx_mod_texture_banks_supported()` devolve 0. `use_host_path()` exigia os bancos e **o remake do HEAT se
  desligava sozinho na configuração de jogo** (a `build/` normal). Corrigido: o desenho de host não usa bancos, só o
  caminho por pacotes da fase 0 precisa deles.
- Com o pacote HD, a thread de render subia +10/+11 pontos com o pós-efeito. O retângulo do marcador do pós-efeito
  era a tela inteira (−256..768 × 0..240), e o renderizador marca esse retângulo como sujo na VRAM; com o HD isso
  pesa. O callback usa a área de desenho do alvo, não o retângulo, então o marcador passou a 1 linha (y 0..1).
  Números na tabela abaixo.

### Dumps HD dos palcos (peça 5, `tools/remake/stage_dumps.py`)
- O cartão de memória guarda o último personagem escolhido e o cursor da seleção começa nele;
  `BAG2_SELECT` (U/D/L/R) em `game_session.py` move o cursor antes do X. `stage_dumps.py "R|RR|RRR|D"` entra em
  quatro partidas com outros personagens (o Stage 1 muda por personagem), liga `hd_textures dump=1` ao vivo e sai
  pelo `quit` (sem matar o processo antes, senão o último lote se perde): 89 + 2 + 3 + 64 = 158 dumps.
  `tools/hd_textures.py --filter esrgan` (incremental): 14 nomes novos, 9 texturas novas ampliadas (o resto já
  estava no pacote como recorte de outra). O cartão do usuário ficou com outro personagem escolhido no fim dos dumps
  e foi devolvido ao HEAT pela própria seleção (`BAG2_SELECT`).

### Desempenho (fase 3)
Build-debug, RTX 3070 Ti, 9x + 16:9, `game_perf.py` (variante = `heat_mode;stage_post`), 60 s por janela, 2 janelas
por partida, ordem trocada entre sessões. Base = estado do 78a887f (remake do HEAT, palco original): `"1 4 0.6 0.25
258;0"`. Tudo = `"1 4 0.6 0.25 2;1 0"`.

**Com o pacote HD** (configuração de jogo; logs `perf_hdA..I.txt`). hdA..C antes de reduzir o retângulo do
marcador, hdD..F depois, hdI com o caminho novo da tela larga:

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| hdA / 1ª | base | 59,9 (mín. 59,9) | 40 % (máx. 42) | 33 % (máx. 45) | 0 |
| hdA / 2ª | tudo | 59,9 (mín. 59,9) | 40 % (máx. 41) | 43 % (máx. 53) | 0 |
| hdB / 1ª | tudo | 59,9 (mín. 59,9) | 39 % (máx. 41) | 43 % (máx. 53) | 0 |
| hdB / 2ª | base | 59,9 (mín. 59,9) | 41 % (máx. 47) | 32 % (máx. 42) | 0 |
| hdC / 1ª | modo 6 (4 HEATs) + tudo | 59,9 (mín. 59,9) | 40 % (máx. 42) | 47 % (máx. 51) | 0 |
| hdC / 2ª | base | 59,9 (mín. 59,9) | 40 % (máx. 42) | 31 % (máx. 45) | 0 |
| hdD / 1ª | base | 59,9 (mín. 59,9) | 40 % (máx. 42) | 35 % (máx. 51) | 0 |
| hdD / 2ª | tudo | 59,9 (mín. 59,9) | 39 % (máx. 41) | 42 % (máx. 52) | 0 |
| hdE / 1ª | tudo | 59,9 (mín. 59,9) | 40 % (máx. 41) | 43 % (máx. 52) | 0 |
| hdE / 2ª | base | 59,9 (mín. 59,9) | 40 % (máx. 42) | 32 % (máx. 44) | 0 |
| hdF / 1ª | modo 6 + tudo | 59,9 (mín. 59,9) | 40 % (máx. 42) | 44 % (máx. 52) | 0 |
| hdF / 2ª | base | 59,9 (mín. 59,9) | 40 % (máx. 42) | 32 % (máx. 44) | 0 |
| hdI / 1ª | tudo | 59,9 (mín. 59,9) | 40 % (máx. 49) | 43 % (máx. 51) | 0 |
| hdI / 2ª | base | 59,9 (mín. 59,9) | 41 % (máx. 45) | 32 % (máx. 43) | 0 |

Nenhum segundo abaixo de 59,5, 0 underruns em todas. Thread do jogo: −1 a 0 ponto contra a base (pedido: até +3).
Thread de render: +10, +11, +7, +11, +11 pontos (média +10, pedido: até +10), no limite. Sem o HD (tabela abaixo)
era −7/+4. Modo 6 + tudo: 59,9 fps, 0 underruns.

**Causa e correção (patch `psxrecomp-host-draw.patch`, 2026-10-27).** Tempo de parede de cada parte do desenho
de host medido no framework (`gl_renderer_host_draw_times`, no `heat_stats`): o `hostdraw_save` levava **1,5–1,9 ms
por desenho de host** (13 `glGetIntegerv`: unidade ativa, textura 2D das unidades 0..7, ARRAY_BUFFER, unpack) e o
resto da chamada ~0,1–0,3 ms. Com o driver da NVIDIA em modo de thread cada leitura de volta espera a fila do driver.
Um marcador vazio (`stage_post "1 1"`, callback sem trabalho) já punha +5 a +7 pontos na render; a soma dos
desenhos de host ia de 3,0–3,7 s (base) para 7,3–7,8 s por minuto. O pós e a sombra no chão somam 3 desenhos de
host por quadro do jogo (o pós sempre com espelho), daí os +10. Correção: o save/restore não lê nada de volta.
Volta aos invariantes do renderizador (unidade 0 ativa, ARRAY_BUFFER 0, unpack 4/0, que ele religa antes de
usar); as unidades 0..7 ficam com o renderizador (os plugins só usam 8..13, conferido em `rmk_gl.c` e
`rmk_post.c`); o recorte vem de `hr_begin` / `wide_band_scissor_x`, que guardam o último. As consultas de tempo
de GPU dos plugins (`gpu_timer.h`) ficaram desligadas por padrão (`gpu_timers 1`), pelo mesmo motivo.
O driver também alterna de modo entre sessões: numa sessão (hdL) o save custou 7 µs, mas a render ficou em
49–59 % até na base. Essas são as sessões "com ruído" de antes (base a 49–62 %).

Depois da correção (`perf_hdMNO.txt`, `perf_hdP.txt`):

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| hdM / 1ª | marcador vazio | 59,9 (mín. 59,9) | 40 % (máx. 42) | 24 % (máx. 31) | 0 |
| hdM / 2ª | base | 59,9 (mín. 59,9) | 40 % (máx. 42) | 26 % (máx. 35) | 0 |
| hdN / 1ª | tudo | 59,9 (mín. 59,9) | 43 % (máx. 46) | 28 % (máx. 41) | 0 |
| hdN / 2ª | base | 59,9 (mín. 59,8) | 40 % (máx. 46) | 27 % (máx. 33) | 0 |
| hdO / 1ª | base | 59,9 (mín. 59,9) | 41 % (máx. 42) | 25 % (máx. 29) | 0 |
| hdO / 2ª | tudo | 59,9 (mín. 59,9) | 39 % (máx. 41) | 25 % (máx. 36) | 0 |
| hdP / 1ª | modo 6 (4 HEATs) + tudo | 59,9 (mín. 59,9) | 44 % (máx. 46) | 29 % (máx. 35) | 0 |
| hdP / 2ª | base | 59,9 (mín. 59,9) | 40 % (máx. 46) | 25 % (máx. 32) | 0 |

Regressão depois da correção (`stage_check.py`, `chk_montagem_hostdraw.png`): abertura, título, menu e seleção com
0 marcadores e 1920×1080; partida no palco do HIRO com texturas certas no HEAT e no HUD; volta ao título no fim. O
HEAT fica com um degradê magenta nas pernas nesse palco (com e sem o pós): é a luz rosa do palco lida do GTE pela
luz do remake (fase 2), não estado de textura.

Desenho de host por chamada: preparar 0,1 µs (era 1,5–1,9 ms), total 65 µs com tudo e 131–135 µs na base (o corpo
do HEAT), soma 0,22–0,24 s por minuto na base e 0,34 s com tudo. **Render: tudo − base = +1 e 0 pontos; jogo +3 e
−2 (as 1ªs janelas da sessão tendem a 1–3 pontos a mais no jogo).** A base também caiu (render 30–35 % → 25–27 %),
porque o desenho do HEAT pagava a mesma espera.

Custo de GPU de cada peça, medido pelos próprios plugins com carimbos `GL_TIMESTAMP` em volta do trabalho
(`src/gpu_timer.h`, `tools/remake/stage_gpu.py`; 30 chamadas por segundo, uma por quadro do jogo): pós-efeito
0,86–0,93 ms por chamada (1,06–1,22 ms no caminho antigo com emenda), sombra no chão 0,18–0,23 ms, corpo do HEAT com
a sombra própria 0,17–0,30 ms. Somado: ~1,1 ms de GPU por quadro do jogo, ~3 % do tempo da GPU. O caminho novo do pós
tirou 20 % da GPU dele e a thread de render não mudou (hdI = hdA/B), então o resto dos pontos não era GPU do
trabalho do remake (causa abaixo). Um A/B de várias variantes na mesma partida (`render_ab`) não serve: a render e
o jogo derivam com a fase da música. `frame_perf` (GL_TIME_ELAPSED do framework) mede o intervalo da cena na GPU
com as esperas dentro (cena + apresentação dão sempre 16,7 ms) e não serve para custo.

**Sem o pacote HD** (junção quebrada, ver acima; medições da 1ª rodada da fase 3, antes da correção do HEAT com HD):

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| A / 1ª | base | 59,9 (mín. 59,9) | 44 % (máx. 47) | 43 % (máx. 49) | 0 |
| A / 2ª | tudo | 59,9 (mín. 59,9) | 39 % (máx. 45) | 36 % (máx. 54) | 0 |
| B / 1ª | tudo | 59,9 (mín. 59,9) | 39 % (máx. 41) | 36 % (máx. 42) | 0 |
| B / 2ª | base | 59,9 (mín. 59,9) | 40 % (máx. 42) | 32 % (máx. 38) | 0 |
| C / 1ª | modo 6 (4 HEATs) + tudo | 59,9 (mín. 59,9) | 43 % (máx. 46) | 44 % (máx. 49) | 0 |
| C / 2ª | tudo | 59,9 (mín. 59,8) | 39 % (máx. 45) | 36 % (máx. 54) | 0 |

Nenhum segundo abaixo de 59,5 em nenhuma janela. Contra a base da mesma sessão: jogo −5 e −1 ponto, render −7 e
+4 pontos (pedido: até +3 e +10). Pós-efeito: 66–72 µs de CPU por chamada na thread de render (2 por quadro do jogo
na tela larga: canônica e espelho, 60 por segundo); desenho de host do HEAT com a sombra no chão 0,37 s de CPU por
minuto de render (4212 chamadas × 88 µs) contra 0,31 s na base (1799 × 175 µs). Custo de GPU
não medido à parte (os números de render incluem as esperas do driver). As 2ªs janelas das sessões A e C, antes da
correção que liga a sombra nova só quando o jogo chama a dele, mostravam 1700 sombras novas contra 1414 do jogo (no
fim da música o jogo para de desenhar a sombra); corrigido e conferido (904 = 904).

### Capturas (`tools/recomp/logs/remake/palco/`, fora do git)
`mascara.png` (máscara 3D do framework quase vazia), `t2.png` e `t3.png` (Sushi Planet, metade direita com o efeito,
parâmetros da afinação), `u_grade.png` / `v_grade.png` (estúdio, metade direita, antes e depois da adaptação),
`ab1_0_2_janela.png` (o que entra no bloom; HUD por cima intacto), `f567_ab.png` e `g_ab.png` (sombra do jogo x
sombra nova, ~0,2 s entre as duas), `fin1..3_ab.png` (palco original x fase 3, estúdio), `palcoA..C.log` (medições).

### Limites e o que não deu (fase 3)
- Contrato novo do desenho de host: o plugin usa só as unidades de textura 8 em diante e não deve ler estado do GL
  por quadro (`glGet*`, consultas). O adaptador do Polygonia (DuckStation) precisa seguir a mesma regra.
- **Geometria e luz do palco pelo caminho novo: não feita** (decisão com dados acima). Fica para depois da fase 4.
- **Dumps HD**: só os Stage 1 de quatro personagens (rotas R, RR, RRR, D) e os palcos que o sorteio do oponente
  trouxe; os palcos seguintes do modo história (2..8) precisam de partidas vencidas em sequência, não feitas.
- Ganho visual grande no palco de noite (neons com halo forte, vibração); no estúdio claro é discreto (vibração,
  vinheta, leve brilho no telão, sombra macia).
- O atraso do som tem incerteza de ±3 quadros entre músicas (ver acima); a verificação objetiva é
  `stage_pulse_check.py`.
- Halo leve também nas partes vermelhas saturadas dos personagens (luvas, chamas da calça).
- Savestate salvo antes de mudar a build não carregou depois (`last_ok 0`): não serve para fixar o oponente entre
  builds.
- Polygonia: o pós-efeito e a sombra estão no núcleo portátil; o adaptador do Android precisa achar o ponto da OT
  (slot 96 / antes da OT do HUD) e o FBO de alta resolução do DuckStation, e não tem a emenda da tela larga.

## Fase 4 no jogo: os dois jogadores e o elenco (2026-10-28)
Todos os testes pelo modo 2 jogadores (`game_session.py` `to_match_2p`, `BAG2_MATCH="P1 P2 palco"`; rota, grade e
pad 2 no `CLAUDE.md`): mesmos personagens e palco nas variantes.

### Como o jogo desenha cada personagem
- `FUN_8002BEB4(estado)` (um por personagem, todo quadro de partida) lê o **tipo** do personagem (estado +4) e o
  **jogador** (+5) e chama `FUN_80029D4C` (tipo 0: só o HEAT) ou uma **função do overlay do personagem** (código em
  `0x801CC510..` para o 1P e `0x801D3B64..` para o 2P; tipos 2, 3 e 14, com variantes por jogador e pelo byte +6),
  sempre com a raiz da árvore (estado +0x28). O COMET (tipo 2, 2P) é desenhado por `0x801D76C0` → `0x801D759C`, uma
  cópia do percurso da árvore com ajustes de ordem por osso (alguns ossos puxam o ponteiro da OT 8 ou 16 slots). Por
  isso o gancho da fase 1 (`FUN_80029D4C`) só pegava o HEAT: numa partida HEAT × COMET ele roda só para o HEAT.
- Os percursos dos overlays chamam `FUN_80029588(mesh, buffer, nó + 0x2C, ...)` por osso, a mesma função do HEAT.
  Gancho de filtro novo nessa entrada (`mod_function_entry_funcs`; `psxrecomp-game` regenerou 1 shard): na raiz de
  uma árvore reconhecida o plugin desenha o remake e pula o osso; os outros ossos da mesma árvore são pulados um a um
  (lista dos nós da árvore). A árvore que `FUN_80029D4C` deixou para o original (modos de comparação) não é refeita.
- Estados dos jogadores: ponteiros em `0x80060E30` (1P) e `0x80060E38` (2P) (`game_session.who()`).
- **Figurino alternativo**: cada personagem tem dois, normal (texturas `87 + 2k`, modelo `123 + 2k`) e alternativo
  (`159 + 2k`, `195 + 2k`); o 2P de uma partida espelhada usa o alternativo. A "paleta do jogador 2 em 88 + 2k" do
  plano era leitura errada: `88 + 2k` e `124 + 2k` são as mesmas texturas e o mesmo modelo do 1P (iguais byte a byte;
  conferido nos TIMs do HEAT e do COMET), carregados no lugar do 2P. Sete personagens mudam o modelo no figurino
  alternativo (HEAT, COMET, HIRO, KELLY, KITTY-N, SHORTY, STRIKE); nos outros só a textura muda.
- **CLUT**: a da VRAM é igual à do disco nos dois jogadores (conferido com `vram_peek` nas linhas do HEAT 1P e do COMET
  2P); o 2P só põe a CLUT 16 linhas abaixo.

### Exportador genérico (`tools/remake/export_char.py`, asset v6; `export_all.py` para o elenco)
- Um asset por personagem e figurino: `mods/remake/<nome>.bin` e `<nome>_alt.bin` (fora do git). `export_heat.py`
  virou atalho para `export_char.py HEAT`; só o HEAT normal ganha a geometria da fase 2 (mãos com dedos, orelhas,
  nariz); os outros têm a malha original soldada + Phong + texturas HD (Real-ESRGAN 4x) + mapas de normal + luz do
  palco + sombra própria + sombra no chão.
- **Árvore**: nós desenhados = os ligados ao nó 0 pela cadeia de pais; a posição na árvore do jogo é a ordem deles no
  arquivo (vários personagens têm nós fora da árvore no meio da lista: STRIKE, HUSTLE KONG, McLOAD, COMET*), então a
  tabela de nós sai na ordem da árvore e o dono de cada vértice é a posição. Byte novo por nó: desenhado (a assinatura
  exige exatamente esse número de ossos; antes era "até 24").
- **Mãos alternativas** viram grupos de faces (como os gestos do HEAT): a posição de cada uma é a do nó desenhado sem
  filhos que se solda ao mesmo osso ("uses" com os mesmos donos); as que não se soldam (COMET 21..26) vieram de
  `char_tree.py` (qual nó o jogo liga em cada posição durante a partida) e, para as que não apareceram, do lado pelo
  sinal de z do centro da malha (`ALT_POS`). O COMET troca a mão esquerda pela que segura a bandeja (nó 26). As que
  ficam sem posição saem do asset: se o jogo ligar uma delas, a assinatura falha e o original aparece naquele
  quadro (contador `bag2_heat.match_fail`).
- **Páginas por nó** (bit por TIM usado pelas faces originais) e **impressão digital de cada TIM** (FNV-1a dos 256
  primeiros bytes da imagem) no cabeçalho das páginas.
- **Semitransparência**: bit 4 do modo da face nas primitivas com o bit semi do arquivo (o byte de textura >> 5 é
  sempre 1 neste jogo) e alfa 143 nos texels cuja cor da CLUT tem o STP (camadas HD: o xBR mantém os valores exatos).
- Recusados (outro esqueleto intercalado na lista de nós; o plugin conhece uma árvore por personagem):
  CHICHI&SALLY, COLUMBO e SUSHI BOY (os três bloqueados no cartão do usuário).

### Plugin para vários personagens (`src/bag2_heat.c`)
- **Carga sob demanda**: na ativação só as tabelas de cada asset (`rmk_asset_peek`, assinaturas). O gancho em
  `LoadImage` calcula a impressão digital dos 256 primeiros bytes de cada imagem; quando é um TIM de um asset, a malha e
  as texturas são lidas numa thread (1,0–1,3 s com os mapas de normal) durante a tela de carregamento da partida, e os
  recursos GL são criados aos poucos por marcadores de preparo do próprio personagem (26–32). Depois que a GPU tem as
  camadas, a cópia de CPU sai (fica só a do HEAT normal, que o caminho por pacotes da fase 0 usa). Se o TIM não foi
  visto (savestate, por exemplo), o primeiro desenho dispara a carga e o original aparece até ela terminar.
- **Personagens na partida** (até 4, chave = raiz do vetor de nós): luzes do palco, chão, pose, sombra no chão,
  marcadores do quadro e as **páginas na VRAM** de cada um. As páginas saem dos pacotes que o jogo montou para cada osso
  (nó +0xA8, tamanho em +0xA4; tpage do 2º par de UV dos polígonos com textura) cruzados com as páginas que o asset diz
  que cada osso usa: osso com uma página ainda desconhecida e um tpage ainda sem dono resolve a página. HEAT 1P: 704,256
  e 768,256 (a constante medida na fase 2); COMET 2P: 960,256 e 896,256. (O buffer do nó não tem todos os pacotes: a
  maior parte dos decalques da cabeça do COMET sai de outro lugar; para a página basta o que tem.)
- **Figurino** quando os dois têm o mesmo modelo: o do TIM que o `LoadImage` pôs na página que o personagem usa.
- **Expressão do rosto** em coordenadas da VRAM: `MoveImage` dentro de uma tpage (64 × 256 halfwords) vira casa na
  tabela global; no quadro, cada personagem converte as casas que caem nas páginas dele. Conferido no HEAT 1P
  (`game_face.py`, `rosto4_*.png`): dentes, olhos semicerrados e fechados iguais ao original no mesmo quadro.
- Marcadores: o personagem vai nas flags do cabeçalho (bits 8..13), um só callback; 24 marcadores por metade.
- Flags novas do `heat_mode`: 1024 = semitransparentes opacas (A/B), 2048 = só o HEAT (os outros carregam e preparam,
  mas ficam originais: é a base das medições). Comando novo `heat_chars` (assets, carga, GL, personagens na partida,
  páginas, casas, últimos `LoadImage` com a impressão digital).

### Semitransparência (núcleo)
- Medido no quadro da GPU (`gpu_frame_dump`, COMET 2P): o avental (nó 4, 51 primitivas) e 18 decalques da cabeça (CLUT
  linha 5, a estrela do boné) saem com o bit semi, modo 0 (0,5 B + 0,5 F). A primeira versão ignorava os decalques
  semi (o buffer do nó não os mostrava) e a estrela do boné saía vermelho-escura (a cor crua da linha 5) em vez de
  amarela.
- Face semitransparente: os texels com STP (alfa do mapa de normal, que agora guarda o STP) saem dos passos 1 e 2 e
  são desenhados num **passo 3**, na mesma ordem de pintor, com `glBlendColor(0,5)` e
  `glBlendFunc(CONSTANT_COLOR, CONSTANT_COLOR)`, sem escrever profundidade nem o alfa (bit 15). As faces semi ficam
  repetidas numa faixa no fim do buffer de instâncias, desenhada mudando o ponteiro dos atributos (o GL 3.3 e o GLES
  3.0 não têm `baseInstance`). O renderizador do framework refaz equação, função e cor da mistura a cada lote, e a
  restauração do desenho de host desliga o blend.
- **Decalque semi** (cor + textura): no jogo, o que fica atrás do GT3 semi é o G3 da mesma face, então a mistura é
  feita no shader (textura com STP pesa 0,5 sobre a cor base), sem passo extra.

### Desempenho (peça 2: HEAT × COMET com os dois remakes)
Build-debug, RTX 3070 Ti, 9x + 16:9, pacote HD ligado ("HD textures: ... replacements on"), `game_perf.py`, palco do
HEAT, 60 s por janela, 2 janelas por partida, ordem trocada entre as sessões. Base = estado do cbe175a (remake do HEAT
e do palco; o COMET original): variante `"1 4 0.6 0.25 2050;1 0"` (flag 2048). Tudo = `"1 4 0.6 0.25 2;1 0"`.
Logs `logs/remake/jogo/perf4A.txt` e `perf4B.txt`.

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| A / 1ª | base (só o HEAT com remake) | 59,9 (mín. 59,9) | 42 % (máx. 47) | 33 % (máx. 45) | 0 |
| A / 2ª | HEAT + COMET com remake | 59,9 (mín. 59,9) | 32 % (máx. 36) | 39 % (máx. 48) | 0 |
| B / 1ª | HEAT + COMET com remake | 59,9 (mín. 59,8) | 33 % (máx. 37) | 38 % (máx. 48) | 0 |
| B / 2ª | base (só o HEAT com remake) | 59,9 (mín. 59,9) | 39 % (máx. 41) | 32 % (máx. 38) | 0 |

Nenhum segundo abaixo de 59,5. Thread do jogo −10 e −6 pontos contra a base (o COMET original passa ~600 primitivas
pela GPU emulada; o remake, um marcador). Render +6 e +6 (pedido: até +10). Plugin na thread do jogo: 41–42 µs por
desenho de personagem com os dois (58–59 µs só com o HEAT, que tem 3,4 mil vértices com as mãos de dedos). Thread de
render: 95 µs de CPU por desenho de host com os dois (135 µs na base). As sombras no chão não entraram nessas janelas
(o palco do HEAT não chama a sombra do jogo nesse trecho; 0 nas duas variantes).

Segundo par, KITTY-N × HIRO no Sushi Planet (palco com a sombra do jogo, logo com a sombra macia no chão dos dois);
aqui a base deixa os **dois originais** (flag 2048; nenhum é o HEAT). Logs `perf4C.txt`, `perf4D.txt`:

| sessão / janela | variante | fps por segundo | thread do jogo | thread de render | underruns |
|---|---|---|---|---|---|
| C / 1ª | base (os dois originais) | 59,9 (mín. 59,9) | 54 % (máx. 56) | 28 % (máx. 40) | 0 |
| C / 2ª | os dois com remake | 59,9 (mín. 59,9) | 32 % (máx. 37) | 34 % (máx. 49) | 0 |
| D / 1ª | os dois com remake | 59,9 (mín. 59,9) | 36 % (máx. 37) | 29 % (máx. 39) | 0 |
| D / 2ª | base (os dois originais) | 59,9 (mín. 59,7) | 49 % (máx. 55) | 27 % (máx. 37) | 0 |

Jogo −22 e −13 pontos, render +6 e +2. Sombra nova no chão em 3108 e 3354 desenhos (a do jogo pulada nos mesmos).
Desenho de host: 27–30 µs de CPU por chamada (corpo + sombra no chão, 7–8 mil por minuto). Plugin na thread do jogo:
23 µs por personagem.

### O elenco (peça 3; `export_all.py`, `chars_tour.py`, `logs/remake/jogo/tour.txt`)
Cada par no 2 jogadores por 25–40 s de árvore (qual nó o jogo liga em cada posição), 3 capturas no modo 3 e o estado
do plugin. Em todas as sessões: 59,9 fps, cada personagem reconhecido e desenhado em todos os quadros da janela
(`frames_outros` = 30 por segundo por personagem), `match_fail` 0 (nenhum quadro caiu no original por mão
desconhecida).

| personagem | normal | alternativo (2P espelhado) | observações |
|---|---|---|---|
| HEAT | testado (fases 0–4) | testado (modelo próprio) | só o normal tem mãos com dedos, orelhas, nariz |
| COMET | testado | testado | avental e decalques semi; mão da bandeja (nó 26); no alternativo os nós 4 e 13 ficam fora da árvore |
| KITTY-N | testado | testado | página 1 só pelo LoadImage (rosto) |
| HIRO | testado | testado | 115 faces semi (decalques) |
| SHORTY | testado | testado | sem mãos alternativas |
| STRIKE | testado | testado | posição ≠ índice (nó 17/16 fora da árvore); mãos 26–28 (25–27) sem posição |
| TSUTOMU | testado | testado (mesma imagem, decidido pela CLUT) | mãos 24 e 25 sem posição |
| CAPOEIRA | testado (dois dançarinos, duas árvores) | não testado (mesma imagem) | 2 camadas só |
| BI-O | testado | testado (mesma imagem, só a CLUT do TIM 0 muda) | |
| KELLY | testado | testado | |
| PANDER, ROBO-Z GOLD, HUSTLE KONG, McLOAD, MICHAEL DOI | exportado, não testado | exportado, não testado | bloqueados no cartão do usuário; ROBO-Z com as mãos 20–24 sem posição |
| CHICHI&SALLY, COLUMBO, SUSHI BOY | recusado | recusado | outro esqueleto intercalado na lista de nós |

Assets: 8–63 MB cada (2–15 camadas), 30 no total (~1,2 GB por pasta de build). Capturas `tour_<P1>_<P2>_0_<nome>.png`
(original à esquerda, remake à direita, 3 momentos), `sombra1.png` (KITTY-N e HIRO no Sushi Planet).

### Capturas (`tools/recomp/logs/remake/jogo/`, fora do git)
`f4b_*` e `ab3_COMET.png` (COMET original × remake, A/B de Phong e semi), `semi2_COMET.png` (estrela do boné e avental
depois do decalque semi), `hh_HEAT*.png` (HEAT × HEAT: o 2P de figurino alternativo), `rosto4_*.png` (expressão do
HEAT na rota nova), `tour_*` (o elenco).

### Regressão (fase 4)
- `stage_check.py` pela rota do 2 jogadores (HEAT × COMET, palco do HEAT; `palco/chk_fase4.txt`, `chk_fase4_grade.png`):
  abertura (FMV), título, menu, seleção de personagem e de palco com 0 marcadores do palco e captura 1920×1080; a
  partida com o efeito, HUD por cima intacto; no fim da música o 2 jogadores volta à seleção de personagem e os
  marcadores param. 16:9 na partida, menus e FMV em 4:3 como antes.
- `audio_buffer_check.py 90` pela rota nova (`ritmo/buf_fase4.txt`): 0 underruns da abertura em diante (título,
  menu/seleção, carregamento, partida, fim da música); **699 no boot** (os primeiros 70 s, que incluem o vídeo de
  abertura). O mesmo apareceu (701) na sessão do A/B do ritmo com o exe do cbe175a, antes de qualquer mudança da
  fase 4, e não apareceu na outra sessão daquele A/B: é do começo da sessão, não do remake. Não investigado a fundo.
- Ritmo: A/B na mesma música (`notes/recomp.md` §13), sem deriva no 566121.
- Expressão do HEAT (`game_face.py`, `rosto4_*.png`) igual ao original; pacote HD ligado em todas as medições ("HD
  textures: ... replacements on").
- Build normal (`build/`, sem TCP) recompilada: sobe com o pacote HD (477 substituições) e sem erro no log em 40 s;
  não joguei uma partida nela (sem servidor de testes nem computer-use nesta sessão).

### Limites e o que não deu (fase 4)
- **Não testados no jogo**: PANDER, ROBO-Z GOLD, HUSTLE KONG, McLOAD e MICHAEL DOI (e os alternativos), bloqueados no
  cartão de memória (não aparecem na grade do 2 jogadores); CAPOEIRA*. Ficam em `mods/remake/`; se a assinatura
  falhar no jogo, o original aparece.
- **Recusados**: CHICHI&SALLY, COLUMBO e SUSHI BOY têm outro esqueleto (ou peça grande) intercalado na lista de nós;
  o plugin conhece uma árvore por personagem. Exigiria reconhecer as árvores separadas.
- **Mãos alternativas sem posição** (STRIKE, TSUTOMU, ROBO-Z): não se soldam ao antebraço e não apareceram nas danças
  observadas; saíram do asset. Se o jogo ligar uma delas, aquele quadro sai original (`bag2_heat.match_fail`).
- **Expressão do rosto dos outros**: a conversão das casas funciona (KITTY-N: 1 casa convertida numa partida; a página
  do rosto achada pelo LoadImage), mas só o HEAT foi conferido por imagem (`game_face.py` é dele).
- **Sombra no chão**: só onde o jogo chama a dele (no palco do HEAT, nenhuma; no Sushi Planet, todas).
- **Memória**: cada asset carregado fica na GPU (~2 × camadas × 4 MB × 1,33 com mips: 30–160 MB) até fechar o jogo;
  a cópia de CPU sai depois da subida. Uma sessão que passe por todo o elenco chega a ~2 GB de VRAM. Para o Polygonia,
  descarregar os que saíram da partida e comprimir as camadas.
- O CAPOEIRA é carregado em todas as partidas (a impressão digital de um TIM ou CLUT dele bate com algo carregado
  sempre); custa 153 ms numa thread e 8 MB.
- Na primeira partida de um personagem visto só no desenho (sem o LoadImage, ex.: savestate), o original aparece
  ~1–2 s até a carga e o preparo.
- `game_face.py`, `game_hands.py` e `game_sidebyside.py` continuam do HEAT; `game_chars.py`/`chars_tour.py` cobrem
  os outros.

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
- **Suavizar não cria detalhe.** (Fase 2: mãos, orelhas e nariz agora são geometria nova gerada por código.) Real-ESRGAN deixa as bordas nítidas mas é a mesma arte 4bpp; o que é desenhado na textura
  (olhos, sobrancelhas, traços) continua pintado.
- (Resolvido na fase 2: a luz segue as luzes do palco do GTE e há sombra própria.)
- Composição: o HEAT entra inteiro num slot da OT (o da face visível mais próxima). Contra o palco e o HUD fica
  igual ao original nas capturas; se o outro dançarino se sobrepuser ao HEAT na mesma profundidade, as faces
  dele à frente desse slot cobrem o HEAT e as de trás ficam por baixo (visto com a cópia deslocada do modo 7,
  `tres_2.png`). Em partida normal os dois ficam lado a lado; não vi isso num quadro real. Oclusão por pixel
  pediria o `pgxp_depth_buffer` do framework (desligado no `game.toml`).
- Caminho GLES 3.0 do núcleo escrito, mas só testado em GL 3.3 core (NVIDIA). Rewind não testado (savestate sim).
- (Resolvido na fase 2: as mãos alternativas, nós 20–23, têm versão com dedos; antes o HEAT original aparecia
  nesses quadros.)
- (Resolvido na fase 4: o "HEAT jogador 2 com outra paleta" era o figurino alternativo, 159/195 + 2k, que o 2P usa
  na partida espelhada; virou asset próprio, `heat_alt.bin`.)
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
