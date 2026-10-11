<#
  Configure and build the emulator on Windows exactly like upstream CI (clang-cl + Ninja),
  then stage a runnable install in _Build\windows\install.

    .\tools\bench\build-windows.ps1            # Release
    .\tools\bench\build-windows.ps1 -Config RelWithDebInfo
#>
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
Set-Location $root

# Load the VS 2022 x64 developer environment into this PowerShell session.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

$qt = if ($env:Qt6_DIR) { $env:Qt6_DIR } else { "C:\Qt\6.10.3\msvc2022_64" }
git submodule update --init --recursive

cmake -S . -B _Build/windows -G Ninja `
    -DCMAKE_BUILD_TYPE=$Config `
    -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
    -DCMAKE_PREFIX_PATH="$qt"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

cmake --build _Build/windows --target launcher --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

cmake --install _Build/windows --prefix _Build/windows/install
Write-Host "Built: $root\_Build\windows\install\kyty_emulator.exe"
