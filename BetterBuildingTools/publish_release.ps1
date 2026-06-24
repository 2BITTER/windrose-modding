# publish_release.ps1 — Full release chain for BBT
# Handles: backup, Dev→Released, GitHub push + release, zip build, Nexus + CurseForge upload
#
# Usage (flags are opt-in — no flags = nothing happens):
#   powershell -ExecutionPolicy Bypass -File publish_release.ps1 -GitHub                          # backup + Dev→Released + commit + zip + gh release
#   powershell -ExecutionPolicy Bypass -File publish_release.ps1 -GitHub -Nexus -CurseForge       # full release to all platforms
#   powershell -ExecutionPolicy Bypass -File publish_release.ps1 -Nexus                           # re-upload existing zip to Nexus only
#   powershell -ExecutionPolicy Bypass -File publish_release.ps1 -CurseForge                      # re-upload existing zip to CurseForge only
#   powershell -ExecutionPolicy Bypass -File publish_release.ps1 -Nexus -CurseForge               # re-upload existing zip to both platforms

param(
    [switch]$GitHub,
    [switch]$Nexus,
    [switch]$CurseForge,
    [string]$Changelog = ""
)

$ErrorActionPreference = "Stop"

# ── PATHS ─────────────────────────────────────────────────────────────
$repoRoot   = Split-Path $PSScriptRoot -Parent
$bbtRoot    = $PSScriptRoot
$devDir     = Join-Path $bbtRoot "-Dev-"
$releasedDir = Join-Path $bbtRoot "Released"
$backupsDir = Join-Path $repoRoot "BackUps\BetterBuildingTools"

$cppSource  = "G:\UnrealEngine\WindRose\BuildingUndo\src"
$luaSource  = "G:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\BetterBuildingTools\Scripts"
$modSource  = "G:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\BetterBuildingTools"
$outputDir  = "F:\WindroseModing\Windrose\releases"
$tempDir    = "F:\WindroseModing\Windrose\releases\_staging"

$nexusApiBase   = "https://api.nexusmods.com/v3"
$nexusModFileId = "7556516"
$cfApiBase      = "https://windrose.curseforge.com/api"
$cfProjectId    = "1580564"

# ── VERSION ───────────────────────────────────────────────────────────
$srcFile = Join-Path $cppSource "dllmain.cpp"
$versionLine = Select-String -Path $srcFile -Pattern 'ModVersion\s*=\s*STR\("([^"]+)"\)' | Select-Object -First 1
if (-not $versionLine) {
    Write-Host "ERROR: Could not find ModVersion in dllmain.cpp" -ForegroundColor Red
    exit 1
}
$version = $versionLine.Matches[0].Groups[1].Value
$zipName = "BetterBuildingTools_v$version.zip"
$zipPath = Join-Path $outputDir $zipName

if (-not $GitHub -and -not $Nexus -and -not $CurseForge) {
    Write-Host "No flags specified. Nothing to do." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Usage:" -ForegroundColor Gray
    Write-Host "  -GitHub                    Backup + Dev->Released + commit + zip + gh release" -ForegroundColor Gray
    Write-Host "  -Nexus                     Upload zip to Nexus" -ForegroundColor Gray
    Write-Host "  -CurseForge                Upload zip to CurseForge" -ForegroundColor Gray
    Write-Host "  -Changelog 'description'   Set changelog text (optional)" -ForegroundColor Gray
    exit 0
}

Write-Host ""
Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "  BBT Release v$version" -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan
Write-Host ""

if (-not $Changelog) {
    $Changelog = "v$version update"
}

