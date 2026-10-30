# Build reproduzível do PSXRecomp + Bust A Groove 2 (nativo, Windows x64, GCC MinGW).
# Uso:  powershell -File recomp\build.ps1 [-Step framework|bios|game|runtime|all] [-Run]
# Pré-requisitos (tudo já instalado por este projeto; ver env.ps1):
#   C:\tools\psxrecomp (clone), C:\tools\winlibs (GCC 16.2), cmake/ninja via pip --user.
param(
    [ValidateSet('framework','bios','game','runtime','all')] [string]$Step = 'all',
    [switch]$Run
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\env.ps1"
$fw   = $env:PSXRECOMP_ROOT
$proj = "$repo\game"
$logs = "$repo\logs"
New-Item -ItemType Directory -Force $logs | Out-Null

function Invoke-Logged([string]$name, [scriptblock]$body) {
    Write-Host "== $name"
    & $body 2>&1 | Tee-Object -FilePath "$logs\$name.log"
    if ($LASTEXITCODE -ne 0) { throw "$name falhou (exit $LASTEXITCODE); veja $logs\$name.log" }
}

if ($Step -in 'framework','all') {
    # 1. Ferramentas do recompilador: psxrecomp-game, psxrecomp-bios, psxrecomp-toml, psxrecomp-analyze
    Invoke-Logged 'recompiler-build' {
        cmake -S "$fw\recompiler" -B "$fw\recompiler\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
        if ($LASTEXITCODE -eq 0) { cmake --build "$fw\recompiler\build" }
    }
}
if ($Step -in 'bios','all') {
    # 2. C do OpenBIOS (generated/OpenBIOS_*.c no framework). Precisa de bash (Git for Windows).
    Invoke-Logged 'regen_bios' {
        $env:PATH = "C:\tools\winlibs\mingw64\bin;$env:APPDATA\Python\Python314\Scripts;$env:PATH"
        & "C:\Program Files\Git\bin\bash.exe" -lc "cd '$($fw -replace '\\','/')' && PATH='/c/tools/winlibs/mingw64/bin:/c/Users/sem-m/AppData/Roaming/Python/Python314/Scripts:'`$PATH bash tools/regen_bios.sh --config bios/OpenBIOS.toml"
    }
}
if ($Step -in 'game','all') {
    # 3. Seeds (alvos de JAL) + C do jogo (generated/SLUS_011.59_*.c)
    Invoke-Logged 'psxrecomp-toml' {
        & "$fw\recompiler\build\psxrecomp-toml.exe" "$repo\extracted\disc\SLUS_011.59" --output "$logs\game.toml.autogen" --seeds "$proj\seeds\functions.txt" --name "Bust A Groove 2" --id SLUS-01159
    }
    Invoke-Logged 'psxrecomp-game' {
        Push-Location $proj
        try { & "$fw\recompiler\build\psxrecomp-game.exe" --config game.toml } finally { Pop-Location }
    }
}
if ($Step -in 'runtime','all') {
    # 4. Runtime nativo do jogo (SDL3 é baixado e compilado na primeira vez)
    Invoke-Logged 'bag2-runtime-build' {
        cmake -S $proj -B "$proj\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DPSXRECOMP_ROOT=($fw -replace '\\','/') -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF
        if ($LASTEXITCODE -eq 0) { cmake --build "$proj\build" --target psx-runtime }
    }
}
if ($Run) {
    Push-Location "$proj\build"
    try { $env:PSX_FRAME_REPORT_MS = "1000"; & ".\Bust_A_Groove_2_Recompiled.exe" --game game.toml --disc "$repo\disc\Bust A Groove 2 (USA).cue" } finally { Pop-Location }
}
