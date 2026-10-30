# Formato de modelo e animação (Metro, Bust A Groove 2)

Tudo little-endian. Fontes no exe: esqueleto `FUN_80022170`, header de mesh `FUN_80021E24`,
montagem de primitivas `FUN_8001FF34`, preenchimento por frame `FUN_80029588` + `FUN_80036230..FUN_8003804C`,
sombra `FUN_80021D5C`, animação `FUN_800124E4` / `FUN_800126B8` / `FUN_80012654`.
Implementação: `archive/web/src/bag2/model.ts`, `archive/web/src/bag2/anim.ts`.

## Onde ficam
Tabelas no exe mapeiam **id do personagem → container** (u16, 18 entradas): `0x80056778` texturas P1,
`0x8005679C` texturas P2, `0x80056850` modelos P1, `0x80056874` modelos P2. A ordem resultante dos
containers (índice `k`) é: BI-O, CAPOEIRA, COMET, HEAT, HIRO, KELLY, KITTY-N, PANDER, ROBO-Z GOLD, SHORTY,
STRIKE, TSUTOMU (elenco original, alfabético), depois CHICHI&SALLY, COLUMBO, HUSTLE KONG, McLOAD,
MICHAEL DOI, SUSHI BOY (novos do BAG2, alfabético). Confirmado em runtime: HEAT carrega `093/001.dbi`.

| Dado | Container | Conteúdo |
|---|---|---|
| modelo + animações | `123 + 2k` (P1) e `124 + 2k` (P2, idêntico) | entrada 0 = modelo, 1 = lista de animações, 2 = modelo extra (acessório), 3 = ? |
| texturas | `87 + 2k` (P1) e `88 + 2k` (paleta P2) | 000.tim retrato 64×56, `.dbi` = código overlay do personagem, 2–3 TIM 4bpp 256×256/256×128, 2× `SEDT` (vozes) |
| passos/dança | `15 + k` (modo 1) e `33 + k` (modo 2) | tabela em `0x80056704` (id do personagem → container) |

Ids de personagem (tabela de nomes em `0x800527F4`): 0 HEAT, 1 STRIKE, 2 COMET, 3 TSUTOMU, 4 KELLY, 5 SHORTY,
6 PANDER, 7 KITTY-N, 8 HIRO, 9 BI-O, 10 CAPOEIRA, 11 ROBO-Z GOLD, 12 COLUMBO, 13 SUSHI BOY, 14 CHICHI&SALLY,
15 MICHAEL DOI, 16 McLOAD, 17 HUSTLE KONG, 18 JAMES SUNEOKA.

Tabela de texturas por personagem (`PTR_PTR_800548CC[slot*19 + id]`): entradas de 0x30 bytes
`{ptr→byte válido, u8 comprimido(BPE), s16 clutX, s16 clutY, s16 tpageX, s16 tpageY, ...runtime}`;
`-1` = usa a posição do header do TIM. Os TIMs de corpo declaram (0,0) e o overlay do personagem
reposiciona na VRAM em tempo de execução. O índice `tex` das primitivas conta só os TIMs de corpo
(o retrato não entra).

## Modelo (esqueleto)
```
u32 N                                 // nº de nós
struct { s32 parent, prev, child, sibling; } links[N]   // -1 = nenhum
nó[0..N-1] em sequência:
  u32 type
  type 0: nada
  type 1: MeshHeader + dados (abaixo)
  type 2: s32 idx         // instancia o mesh do nó idx
  type 3: u32 ?, u32 n, n × 8 bytes   // não desenhado (colisão/bbox?)
  type 4: Shadow (abaixo)
  depois de cada nó: 10 × s32 de transformação:
     w0.lo = rotX, w0.hi = rotY, w1.lo = rotZ   (ângulos de 12 bits, 4096 = 360°)
     w2..w4 = escala x,y,z (4096 = 1.0), w5 = ?, w6..w8 = translação, w9 = 0
```
Matriz do nó: `M = Mpai · T(pos) · R`, com `R = Rz·Ry·Rx` (m00 = cy·cz, m20 = −sy, ver `FUN_8001EEF8`).
Unidades: personagem tem ~30000 de altura. Eixo Y aponta para baixo no PS1.

