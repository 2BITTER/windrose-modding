# BBT Release Packager
# Builds the release zip for Nexus/CurseForge/Vortex
# Usage: powershell -ExecutionPolicy Bypass -File build_release.ps1

$ErrorActionPreference = "Stop"

$modSource = "G:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\BetterBuildingTools"
$outputDir = "F:\WindroseModing\Windrose\releases"
$tempDir   = "F:\WindroseModing\Windrose\releases\_staging"

# Pull version from the DLL's Lua bridge by reading dllmain.cpp
$srcFile = "G:\UnrealEngine\WindRose\BuildingUndo\src\dllmain.cpp"
$versionLine = Select-String -Path $srcFile -Pattern 'ModVersion\s*=\s*STR\("([^"]+)"\)' | Select-Object -First 1
if (-not $versionLine) {
    Write-Host "ERROR: Could not find ModVersion in dllmain.cpp" -ForegroundColor Red
    exit 1
}
$version = $versionLine.Matches[0].Groups[1].Value
Write-Host "Building BBT release v$version" -ForegroundColor Cyan

$zipName = "BetterBuildingTools_v$version.zip"
$zipPath = Join-Path $outputDir $zipName

# Clean staging
if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }
New-Item -ItemType Directory -Force $tempDir | Out-Null
$modStage = Join-Path $tempDir "BetterBuildingTools"
New-Item -ItemType Directory -Force $modStage | Out-Null

# Copy mod files
"1" | Out-File -FilePath (Join-Path $modStage "enabled.txt") -Encoding ascii -NoNewline
Copy-Item (Join-Path $modSource "config.txt") $modStage
New-Item -ItemType Directory -Force (Join-Path $modStage "dlls") | Out-Null
Copy-Item (Join-Path $modSource "dlls\main.dll") (Join-Path $modStage "dlls\main.dll")
New-Item -ItemType Directory -Force (Join-Path $modStage "Scripts") | Out-Null
Copy-Item (Join-Path $modSource "Scripts\main.lua") (Join-Path $modStage "Scripts\main.lua")

# Create output directory
if (-not (Test-Path $outputDir)) { New-Item -ItemType Directory -Force $outputDir | Out-Null }

# Remove old zip if exists
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

# Build zip
Compress-Archive -Path $modStage -DestinationPath $zipPath -CompressionLevel Optimal

# Clean staging
Remove-Item -Recurse -Force $tempDir

$size = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
Write-Host ""
Write-Host "Release built: $zipPath ($size KB)" -ForegroundColor Green
Write-Host ""
Write-Host "Contents:" -ForegroundColor Yellow
Write-Host "  BetterBuildingTools/"
Write-Host "    enabled.txt"
Write-Host "    config.txt"
Write-Host "    dlls/main.dll"
Write-Host "    Scripts/main.lua"
