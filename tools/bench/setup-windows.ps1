<#
  One-time setup of a Windows 11 test rig for building and benchmarking this fork.
  Run in an elevated PowerShell:  Set-ExecutionPolicy -Scope Process Bypass; .\tools\bench\setup-windows.ps1

  Installs: Git, CMake, Ninja, Python 3.12, VS 2022 Build Tools (C++ + clang-cl),
  Vulkan SDK (glslangValidator, vulkaninfo), Qt 6.10.3 msvc2022_64 (via aqtinstall),
  and Claude Code. Everything else (submodules, build) is done by build-windows.ps1.
#>
$ErrorActionPreference = "Stop"

function Install-Winget($id, $extra = @()) {
    Write-Host "==> $id"
    winget install --id $id --exact --accept-package-agreements --accept-source-agreements --silent @extra
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { # already installed
        throw "winget install $id failed ($LASTEXITCODE)"
    }
}

Install-Winget Git.Git
Install-Winget Kitware.CMake
Install-Winget Ninja-build.Ninja
Install-Winget Python.Python.3.12
Install-Winget KhronosGroup.VulkanSDK
Install-Winget Microsoft.VisualStudio.2022.BuildTools @(
    "--override",
    "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended " +
    "--add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset"
)

# Refresh PATH for this session.
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")

Write-Host "==> Qt 6.10.3 (msvc2022_64) into C:\Qt"
py -3.12 -m pip install --upgrade aqtinstall
py -3.12 -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O C:\Qt
[Environment]::SetEnvironmentVariable("Qt6_DIR", "C:\Qt\6.10.3\msvc2022_64", "User")

Write-Host "==> Claude Code"
irm https://claude.ai/install.ps1 | iex

Write-Host ""
Write-Host "Done. Open a NEW terminal, then:"
Write-Host "  git clone --recursive <your fork URL> C:\dev\KytyPS5; cd C:\dev\KytyPS5"
Write-Host "  .\tools\bench\build-windows.ps1"
Write-Host "  claude remote-control      # makes this machine drivable from the Claude app"