# ── GITHUB ────────────────────────────────────────────────────────────
if ($GitHub) {
    # Step 1: Sync working source to -Dev-
    Write-Host "[1/6] Syncing working source to -Dev-..." -ForegroundColor Yellow
    Get-ChildItem "$cppSource\*" -Include "*.cpp","*.h" | Copy-Item -Destination "$devDir\src\" -Force
    Get-ChildItem "$luaSource\*" -Include "*.lua" | Copy-Item -Destination "$devDir\Scripts\" -Force
    if (Test-Path (Join-Path $modSource "config.txt")) {
        Copy-Item (Join-Path $modSource "config.txt") "$devDir\config.example.txt" -Force
    }
    Write-Host "  Done." -ForegroundColor Gray

    # Step 2: Backup current Released → BackUps (previous live version)
    Write-Host "[2/6] Backing up current Released..." -ForegroundColor Yellow
    $oldVersion = "unknown"
    $oldSrcFile = Join-Path $releasedDir "src\dllmain.cpp"
    if (Test-Path $oldSrcFile) {
        $oldMatch = Select-String -Path $oldSrcFile -Pattern 'ModVersion\s*=\s*STR\("([^"]+)"\)' | Select-Object -First 1
        if ($oldMatch) { $oldVersion = $oldMatch.Matches[0].Groups[1].Value }
    }
    $backupTarget = Join-Path $backupsDir "previous_live_v$oldVersion"
    if (Test-Path $releasedDir) {
        $hasFiles = (Get-ChildItem $releasedDir -Recurse -File).Count -gt 0
        if ($hasFiles) {
            if (Test-Path $backupTarget) { Remove-Item -Recurse -Force $backupTarget }
            New-Item -ItemType Directory -Force $backupTarget | Out-Null
            Copy-Item "$releasedDir\*" $backupTarget -Recurse -Force
            Write-Host "  Backed up to: $backupTarget (was v$oldVersion)" -ForegroundColor Gray
        } else {
            Write-Host "  Released is empty, skipping backup." -ForegroundColor Gray
        }
    }

    # Step 3: Copy -Dev- → Released
    Write-Host "[3/6] Copying -Dev- to Released..." -ForegroundColor Yellow
    if (Test-Path "$releasedDir\src") { Get-ChildItem "$releasedDir\src" | Remove-Item -Force }
    if (Test-Path "$releasedDir\Scripts") { Get-ChildItem "$releasedDir\Scripts" | Remove-Item -Force }
    Get-ChildItem "$devDir\src\*" | Copy-Item -Destination "$releasedDir\src\" -Force
    Get-ChildItem "$devDir\Scripts\*" | Copy-Item -Destination "$releasedDir\Scripts\" -Force
    Copy-Item "$devDir\config.example.txt" "$releasedDir\config.example.txt" -Force
    Write-Host "  Done." -ForegroundColor Gray

    # Step 4: Build release zip (packaged mod for users — DLL + Lua + config + enabled.txt)
    Write-Host "[4/6] Building release zip..." -ForegroundColor Yellow
    if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }
    New-Item -ItemType Directory -Force $tempDir | Out-Null
    $modStage = Join-Path $tempDir "BetterBuildingTools"
    New-Item -ItemType Directory -Force $modStage | Out-Null

    "1" | Out-File -FilePath (Join-Path $modStage "enabled.txt") -Encoding ascii -NoNewline
    Copy-Item (Join-Path $modSource "config.txt") $modStage
    New-Item -ItemType Directory -Force (Join-Path $modStage "dlls") | Out-Null
    Copy-Item (Join-Path $modSource "dlls\main.dll") (Join-Path $modStage "dlls\main.dll")
    New-Item -ItemType Directory -Force (Join-Path $modStage "Scripts") | Out-Null
    Get-ChildItem "$luaSource\*" -Include "*.lua" | Copy-Item -Destination (Join-Path $modStage "Scripts") -Force

    if (-not (Test-Path $outputDir)) { New-Item -ItemType Directory -Force $outputDir | Out-Null }
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path $modStage -DestinationPath $zipPath -CompressionLevel Optimal
    Remove-Item -Recurse -Force $tempDir

    $size = [math]::Round((Get-Item $zipPath).Length / 1KB, 1)
    Write-Host "  Built: $zipPath ($size KB)" -ForegroundColor Gray

    # Step 5: Git commit + push
    Write-Host "[5/6] Committing and pushing to GitHub..." -ForegroundColor Yellow
    Set-Location $repoRoot
    git add -A
    $status = git status --porcelain
    if ($status) {
        git commit -m "release: v$version - $Changelog"
        git push
        Write-Host "  Pushed." -ForegroundColor Gray
    } else {
        Write-Host "  No changes to commit." -ForegroundColor Gray
    }

    # Step 6: Create GitHub Release with zip attached
    Write-Host "[6/6] Creating GitHub Release..." -ForegroundColor Yellow
    $tagName = "v$version"
    $releaseName = "BetterBuildingTools v$version"
    & gh release create $tagName $zipPath --title $releaseName --notes $Changelog
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  GitHub Release created: $tagName" -ForegroundColor Gray
    } else {
        Write-Host "  WARNING: gh release create failed (tag may already exist)" -ForegroundColor Yellow
    }

    Write-Host ""
    Write-Host "GitHub release complete!" -ForegroundColor Green
    Write-Host ""
}

