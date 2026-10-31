"""Monta a árvore de dados do APK Android (docs/android-apk.md).

    python recomp/android/stage.py [full|external]

Sempre escreve a árvore completa em game/build-android/stage/ (links físicos para os arquivos de game/build/ e
disc/, sem cópia): game.toml (de game/game.android.toml), settings.toml (de game/settings.android.toml), keybinds.ini, input.ini, bios/, disc/,
mods/bundled, mods/remake (assets v7 dos personagens e dos palcos), mods/texture-packs/.../replacements (pacote HD),
assets/psxrecomp.png. É o que vai para /sdcard/Android/data/io.github.betinn.bag2/files no aparelho.

  full      — também liga a árvore em android/app/src/main/assets/bag2/ com manifest.txt (version=<carimbo>):
              o APK completo, que a SetupActivity extrai na primeira execução.
  external  — assets/bag2 só com manifest.txt (mode=external): APK pequeno para desenvolvimento; os dados vão
              por `adb push` (recomp/android/build.sh push).

Grava também android/app/version.properties (versionCode = nº de commits, versionName = VERSION da release +
"-android").
"""
import hashlib
import os
import shutil
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.path.join(REPO, "game", "build")
STAGE = os.path.join(REPO, "game", "build-android", "stage")
ASSETS = os.path.join(REPO, "android", "app", "src", "main", "assets", "bag2")


def link(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.lexists(dst):
        if os.path.isfile(dst) and os.path.samefile(src, dst):
            return
        os.remove(dst)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def link_tree(src_dir, dst_dir, keep=None):
    out = []
    for root, _dirs, files in os.walk(src_dir):
        for f in files:
            if keep and not keep(f):
                continue
            s = os.path.join(root, f)
            rel = os.path.relpath(s, src_dir)
            link(s, os.path.join(dst_dir, rel))
            out.append(os.path.join(os.path.relpath(dst_dir, STAGE), rel).replace("\\", "/"))
    return out


def build_stage():
    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE)
    os.makedirs(STAGE)
    files = []

    def one(src, rel):
        if not os.path.isfile(src):
            sys.exit(f"falta {src}")
        link(src, os.path.join(STAGE, rel))
        files.append(rel)

    one(os.path.join(REPO, "game", "game.android.toml"), "game.toml")
    one(os.path.join(REPO, "game", "settings.android.toml"), "settings.toml")
    one(os.path.join(BUILD, "keybinds.ini"), "keybinds.ini")
    one(os.path.join(BUILD, "input.ini"), "input.ini")
    one(os.path.join(BUILD, "bios", "openbios.bin"), "bios/openbios.bin")
    one(os.path.join(BUILD, "bios", "OpenBIOS.LICENSE"), "bios/OpenBIOS.LICENSE")
    one(os.path.join(BUILD, "assets", "psxrecomp.png"), "assets/psxrecomp.png")
    one(os.path.join(REPO, "disc", "Bust A Groove 2 (USA).cue"), "disc/Bust A Groove 2 (USA).cue")
    one(os.path.join(REPO, "disc", "Bust A Groove 2 (USA).bin"), "disc/Bust A Groove 2 (USA).bin")
    files += link_tree(os.path.join(BUILD, "mods", "bundled"), os.path.join(STAGE, "mods", "bundled"))
    files += link_tree(os.path.join(BUILD, "mods", "remake"), os.path.join(STAGE, "mods", "remake"),
                       keep=lambda f: f.endswith((".bin", ".stg")))
    files += link_tree(os.path.join(BUILD, "mods", "texture-packs", "SLUS-01159", "replacements"),
                       os.path.join(STAGE, "mods", "texture-packs", "SLUS-01159", "replacements"))
    files.sort()
    return files


def stamp(files):
    h = hashlib.sha1()
    total = 0
    for rel in files:
        st = os.stat(os.path.join(STAGE, rel))
        h.update(f"{rel}\t{st.st_size}\t{int(st.st_mtime)}\n".encode())
        total += st.st_size
    return h.hexdigest()[:12], total


def write_version():
    commits = subprocess.run(["git", "-C", REPO, "rev-list", "--count", "HEAD"], capture_output=True, text=True,
                             check=True).stdout.strip()
    ver = open(os.path.join(REPO, "releases", "v2.0.0-remake", "VERSION.txt"), encoding="utf-8").readline()
    ver = ver.strip().split()[0].lstrip("v") if ver.strip() else "2.0.0"
    with open(os.path.join(REPO, "android", "app", "version.properties"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"versionCode={commits}\nversionName={ver}-android.{commits}\n")
    return commits, ver


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "external"
    if mode not in ("full", "external"):
        sys.exit(__doc__)
    files = build_stage()
    ver, total = stamp(files)
    commits, release = write_version()
    if os.path.isdir(ASSETS):
        shutil.rmtree(ASSETS)
    os.makedirs(ASSETS)
    with open(os.path.join(ASSETS, "manifest.txt"), "w", encoding="utf-8", newline="\n") as m:
        if mode == "external":
            m.write("mode=external\n")
        else:
            m.write(f"version={ver}\n")
            for rel in files:
                link(os.path.join(STAGE, rel), os.path.join(ASSETS, rel))
                m.write(f"{rel}\t{os.path.getsize(os.path.join(STAGE, rel))}\n")
    print(f"stage: {len(files)} arquivos, {total / 2**20:.0f} MB, carimbo {ver}, modo {mode}, "
          f"versionCode {commits}, versionName {release}-android.{commits}")


if __name__ == "__main__":
    main()
