# BBT Release Packager + Nexus/CurseForge Publisher
# Builds the release zip and publishes to mod platforms
# Usage:
#   powershell -ExecutionPolicy Bypass -File build_release.ps1                        # build only
#   powershell -ExecutionPolicy Bypass -File build_release.ps1 -Publish               # build + upload to Nexus + CurseForge
#   powershell -ExecutionPolicy Bypass -File build_release.ps1 -PublishOnly            # upload existing zip to Nexus + CurseForge
#   powershell -ExecutionPolicy Bypass -File build_release.ps1 -Publish -NexusOnly     # build + Nexus only
#   powershell -ExecutionPolicy Bypass -File build_release.ps1 -Publish -CurseForgeOnly # build + CurseForge only

param(
    [switch]$Publish,
    [switch]$PublishOnly,
    [switch]$NexusOnly,
    [switch]$CurseForgeOnly
)

$ErrorActionPreference = "Stop"

$modSource = "G:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\BetterBuildingTools"
$outputDir = "F:\WindroseModing\Windrose\releases"
$tempDir   = "F:\WindroseModing\Windrose\releases\_staging"

$nexusApiBase      = "https://api.nexusmods.com/v3"
$nexusModFileId    = "7556516"
$cfApiBase         = "https://windrose.curseforge.com/api"
$cfProjectId       = "1580564"

$doNexus      = ($Publish -or $PublishOnly) -and (-not $CurseForgeOnly)
$doCurseForge = ($Publish -or $PublishOnly) -and (-not $NexusOnly)

# Pull version from the DLL's Lua bridge by reading dllmain.cpp
$srcFile = "G:\UnrealEngine\WindRose\BuildingUndo\src\dllmain.cpp"
$versionLine = Select-String -Path $srcFile -Pattern 'ModVersion\s*=\s*STR\("([^"]+)"\)' | Select-Object -First 1
if (-not $versionLine) {
    Write-Host "ERROR: Could not find ModVersion in dllmain.cpp" -ForegroundColor Red
    exit 1
}
$version = $versionLine.Matches[0].Groups[1].Value
$zipName = "BetterBuildingTools_v$version.zip"
$zipPath = Join-Path $outputDir $zipName

# ── BUILD ──────────────────────────────────────────────────────────────
if (-not $PublishOnly) {
    Write-Host "Building BBT release v$version" -ForegroundColor Cyan

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
}

