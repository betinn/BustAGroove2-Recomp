# Formatos de mídia: CD-XA ADPCM e STR/MDEC

Referência para `archive/web/src/media/xa.ts`, `archive/web/src/media/str.ts`, `tools/extract/xa2wav.py` e `tools/extract/str2png.py`.
Fonte principal: psx-spx (nocash), seções "CDROM XA Audio ADPCM Compression", "CDROM XA Subheader"
e "CDROM File Video BS Compression"; jPSXdec usado só como documentação de formato.

## Setor bruto (MODE2/2352)

| Offset | Tam. | Conteúdo |
|---|---|---|
| 0x000 | 12 | sync `00 FF×10 00` |
| 0x00C | 4 | minuto, segundo, frame (BCD), modo (=2) |
| 0x010 | 8 | subheader XA: `file, channel, submode, coding` + cópia dos 4 bytes |
| 0x018 | 2048 / 2324 | dados (Form1: 2048 + 4 EDC + 276 ECC; Form2: 2324 + 4 EDC) |

Áudio XA usa Form2: 18 "sound groups" × 128 bytes = 2304 bytes, seguidos de 20 bytes zerados e o EDC.
Vídeo STR usa Form1 (2048 bytes: 32 de cabeçalho STR + 2016 de payload).
Os decoders aceitam também setores de 2336 bytes (sem sync/header); a detecção é pelo padrão de sync.

### Bits do submode (byte 2 do subheader)

| Bit | Nome | Uso |
|---|---|---|
| 0 | EOR | fim de registro (acompanha EOF) |
| 1 | Video | (STR da Sony é marcado como Data, não Video) |
| 2 | Audio | setor XA-ADPCM |
| 3 | Data | setor de dados |
| 4 | Trigger | uso da aplicação |
| 5 | Form2 | 1 = 2324 bytes de dados |
| 6 | Real Time | o hardware só toca ADPCM com bits 2 e 6 ligados |
| 7 | EOF | último setor do arquivo/canal |

Setores de preenchimento nos .XA do jogo têm submode 0 (file 1, canal 0, coding 0) e são ignorados pelo
filtro "bit Audio ligado". O último setor de cada canal tem submode `0xE4` (EOF+RT+Form2+Audio).

### Coding info (byte 3 do subheader, só em áudio)

| Bits | Significado |
|---|---|
| 0-1 | 0 = mono, 1 = estéreo |
| 2-3 | 0 = 37800 Hz, 1 = 18900 Hz |
| 4-5 | 0 = 4 bits/amostra, 1 = 8 bits |
| 6 | ênfase (não implementada; o disco não usa) |

Todos os canais do jogo usam `0x01`: estéreo, 37800 Hz, 4 bits.

## XA-ADPCM

Cada sound group (128 bytes):

```
00h-03h  cópia de 04h-07h
04h-0Bh  parâmetro (shift/filtro) dos blocos 0..7 (4 bits) ou 0..3 (8 bits)
0Ch-0Fh  cópia de 08h-0Bh
10h-7Fh  28 palavras de 32 bits; a palavra j traz a j-ésima amostra de todos os blocos
```

Em 4 bits o nibble do bloco `b` está no byte `16 + j*4 + (b>>1)`, nibble baixo para `b` par.
Em 8 bits o byte do bloco `b` está em `16 + j*4 + b`. Estéreo: blocos pares = L, ímpares = R.
Amostras por setor por canal: 4 bits estéreo 2016, mono 4032; 8 bits estéreo 1008, mono 2016.

Parâmetro: bits 0-3 = shift (13..15 valem 9), bits 4-5 = filtro (0..3).

| filtro | K0 | K1 |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 60 | 0 |
| 2 | 115 | -52 |
| 3 | 98 | -55 |

```
s = (nibble << 12) >> shift            // nibble com sinal (-8..7); 8 bits: (byte << 8) >> shift
s += (old*K0 + older*K1 + 32) >> 6
s = clamp(s, -32768, 32767)
older = old; old = s
```

O histórico `old/older` é contínuo por canal (L e R separados) entre blocos, grupos e setores.
O console reamostra 37800 → 44100 Hz com um filtro de 25 pontos; no browser deixamos o WebAudio
reamostrar (AudioBuffer criado a 37800 Hz).

Intercalação: com 8 canais estéreo a 37800 Hz, cada canal ocupa 1 setor em 8 (2016 amostras a cada
8/150 s = exatamente 37800 Hz em velocidade 2×).

## STR (vídeo)

Cabeçalho de 32 bytes no início dos 2048 bytes de dados de cada setor de vídeo (little-endian):

