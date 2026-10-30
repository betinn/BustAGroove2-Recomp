# Dot-source este arquivo numa sessão PowerShell para ter a toolchain do PSXRecomp no PATH:
#   . .\recomp\env.ps1
# Toolchain (tudo instalado sem admin):
#   - GCC 16.2 MinGW-w64 UCRT (WinLibs)      C:\tools\winlibs\mingw64\bin
#   - CMake 4.4.4 + Ninja 1.13 (pip --user)  %APPDATA%\Python\Python314\Scripts
#   - Python 3.14                            já no PATH
#   - emsdk (Emscripten 6.0.13)              C:\tools\emsdk  (use emsdk_env.bat para o alvo WASM)
#   - psxrecomp (framework)                  C:\tools\psxrecomp
$env:PSXRECOMP_ROOT = 'C:\tools\psxrecomp'
# O cmake embutido do WinLibs (4.4.3, GnuTLS sem CA bundle) falha nos downloads do FetchContent
# ("SSL peer certificate ... not OK"); o cmake/ninja do pip (build oficial Kitware) tem de vir ANTES.
$env:Path = "$env:APPDATA\Python\Python314\Scripts;" + 'C:\tools\winlibs\mingw64\bin;' + $env:Path
$env:SSL_CERT_FILE = 'C:\tools\winlibs\mingw64\bin\curl-ca-bundle.crt'
$env:CC  = 'C:\tools\winlibs\mingw64\bin\gcc.exe'
$env:CXX = 'C:\tools\winlibs\mingw64\bin\g++.exe'
