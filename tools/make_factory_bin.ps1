param(
    [string]$BuildDir = "",
    [string]$OutFile = "",
    [string]$OtaOutFile = "",
    [ValidateSet("panel4", "panel10", "panels3", "both", "auto")]
    [string]$Variant = "auto"
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
        [string]$TempOutPath,
        [string]$VariantSuffix = ""
    )

    if (-not (Test-Path $ArchiveDir)) {
        New-Item -ItemType Directory -Path $ArchiveDir -Force | Out-Null
    }

    $tempFullPath = [System.IO.Path]::GetFullPath($TempOutPath)
    $variantPattern = if ([string]::IsNullOrWhiteSpace($VariantSuffix)) { "betta86-ha-panel*.factory*.bin" } else { "betta86-ha-panel*-$VariantSuffix.factory*.bin" }
    $factoryImages = Get-ChildItem -LiteralPath $OutDir -File -Filter "*.bin" |
        Where-Object {
            $_.Name -like $variantPattern -and
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

function Move-ExistingOtaImagesToArchive {
    param(
        [string]$OutDir,
        [string]$ArchiveDir,
        [string]$TempOutPath,
        [string]$VariantSuffix = ""
    )

    if (-not (Test-Path $ArchiveDir)) {
        New-Item -ItemType Directory -Path $ArchiveDir -Force | Out-Null
    }

    $tempFullPath = [System.IO.Path]::GetFullPath($TempOutPath)
    $variantPattern = if ([string]::IsNullOrWhiteSpace($VariantSuffix)) { "betta86-ha-panel*.ota*.bin" } else { "betta86-ha-panel*-$VariantSuffix.ota*.bin" }
    $otaImages = Get-ChildItem -LiteralPath $OutDir -File -Filter "*.bin" |
        Where-Object {
            $_.Name -like $variantPattern -and
            [System.IO.Path]::GetFullPath($_.FullName) -ne $tempFullPath
        }

    foreach ($image in $otaImages) {
        $destination = Resolve-ArchiveDestination -ArchiveDir $ArchiveDir -FileName $image.Name
        Move-Item -LiteralPath $image.FullName -Destination $destination
        Write-Host "Archived old OTA image:"
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

function Resolve-AppImagePath {
    param(
        [string]$BuildPath
    )

    $flasherArgsPath = Join-Path $BuildPath "flasher_args.json"
    if (Test-Path $flasherArgsPath) {
        try {
            $flasher = Get-Content -Raw -Path $flasherArgsPath | ConvertFrom-Json
            $appFile = [string]$flasher.app.file
            if (-not [string]::IsNullOrWhiteSpace($appFile)) {
                $candidate = Join-Path $BuildPath $appFile
                if (Test-Path $candidate) {
                    return (Resolve-Path $candidate).Path
                }
            }
        }
        catch {
            throw "Failed to parse app image from ${flasherArgsPath}: $($_.Exception.Message)"
        }
    }

    $appArgsCandidates = @(
        (Join-Path $BuildPath "app-flash_args"),
        (Join-Path $BuildPath "flash_app_args")
    )
    foreach ($appArgsPath in $appArgsCandidates) {
        if (-not (Test-Path $appArgsPath)) {
            continue
        }

        foreach ($line in (Get-Content -Path $appArgsPath)) {
            $trimmed = $line.Trim()
            if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#") -or $trimmed.StartsWith("--")) {
                continue
            }

            $match = [regex]::Match($trimmed, '^(0x[0-9A-Fa-f]+)\s+(.+)$')
            if ($match.Success) {
                $candidate = Join-Path $BuildPath $match.Groups[2].Value.Trim()
                if (Test-Path $candidate) {
                    return (Resolve-Path $candidate).Path
                }
            }
        }
    }

    throw "App image for OTA not found. Run `idf.py build` first."
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

function Resolve-OutputPath {
    param(
        [string]$Path,
        [bool]$WasProvided,
        [string]$RepoRoot
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    $baseDir = if ($WasProvided) { (Get-Location).Path } else { $RepoRoot }
    return [System.IO.Path]::GetFullPath((Join-Path $baseDir $Path))
}

$repoRoot = Resolve-RepoRoot
$releaseVersion = Resolve-ReleaseVersion -RepoRoot $repoRoot
$safeReleaseVersion = ConvertTo-SafeFileNamePart -Value $releaseVersion

function Invoke-VariantBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir,
        [string]$OutFile,
        [string]$OtaOutFile,
        [string]$VariantSuffix,
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,
        [Parameter(Mandatory = $true)]
        [string]$ReleaseVersion,
        [Parameter(Mandatory = $true)]
        [string]$SafeReleaseVersion
    )

    $outFileWasProvided = -not [string]::IsNullOrWhiteSpace($OutFile)
    if (-not $outFileWasProvided) {
        $suffix = if ([string]::IsNullOrWhiteSpace($VariantSuffix)) { "" } else { "-$VariantSuffix" }
        $OutFile = Join-Path "release" "betta86-ha-panel-$SafeReleaseVersion$suffix.factory.bin"
    }
    $otaOutFileWasProvided = -not [string]::IsNullOrWhiteSpace($OtaOutFile)
    if (-not $otaOutFileWasProvided) {
        $suffix = if ([string]::IsNullOrWhiteSpace($VariantSuffix)) { "" } else { "-$VariantSuffix" }
        $OtaOutFile = Join-Path (Join-Path "release" "ota") "betta86-ha-panel-$SafeReleaseVersion$suffix.ota.bin"
    }

    if (-not (Test-Path $BuildDir)) {
        throw "Build directory not found: $BuildDir. Run ``idf.py -B $BuildDir build`` first."
    }
    $buildPath = (Resolve-Path $BuildDir).Path
    $flashArgsPath = Join-Path $buildPath "flash_args"
    if (-not (Test-Path $flashArgsPath)) {
        throw "Missing $flashArgsPath. Run ``idf.py -B $BuildDir build`` first."
    }

    $buildConfig = Resolve-BuildConfig -BuildPath $buildPath
    $serialToolInfo = Resolve-SerialToolInfo -BuildPath $buildPath
    $pythonExe = Resolve-Python -SerialToolInfo $serialToolInfo
    $esptoolPath = Resolve-EspToolPath -BuildConfig $buildConfig -SerialToolInfo $serialToolInfo
    $appImagePath = Resolve-AppImagePath -BuildPath $buildPath

    $outPath = Resolve-OutputPath -Path $OutFile -WasProvided $outFileWasProvided -RepoRoot $RepoRoot
    $outDir = Split-Path -Parent $outPath
    if (-not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    }
    $archiveDir = Join-Path $outDir "archive"
    if (-not (Test-Path $archiveDir)) {
        New-Item -ItemType Directory -Path $archiveDir -Force | Out-Null
    }

    $otaOutPath = Resolve-OutputPath -Path $OtaOutFile -WasProvided $otaOutFileWasProvided -RepoRoot $RepoRoot
    $otaOutDir = Split-Path -Parent $otaOutPath
    if (-not (Test-Path $otaOutDir)) {
        New-Item -ItemType Directory -Path $otaOutDir -Force | Out-Null
    }
    $otaArchiveDir = Join-Path $otaOutDir "archive"
    if (-not (Test-Path $otaArchiveDir)) {
        New-Item -ItemType Directory -Path $otaArchiveDir -Force | Out-Null
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

    Write-Host ""
    Write-Host "===================================================="
    Write-Host "Creating factory image:"
    if (-not [string]::IsNullOrWhiteSpace($VariantSuffix)) {
        Write-Host "  Variant: $VariantSuffix"
    }
    Write-Host "  Version: $ReleaseVersion"
    Write-Host "  Out: $outPath"
    Write-Host "  OTA: $otaOutPath"
    Write-Host "  Archive: $archiveDir"
    Write-Host "  Chip: $chip"
    Write-Host "  Build dir: $buildPath"
    Write-Host "  Source: $flashArgsPath"
    Write-Host "  App image: $appImagePath"
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

    Move-ExistingFactoryImagesToArchive -OutDir $outDir -ArchiveDir $archiveDir -TempOutPath $tempOutPath -VariantSuffix $VariantSuffix
    Move-Item -LiteralPath $tempOutPath -Destination $outPath -Force

    $otaTempOutPath = Join-Path $otaOutDir ".$([System.IO.Path]::GetFileName($otaOutPath)).tmp"
    if (Test-Path $otaTempOutPath) {
        Remove-Item -LiteralPath $otaTempOutPath -Force
    }
    Copy-Item -LiteralPath $appImagePath -Destination $otaTempOutPath -Force
    Move-ExistingOtaImagesToArchive -OutDir $otaOutDir -ArchiveDir $otaArchiveDir -TempOutPath $otaTempOutPath -VariantSuffix $VariantSuffix
    Move-Item -LiteralPath $otaTempOutPath -Destination $otaOutPath -Force

    Write-Host ""
    Write-Host "Factory image created:"
    Write-Host "  $outPath"
    Write-Host "OTA image created:"
    Write-Host "  $otaOutPath"

    return [PSCustomObject]@{
        Variant     = $VariantSuffix
        Chip        = $chip
        FactoryPath = $outPath
        OtaPath     = $otaOutPath
    }
}

# --- Decide which variants to build ----------------------------------------
# Known variants and their expected build directories / filename suffixes.
$variantMap = @{
    "panel4"   = [PSCustomObject]@{ BuildDir = "build-panel4";   Suffix = "panel4"   }
    "panel10"  = [PSCustomObject]@{ BuildDir = "build-panel10";  Suffix = "panel10"  }
    "panels3"  = [PSCustomObject]@{ BuildDir = "build-panels3";  Suffix = "panels3"  }
}

$variantsToBuild = @()

if ($Variant -eq "both") {
    foreach ($v in @("panel4", "panel10", "panels3")) {
        $info = $variantMap[$v]
        $path = Join-Path $repoRoot $info.BuildDir
        if (-not (Test-Path $path)) {
            throw "Variant '$v' requested but build directory not found: $path. Run ``idf.py -B $($info.BuildDir) build`` first."
        }
        $variantsToBuild += [PSCustomObject]@{ Name = $v; BuildDir = $path; Suffix = $info.Suffix }
    }
} elseif ($Variant -eq "panel4" -or $Variant -eq "panel10" -or $Variant -eq "panels3") {
    $info = $variantMap[$Variant]
    $buildRoot = if ([string]::IsNullOrWhiteSpace($BuildDir)) { (Join-Path $repoRoot $info.BuildDir) } else { $BuildDir }
    $variantsToBuild += [PSCustomObject]@{ Name = $Variant; BuildDir = $buildRoot; Suffix = $info.Suffix }
} else {
    # auto: honour -BuildDir if explicitly set; else pick whichever variant build dir exists.
    if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
        $variantsToBuild += [PSCustomObject]@{ Name = "custom"; BuildDir = $BuildDir; Suffix = "" }
    } else {
        foreach ($v in @("panel4", "panel10", "panels3")) {
            $info = $variantMap[$v]
            $path = Join-Path $repoRoot $info.BuildDir
            if (Test-Path $path) {
                $variantsToBuild += [PSCustomObject]@{ Name = $v; BuildDir = $path; Suffix = $info.Suffix }
            }
        }
        if ($variantsToBuild.Count -eq 0) {
            # Last-ditch fallback to the historical default.
            $legacy = Join-Path $repoRoot "build"
            if (-not (Test-Path $legacy)) {
                throw "No build directory found. Expected one of: build-panel4/, build-panel10/, build-panels3/, build/. Run ``idf.py -B build-panel4 build`` (or similar) first."
            }
            $variantsToBuild += [PSCustomObject]@{ Name = "legacy"; BuildDir = $legacy; Suffix = "" }
        }
    }
}

# If the user provided explicit output filenames and more than one variant is
# being built, refuse: we can't write two outputs to the same file.
if (($variantsToBuild.Count -gt 1) -and
    ((-not [string]::IsNullOrWhiteSpace($OutFile)) -or (-not [string]::IsNullOrWhiteSpace($OtaOutFile)))) {
    throw "-OutFile / -OtaOutFile cannot be used together with multiple variants. Run the script once per variant or omit the explicit paths."
}

$results = @()
foreach ($entry in $variantsToBuild) {
    # Capture all pipeline output from the function; PS5 can leak intermediate
    # objects. Filter to only the PSCustomObject we explicitly return at the end.
    $allOut = @(Invoke-VariantBuild `
        -BuildDir $entry.BuildDir `
        -OutFile $OutFile `
        -OtaOutFile $OtaOutFile `
        -VariantSuffix $entry.Suffix `
        -RepoRoot $repoRoot `
        -ReleaseVersion $releaseVersion `
        -SafeReleaseVersion $safeReleaseVersion)
    $results += @($allOut | Where-Object { $null -ne $_ -and $_ -is [PSCustomObject] -and $_.PSObject.Properties.Name -contains 'FactoryPath' })
}

Write-Host ""
Write-Host "===================================================="
$setPlural = if ($results.Count -ne 1) { "s" } else { "" }
Write-Host "Summary ($($results.Count) image set${setPlural}):"
foreach ($r in $results) {
    $label = if ([string]::IsNullOrWhiteSpace($r.Variant)) { "(default)" } else { $r.Variant }
    Write-Host "  [$label] factory: $($r.FactoryPath)"
    Write-Host "  [$label] ota    : $($r.OtaPath)"
}
Write-Host ""
Write-Host "Flash example (factory image to offset 0x0):"
foreach ($r in $results) {
    Write-Host "  python -m esptool --chip $($r.Chip) -p COM3 --before default_reset --after hard_reset write_flash 0x0 `"$($r.FactoryPath)`""
}

