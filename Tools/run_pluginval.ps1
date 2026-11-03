param(
    [string]$PluginPath = '',
    [int]$Strictness = 10,
    [switch]$SkipGuiTests,
    [int]$Repeat = 1,
    [int]$TimeoutMs = 120000
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($PluginPath)) {
    $PluginPath = Join-Path $root 'Build\Polarity_artefacts\Release\VST3\Polarity.vst3'
}

if (-not (Test-Path -LiteralPath $PluginPath)) {
    throw "VST3 not found: $PluginPath. Build Polarity_VST3 first."
}

$PluginPath = (Resolve-Path -LiteralPath $PluginPath).Path

$exe = $null
$candidates = @(
    (Join-Path $PSScriptRoot 'pluginval.exe'),
    (Join-Path $PSScriptRoot 'pluginval\pluginval.exe')
)

foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
        $exe = $candidate
        break
    }
}

if (-not $exe) {
    $command = Get-Command pluginval.exe -ErrorAction SilentlyContinue
    if ($command) {
        $exe = $command.Source
    }
}

$version = '1.0.4'
$url = "https://github.com/Tracktion/pluginval/releases/download/v$version/pluginval_Windows.zip"

if (-not $exe) {
    $directory = Join-Path $PSScriptRoot 'pluginval'
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $zip = Join-Path $directory 'pluginval_Windows.zip'

    Write-Host "Downloading pluginval $version ..."
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    }
    catch {
        Write-Host 'Invoke-WebRequest failed, trying curl.exe ...'
        & curl.exe -L $url -o $zip
    }

    if (-not (Test-Path -LiteralPath $zip)) {
        Write-Host "Could not download pluginval. Download it manually from:"
        Write-Host "  $url"
        Write-Host "and place pluginval.exe in $directory"
        exit 2
    }

    Expand-Archive -LiteralPath $zip -DestinationPath $directory -Force
    $found = Get-ChildItem -LiteralPath $directory -Recurse -Filter 'pluginval.exe' |
             Select-Object -First 1

    if (-not $found) {
        Write-Host "pluginval.exe was not found after extracting $zip"
        exit 2
    }

    $exe = $found.FullName
}

$outputDirectory = Join-Path $root 'Build\pluginval'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$junit = Join-Path $outputDirectory 'pluginval.xml'

$arguments = @(
    'validate',
    '--strictness-level', "$Strictness",
    '--timeout-ms', "$TimeoutMs",
    '--sample-rates', '44100,48000,96000,192000',
    '--block-sizes', '32,512,2048',
    '--repeat', "$Repeat",
    '--verbose',
    '--output-dir', "`"$outputDirectory`"",
    '--junit-xml', "`"$junit`""
)

if ($SkipGuiTests) {
    $arguments += '--skip-gui-tests'
}

$arguments += "`"$PluginPath`""

Write-Host "Running: $exe $($arguments -join ' ')"
& $exe @arguments
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "pluginval PASSED (strictness $Strictness). JUnit: $junit"
}
else {
    Write-Host "pluginval FAILED with exit code $exitCode. See $junit"
}

exit $exitCode
