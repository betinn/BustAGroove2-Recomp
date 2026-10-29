# Bust A Groove 2 (USA) — formatos descobertos

Disco: PS1 (não PS2). Imagem MODE2/2352, 260379 setores, ISO9660.
Executável: `SLUS_011.59` (PS-X EXE), text em 0x80010000, tamanho 0x4E800, entry `pc0 = 0x80038270`,
`sp = 0x801FFFF0`. PsyQ ~1997 (string `intr.c,v 1.75 1997/02/07`). ~466 funções.

## Arquivos do disco
| Arquivo | LBA | Tamanho | Conteúdo |
|---|---|---|---|
| SYSTEM.CNF | 23 | 68 | `BOOT = cdrom:\SLUS_011.59;1` |
| SLUS_011.59 | 24 | 323584 | executável |
| BUST2.BIN | 182 | 80300032 | arquivo único com todos os assets (raiz `DF2_`) |
| XA/BUST2_01..04.XA | 39392.. | 54–81 MB | música XA-ADPCM (interleaved, vários canais) |
| XA/BUST2E01..24.XA | 159792.. | 4–18 MB | XA extra (vozes/ending?) |
| STR/BUST2_OP.STR | 237369 | 19169280 | FMV de abertura (MDEC) |
| DUMMY.BIN | 246729 | 27648000 | padding |

## Containers
Todos little-endian. Função no exe `dfs_get(container, idx, &ptr) -> size` em `0x8001F8CC`.

### `DF2_` (raiz do BUST2.BIN) e `DFS_` (sub-container)
```
u32 magic   'DF2_' | 'DFS_'
u32 count
struct { u32 offset; u32 size; } entries[count]   // offset relativo ao início do container
```
Raiz: 501 entradas (494 `DFS_`, 5 `DBI_`, 2 `BPE_`). `DFS_` aninha recursivamente.
Checagens de magic no exe: isDFS `0x8001F86C`, isDBI `0x8001F884`, isBPE `0x8001F89C`, isDF2 `0x8001F8B4`.

### `DBI_`
```
u32 magic 'DBI_'
u32 payload_size   // = tamanho - 12
u32 ?              // parece checksum/hash
u32 count/id
...
```
Payload contém ponteiros absolutos (0x8008xxxx, 0x801Cxxxx) e strings (caminhos `\STR\BUST2_OP.STR;1`,
nomes como `FEVER4`). Provável bloco de dados já relocado para endereço fixo (tabelas do jogo).
Único caller: `0x8002A708`.

### `BPE_`
Compressão Byte Pair Encoding. Header: `'BPE_'` + u32 **big-endian** = tamanho do bloco comprimido.
Payload descomprimido começa com `DFS_` (visível em texto no stream). Caller: `0x8002A744`. Decoder ainda não extraído.

### `SEDT` (sound edit)
```
u32 magic 'SEDT'
u32 count (3)
u32 header_size (0x14)
u32 offsets[count]
```
Contém `pBAV` (VAB = banco de samples SPU ADPCM) e provavelmente SEQ/tabelas de SFX. 189 ocorrências.

## Folhas (leaf blobs)
Contagem após extração: TIM 750, SEDT 189, DBI 104, desconhecidos 429, BPE 2.

TIM: 303× 4bpp 256×256, 218× 8bpp 256×240 (fundos/telas), resto HUD pequeno. Conversão: `tools/tim2png.py`.

Desconhecidos (dois padrões dominantes):
1. `u32 N; ff ff ff ff ff ff ff ff; u32 1; ...` — N pequeno (1–0x2d), tamanhos 2–70 KB. Provável animação/step chart (N = nº de cenas ou movimentos?).
2. `u32 size; u32 0x1C; u16 count; u8 02 13; u8 00 01 02 03 ...` — 120–280 KB, header de 28 bytes, lista de índices sequenciais (bones?). Provável modelo de personagem (formato próprio, não TMD).
3. `u32 0x2D4; ...` 36× de 10180 bytes idênticos em tamanho — tabela fixa por personagem.

## Overlays
Nenhum arquivo de overlay separado; `DBI_` com ponteiros 0x8008xxxx/0x800Axxxx sugere dados carregados em
endereços fixos acima do exe (exe termina em 0x8005E800).

