# Remake visual: piloto com o HEAT (2026-10-25)

Pergunta do piloto: dá para modernizar o visual dos personagens **sem artista humano**, partindo das malhas
originais (suavizar, texturas HD, iluminação moderna), mantendo a mesma animação? O piloto foi feito só
com o HEAT (id 0, container `k = 3`) e só fora do jogo: são imagens e GIFs renderizados offline, nada disso
roda dentro do exe ainda.

## Como reproduzir
```
python -m venv tools/recomp/logs/remake/venv
tools/recomp/logs/remake/venv/Scripts/python -m pip install -r tools/remake/requirements.txt
tools/recomp/logs/remake/venv/Scripts/python tools/remake/heat_remake.py            # ~1 min, GPU
```
Saída em `tools/recomp/logs/remake/heat/` (ignorado pelo git; contém render de assets do jogo):
`compare_a14_f25_y25.png` (frente), `compare_a14_f25_y205.png` (costas), `compare_rosto_a14_f25.png`,
`metodos_{corpo,rosto,pe}.png` (só a geometria muda), `turntable.gif`, `danca_a14.gif`, `textures/` (cache
do Real-ESRGAN). Opções: `--anim N --frame F --yaw G`, `--method loop`, `--styles ps1,hd,flat,modern,toon`,
`--list-anims`. O disco é lido direto do `.bin` (caminho em `DISC`, `heat_remake.py`); ESRGAN em
`C:/tools/realesrgan/bin` (reusa `tools/hd_textures.py`).

## Pipeline (`tools/remake/`)
1. `bag2model.py`: lê BUST2.BIN do disco, containers `123 + 2k` (modelo + animações) e `87 + 2k` (TIMs).
   Porta em Python de `web/src/bag2/model.ts`/`anim.ts`, com duas correções de formato (abaixo).
2. `rig.py`: **malha soldada**. Cada osso do PS1 é um mesh rígido; vértices de junta são copiados entre
   ossos por um buffer global. Aqui cada vértice recebe um id único (dono = osso que o define), então o
   corpo vira uma superfície só e a pose de cada frame é só "matriz do osso dono × posição local".
   Faces reorientadas pelas normais do arquivo (1091 de 1444 estavam na ordem oposta; o jogo não usa
   culling, então a ordem nunca importou para ele).
3. Suavização por frame (padrão **Phong Tessellation**, Boubekeur & Alexa 2008, α = 0,6, 16 triângulos por
   face): cada ponto vira a média das projeções nos planos tangentes dos três vértices. UV, cores e camada
   de textura são interpolados linearmente dentro da face original, então a textura não escorrega.
   Normais de sombreamento: média por área com quebra em vincos de 75°.
4. Texturas: cada par (TIM, linha de CLUT) usado vira uma camada 256×256 (HEAT usa 12). Real-ESRGAN
   `realesrgan-x4plus-anime` 4x no RGB (pixels transparentes dilatados antes), alfa por xBR. Em jogo isso
   seria o mesmo que o pacote HD da v1.3.0 já faz.
5. `render.py` (moderngl, OpenGL 3.3 offscreen):
   - **ORIGINAL (PS1)**: luz por vértice como o GTE, textura afim sem filtro, 15 bits com dithering, 200×267
     ampliado sem filtro. **ORIGINAL em alta resolução**: o mesmo em 600×800 (perto do que a recomp mostra).
   - **MODERNIZADO**: luz por pixel (principal quente com shadow map PCF, preenchimento frio, contraluz,
     ambiente de hemisfério), GGX, oclusão ambiente por vértice (96 mapas de profundidade na GPU,
     suavizada), chão com sombra, ACES, MSAA 8x + supersampling 2x.
   - **cel-shading**: duas faixas de luz, sombra puxada para o violeta, brilho/contraluz recortados,
     contorno por casco invertido.

Validação: as linhas de CLUT do modelo batem com os dumps de textura do jogo rodando (pacote HD,
`dumps/`): por exemplo a região (0,149) com a linha 3 é o óculos azul, e o modelo usa exatamente essa
região/linha nas listas 2/3 do nó 13 (cabeça). As 101 animações decodificam em poses de dança coerentes
(incluindo giro de cabeça no chão na 22).

## Correções no formato (já em `notes/model-format.md`)
- Índice de vértice compartilhado (`refs`/`uses`): slot global = **nó × 256 + vértice local**.
- Índice de normal das primitivas é **global** (concatenação das normais de todos os nós na ordem); juntas
  usam a normal já iluminada do osso vizinho. O visualizador web lia como local e caía fora do array.
