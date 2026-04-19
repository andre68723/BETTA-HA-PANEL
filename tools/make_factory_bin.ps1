param(
    [string]$BuildDir = "build",
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        return (Split-Path -Parent $PSScriptRoot)
    }

    return (Get-Location).Path
}

function Resolve-ReleaseVersion {
    param(
        [string]$RepoRoot
    )

    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakePath)) {
        throw "Missing root CMakeLists.txt at $cmakePath."
    }

    $content = Get-Content -Raw -Path $cmakePath
    $match = [regex]::Match($content, 'set\s*\(\s*BETTAOS_RELEASE_VERSION\s+"([^"]+)"\s*\)')
    if ($match.Success) {
        return $match.Groups[1].Value
    }

    $match = [regex]::Match($content, 'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)')
    if ($match.Success) {
        return $match.Groups[1].Value
    }

    throw "Release version not found in $cmakePath."
}

function ConvertTo-SafeFileNamePart {
    param(
        [string]$Value
    )

    $safe = [regex]::Replace($Value.Trim(), '[^A-Za-z0-9._-]+', "_")
    if ([string]::IsNullOrWhiteSpace($safe)) {
        throw "Release version '$Value' cannot be used in a file name."
    }
    return $safe
}

function Resolve-ArchiveDestination {
    param(
        [string]$ArchiveDir,
        [string]$FileName
    )

    $destination = Join-Path $ArchiveDir $FileName
    if (-not (Test-Path $destination)) {
        return $destination
    }

    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($FileName)
    $extension = [System.IO.Path]::GetExtension($FileName)
    $candidate = Join-Path $ArchiveDir "$baseName-$timestamp$extension"
    $index = 2
    while (Test-Path $candidate) {
        $candidate = Join-Path $ArchiveDir "$baseName-$timestamp-$index$extension"
        $index++
    }
    return $candidate
}

function Move-ExistingFactoryImagesToArchive {
    param(
        [string]$OutDir,
        [string]$ArchiveDir,
        [string]$TempOutPath
    )

    if (-not (Test-Path $ArchiveDir)) {
        New-Item -ItemType Directory -Path $ArchiveDir -Force | Out-Null
    }

    $tempFullPath = [System.IO.Path]::GetFullPath($TempOutPath)
    $factoryImages = Get-ChildItem -LiteralPath $OutDir -File -Filter "*.bin" |
        Where-Object {
            $_.Name -like "betta86-ha-panel*.factory*.bin" -and
            [System.IO.Path]::GetFullPath($_.FullName) -ne $tempFullPath
        }

    foreach ($image in $factoryImages) {
        $destination = Resolve-ArchiveDestination -ArchiveDir $ArchiveDir -FileName $image.Name
        Move-Item -LiteralPath $image.FullName -Destination $destination
        Write-Host "Archived old factory image:"
        Write-Host "  $($image.FullName)"
        Write-Host "  -> $destination"
    }
}

function Resolve-BuildConfig {
    param(
        [string]$BuildPath
    )

    $configPath = Join-Path $BuildPath "config.env"
    if (-not (Test-Path $configPath)) {
        return $null
    }

    try {
        return Get-Content -Raw -Path $configPath | ConvertFrom-Json
    }
    catch {
        throw "Failed to parse ${configPath}: $($_.Exception.Message)"
    }
}

function Resolve-SerialToolInfo {
    param(
        [string]$BuildPath
    )

    $ninjaCandidates = @(
        (Join-Path $BuildPath "build.ninja"),
        (Join-Path $BuildPath "bootloader\build.ninja")
    )
    $pattern = 'SERIAL_TOOL=([^;"]+);;([^;"]+esptool\.py)'

    foreach ($ninjaPath in $ninjaCandidates) {
        if (-not (Test-Path $ninjaPath)) {
            continue
        }

        $match = [regex]::Match((Get-Content -Raw -Path $ninjaPath), $pattern)
        if ($match.Success) {
            return [PSCustomObject]@{
                Python = $match.Groups[1].Value
                EspTool = $match.Groups[2].Value
            }
        }
    }

    return $null
}

