# tools/extract — extração do disco e engenharia reversa

Scripts usados para entender o disco e o executável do jogo. Rodar da raiz do repositório.

| Script | O que faz |
|---|---|
| `bust2_extract.py` | Lê a imagem `.bin` MODE2/2352 + ISO9660 e abre o arquivão `BUST2.BIN`: containers `DF2_`/`DFS_` aninhados, overlays `DBI_`, bancos de som `SEDT`. Formato em [docs/formats.md](../../docs/formats.md) |
| `bpe.py` | Descompressão BPE usada pelos containers |
| `tim2png.py` | Texturas TIM para PNG ([docs/model-format.md](../../docs/model-format.md)) |
| `str2png.py`, `xa2wav.py` | Vídeo STR (MDEC) e música CD-XA ([docs/media-formats.md](../../docs/media-formats.md)) |
| `exe_dis.py`, `exe_scan.py` | Disassembler rápido do executável com capstone |

```bash
python tools/extract/bust2_extract.py "disc/Bust A Groove 2 (USA).bin" extracted
python tools/extract/tim2png.py extracted/bust2 extracted/png
python tools/extract/exe_dis.py extracted/disc/SLUS_011.59 80035dc4 80035f60
```

Dependências: Python 3.14 com `capstone` e `py7zr`. A saída (`extracted/`) fica fora do git. A análise com o Ghidra
que mapeou o executável está em [archive/ghidra/](../../archive/ghidra) (resultado em
[docs/formats.md](../../docs/formats.md)).