## Atualização: BPE_ e DBI_ (resolvido)
- `BPE_` decodificado por `tools/bpe.py` (porte de `0x80035DC4`). As 2 entradas de raiz descomprimem para
  `DFS_` (246 KB e 941 KB) cheios de TIM + SEDT. Função de carga por índice: `0x8002A6C0(idx)`; índices
  `< 5` esperam `DBI_`, demais esperam `BPE_` e descomprimem para buffer de 0x15050 bytes.
- `DBI_` = overlay: `{magic, u32 size, u32 bss_size, payload}`. `0x8001F7E0` copia payload para o
  endereço em `0x80010E50` = **`0x8008F6C0`** e zera `bss_size` bytes depois. 5 overlays na raiz + 104 em
  sub-containers (dados com ponteiros absolutos, mesma mecânica).
- Tabela `índice de módulo -> índice no DF2_` em `0x80052CBC` (u16): `4,6,5,3,2,0,...`.
- Carga de arquivo por índice do DF2_: `0x8001F6C0(idx, dst)` (lê setores do CD).

## Ghidra (headless, `ghidra/`)
Comando em `ghidra/run.sh`. Saída em `ghidra/out/` (ignorado pelo git):
`functions.csv` (1255 funções), `symbols.csv`, `decomp.c` (pseudo-C de tudo).

Assinaturas PsyQ (loader usa `psyq470.gdt`) nomearam **906** funções de lib; sobraram **348 `FUN_`**
= código do jogo, **107 KB** (`0x800120CC`..`0x8004ED5C`). Libs presentes: libgpu, libgte, libcd,
libspu, libsnd (SsSeq/VAB), libapi/libc, ISO9660, libetc (pad). `start` → `InitHeap(0x8008F6C4, 0x168940)`
→ `main`. Memória: exe até `0x8005E800`, bss até `0x8008F6C0`, overlay/heap a partir daí.

Nomes já confirmados (renomear no projeto Ghidra):
| Endereço | Nome | Papel |
|---|---|---|
| 0x8001F5E8 | cd_read_file_async(idx, dst) | lê entrada do DF2_ por índice |
| 0x8001F6C0 | cd_read_file(idx, dst) | versão síncrona (retry até ok) |
| 0x8001F7E0 | overlay_install(dbi, dest) | copia payload, zera bss, DrawSync, FlushCache |
| 0x8001F86C/884/89C/8B4 | is_dfs/is_dbi/is_bpe/is_df2 | checagem de magic |
| 0x8001F8CC | dfs_get(c, i, &ptr) → size | acesso ao container |
| 0x8002A6C0 | load_module(idx) | DBI direto (idx<5) ou BPE→DBI |
| 0x80035DC4 | bpe_decode(src, dst, &end) | descompressão |

## Overlays `DBI_`: endereços de carga (confirmados em runtime, 2026-10-23)
104 blocos `DBI_` no `BUST2.BIN` (inventário em `tools/recomp/aot/dbi_inventory.json`):
| Classe | Containers | Destino (payload, após 12 bytes de header) | Loader |
|---|---|---|---|
| módulos principais | raiz 2..6 (tabela `0x80052CBC`) | `0x8008F6C0` | `FUN_8002A6C0` → `FUN_8001F7E0` |
| personagem P1 | `87+2k` e `159+2k` (entrada após os TIM) | `0x801CC510` | `FUN_8003235C` |
| personagem P2 | `88+2k`, `160+2k` e 252/274/294/320/344/366/387/410/415/423/448/485 | `0x801D3B64` | `FUN_80032508` |
| estágio | 231..245 (entrada 0) | `0x801DB1B8` | `FUN_800326F8` |
Capturas de runtime (PSXRecomp) também mostram código executado em `0x80002000`/`0x8000D000` (kernel
patchado pelo PsyQ) e `0x8004B000` (dentro do exe). Perfil AOT: `tools/recomp/aot/overlays.json`.

## Containers 15–32 / 33–50 (dados de dança por personagem, modo 1 / modo 2)
Dois blobs por container, ambos listas `{u32 len; u8 data[len]}` terminadas por `len = 0`:
- `000.bin`: animações no mesmo formato de `notes/model-format.md` (ex.: 015/000 = chunk de 16308 bytes,
  471 frames, step 2, 19 ossos) — a coreografia da música, anexada à lista de animações do modelo em
  runtime (`FUN_800313EC` copia a tabela `+0x824` e chama `FUN_800123E8`).