# ── PUBLISH TO NEXUS ───────────────────────────────────────────────────
if ($doNexus) {
    Write-Host ""
    Write-Host "===================================================" -ForegroundColor Magenta
    Write-Host "  Publishing to Nexus Mods..." -ForegroundColor Magenta
    Write-Host "===================================================" -ForegroundColor Magenta

    # Validate
    if (-not (Test-Path $zipPath)) {
        Write-Host "ERROR: ZIP not found at $zipPath" -ForegroundColor Red
        Write-Host "Run without -PublishOnly first, or check the version." -ForegroundColor Red
        exit 1
    }
    $apiKey = $env:NEXUS_API_KEY
    if (-not $apiKey) {
        $apiKey = [System.Environment]::GetEnvironmentVariable("NEXUS_API_KEY", "User")
    }
    if (-not $apiKey) {
        Write-Host "ERROR: NEXUS_API_KEY environment variable not set." -ForegroundColor Red
        Write-Host "Set it with: setx NEXUS_API_KEY `"your-key-here`"" -ForegroundColor Red
        exit 1
    }

    $headers = @{ "apikey" = $apiKey; "Content-Type" = "application/json" }
    $fileInfo = Get-Item $zipPath
    $fileSizeBytes = $fileInfo.Length

    # Step 1: Create multipart upload session (single-part presigned URLs have signing issues)
    Write-Host ""
    Write-Host "[1/5] Creating upload session..." -ForegroundColor Yellow
    $uploadBody = @{ size_bytes = $fileSizeBytes; filename = $zipName } | ConvertTo-Json
    $uploadResp = Invoke-RestMethod -Uri "$nexusApiBase/uploads/multipart" -Method Post -Headers $headers -Body $uploadBody
    $uploadId = $uploadResp.data.id
    $partUrls = $uploadResp.data.part_presigned_urls
    $partSize = $uploadResp.data.part_size_bytes
    $completeUrl = $uploadResp.data.complete_presigned_url
    Write-Host "  Upload ID: $uploadId ($($partUrls.Count) part(s))" -ForegroundColor Gray

    # Step 2: Upload parts via curl
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

    # Step 3: Complete multipart upload + finalise
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

    # Step 4: Poll until available
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
        Write-Host "Upload ID: $uploadId -- check manually on Nexus." -ForegroundColor Red
        exit 1
    }

    # Step 5: Create new version on the mod file
    Write-Host "[5/5] Publishing v$version to Nexus..." -ForegroundColor Yellow
    $versionBody = @{
        upload_id                  = $uploadId
        name                       = "BetterBuildingTools v$version"
        version                    = $version
        file_category              = "main"
        archive_existing_file      = $true
        primary_mod_manager_download = $true
        allow_mod_manager_download   = $true
    } | ConvertTo-Json
    $versionResp = Invoke-RestMethod -Uri "$nexusApiBase/mod-files/$nexusModFileId/versions" -Method Post -Headers $headers -Body $versionBody
    Write-Host ""
    Write-Host "Published!" -ForegroundColor Green
    Write-Host "  Version: $version" -ForegroundColor Green
    Write-Host "  Version ID: $($versionResp.data.version.id)" -ForegroundColor Gray
    Write-Host ""
    Write-Host "===================================================" -ForegroundColor Green
    Write-Host "  v$version is LIVE on Nexus Mods" -ForegroundColor Green
    Write-Host "===================================================" -ForegroundColor Green
}

# ── PUBLISH TO CURSEFORGE ─────────────────────────────────────────────
if ($doCurseForge) {
    Write-Host ""
    Write-Host "===================================================" -ForegroundColor Magenta
    Write-Host "  Publishing to CurseForge..." -ForegroundColor Magenta
    Write-Host "===================================================" -ForegroundColor Magenta

    # Validate
    if (-not (Test-Path $zipPath)) {
        Write-Host "ERROR: ZIP not found at $zipPath" -ForegroundColor Red
        Write-Host "Run without -PublishOnly first, or check the version." -ForegroundColor Red
        exit 1
    }
    $cfApiKey = [System.Environment]::GetEnvironmentVariable("CURSEFORGE_API_KEY", "User")
    if (-not $cfApiKey) {
        $cfApiKey = $env:CURSEFORGE_API_KEY
    }
    if (-not $cfApiKey) {
        Write-Host "ERROR: CURSEFORGE_API_KEY environment variable not set." -ForegroundColor Red
        Write-Host "Set it with: setx CURSEFORGE_API_KEY `"your-token-here`"" -ForegroundColor Red
        exit 1
    }

    # Build metadata JSON — write to temp file to avoid PowerShell/curl quoting issues
    $cfMetadata = @{
        changelog     = "v$version update"
        changelogType = "text"
        displayName   = "BetterBuildingTools v$version"
        releaseType   = "release"
    } | ConvertTo-Json -Compress
    $cfMetaFile = Join-Path $env:TEMP "bbt_cf_metadata.json"
    [System.IO.File]::WriteAllText($cfMetaFile, $cfMetadata)

    Write-Host ""
    Write-Host "Uploading $zipName to CurseForge (project $cfProjectId)..." -ForegroundColor Yellow
    $cfUrl = "$cfApiBase/projects/$cfProjectId/upload-file"
    $curlOut = & curl.exe -s --fail-with-body -X POST `
        -H "X-Api-Token: $cfApiKey" `
        -F "metadata=<$cfMetaFile" `
        -F "file=@$zipPath" `
        $cfUrl 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: CurseForge upload failed:" -ForegroundColor Red
        Write-Host $curlOut -ForegroundColor Red
        exit 1
    }

    $cfResp = $curlOut | ConvertFrom-Json
    $cfFileId = $cfResp.id
    Write-Host ""
    Write-Host "Published!" -ForegroundColor Green
    Write-Host "  Version: $version" -ForegroundColor Green
    Write-Host "  File ID: $cfFileId" -ForegroundColor Gray
    Write-Host ""
    Write-Host "===================================================" -ForegroundColor Green
    Write-Host "  v$version is LIVE on CurseForge" -ForegroundColor Green
    Write-Host "===================================================" -ForegroundColor Green
}