### MeshHeader (type 1) — 0x120 bytes
| Offset | Campo |
|---|---|
| 0x00 | type = 1 |
| 0x04..0x90 | ponteiros preenchidos em runtime por `FUN_80021E24` (zero no arquivo): +0x04 vértices (= +0x120), +0x08..+0x10 grupos de normais e refs/uses (na ordem dos dados), +0x14 + 4k = lista k (k = 0..31; conferido no nó 13 do HEAT) |
| 0x94 | u32 nVerts |
| 0x98 | u32 índice-base das normais (soma das normais dos nós anteriores; identifica o mesh também na RAM) |
| 0x9C, 0xA0 | u32 nGruposA (NCDS/NCDT), nGruposB (luz especular `ll`/`cdp`) |
| 0xA4 | u32 nRefs: `u32 {src:8, dst:16<<16}` copia vértice local → buffer global do modelo |
| 0xA8 | u32 nUses: `u32 {src:16, dst:16<<16}` copia buffer global → local (vértices compartilhados entre ossos) |
| 0xAC..0xEA | 32 × s16 contagem de cada lista de primitivas |
| 0x120 | início dos dados |

Slot do buffer global = **nó × 256 + vértice local** (um nó posterior pode sobrescrever o slot de outro,
então a ordem dos nós importa). Os `uses` estendem o array local: `dst` = nVerts, nVerts+1, ...; as
primitivas indexam esse array estendido. Os índices de **normal** das primitivas são globais no modelo
(concatenação das normais de todos os nós, na ordem dos nós): juntas reusam a normal do osso vizinho,
transformada pela matriz daquele osso. HEAT: nós 20–23 (sem pai) são mãos alternativas, nós 24–37 (tipo 4)
um esqueleto de sombra. Leitor em Python: `tools/remake/bag2model.py` + `rig.py` (`docs/remake-visual.md`).

Dados, em ordem: `nVerts × SVECTOR{s16 x,y,z,pad}`; grupos de normais
`{u8 r,g,b,code; u32 n; n × SVECTOR normal}` (índice global de normal = posição na concatenação; a cor do
grupo é a cor do material); `nRefs` u32; `nUses` u32; depois as 32 listas.