- `001.bin` (10180 ou 17428 bytes): chunks de faixas `{u16 count; u16 flags; count × 12 bytes}`
  (`FUN_800140EC`, len arredondado a 4). Primeiro chunk de 015/001: 60 registros `{s16 a; s16 b; s16 0;
  s16 c; s16 t; s16 0}` com `t` crescente (~23 por registro) e `a` decrescente — parece trilha de câmera
  ou de eventos sincronizados com o tempo. Semântica em aberto.

## Palco em partida (fase 3 do remake, 2026-10-27; `tools/remake/stage_probe.py`, `stage_beat.py`, `stage_sync.py`)
Medido na build-debug com `gpu_frame_dump` (cada pacote GP0 tem o endereço de origem `src` e o rank na OT) e
amostras da RAM a cada quadro. A função que emite é sempre a DMA da OT (`DrawOTag`), então a autoria vem do
endereço do pacote.
- **Overlay do palco**: containers 231..245 (entrada 0) em `0x801DB1B8` (tabela "Overlays DBI_" acima). O Stage 1
  muda com o oponente sorteado: vistos o "Sushi Planet" (noite, neons, contra COMET) e o estúdio de TV com telão
  (claro, contra KITTY-N). Os outros palcos não foram mapeados nesta fase.
- **Primitivas por quadro** (Sushi Planet, 1750 pacotes, 1700 de desenho): fundo e chão em `0x800D0000..` (134 FT4,
  tpage 10/13, ranks 0..7, os primeiros a sair); céu, letreiros e brilhos em `0x80080000..` (52 FT4, 32 FT4
  aditivos `B+F` tpage 44, 25 sprites, Rect); adereços 3D em `0x800B4000..0x800C3FFF` (~640 G3/G4/GT3/GT4/FT4,
  ranks 1445..3581); outro dançarino `0x801A4000..` (~630, ranks 3100..3204); sombras dos dois dançarinos (F3/F4
  semitransparentes `B-F`, ~90 cada, rank 24). Modos: 186 `B-F` (sombras), 33 `B+F` (brilhos), 53 `0.5B+0.5F`.
- **OT**: `ClearOTagR(DAT_8006EE88 - 4, 0x1001)`; o slot `s` do plugin fica em `DAT_8006EE88 - 0x180 + 4 s`
  (95..4191) e sai na ordem rank = 4191 - s. A entrada 0 (slot 95) o jogo sobrescreve para pendurar a OT do HUD
  (4 entradas, ranks 4097..4100: textos, barras, "Ready", "MISS"), então o último slot útil da cena é o 96.
- **Sombra no chão** (`FUN_8002A46C(raiz, ..., MATRIX do personagem)`): cisalha o esqueleto de sombra (nós
  tipo 4, `FUN_8002A374`) para o plano y = 0 do mundo ao longo de `L` = 3 shorts apontados por `DAT_80060CB8`
  (ponteiro! ex. `0x800B4B58` -> (-2922, 2106, 1952); só desenha com `L.y > 0`), com a câmera do palco em
  `DAT_8005E4D0` (MATRIX mundo -> câmera), e liga tudo no slot 4167 (`DAT_8006EE88 + 0x3F9C`) com tpage `B-F`.
  Chamada depois do corpo no mesmo quadro (904 de 904).
- **Partida x menus**: o ponteiro em `0x8008A2C8` é 0 na abertura, título e seleção e aponta para o estado da
  partida (`0x800B5884`) depois que ela carrega.
- **Tempo da música**: a música é XA, mas o jogo conta ticks em `0x8005FBB4` (32 bits, 256 por batida, ~9,4 por
  quadro a 132 BPM); batida inteira em `0x8005FBD8` (16.16); quadros desde a virada em `0x8005FC7C` (16.16) e uma
  cópia ~4 quadros depois em `0x8005FB5C`; struct de `0x8005FB40` com ponteiros para dados em `0x801E41D4`. Na
  troca de música o tick volta a 0. O ataque do som no SPU vem de +1 a +8 quadros depois da virada do tick (método
  e números em `notes/remake-visual.md`, "Fase 3 no jogo").
- **Atualização de personagem**: `FUN_8002BEB4` (por personagem, todo quadro de partida que o jogo monta; o jogo
  monta a OT a 30 Hz) chama `FUN_80029D4C` para o HEAT e funções do overlay para outros personagens.

