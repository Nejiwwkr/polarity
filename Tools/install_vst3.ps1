param(
    [string]$Source = '',
    [switch]$System,
    [string]$Destination = '',
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Source)) {
    $repoBuild = Join-Path $PSScriptRoot '..\Build\Polarity_artefacts\Release\VST3\Polarity.vst3'
    $packaged = Join-Path $PSScriptRoot 'VST3\Polarity.vst3'

    if (Test-Path -LiteralPath $repoBuild) {
        $Source = $repoBuild
    }
    elseif (Test-Path -LiteralPath $packaged) {
        $Source = $packaged
    }
    else {
        throw 'Polarity.vst3 not found. Build Polarity_VST3 or run this script from the release package.'
    }
}

if (-not (Test-Path -LiteralPath $Source)) {
    throw "Polarity.vst3 not found: $Source"
}

$Source = (Resolve-Path -LiteralPath $Source).Path

if ([string]::IsNullOrWhiteSpace($Destination)) {
    if ($System) {
        $Destination = 'C:\Program Files\Common Files\VST3'
    }
    else {
        $Destination = Join-Path $env:APPDATA 'VST3'
    }
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())

if ($System -and -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Administrator rights are required for a system-wide install. Relaunching...'

    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', "`"$PSCommandPath`"",
        '-System',
        '-Source', "`"$Source`"",
        '-Destination', "`"$Destination`""
    )

    if ($NoPause) {
        $arguments += '-NoPause'
    }

    Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments
    exit
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$target = Join-Path $Destination 'Polarity.vst3'

if (Test-Path -LiteralPath $target) {
    try {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    catch {
        throw "Could not replace $target. Close any DAW using the plugin and try again."
    }
}

Copy-Item -LiteralPath $Source -Destination $target -Recurse -Force

Write-Host ''
Write-Host "Installed: $target"
Write-Host ''

if ($System) {
    Write-Host 'System VST3 folder. FL Studio scans this location by default.'
}
else {
    Write-Host 'User VST3 folder. Add this path in FL Studio -> Options -> Manage plugins:'
    Write-Host "  $Destination"
}

Write-Host ''
Write-Host 'In FL Studio: Options -> Manage plugins -> Find installed plugins.'
Write-Host 'Do not keep both the user-level and system-wide copies installed;'
Write-Host 'remove the one you no longer want to avoid duplicate entries.'
Write-Host ''

if (-not $NoPause) {
    Read-Host 'Press Enter to close'
}