function Resolve-Python {
    param(
        [object]$SerialToolInfo
    )

    $candidates = @()
    if ($env:IDF_PYTHON_ENV_PATH) {
        $candidates += (Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe")
    }
    if ($SerialToolInfo -and $SerialToolInfo.Python) {
        $candidates += [string]$SerialToolInfo.Python
    }
    $candidates += "python"

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if ($candidate -eq "python") {
            try {
                & python --version *> $null
                if ($LASTEXITCODE -eq 0) {
                    return "python"
                }
            }
            catch {
                # try next
            }
        } elseif (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "Python not found. Activate ESP-IDF environment or set IDF_PYTHON_ENV_PATH."
}

function Resolve-EspToolPath {
    param(
        [object]$BuildConfig,
        [object]$SerialToolInfo
    )

    $candidates = @()
    if ($SerialToolInfo -and $SerialToolInfo.EspTool) {
        $candidates += [string]$SerialToolInfo.EspTool
    }
    if ($env:IDF_PATH) {
        $candidates += (Join-Path $env:IDF_PATH "components\esptool_py\esptool\esptool.py")
    }
    if ($BuildConfig -and $BuildConfig.IDF_PATH) {
        $candidates += (Join-Path ([string]$BuildConfig.IDF_PATH) "components\esptool_py\esptool\esptool.py")
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "esptool.py not found. Run `idf.py build` first or activate an ESP-IDF environment."
}

function Resolve-Chip {
    param(
        [string]$BuildPath
    )

    $flasherArgsPath = Join-Path $BuildPath "flasher_args.json"
    if (Test-Path $flasherArgsPath) {
        try {
            $flasher = Get-Content -Raw -Path $flasherArgsPath | ConvertFrom-Json
            $chip = [string]$flasher.extra_esptool_args.chip
            if (-not [string]::IsNullOrWhiteSpace($chip)) {
                return $chip
            }
        }
        catch {
            # fall through to default
        }
    }

    return "esp32p4"
}

$repoRoot = Resolve-RepoRoot
$releaseVersion = Resolve-ReleaseVersion -RepoRoot $repoRoot
$safeReleaseVersion = ConvertTo-SafeFileNamePart -Value $releaseVersion
$outFileWasProvided = -not [string]::IsNullOrWhiteSpace($OutFile)
if (-not $outFileWasProvided) {
    $OutFile = Join-Path "release" "betta86-ha-panel-$safeReleaseVersion.factory.bin"
}

$buildPath = (Resolve-Path $BuildDir).Path
$flashArgsPath = Join-Path $buildPath "flash_args"
if (-not (Test-Path $flashArgsPath)) {
    throw "Missing $flashArgsPath. Run `idf.py build` first."
}

$buildConfig = Resolve-BuildConfig -BuildPath $buildPath
$serialToolInfo = Resolve-SerialToolInfo -BuildPath $buildPath
$pythonExe = Resolve-Python -SerialToolInfo $serialToolInfo
$esptoolPath = Resolve-EspToolPath -BuildConfig $buildConfig -SerialToolInfo $serialToolInfo

$outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) {
    $OutFile
}
else {
    $baseDir = if ($outFileWasProvided) { (Get-Location).Path } else { $repoRoot }
    Join-Path $baseDir $OutFile
}
$outPath = [System.IO.Path]::GetFullPath($outPath)
$outDir = Split-Path -Parent $outPath
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}
$archiveDir = Join-Path $outDir "archive"
if (-not (Test-Path $archiveDir)) {
    New-Item -ItemType Directory -Path $archiveDir -Force | Out-Null
}

$chip = Resolve-Chip -BuildPath $buildPath

$tempOutPath = Join-Path $outDir ".$([System.IO.Path]::GetFileName($outPath)).tmp"
if (Test-Path $tempOutPath) {
    Remove-Item -LiteralPath $tempOutPath -Force
}

$mergeArgs = @("--chip", $chip, "merge_bin", "-o", $tempOutPath)
$parts = @()

foreach ($line in (Get-Content -Path $flashArgsPath)) {
    $trimmed = $line.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#")) {
        continue
    }

    if ($trimmed.StartsWith("--")) {
        $mergeArgs += ($trimmed -split "\s+")
        continue
    }

    $match = [regex]::Match($trimmed, '^(0x[0-9A-Fa-f]+)\s+(.+)$')
    if (-not $match.Success) {
        throw "Unsupported line in ${flashArgsPath}: $trimmed"
    }

    $offset = $match.Groups[1].Value
    $relPath = $match.Groups[2].Value.Trim()
    $absPath = Join-Path $buildPath $relPath
    if (-not (Test-Path $absPath)) {
        throw "Missing flash file: $absPath"
    }

    $offsetNum = [Convert]::ToInt64($offset, 16)
    $parts += [PSCustomObject]@{
        Offset = $offset
        OffsetNum = $offsetNum
        Path = $absPath
    }
}

$parts = $parts | Sort-Object OffsetNum
foreach ($part in $parts) {
    $mergeArgs += @($part.Offset, $part.Path)
}

Write-Host "Creating factory image:"
Write-Host "  Version: $releaseVersion"
Write-Host "  Out: $outPath"
Write-Host "  Archive: $archiveDir"
Write-Host "  Chip: $chip"
Write-Host "  Source: $flashArgsPath"
Write-Host "  Parts:"
foreach ($part in $parts) {
    Write-Host "    $($part.Offset)  $($part.Path)"
}

& $pythonExe $esptoolPath @mergeArgs
if ($LASTEXITCODE -ne 0) {
    if (Test-Path $tempOutPath) {
        Remove-Item -LiteralPath $tempOutPath -Force
    }
    throw "merge_bin failed with exit code $LASTEXITCODE"
}

Move-ExistingFactoryImagesToArchive -OutDir $outDir -ArchiveDir $archiveDir -TempOutPath $tempOutPath
Move-Item -LiteralPath $tempOutPath -Destination $outPath -Force

Write-Host ""
Write-Host "Factory image created:"
Write-Host "  $outPath"
Write-Host ""
Write-Host "Flash with esptool at offset 0x0, example:"
Write-Host "  python -m esptool --chip $chip -p COM3 --before default_reset --after hard_reset write_flash 0x0 `"$outPath`""