### Palcos: conteúdo e desenho (fase 6, 2026-10-28; `tools/remake/stage_inventory.py`, `export_stage.py`)
- **Container do palco** (231..245): entrada 0 = DBI_ do código (overlay em `0x801DB1B8`), 1 = SEDT, depois 3..14 TIMs
  4 bpp 256×256 (cabeçalho com a posição na VRAM, que o jogo usa como está), um **modelo no formato dos personagens**
  (`notes/model-format.md`; lido sem sobra pelo `bag2model.parse_model` em todos) e uma lista de trilhas
  (`{u32 len; data}`, 18..31 blocos: câmera/eventos, não decodificados). 238, 239 e 240 são iguais byte a byte.
- **Modelo**: 22..69 nós; quase todos raízes (pai -1, posição no mundo), tipo 1 (mesh) ou 0 (junta sem mesh); 20..51 nós
  com mesh, 781..1712 vértices, 692..1816 primitivas, quase todas sem luz do GTE (listas 24..27, 10, 11: cor por vértice
  já pintada); 233, 237, 243 e 245 têm listas com luz (0..9). Nenhum `refs`/`uses` entre nós, e os vértices na RAM são
  os do arquivo (nada animado na malha).
- **Tela de palco do 2 jogadores** (toques para a direita a partir do palco do HEAT, HEAT × COMET): com os 18 personagens
  liberados, 0 = 234 (ponte de aço), 1 = 233 (Sushi Planet), 2 = 242 (parque), 3 = 243 (fábrica), 4 = 232 (templo de
  pedra), 5 = 237 (cenário de papel), 6 = 244 (templo oriental), 7 = 235 (discoteca), 8 = 236 (rua com táxis), 9 = 231
  (navio pirata), 10 = 238 (estúdio de TV); a lista dá a volta depois disso. Com 10 personagens no cartão a lista é mais
  curta (a posição 10 cai no 242). **241 e 245 não aparecem na tela de palco** (do modo história, não conferidos).
- **Nós do modelo**: o código do overlay põe a matriz no GTE (RT em `gte_ctrl` 0..4, TR em 5..7; em parte dos nós não é a
  de +0x6C do nó) e chama `FUN_80029588(mesh, buffer, nó + 0x2C, ...)`, que transforma com a matriz do GTE (o terceiro
  argumento nem é lido) e escreve **um pacote por primitiva no buffer do nó** (+0xA8, tamanho em +0xA4), na ordem das
  listas de `rig.GAME_LIST_ORDER`, tamanho fixo por lista: F/G pela GOURAUD, T se a lista tem textura; as listas com
  decalque (8, 9, 12, 13, 15, 28, 29) dão dois pacotes, base sem textura e o com textura. Primitiva cortada não entra na
  OT, mas o lugar dela no buffer fica.
- **Geometria do código do overlay** (chão, vigas, fundos, painéis): pacotes montados pelo próprio overlay, com os
  vértices de `FUN_8001F278(SVECTOR* v, long* sxy, long* sz, int n)` (o chão do 234 é uma grade de 105 vértices),
  `RotTransPers`, `RotTransPers3`, `RotTransPers4` da libgte ou de RTPT/RTPS em linha no overlay (233, 236 e 238 usam
  mais RTPT em linha e RotTransSV). RTPT/RTPS em linha nos overlays de palco (todos carregados em `0x801DB000`; 8 dos 13
  têm): RTPT (`0x4A280030`) em 0x801DB2B0, 2FC, 304, 31C, 348, 354, 374, 3A0, 40C, 0x801E0304, 035C, 1E0C, 1E64, 1ED0,
  2868, 28C0, 3440, 3498; RTPS (`0x4A180001`) em 0x801DB3BC e 3DC (o mesmo endereço é outra instrução em outro palco).
  Os vértices estão em V0..V2 do GTE na hora da instrução, com a matriz e o deslocamento do GTE (RT, TR, OFX, OFY, H).
- **Envio da OT**: o laço principal (`main`) chama `DrawOTagEnv(ot, env)` (0x800486AC) quando a OT do quadro está pronta
  (30 Hz) e espera o `DrawSync` chamando `rand()`; então o tempo que a GPU emulada leva para andar a OT muda o sorteio
  (peças com movimento aleatório, como a gaiola do 234, divergem entre duas variantes depois de um savestate).
- **Texturas animadas**: alguns palcos trocam CLUT ou imagem na VRAM durante a partida (luzes do disco, 8 chaves no 235).
