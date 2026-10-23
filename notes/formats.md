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
