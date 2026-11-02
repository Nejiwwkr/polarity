# 把构建好的 Polarity.vst3 安装到 FL Studio 20.9 唯一会扫描的 VST3 目录。
# 用法：右键“使用 PowerShell 运行”，或在 PowerShell 里执行本脚本；会自动请求管理员权限。

$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot '..\Build\Polarity_artefacts\Release\VST3\Polarity.vst3'

if (-not (Test-Path -LiteralPath $source)) {
    throw "未找到构建产物，请先构建 Polarity_VST3：$source"
}

$source = (Resolve-Path -LiteralPath $source).Path
$destinationDirectory = 'C:\Program Files\Common Files\VST3'
$destination = Join-Path $destinationDirectory 'Polarity.vst3'

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())

if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host '需要管理员权限，正在重新启动...'
    Start-Process powershell.exe -Verb RunAs -ArgumentList @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', "`"$PSCommandPath`""
    )
    exit
}

if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}

Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
Write-Host "已安装到: $destination"
Write-Host ''
Write-Host '回到 FL Studio：Options -> Manage plugins'
Write-Host '  1. 确认 VST3 路径包含 C:\Program Files\Common Files\VST3'
Write-Host '  2. 勾选 Verify plugins / Rescan previously verified plugins / Rescan plugins with errors'
Write-Host '  3. 点击 Find installed plugins，扫描完成后重启 FL Studio'
Write-Host ''
Read-Host '按 Enter 关闭'