### Listas de primitivas
Palavra 0 de todo registro: `v3 | v0<<8 | v1<<16 | v2<<24` (índices de vértice, 8 bits). **Quads:** a
diagonal é v1–v3, triângulos (v0, v1, v3) e (v3, v1, v2); com isso a malha do HEAT é fechada e manifold (0
arestas abertas; a leitura de antes de 2026-10-26, triângulos 012 e 132, cruzava a diagonal).
**Atributos por canto do quad (medido na GPU em 2026-10-27):** os slots A, B, C, D do GT4/FT4 que o jogo manda
recebem os vértices **v2, v3, v1, v0** e os atributos de índice **0, 3, 1, 2** na ordem dos offsets da tabela
abaixo (ex. GT4 k5: índice 0 = @12, 1 = @14, 2 = @16, 3 = @18). Ou seja, v0 usa o atributo 2, v1 o 1, v2 o 0 e
v3 o 3 (`QUAD_ATTR` em `tools/remake/bag2model.py`); vale para UV, normais e cores. Medição: `gpu_frame_dump` e
`heat_dump` do mesmo quadro, 229 quads texturizados do HEAT (tipos 3, 5 e 9) casados pelo conjunto de UV e pela
posição na tela (~1 px); triângulos: identidade (540 casados). A leitura de 2026-10-26 ("UV em sequência
v0..v3") trocava os atributos de v0 e v2: a calça, os sapatos e as mangas saíam com a estampa em cacos no remake
e nos renders offline. Slot da OT: `(z0 + z1) >> 3` com v0, v1 nos triângulos e v0, v2 nos quads.
UV são bytes (u,v) relativos à origem da página de textura. Na tabela, os rótulos "uv0, uv2, uv3, uv1" dos quads
são só a ordem dos offsets (índices 0..3); o vértice de cada um é o do parágrafo acima.
`tex` = byte: bits 0–4 índice de textura, bits 5–7 semitransparência. `clut` = byte: linha da CLUT (+y).

| k | contagem em | palavras | tipo | cor | layout |
|---|---|---|---|---|---|
| 0 | 0xAC | 2 | F3 lit | normal u16 @4 | |
| 1 | 0xAE | 2 | F4 lit | normal @4 | |
| 2 | 0xB0 | 4 | FT3 lit | normal @4 | uv @8,10,12; tex @14; clut @15 |
| 3 | 0xB2 | 4 | FT4 lit | normal @4 | uv0 @6, uv2 @8, uv3 @10, uv1 @12; tex @14; clut @15 |
| 4 | 0xB4 | 5 | GT3 lit | normais u16 @4,6,8 | uv @12,14,16; tex @18; clut @19 |
| 5 | 0xB6 | 6 | GT4 lit | normais @4,6,8,10 | uv0 @12, uv2 @14, uv3 @16, uv1 @18; tex @20; clut @21 |
| 6 | 0xB8 | 3 | G3 lit | normais @4,6,8 | |
| 7 | 0xBA | 3 | G4 lit | normais @4,6,8,10 | |
| 8 | 0xBC | 6 | G3 + GT3 lit | normais @4..8 (G3), @10..14 (GT3) | uv @16,18,20; tex @22; clut @23 |
| 9 | 0xBE | 8 | G4 + GT4 lit | normais @4..10, @12..18 | uv0 @20, uv2 @22, uv3 @24, uv1 @26; tex @28; clut @29 |
| 10 | 0xC0 | 4 | FT3 estático | RGB @4 | uv @8,10,12; tex @14; clut @15 |
| 11 | 0xC2 | 5 | FT4 estático | RGB @4 | uv0 @8, uv2 @10, uv3 @12, uv1 @14; tex @16; clut @17 |
| 12 | 0xC4 | 5 | G3 lit + FT3 | normais @4 | uv @12,14,16; tex @18; clut @19 |
| 13 | 0xC6 | 6 | G4 lit + FT4 | normais @4 | uv0 @12, uv2 @14, uv3 @16, uv1 @18; tex @20; clut @21 |
| 14 | 0xC8 | 4 | F3 estático | RGB @4 | |
| 15 | 0xCA | 5 | F4 lit + FT4 | normal @4 | uv0 @8, uv2 @10, uv3 @12, uv1 @14; tex @16; clut @17 |
| 16–19 | 0xCC–0xD2 | 2 | F3/F4 estáticos (19/23 c/ tpage por byte 7) | RGB @4 | |
| 20, 21 | 0xD4, 0xD6 | 3, 4 | G3/G4 lit semitransp. | normais @4 | |
| 22, 23 | 0xD8, 0xDA | 2 | F3/F4 estáticos | RGB @4 | |
| 24, 25 | 0xDC, 0xDE | 4, 5 | G3/G4 estáticos | RGB×3/4 @4 | |
| 26 | 0xE0 | 6 | GT3 estático | RGB×3 @4 | uv @16,18,20; tex @22; clut @23 |
| 27 | 0xE2 | 8 | GT4 estático | RGB×4 @4 | uv0 @20, uv2 @22, uv3 @24, uv1 @26; tex @28; clut @29 |
| 28, 29 | 0xE4, 0xE6 | 6, 8 | G3+GT3 / G4+GT4 estáticos | RGB | como 26/27 |
| 30, 31 | 0xE8, 0xEA | 4, 5 | G3/G4 estáticos c/ tpage | RGB | |

Uso real (todos os modelos): k6 G3 57k, k4 GT3 42k, k7 G4 23k, k5 GT4 20k, k8 18k, k27 7.6k, k9 5.5k.
Iluminação: cor = material × luz (GTE `ncds/ncdt`, material 0x80 = neutro); textura: texel × cor/128.
Profundidade na OT: média do Z de dois vértices >> 3.

### Shadow (type 4) — 0x4C bytes + dados
`u32 nVerts @16, s16 nTris @20, s16 nQuads @22`; dados: vértices SVECTOR, tris e quads (8 bytes cada, só a
palavra de índices importa). Desenhados como F3/F4 cor 0x303030 semitransparentes (sombra no chão).

## Animação (entrada 1 do container do modelo)
Lista `{u32 len; u8 data[len]}` terminada por `len = 0`; `len = 4` com zero = vazia.
```
u32 hdrSize = 0x1C
u16 frames
u8  step            // 1, 2 ou 3 frames por keyframe
u8  boneCount       // 19 nos personagens
u8  boneMap[boneCount]  // nó do modelo de cada osso animado (na prática 0..18)
...pad até hdrSize
u32 footerOffset    // relativo a hdrSize: tabela de eventos (por decifrar)
streams a partir de hdrSize+4: 3 de translação da raiz, depois 3 por osso (rot x,y,z)
  stream = { u16 size (inclui estes 4 bytes); u8 initLo, initHi; u8 codes[size-4] }
```
Decodificação por keyframe (`FUN_800126B8`):
- translação: byte `b != 0x80` → delta = int8(b); `b == 0x80` → delta = int16 big-endian dos 2 bytes seguintes.
  Valor guardado ×2.
- rotação: `(b & 0xF0) == 0xF0` → delta = 12 bits com sinal de `(b<<8 | próximo) & 0xFFF`; senão delta = `b − 0x78`.
- valor acumula a cada keyframe; keyframe de um frame `f`: `f / step` (último frame: `(frames−2)/step + 1`).
Rotações substituem a rotação de descanso do nó; translação soma à posição do nó 0.
Estado em runtime: `+0x0C` ponteiros iniciais[93], `+0x180` ponteiros atuais, `+0x2FC` valores iniciais[31][4],
`+0x3EC` valores atuais[31][4] (slot 0 = translação, slot k+1 = osso boneMap[k]).

## Em tempo de execução (medido em 2026-10-27, HEAT jogador 1)
- Nó da árvore na RAM (0xB0 bytes): além de +0x4C (matriz de luz), +0x6C (MATRIX câmera×osso), +0x80 (T),
  +0x8C (MeshHeader), +0x94/+0x98 (filho/irmão), +0xA0 (tipo): +0xA4 = tamanho e +0xA8/+0xAC = os dois buffers de
  pacotes GPU do nó (buffer duplo). Os pacotes são montados uma vez com UV/CLUT/tpage; por quadro só xy e cores mudam.
- TIMs de corpo do HEAT na VRAM: TIM 0 em 704,256 (tpage 0x1B), TIM 1 em 768,256 (tpage 0x1C); CLUTs nas linhas 0..3
  perto de x 960.
- **Expressão do rosto**: a página do TIM 1 é um atlas com casas (olho A, olho B, boca) e variantes; o código do
  personagem copia a variante para a casa com `MoveImage` (libgpu `0x800482F4`; `LoadImage` = `0x80048234`,
  `ClearImage` = `0x8004819C`, `StoreImage` = `0x80048294`, `SetDrawMove` = `0x8004AF9C`). Coordenadas das casas em
  `tools/remake/faces.py`, técnica em `docs/remake-visual.md`.

## Em aberto
- Tabela de eventos (footer) da animação; interpolação exata entre keyframes (o jogo parece usar só o keyframe).
- Reposicionamento real das texturas na VRAM (feito pelo overlay do personagem em `0x801Cxxxx`); para o HEAT P1 as
  posições estão medidas acima.
- Matrizes de luz/cor do GTE usadas pelo jogo (afeta o brilho dos materiais).
- Blobs `015..050` (passos) e `DBI_` secundários.
