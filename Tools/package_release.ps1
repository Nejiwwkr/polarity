param(
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [switch]$NoZip,
    [switch]$SkipInstaller,
    [string]$InnoSetupPath = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cmakeLists = Join-Path $root 'CMakeLists.txt'
$text = Get-Content -Raw -LiteralPath $cmakeLists

if ($text -notmatch 'project\s*\(\s*Polarity\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    throw 'Could not read the project version from CMakeLists.txt'
}

$version = $Matches[1]
Write-Host "Packaging Polarity $version ($Configuration)"

if (-not $SkipBuild) {
    & cmake --build (Join-Path $root 'Build') --config $Configuration `
        --target Polarity_VST3 Polarity_Standalone --parallel

    if ($LASTEXITCODE -ne 0) {
        throw 'Build failed'
    }
}

$artefacts = Join-Path $root "Build\Polarity_artefacts\$Configuration"
$vst3 = Join-Path $artefacts 'VST3\Polarity.vst3'
$standalone = Join-Path $artefacts 'Standalone\Polarity.exe'
$readme = Join-Path $root 'README.md'
$changelog = Join-Path $root 'CHANGELOG.md'
$installer = Join-Path $PSScriptRoot 'install_vst3.ps1'

foreach ($required in @($vst3, $standalone, $readme, $changelog, $installer)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing required file: $required"
    }
}

$moduleInfo = Join-Path $vst3 'Contents\Resources\moduleinfo.json'

if (Test-Path -LiteralPath $moduleInfo) {
    $json = Get-Content -Raw -LiteralPath $moduleInfo

    if ($json -notmatch [regex]::Escape("`"Version`": `"$version`"")) {
        throw "moduleinfo.json does not report version $version. Rebuild Polarity_VST3."
    }
}
else {
    Write-Host 'Warning: moduleinfo.json not found, skipping version check'
}

$distRoot = Join-Path $root 'dist'
$stage = Join-Path $distRoot "Polarity-$version-win64"
$zip = Join-Path $distRoot "Polarity-$version-win64.zip"

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}

New-Item -ItemType Directory -Force -Path (Join-Path $stage 'VST3') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'Standalone') | Out-Null

Copy-Item -LiteralPath $vst3 -Destination (Join-Path $stage 'VST3\Polarity.vst3') -Recurse -Force
Copy-Item -LiteralPath $standalone -Destination (Join-Path $stage 'Standalone\Polarity.exe') -Force
Copy-Item -LiteralPath $readme -Destination $stage -Force
Copy-Item -LiteralPath $changelog -Destination $stage -Force
Copy-Item -LiteralPath $installer -Destination $stage -Force

Write-Host "Staged: $stage"

if (-not $NoZip) {
    if (Test-Path -LiteralPath $zip) {
        Remove-Item -LiteralPath $zip -Force
    }

    Compress-Archive -Path $stage -DestinationPath $zip -Force
    $hash = Get-FileHash -LiteralPath $zip -Algorithm SHA256

    Write-Host "Zip:    $zip"
    Write-Host "Size:   $([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 2)) MB"
    Write-Host "SHA256: $($hash.Hash)"
}

if (-not $SkipInstaller) {
    $iscc = $InnoSetupPath

    if ([string]::IsNullOrWhiteSpace($iscc)) {
        $candidates = @()

        if (${env:ProgramFiles(x86)}) {
            $candidates += Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
        }

        if ($env:ProgramFiles) {
            $candidates += Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'
        }

        if ($env:LOCALAPPDATA) {
            $candidates += Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
        }

        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath $candidate) {
                $iscc = $candidate
                break
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($iscc)) {
        Write-Host ''
        Write-Host 'Inno Setup 6 not found; skipping installer.'
        Write-Host 'Install it with: winget install --id JRSoftware.InnoSetup -e'
        Write-Host 'or pass -InnoSetupPath <path to ISCC.exe>; use -SkipInstaller to silence this.'
    }
    else {
        $iss = Join-Path $PSScriptRoot 'polarity.iss'

        & $iscc "/DMyAppVersion=$version" "/DStageDir=$stage" "/O$distRoot" $iss

        if ($LASTEXITCODE -ne 0) {
            throw 'Inno Setup failed'
        }

        $setup = Join-Path $distRoot "Polarity-$version-win64-setup.exe"

        if (Test-Path -LiteralPath $setup) {
            $setupHash = Get-FileHash -LiteralPath $setup -Algorithm SHA256

            Write-Host "Setup:  $setup"
            Write-Host "Size:   $([math]::Round((Get-Item -LiteralPath $setup).Length / 1MB, 2)) MB"
            Write-Host "SHA256: $($setupHash.Hash)"
        }
        else {
            throw "Installer was not produced: $setup"
        }
    }
}
