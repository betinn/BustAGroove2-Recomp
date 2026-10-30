# Build WASM (Emscripten) do Bust A Groove 2 recompilado, com o fork kem0x/psxrecomp.
# Uso:  powershell -File archive\web-recomp\build-web.ps1 [-Step framework|game|configure|build|copy|all]
# Pré-requisitos (ver docs/recomp.md §7):
#   C:\tools\psxrecomp-web  = git clone --recurse-submodules https://github.com/kem0x/psxrecomp (branch compat/windows-web-stability)
#   C:\tools\emsdk          = Emscripten 6.0.13 instalado/ativado (emsdk install latest; emsdk activate latest)
#   C:\tools\winlibs, cmake/ninja do pip (env.ps1) — o recompilador do fork é nativo (GCC).
param(
    [ValidateSet('framework','game','configure','build','copy','all')] [string]$Step = 'all'
)
$ErrorActionPreference = 'Stop'
$repo  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\env.ps1"
$fw    = 'C:\tools\psxrecomp-web'
$emsdk = 'C:\tools\emsdk'
$proj  = "$PSScriptRoot\bag2"
$src   = "$proj\web"
$bld   = "$proj\build-web"
$logs  = "$PSScriptRoot\logs"
$out   = "$repo\web\public\recomp"
New-Item -ItemType Directory -Force $logs | Out-Null

# Equivalente ao emsdk_env.bat (que só mexe no PATH/EMSDK). O python/node do emsdk são usados pelo emcc.
$env:EMSDK = $emsdk
$env:EM_CONFIG = "$emsdk\.emscripten"
$env:EMSDK_PYTHON = (Get-ChildItem "$emsdk\python\*\python.exe" | Select-Object -First 1).FullName
$env:Path = "$emsdk;$emsdk\upstream\emscripten;" + $env:Path

function Invoke-Logged([string]$name, [scriptblock]$body) {
    Write-Host "== $name"
    # emcmake/emcc escrevem progresso no stderr; com 'Stop' o PowerShell 5.1 transformaria isso em erro fatal.
    $ErrorActionPreference = 'Continue'
    & $body 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath "$logs\$name.log"
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "$name falhou (exit $LASTEXITCODE); veja $logs\$name.log" }
}

if ($Step -in 'framework','all') {
    # 1. Recompilador do fork (nativo, GCC): psxrecomp-game/-bios/-toml
    Invoke-Logged 'web-recompiler-build' {
        cmake -S "$fw\recompiler" -B "$fw\recompiler\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
        if ($LASTEXITCODE -eq 0) { cmake --build "$fw\recompiler\build" }
    }
}
if ($Step -in 'game','all') {
    # 2. C do jogo no esquema do fork (generated-web/SLUS_011.59_full.c + _dispatch.c). Precisa das DLLs do
    #    WinLibs no PATH (env.ps1), senão o exe morre com 0xC0000139 (libstdc++ errada do Git/mingw64).
    Invoke-Logged 'web-psxrecomp-game' {
        Push-Location $proj
        try { & "$fw\recompiler\build\psxrecomp-game.exe" --config game-web.toml } finally { Pop-Location }
    }
}
if ($Step -in 'configure','all') {
    # 3. Configure com a toolchain do Emscripten (emcmake). SDL2 vem da port do Emscripten (--use-port=sdl2).
    Invoke-Logged 'web-configure' {
        & "$emsdk\upstream\emscripten\emcmake.exe" cmake -S $src -B $bld -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_LAUNCHER=OFF -DPSX_DEBUG_TOOLS=OFF -DBAG2_WEB_DEBUG=OFF "-DPSXRECOMP_ROOT=$($fw -replace '\\','/')"
    }
}
if ($Step -in 'build','all') {
    # 4. Build (o SLUS_011.59_full.c de 21 MB demora; -j default do ninja)
    Invoke-Logged 'web-build' {
        cmake --build $bld --target psx-runtime
    }
}
if ($Step -in 'copy','all') {
    # 5. Saída para a página Vite (web/public/recomp/ está no .gitignore)
    New-Item -ItemType Directory -Force "$out\bios" | Out-Null
    Copy-Item "$bld\bag2.js","$bld\bag2.wasm" $out -Force
    if (Test-Path "$bld\bag2.ww.js") { Copy-Item "$bld\bag2.ww.js" $out -Force }
    if (Test-Path "$bld\bag2.worker.js") { Copy-Item "$bld\bag2.worker.js" $out -Force }
    Copy-Item "$bld\bios\openbios.bin","$bld\bios\OpenBIOS.LICENSE" "$out\bios" -Force
    Get-ChildItem $out -Recurse -File | Select-Object FullName, Length
}