# ── NEXUS ─────────────────────────────────────────────────────────────
if ($Nexus) {
    Write-Host "===================================================" -ForegroundColor Magenta
    Write-Host "  Publishing to Nexus Mods..." -ForegroundColor Magenta
    Write-Host "===================================================" -ForegroundColor Magenta

    if (-not (Test-Path $zipPath)) {
        Write-Host "ERROR: ZIP not found at $zipPath" -ForegroundColor Red
        Write-Host "Run with -GitHub first to build the zip." -ForegroundColor Red
        exit 1
    }
    $apiKey = $env:NEXUS_API_KEY
    if (-not $apiKey) {
        $apiKey = [System.Environment]::GetEnvironmentVariable("NEXUS_API_KEY", "User")
    }
    if (-not $apiKey) {
        Write-Host "ERROR: NEXUS_API_KEY not set. Use: setx NEXUS_API_KEY `"your-key`"" -ForegroundColor Red
        exit 1
    }

    $headers = @{ "apikey" = $apiKey; "Content-Type" = "application/json" }
    $fileInfo = Get-Item $zipPath
    $fileSizeBytes = $fileInfo.Length

    Write-Host "[1/5] Creating upload session..." -ForegroundColor Yellow
    $uploadBody = @{ size_bytes = $fileSizeBytes; filename = $zipName } | ConvertTo-Json
    $uploadResp = Invoke-RestMethod -Uri "$nexusApiBase/uploads/multipart" -Method Post -Headers $headers -Body $uploadBody
    $uploadId = $uploadResp.data.id
    $partUrls = $uploadResp.data.part_presigned_urls
    $partSize = $uploadResp.data.part_size_bytes
    $completeUrl = $uploadResp.data.complete_presigned_url
    Write-Host "  Upload ID: $uploadId ($($partUrls.Count) part(s))" -ForegroundColor Gray

    Write-Host "[2/5] Uploading $zipName ($([math]::Round($fileSizeBytes/1KB, 1)) KB)..." -ForegroundColor Yellow
    $fileBytes = [System.IO.File]::ReadAllBytes($zipPath)
    $etags = @()
    for ($i = 0; $i -lt $partUrls.Count; $i++) {
        $offset = $i * $partSize
        $remaining = $fileBytes.Length - $offset
        $chunkSize = [math]::Min($partSize, $remaining)
        $tmpPart = Join-Path $env:TEMP "bbt_upload_part_$i.bin"
        [System.IO.File]::WriteAllBytes($tmpPart, $fileBytes[$offset..($offset + $chunkSize - 1)])
        $partNum = $i + 1
        Write-Host "  Part $partNum/$($partUrls.Count) ($chunkSize bytes)..." -ForegroundColor Gray
        $curlOut = & curl.exe -s -D - -o NUL -X PUT -H "Content-Type: application/octet-stream" -T $tmpPart $partUrls[$i] 2>&1
        $curlExit = $LASTEXITCODE
        Remove-Item $tmpPart -Force -ErrorAction SilentlyContinue
        if ($curlExit -ne 0) {
            Write-Host "ERROR: Part $partNum upload failed (exit $curlExit)" -ForegroundColor Red
            exit 1
        }
        $etagLine = ($curlOut | Select-String -Pattern "ETag:\s*(.+)") | Select-Object -First 1
        if (-not $etagLine) {
            Write-Host "ERROR: No ETag returned for part $partNum" -ForegroundColor Red
            exit 1
        }
        $etag = $etagLine.Matches[0].Groups[1].Value.Trim().Replace('"', '')
        $etags += $etag
        Write-Host "    ETag: $etag" -ForegroundColor Gray
    }

    Write-Host "[3/5] Completing upload..." -ForegroundColor Yellow
    $xmlParts = ""
    for ($i = 0; $i -lt $etags.Count; $i++) {
        $xmlParts += "<Part><PartNumber>$($i+1)</PartNumber><ETag>$($etags[$i])</ETag></Part>"
    }
    $completeXml = "<CompleteMultipartUpload>$xmlParts</CompleteMultipartUpload>"
    $curlOut = & curl.exe -s --fail-with-body -X POST -H "Content-Type: application/xml" -d $completeXml $completeUrl 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Multipart complete failed: $curlOut" -ForegroundColor Red
        exit 1
    }
    Invoke-RestMethod -Uri "$nexusApiBase/uploads/$uploadId/finalise" -Method Post -Headers $headers
    Write-Host "  Finalised." -ForegroundColor Gray

    Write-Host "[4/5] Waiting for processing..." -ForegroundColor Yellow
    $maxAttempts = 30
    $attempt = 0
    do {
        Start-Sleep -Seconds 2
        $attempt++
        $statusResp = Invoke-RestMethod -Uri "$nexusApiBase/uploads/$uploadId" -Method Get -Headers $headers
        $state = $statusResp.data.state
        Write-Host "  State: $state (attempt $attempt/$maxAttempts)" -ForegroundColor Gray
    } while ($state -ne "available" -and $attempt -lt $maxAttempts)
    if ($state -ne "available") {
        Write-Host "ERROR: Upload did not become available after $maxAttempts attempts." -ForegroundColor Red
        exit 1
    }

    Write-Host "[5/5] Publishing v$version to Nexus..." -ForegroundColor Yellow
    $versionBody = @{
        upload_id                    = $uploadId
        name                         = "BetterBuildingTools v$version"
        version                      = $version
        file_category                = "main"
        archive_existing_file        = $true
        primary_mod_manager_download = $true
        allow_mod_manager_download   = $true
    } | ConvertTo-Json
    $versionResp = Invoke-RestMethod -Uri "$nexusApiBase/mod-files/$nexusModFileId/versions" -Method Post -Headers $headers -Body $versionBody
    Write-Host ""
    Write-Host "  v$version is LIVE on Nexus Mods" -ForegroundColor Green
    Write-Host "  Version ID: $($versionResp.data.version.id)" -ForegroundColor Gray
    Write-Host ""
}

# ── CURSEFORGE ────────────────────────────────────────────────────────
if ($CurseForge) {
    Write-Host "===================================================" -ForegroundColor Magenta
    Write-Host "  Publishing to CurseForge..." -ForegroundColor Magenta
    Write-Host "===================================================" -ForegroundColor Magenta

    if (-not (Test-Path $zipPath)) {
        Write-Host "ERROR: ZIP not found at $zipPath" -ForegroundColor Red
        Write-Host "Run with -GitHub first to build the zip." -ForegroundColor Red
        exit 1
    }
    $cfApiKey = [System.Environment]::GetEnvironmentVariable("CURSEFORGE_API_KEY", "User")
    if (-not $cfApiKey) { $cfApiKey = $env:CURSEFORGE_API_KEY }
    if (-not $cfApiKey) {
        Write-Host "ERROR: CURSEFORGE_API_KEY not set. Use: setx CURSEFORGE_API_KEY `"your-token`"" -ForegroundColor Red
        exit 1
    }

    $cfMetadata = @{
        changelog     = $Changelog
        changelogType = "text"
        displayName   = "BetterBuildingTools v$version"
        releaseType   = "release"
    } | ConvertTo-Json -Compress
    $cfMetaFile = Join-Path $env:TEMP "bbt_cf_metadata.json"
    [System.IO.File]::WriteAllText($cfMetaFile, $cfMetadata)

    Write-Host "Uploading $zipName to CurseForge..." -ForegroundColor Yellow
    $cfUrl = "$cfApiBase/projects/$cfProjectId/upload-file"
    $curlOut = & curl.exe -s --fail-with-body -X POST `
        -H "X-Api-Token: $cfApiKey" `
        -F "metadata=<$cfMetaFile" `
        -F "file=@$zipPath" `
        $cfUrl 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: CurseForge upload failed: $curlOut" -ForegroundColor Red
        exit 1
    }

    $cfResp = $curlOut | ConvertFrom-Json
    Write-Host ""
    Write-Host "  v$version is LIVE on CurseForge" -ForegroundColor Green
    Write-Host "  File ID: $($cfResp.id)" -ForegroundColor Gray
    Write-Host ""
}

# ── SUMMARY ───────────────────────────────────────────────────────────
Write-Host "===================================================" -ForegroundColor Green
Write-Host "  Release v$version complete!" -ForegroundColor Green
if ($GitHub)     { Write-Host "    GitHub:     YES" -ForegroundColor Green }
if ($Nexus)      { Write-Host "    Nexus:      YES" -ForegroundColor Green }
if ($CurseForge) { Write-Host "    CurseForge: YES" -ForegroundColor Green }
Write-Host "===================================================" -ForegroundColor Green
