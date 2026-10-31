# Dot-source este arquivo numa sessão PowerShell para ter a toolchain do PSXRecomp no PATH:
#   . .\recomp\env.ps1
# Toolchain (tudo instalado sem admin):
#   - GCC 16.2 MinGW-w64 UCRT (WinLibs)      $BAG2_TOOLS\winlibs\mingw64\bin
#   - CMake 4.4.4 + Ninja 1.13 (pip --user)  %APPDATA%\Python\Python314\Scripts
#   - Python 3.14                            já no PATH
#   - emsdk (Emscripten 6.0.13)              $BAG2_TOOLS\emsdk  (use emsdk_env.bat para o alvo WASM)
#   - psxrecomp (framework)                  $BAG2_TOOLS\psxrecomp
# $BAG2_TOOLS = pasta das ferramentas (winlibs, psxrecomp, realesrgan...). Defina a variável de ambiente
# BAG2_TOOLS; sem ela, usa a pasta "tools" ao lado da pasta do repositório.
if (-not $env:BAG2_TOOLS) { $env:BAG2_TOOLS = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'tools' }
$env:PSXRECOMP_ROOT = Join-Path $env:BAG2_TOOLS 'psxrecomp'
$gccBin = Join-Path $env:BAG2_TOOLS 'winlibs\mingw64\bin'
# O cmake embutido do WinLibs (4.4.3, GnuTLS sem CA bundle) falha nos downloads do FetchContent
# ("SSL peer certificate ... not OK"); o cmake/ninja do pip (build oficial Kitware) tem de vir ANTES.
$env:Path = "$env:APPDATA\Python\Python314\Scripts;" + $gccBin + ';' + $env:Path
$env:SSL_CERT_FILE = Join-Path $gccBin 'curl-ca-bundle.crt'
$env:CC  = Join-Path $gccBin 'gcc.exe'
$env:CXX = Join-Path $gccBin 'g++.exe'