```
00h u16 status      0160h
02h u16 type        8001h = MDEC
04h u16 chunk       número do setor dentro do frame (0 = primeiro)
06h u16 chunks      setores por frame
08h u32 frameNo     1 = primeiro
0Ch u32 frameSize   bytes de BS do frame (sem cabeçalhos/padding)
10h u16 width
12h u16 height
14h u16 mdecSize    cópia da palavra 0 do cabeçalho BS
16h u16 3800h       cópia da palavra 1
18h u16 quantScale  cópia da palavra 2
1Ah u16 version     cópia da palavra 3
1Ch u32 0
20h 2016 bytes      payload do frame
```

O frame é a concatenação dos payloads dos `chunks` setores, cortada em `frameSize` arredondado a 4.
Os 8 primeiros bytes são o cabeçalho BS: `mdecSize/4, 3800h, quantScale, version`.

Taxa de quadros: implícita na posição dos setores. Velocidade do leitor estimada pelo áudio
(espaçamento dos setores de áudio × amostras/setor ÷ taxa). Instante de exibição do frame =
`sectorIndex / sectorsPerSecond`, o que mantém o vídeo travado no relógio do áudio.

## Bitstream BS (MDEC) v2/v3

- Lido em halfwords de 16 bits little-endian, bits do mais significativo para o menos.
- Macroblocos 16×16 em ordem de coluna (todos os da coluna 0 de cima para baixo, depois coluna 1...).
- Cada macrobloco: 6 blocos 8×8 na ordem Cr, Cb, Y1 (sup. esq.), Y2 (sup. dir.), Y3 (inf. esq.), Y4 (inf. dir.).
- Cada bloco: DC, depois códigos AC (run, level) até EOB (`10`).

DC:
- v1/v2: 10 bits com sinal, direto. `0111111111` (+1FFh) no lugar de um Cr = fim do frame.
- v3: diferença Huffman (×4) em relação ao DC anterior do mesmo tipo (preditores separados para
  Cr, Cb e Y, zerados no início do frame). Tabelas de "tamanho" estilo MPEG-1:
  Cr/Cb `00`→0, `01`→1, `10`→2, `110`→3, `1110`→4 … `11111110`→8;
  Y `100`→0, `00`→1, `01`→2, `101`→3, `110`→4, `1110`→5 … `1111110`→8;
  seguidos de `tamanho` bits (MSB 0 = negativo, valor = bits − (2^tam − 1)). `1111111111` = fim do frame.
  O resultado é truncado a 10 bits com sinal (comportamento dos decoders v3 "novos").

AC: tabela MPEG-1 intra (B.14) com um bit de sinal após cada código; escape `000001` + run (6 bits) +
level (10 bits com sinal). A tabela em `str.ts` (`AC_TABLE`) está na forma da psx-spx: prefixo + lista
de halfwords MDEC `run<<10 | level` indexados pelos bits "x".

Dequantização (igual ao hardware MDEC), `k` = índice em zigzag, `qt` = tabela padrão (MPEG intra,
DC = 2) em ordem zigzag:

```
coef[0]        = DC * qt[0]                               (clamp -1024..1023)
coef[zagzig k] = (level * qt[k] * quantScale + 4) / 8     (truncado, clamp -1024..1023)
quantScale == 0: valores * 2, sem tabela e sem zigzag
```

Depois IDCT 8×8 (implementação em ponto flutuante, separável) e conversão
`R = Y + 1.402 Cr`, `G = Y − 0.3437 Cb − 0.7143 Cr`, `B = Y + 1.772 Cb`, somando 128 e limitando a 0..255.
Cr/Cb cobrem 2×2 pixels de luma.

## Medições no disco

- `BUST2_01.XA`: 27008 setores, 8 canais (file 1, canais 0..7), todos estéreo 37800 Hz 4 bits,
  2652–3376 setores por canal = 141–180 s cada (canal 7 = 180,1 s). 2708 setores vazios (submode 0).
- `BUST2_02..04.XA`: 8 canais cada, 143–4960 setores por canal. `BUST2E01..E24.XA`: file 1,
  1–7 canais usados (canal 6 nunca aparece nos E), 132–1097 setores por canal.
- `BUST2_OP.STR`: 9360 setores = 936 frames 320×240, BS v2, 9 (702 frames) ou 8 (234) setores de
  vídeo por frame, 1 setor de áudio a cada 8 (file 1, canal 1, estéreo 37800 Hz, 1170 setores = 62,4 s),
  frame a cada 10 setores → 150 setores/s, 15,000 fps, 62,4 s. quantScale 1..7. Frames de 2712 a 18128 bytes.
- Decoder TS no Node 24: canal XA de 157 s em ~90 ms; frame MDEC 320×240 em ~2,6 ms (936 frames em 2,5 s).
- Saída do TS idêntica à do Python de referência: WAV byte a byte; PNGs iguais a ±1 LSB (diferença só no
  arredondamento final RGB: `Uint8ClampedArray` arredonda meio-para-par, o Python trunca após +0,5).
