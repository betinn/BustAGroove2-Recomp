"""Gera recomp/patches/psxrecomp-android.patch a partir do checkout local do framework.

O checkout em $BAG2_TOOLS/psxrecomp tem CRLF (autocrlf) e os três patches do PC já aplicados; um `git diff` direto
sairia com o arquivo inteiro. Procedimento (memória project-bag2-hd-cache-patch): repositório temporário com os
arquivos do upstream (`git show HEAD:arquivo`), os patches 1–3 aplicados em ordem, depois os arquivos atuais
copiados com CRLF -> LF, e `git diff` disso.

    python recomp/patches/make_android_patch.py [--check]

--check: em vez de gerar, aplica os quatro patches num repositório temporário limpo e compara com os arquivos
atuais (confere que o patch reproduz o checkout).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.environ.get("BAG2_TOOLS", "C:/tools")
FW = os.path.join(TOOLS, "psxrecomp")
PREV = ["psxrecomp-hd-cache-caps.patch", "psxrecomp-texture-bank-render-thread.patch", "psxrecomp-host-draw.patch"]
OUT = os.path.join(HERE, "psxrecomp-android.patch")
# arquivos tocados pelo patch do Android
FILES = [
    "runtime/runtime.cmake",
    "runtime/src/psx_fiber.c",
    "runtime/src/gpu_gl_renderer.c",
    "runtime/src/main.cpp",
    "runtime/src/quality_probe.c",
]


def patch_files(path):
    with open(path, encoding="utf-8") as f:
        return sorted(set(re.findall(r"^\+\+\+ b/(\S+)", f.read(), re.M)))


def git(cwd, *args, **kw):
    return subprocess.run(["git", "-C", cwd, *args], check=True, capture_output=True, text=True, **kw)


def write_lf(dst, data: bytes):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data.replace(b"\r\n", b"\n"))


def build_base(tmp, files):
    """Repositório temporário = upstream HEAD dos arquivos + patches 1–3 aplicados."""
    git(tmp, "init", "-q")
    git(tmp, "config", "core.autocrlf", "false")
    git(tmp, "config", "user.email", "x@x")
    git(tmp, "config", "user.name", "x")
    for rel in files:
        data = subprocess.run(["git", "-C", FW, "show", f"HEAD:{rel}"], check=True, capture_output=True).stdout
        write_lf(os.path.join(tmp, rel), data)
    git(tmp, "add", "-A")
    git(tmp, "commit", "-q", "-m", "upstream")
    for p in PREV:
        git(tmp, "apply", "--ignore-whitespace", os.path.join(HERE, p))
    git(tmp, "add", "-A")
    git(tmp, "commit", "-q", "-m", "patches do PC")


def main():
    check = "--check" in sys.argv
    files = sorted(set(FILES) | {f for p in PREV for f in patch_files(os.path.join(HERE, p))})
    tmp = tempfile.mkdtemp(prefix="bag2patch")
    try:
        build_base(tmp, files)
        if check:
            git(tmp, "apply", "--ignore-whitespace", OUT)
        else:
            for rel in FILES:
                with open(os.path.join(FW, rel), "rb") as f:
                    write_lf(os.path.join(tmp, rel), f.read())
            diff = git(tmp, "diff", "--no-color", "--", *FILES).stdout
            with open(OUT, "w", encoding="utf-8", newline="\n") as f:
                f.write(diff)
            print(f"{OUT}: {diff.count(chr(10))} linhas")
            return
        bad = 0
        for rel in files:
            with open(os.path.join(FW, rel), "rb") as f:
                cur = f.read().replace(b"\r\n", b"\n")
            with open(os.path.join(tmp, rel), "rb") as f:
                exp = f.read()
            if cur != exp:
                bad += 1
                print("difere:", rel)
        print("ok: os 4 patches reproduzem o checkout" if not bad else f"{bad} arquivo(s) diferem")
        sys.exit(1 if bad else 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
