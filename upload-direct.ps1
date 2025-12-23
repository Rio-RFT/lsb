
# LSB Launcher Direct VPS Smart Sync (Optimized UI)
# Uploads files directly from Source to VPS (No Staging, No Zip)

param (
    [string]$RemoteHost = "",
    [string]$RemoteUser = "",
    [string]$RemoteDir = "",
    [string]$SourceDir = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# Helper for Text Progress Bar
function Get-ProgressBar {
    param([int]$Percent, [int]$Width = 20)
    $chars = [int]($Percent / 100 * $Width)
    $bar = "█" * $chars
    $space = "░" * ($Width - $chars)
    return "[$bar$space] $Percent%"
}

# Use specific whitelist logic to gather files
function Get-SourceFiles {
    param ($Root)
    $files = @()
    
    # 1. Bin Folder (Recursive)
    $binPath = Join-Path $Root "bin"
    if (Test-Path $binPath) {
        Get-ChildItem -Path $binPath -Recurse -File | ForEach-Object {
            $rel = "bin/" + $_.FullName.Substring($binPath.Length).TrimStart("\").Replace("\", "/")
            $_ | Add-Member -NotePropertyName "RelPath" -NotePropertyValue $rel -Force
            $files += $_
        }
    }

    # 2. Citizen Folder (Recursive)
    $citPath = Join-Path $Root "citizen"
    if (Test-Path $citPath) {
        Get-ChildItem -Path $citPath -Recurse -File | ForEach-Object {
            $rel = "citizen/" + $_.FullName.Substring($citPath.Length).TrimStart("\").Replace("\", "/")
            $_ | Add-Member -NotePropertyName "RelPath" -NotePropertyValue $rel -Force
            $files += $_
        }
    }

    # 3. Root Files (Whitelisted extensions)
    Get-ChildItem -Path $Root -File | Where-Object { $_.Extension -match "\.(dll|exe|bin|json|com|ini)$" } | ForEach-Object {
        $_ | Add-Member -NotePropertyName "RelPath" -NotePropertyValue $_.Name -Force
        $files += $_
    }

    return $files
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "       LSB Launcher Direct Sync" -ForegroundColor Cyan
Write-Host "============================================================"
Write-Host ""

# 1. Analyze Local Files
Write-Host "[LOCAL] Scanning source directory..." -ForegroundColor Yellow
if (-not (Test-Path $SourceDir)) {
    Write-Host "[ERROR] Source directory not found: $SourceDir" -ForegroundColor Red
    exit 1
}

$AllFiles = Get-SourceFiles -Root $SourceDir
Write-Host "[LOCAL] Found $( $AllFiles.Count ) files to check." -ForegroundColor Green

# 2. Get Remote Hashes
Write-Host "[REMOTE] Fetching remote file hashes..." -ForegroundColor Yellow
$RemoteHashes = @{}
try {
    $sshCmd = "cd $RemoteDir && find . -type f -exec sha256sum {} +"
    $remoteOutput = ssh $RemoteUser@$RemoteHost $sshCmd
    if ($remoteOutput) {
        $remoteOutput -split "`n" | ForEach-Object {
            $parts = $_.Trim() -split "\s+", 2
            if ($parts.Length -eq 2) {
                $cleanPath = $parts[1].TrimStart(".").TrimStart("/")
                $RemoteHashes[$cleanPath] = $parts[0]
            }
        }
    }
} catch {
    Write-Host "[WARNING] Could not fetch remote hashes." -ForegroundColor DarkGray
}

# 3. Calculate Local Hashes & Differences
Write-Host "[LOCAL] Calculating SHA256..." -ForegroundColor Yellow
$FilesToUpload = @()
foreach ($file in $AllFiles) {
    $hash = (Get-FileHash -Path $file.FullName -Algorithm SHA256).Hash.ToLower()
    if (-not $RemoteHashes.ContainsKey($file.RelPath) -or $RemoteHashes[$file.RelPath] -ne $hash) {
        $FilesToUpload += $file
    }
}

Write-Host ""
if ($FilesToUpload.Count -eq 0) {
    Write-Host "[INFO] No changes detected. VPS is up to date." -ForegroundColor Green
} else {
    Write-Host "[SYNC] Ready to upload $( $FilesToUpload.Count ) files." -ForegroundColor Cyan
    Write-Host ""
    
    # 4. Upload Files
    # Create directories
    $RemoteDirs = $FilesToUpload | ForEach-Object { 
        # Manual string splitting to handle forward slashes safely on Windows
        $idx = $_.RelPath.LastIndexOf("/")
        if ($idx -gt 0) {
            $dir = $_.RelPath.Substring(0, $idx)
            "/opt/launcher-backend/release/$dir" 
        }
    } | Select-Object -Unique

    if ($RemoteDirs) {
        $DirsArray = $RemoteDirs | Sort-Object
        # Reduced batch size to 20 to avoid length limits
        for ($i = 0; $i -lt $DirsArray.Count; $i += 20) {
            $end = [math]::Min($i+19, $DirsArray.Count-1)
            $chunk = $DirsArray[$i..$end]
            # Wrap paths in quotes to handle spaces
            $chunkStr = $chunk | ForEach-Object { "'$_'" }
            $chunkCmd = $chunkStr -join " "
            # Debug
            # Write-Host "[DEBUG] mkdir -p $chunkCmd" -ForegroundColor DarkGray
            ssh $RemoteUser@$RemoteHost "mkdir -p $chunkCmd"
        }
    }

    $Counter = 0
    $TotalCount = $FilesToUpload.Count
    
    foreach ($file in $FilesToUpload) {
        $Counter++
        $percent = [math]::Round(($Counter / $TotalCount) * 100)
        $RemotePath = "$RemoteDir/$($file.RelPath)"
        
        # UI Elements
        $pbar = Get-ProgressBar -Percent $percent -Width 20
        $shortName = if ($file.RelPath.Length -gt 45) { "..." + $file.RelPath.Substring($file.RelPath.Length - 42) } else { $file.RelPath }
        $sizeStr = "{0:N2}MB" -f ($file.Length / 1MB)
        
        # PRINT UPLOADING LINE
        # [███░░░░] 30% | UPLOADING | filename... | 1.2MB
        $line = "$pbar | UPLOADING | $($shortName.PadRight(45)) | $sizeStr"
        Write-Host -NoNewline "`r$line" -ForegroundColor Yellow
        
        $startTime = Get-Date
        
        # FIX: For modern OpenSSH (SFTP mode), do NOT quote or escape spaces in the path string itself.
        # Just pass the string containing spaces as a single argument to the SCP executable.
        $RemoteArg = "${RemoteUser}@${RemoteHost}:${RemotePath}"
        
        # PowerShell will pass "$RemoteArg" as one argument even if it has spaces
        scp -q $file.FullName "$RemoteArg"
        $endTime = Get-Date
        
        # Speed Calc
        $duration = ($endTime - $startTime).TotalSeconds
        if ($duration -le 0) { $duration = 0.001 }
        $speed = ($file.Length / 1MB) / $duration
        $speedStr = "{0:N2}MB/s" -f $speed
        
        # DONE LINE
        # [███░░░░] 30% |   DONE    | filename... | 1.2MB | 5.0MB/s
        $lineDone = "$pbar |   DONE    | $($shortName.PadRight(45)) | $sizeStr"
        Write-Host -NoNewline "`r"
        Write-Host -NoNewline "$lineDone" -ForegroundColor Cyan
        Write-Host " | $speedStr" -ForegroundColor DarkGray
    }
    
    Write-Host ""
    Write-Host "[OK] Synchronization complete." -ForegroundColor Green
}

# 5. Manifest
Write-Host "[MANIFEST] Ensuring permissions..." -ForegroundColor Yellow
# Fix permissions so Docker container (node user) can read/write
ssh $RemoteUser@$RemoteHost "chmod -R 777 /opt/launcher-backend/release"

Write-Host "[MANIFEST] Verifying remote files (Debug)..." -ForegroundColor DarkGray
$verify = ssh $RemoteUser@$RemoteHost "ls -1 /opt/launcher-backend/release | head -n 5"
if (-not $verify) {
    Write-Host "[ERROR] Release directory appears empty on VPS!" -ForegroundColor Red
} else {
    Write-Host "Files found: $verify ..." -ForegroundColor DarkGray
}

Write-Host "[MANIFEST] Generating manifest..." -ForegroundColor Yellow
# Restart container to ensure mounts aren't stale (safety measure)
ssh $RemoteUser@$RemoteHost "cd /opt/launcher-backend && docker-compose restart launcher-backend"
Start-Sleep -Seconds 5
# Exec generation
ssh $RemoteUser@$RemoteHost "cd /opt/launcher-backend && docker-compose exec -T launcher-backend node generate-manifest.js"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "       DONE" -ForegroundColor Cyan
Write-Host "============================================================"
Write-Host "============================================================"
Write-Host ""
Read-Host "Press Enter to exit..."
