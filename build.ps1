# Windows build for Metronomicon (Disting NT plug-in), no make needed.
#
#   .\build.ps1                          # looks for the API in .\distingNT_API or ..\distingNT_API
#   .\build.ps1 -NtApi C:\path\to\distingNT_API
#
# Output: plugins\metronomicon.o  -> copy to the SD card at /programs/plug-ins/

param([string]$NtApi = "")

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# Find the API headers
if (-not $NtApi) {
    # Search this folder and its parent (a few levels deep, so nested "Extract All"
    # folders are found too). Prefer a 1.18.0 copy if there are several.
    $found = @()
    foreach ($root in @(".", "..")) {
        $found += Get-ChildItem -Path $root -Filter "api.h" -Recurse -Depth 4 -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "[\\/]include[\\/]distingnt[\\/]api\.h$" }
    }
    if ($found.Count -gt 0) {
        $pick = $found | Where-Object { $_.FullName -match "1\.18" } | Select-Object -First 1
        if (-not $pick) { $pick = $found | Select-Object -First 1 }
        $NtApi = $pick.Directory.Parent.Parent.FullName
    }
}
if (-not $NtApi -or -not (Test-Path (Join-Path $NtApi "include\distingnt\api.h"))) {
    Write-Host "Can't find the Disting NT API headers." -ForegroundColor Red
    Write-Host "Download the API version that matches your firmware, e.g. for firmware 1.18:"
    Write-Host "  https://github.com/expertsleepersltd/distingNT_API/archive/refs/tags/v1.18.0.zip"
    Write-Host "unzip it next to this folder, or run:  .\build.ps1 -NtApi <path-to-distingNT_API>"
    exit 1
}

# Find the compiler
$cxx = Get-Command "arm-none-eabi-c++" -ErrorAction SilentlyContinue
if (-not $cxx) { $cxx = Get-Command "arm-none-eabi-g++" -ErrorAction SilentlyContinue }
if (-not $cxx) {
    Write-Host "arm-none-eabi-c++ not found on PATH." -ForegroundColor Red
    Write-Host "Install the Arm GNU Toolchain (Windows, arm-none-eabi target) from"
    Write-Host "  https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"
    Write-Host "and tick 'Add path to environment variable' at the end of the installer,"
    Write-Host "then open a NEW PowerShell window and run this script again."
    exit 1
}

New-Item -ItemType Directory -Force -Path "plugins" | Out-Null

$flags = @(
    "-std=c++11", "-mcpu=cortex-m7", "-mfpu=fpv5-d16", "-mfloat-abi=hard", "-mthumb",
    "-fno-rtti", "-fno-exceptions", "-Os", "-fPIC", "-Wall",
    "-I$NtApi\include", "-Isrc",
    "-c", "-o", "plugins\metronomicon.o", "src\metronome.cpp"
)

Write-Host "Using API at $NtApi"
& $cxx.Source @flags
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed - see the errors above." -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Built plugins\metronomicon.o - copy it to /programs/plug-ins/ on the SD card." -ForegroundColor Green
