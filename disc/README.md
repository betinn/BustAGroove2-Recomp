# disc/ — o disco de onde tudo sai

Tudo neste repositório foi tirado de **um** dump do disco original. Este README fica no git como referência
exata desse dump; a imagem (`.bin`/`.cue`) fica nesta pasta só localmente e **não vai para o git** (`*.bin` e
`*.cue` no `.gitignore`): o repositório é público, a imagem é do jogo comercial e tem 612 MB (o GitHub recusa
arquivos acima de 100 MB).

Para compilar ou rodar a partir do código, coloque o seu dump aqui com estes nomes:

```
disc/Bust A Groove 2 (USA).bin
disc/Bust A Groove 2 (USA).cue
```

## Identidade do dump

| | |
|---|---|
| Jogo | Bust A Groove 2 (USA), PlayStation 1, Enix / Metro, 2000 |
| Serial | SLUS-01159 (`SYSTEM.CNF`: `BOOT = cdrom:\SLUS_011.59;1`, `TCB = 4`, `EVENT = 16`, `STACK = 801FFF00`) |
| Formato | 1 trilha, `MODE2/2352` (o `.cue` só tem `TRACK 01 MODE2/2352`, `INDEX 01 00:00:00`) |

| Arquivo | Bytes | CRC32 | MD5 | SHA-1 |
|---|---|---|---|---|
| `Bust A Groove 2 (USA).bin` | 612 411 408 | `c50191c0` | `a91343a99ddc96854a556df6461a1587` | `451e6a20f719c0cb718c1eea3575f7959627a5bb` |
| `Bust A Groove 2 (USA).cue` | 87 | `4d851c49` | `bca4a68b206534e813ac75a1e5b7c3f7` | `5644b366c4041f23e6ae24892c5d7256d2fbfc4d` |

Conferir o seu dump (Git Bash):

```bash
sha1sum "disc/Bust A Groove 2 (USA).bin"
```

## Quem lê esta pasta

- `tools/recomp/bag2/game.toml` (`disc = "../../../disc/Bust A Groove 2 (USA).cue"`): o recompilador, o pipeline
  dos overlays AOT e o exe da build (que procura o caminho subindo a partir da pasta do exe).
- `tools/bust2_extract.py`: extrai o disco para `extracted/` (`SLUS_011.59`, `BUST2.BIN`, XA, STR).
- Os scripts de `tools/remake/` (`game_session.py`, `heat_remake.py`, `hand_table.py`, `stage_inventory.py`) e
  `tools/recomp/bag2perf.py`, `tools/recomp/build.ps1 -Run`, `tools/recomp/bag2/Jogar.bat`.
- A release `releases/v2.0.0-remake/` leva a própria cópia em `releases/v2.0.0-remake/disc/`.
