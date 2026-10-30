#!/usr/bin/env bash
# Re-run Ghidra headless analysis + export. Needs C:\tools\ghidra_12.1.4_PUBLIC and C:\tools\jdk-21*.
set -e
cd "$(dirname "$0")/../.."
export JAVA_HOME=$(ls -d /c/tools/jdk-21* | head -1); export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p archive/ghidra/project archive/ghidra/out
ARGS=(-import "$(cygpath -w "$PWD/extracted/disc/SLUS_011.59")" -loader PsxLoader)
[ -d archive/ghidra/project/bag2.rep ] && ARGS=(-process SLUS_011.59 -noanalysis)
/c/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless.bat "$(cygpath -w "$PWD/archive/ghidra/project")" bag2 "${ARGS[@]}" \
  -scriptPath "$(cygpath -w "$PWD/archive/ghidra/scripts")" -postScript ExportDecomp.java "$(cygpath -w "$PWD/archive/ghidra/out")" \
  -log "$(cygpath -w "$PWD/archive/ghidra/out/headless.log")" | grep -vE "ghidra_scripts$"
