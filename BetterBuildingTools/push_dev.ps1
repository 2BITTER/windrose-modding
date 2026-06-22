# push_dev.ps1 — Sync working source to -Dev- folder and push to GitHub
# Run anytime during development to keep the repo's Dev folder current.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File push_dev.ps1
#   powershell -ExecutionPolicy Bypass -File push_dev.ps1 -Message "description of changes"

param(
    [string]$Message = "dev sync"
)

$ErrorActionPreference = "Stop"

$cppSource  = "G:\UnrealEngine\WindRose\BuildingUndo\src"
$luaSource  = "G:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\BetterBuildingTools\Scripts"
$configSource = "G:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\BetterBuildingTools\config.txt"
$devDir     = "$PSScriptRoot\-Dev-"
$repoRoot   = Split-Path $PSScriptRoot -Parent

# Sync C++ source
Write-Host "Syncing C++ source..." -ForegroundColor Cyan
Get-ChildItem "$cppSource\*" -Include "*.cpp","*.h" | Copy-Item -Destination "$devDir\src\" -Force

# Sync Lua scripts (exclude Vortex marker)
Write-Host "Syncing Lua scripts..." -ForegroundColor Cyan
Get-ChildItem "$luaSource\*" -Include "*.lua" | Copy-Item -Destination "$devDir\Scripts\" -Force

# Sync config as example (strip any personal values — just copy as-is for now)
if (Test-Path $configSource) {
    Copy-Item $configSource "$devDir\config.example.txt" -Force
}

Write-Host "Files synced to -Dev-" -ForegroundColor Green

# Git commit and push
Set-Location $repoRoot
git add -A
$status = git status --porcelain
if ($status) {
    git commit -m "dev: $Message"
    git push
    Write-Host "Pushed to GitHub" -ForegroundColor Green
} else {
    Write-Host "No changes to commit" -ForegroundColor Yellow
}