- HEAT: nós 20–23 (sem pai) são **mãos alternativas** (trocadas pelo jogo em gestos); nós 24–37 (tipo 4)
  são um esqueleto paralelo de sombra. Os dois ficam fora do piloto.

## O que funcionou
- Formato, esqueleto, texturas e animação: tudo do disco até o frame animado, sem travar em nada binário.
- Phong Tessellation: arredonda pernas, calças, sapatos, rosto e cabelo sem abrir buracos e sem depender
  de topologia limpa. É calculado por frame (barato: 23 mil triângulos).
- Iluminação + oclusão + sombra no chão: é o que mais muda a impressão. Comparando `metodos_*.png` com o
  painel "ORIGINAL em alta resolução", a luz nova sozinha já responde pela maior parte da diferença.
- Cel-shading combina com o estilo anime dos desenhos de textura (olhos, traços do rosto).

## O que não funcionou
- **Subdivisão Loop**: a malha do PS1 é uma "sopa" de painéis. 930 das 2492 arestas são borda aberta, 264 são
  não-manifold e 597 dos 726 vértices desenhados tocam alguma borda. Com a regra de borda do Loop, as
  bordas encolhem e a malha fica amassada; prendendo as bordas, quase nada se mexe (82 % dos vértices
  ficam parados). As duas variantes estão em `metodos_*.png` para comparação; Phong ganhou nas três vistas.
- Normais do arquivo: ~8 % apontam para dentro em relação ao centro do osso (concavidades e painéis
  duplos), então a oclusão ambiente não usa normais (visibilidade na esfera inteira, ×2).

## Limites reais (honesto)
- **Suavizar não cria detalhe.** Dedos continuam sendo uma luva de poucos polígonos, a orelha é um cone, o
  nariz é a textura. Phong só arredonda o que já existe; pontas vivas (cravos da sola, mechas) ficam um pouco
  rombudas.
- Real-ESRGAN deixa as bordas nítidas mas não inventa tecido, pele ou cabelo: é a mesma arte 4bpp em
  resolução maior. Partes desenhadas na textura (olhos, sobrancelhas, traços) continuam "pintadas".
- Cores de material são chapadas por face (é assim no arquivo); não há mapas de normal, rugosidade etc. A
  iluminação é de estúdio, inventada; não reproduz a luz de palco do jogo.
- A malha tem aberturas de verdade (sola do sapato, punhos): no jogo o chão/ângulo de câmera esconde; em
  close aparece.
- Animação interpolada linearmente entre keyframes (o jogo parece segurar o keyframe); fica mais suave
  que o original, o que pode ou não ser desejado.
- Não verificado: o emblema do peito sai "de cabeça para baixo" em relação à figura do TIM (o desenho do
  robô com antenas aparece invertido no corpo). Os dois painéis usam a mesma decodificação, então a
  comparação é justa, mas falta conferir com uma captura do jogo. Espelhamento esquerda/direita: as duas
  convenções (PS1 e GL) são destras e a transformação é rotação pura, então não deve haver espelho.
- Nada disso está no exe. É renderização offline.

## Esforço estimado para seguir
- **Outros 17 personagens, só renders offline (como este piloto)**: o parser é genérico; por personagem é
  preciso achar os nós que não são desenhados por padrão (como as mãos alternativas), conferir listas de
  primitiva que o HEAT não usa (estáticas 10–31, semitransparência, que não está implementada), modelo
  extra da entrada 2 (acessórios), e olhar os renders. Estimativa: 1–2 sessões para todos, mais ajustes
  pontuais nos personagens que fugirem do esqueleto de 19 ossos ou usarem primitivas que o HEAT não usa.
- **Palcos**: o container e o formato dos palcos ainda não foram mapeados para este pipeline; têm muito mais
  geometria, texturas animadas/rolantes e iluminação própria. Estimativa: várias sessões só para extrair e
  renderizar um palco; depois o mesmo tratamento.
- **Levar para dentro do jogo** (o que de fato faria o "remake jogável"): exige interceptar no runtime
  recompilado o desenho dos personagens (`FUN_8001FF34`/`FUN_80029588`, que montam as primitivas para a
  GPU), ler as matrizes dos ossos da memória do jogo a cada frame e desenhar a malha nova com um renderizador
  GL próprio, compondo com o palco que a GPU emulada desenha (profundidade, ordem, efeitos, câmera,
  widescreen). É a parte grande e arriscada: semanas de sessões, sem garantia de que a composição com a
  GPU do PS1 fique limpa. Recomendo decidir pelo visual destes renders antes de começar.
