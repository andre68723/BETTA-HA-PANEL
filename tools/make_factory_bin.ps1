param(
    [string]$BuildDir = "build",
    [string]$OutFile = "release/betta86-ha-panel.factory.bin"
)

$ErrorActionPreference = "Stop"

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
    Join-Path (Get-Location) $OutFile
}
$outPath = [System.IO.Path]::GetFullPath($outPath)
$outDir = Split-Path -Parent $outPath
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$chip = Resolve-Chip -BuildPath $buildPath

$mergeArgs = @("--chip", $chip, "merge_bin", "-o", $outPath)
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
Write-Host "  Out: $outPath"
Write-Host "  Chip: $chip"
Write-Host "  Source: $flashArgsPath"
Write-Host "  Parts:"
foreach ($part in $parts) {
    Write-Host "    $($part.Offset)  $($part.Path)"
}

& $pythonExe $esptoolPath @mergeArgs
if ($LASTEXITCODE -ne 0) {
    throw "merge_bin failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "Factory image created:"
Write-Host "  $outPath"
Write-Host ""
Write-Host "Flash with esptool at offset 0x0, example:"
Write-Host "  python -m esptool --chip $chip -p COM3 --before default_reset --after hard_reset write_flash 0x0 `"$outPath`""
