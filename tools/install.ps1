#Requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateSet('Status','Install','Disable')][string]$Action = 'Status',
    [string]$HostDirectory = 'C:/Program Files/Netease/GameViewer/bin',
    [string]$BuildDirectory = '',
    [string]$ConfigFile = ''
)
$ErrorActionPreference = 'Stop'
$root = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'config.example.json')) { $PSScriptRoot } else { Split-Path $PSScriptRoot -Parent }
$hostPath = [IO.Path]::GetFullPath($HostDirectory)
if (!$BuildDirectory) { $BuildDirectory = if (Test-Path -LiteralPath (Join-Path $root 'version.dll')) { '.' } else { 'build/portable/Release' } }
$candidate = Join-Path ([IO.Path]::GetFullPath($BuildDirectory, $root)) 'version.dll'
$installed = Join-Path $hostPath 'version.dll'
$payload = Join-Path $hostPath 'uu-adb-patch'
$receiptFile = Join-Path $payload 'install.json'
$expected = @{
    'GameViewerServer.exe' = '91cfe3bfc5582776a56a2cf6294c45afb7d65d68abe8c4a483e99a8549b00b5b'
    'nrd_renderer_ipc.dll' = 'd3960b8b14ec0020b89ccbc51f7414cdbfa67897aa5fa29b2014ed8e84e6aa1a'
    'nemu-api.dll' = '40f44159d6fd637e2226a133508d032470e5caf22f35ef1065c86438fdd90abb'
}
function Assert-PlainPath([string]$Path) {
    $part = [IO.Path]::GetFullPath($Path)
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            $item = Get-Item -LiteralPath $part -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw '路径包含链接或目录联接，保留原文件。' }
        }
        $part = Split-Path $part -Parent
    }
}
function Read-Receipt {
    if (!(Test-Path -LiteralPath $receiptFile)) { throw '缺少安装记录，无法确认文件归属；保留原文件。' }
    $receipt = Get-Content -LiteralPath $receiptFile -Raw | ConvertFrom-Json
    if ($receipt.schema -ne 1 -or $receipt.dll_sha256 -notmatch '^[a-f0-9]{64}$') { throw '安装记录无效；保留原文件。' }
    return $receipt
}
if ($Action -eq 'Disable') {
    if (!(Test-Path -LiteralPath $installed)) { Write-Output '安装目录没有同名代理库。'; return }
    Assert-PlainPath $installed; Assert-PlainPath $payload
    $receipt = Read-Receipt
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $receipt.dll_sha256) {
        throw '安装目录存在未知或其他版本的同名库，不能自动改名；保留原文件。'
    }
    $backup = $installed + '.disabled-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    if ($PSCmdlet.ShouldProcess($installed, '改名保留补丁库，下次后台启动时停用')) {
        Move-Item -LiteralPath $installed -Destination $backup
        Write-Output '已改名保留补丁文件。正在运行的补丁保持生效；后台下次正常启动时停用。依赖和配置保留。'
    }
    return
}
$supported = $true
foreach ($name in ($expected.Keys | Sort-Object)) {
    $file = Join-Path $hostPath $name
    $matches = (Test-Path -LiteralPath $file) -and ((Get-FileHash -LiteralPath $file).Hash -eq $expected[$name])
    [pscustomobject]@{ Component=$name; MatchesSupportedBuild=$matches }
    if (!$matches) { $supported = $false }
}
Write-Output ('安装目录中有同名库：' + (Test-Path -LiteralPath $installed))
if ($Action -eq 'Status') {
    Write-Output ('存在安装记录：' + (Test-Path -LiteralPath $receiptFile))
    Write-Output '文件已安装不代表当前进程已加载；此脚本不会停止、启动或重启远程程序。'
    return
}
if (!$supported) { throw '官方组件不匹配受支持构建，未安装补丁。' }
Assert-PlainPath $hostPath; Assert-PlainPath $payload
if (Test-Path -LiteralPath (Join-Path $hostPath 'patch-root.txt')) { throw '被控端目录存在开发根目录指针，可能绕过安装配置；保留原文件，未安装。' }
if (!(Test-Path -LiteralPath $candidate -PathType Leaf)) { throw '找不到候选补丁库。' }
$dllHash = (Get-FileHash -LiteralPath $candidate).Hash.ToLowerInvariant()
$manifestFile = Join-Path (Split-Path $candidate -Parent) 'manifest.json'
if (Test-Path -LiteralPath $manifestFile) {
    $manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
    $entry = @($manifest.files | Where-Object path -eq 'version.dll')
    if ($entry.Count -ne 1 -or $entry[0].sha256 -ne $dllHash) { throw '候选补丁与包内摘要不符，未安装。' }
}
if (Test-Path -LiteralPath $installed) {
    Assert-PlainPath $installed
    $receipt = Read-Receipt
    if ((Get-FileHash -LiteralPath $installed).Hash -eq $dllHash -and $receipt.dll_sha256 -eq $dllHash) {
        Write-Output '同一补丁已安装；配置直接在被控端目录的 uu-adb-patch/config.local.json（本地配置）中修改。'; return
    }
    throw '存在其他同名库，未覆盖。升级前请先停用，并在约定的断连窗口退出后台、保留旧依赖目录。'
}
if (Test-Path -LiteralPath $payload) { throw '依赖目录已经存在，保留原文件。请在后台退出后将旧目录改名备份，再安装。' }
if (!$ConfigFile) { $ConfigFile = Join-Path $root 'config.local.json' }
$ConfigFile = [IO.Path]::GetFullPath($ConfigFile)
$config = Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json -AsHashtable
$configRoot = Split-Path $ConfigFile -Parent
foreach ($key in @('adb','scrcpy')) {
    if ($config[$key] -isnot [string] -or !$config[$key]) { throw '请在本地配置中填写安卓调试桥和投屏组件位置。' }
}
$adb = [IO.Path]::GetFullPath($config.adb, $configRoot)
$scrcpy = [IO.Path]::GetFullPath($config.scrcpy, $configRoot)
$adbRoot = Split-Path $adb -Parent
Assert-PlainPath $candidate
$adbFiles = @($adb, (Join-Path $adbRoot 'AdbWinApi.dll'), (Join-Path $adbRoot 'AdbWinUsbApi.dll'))
$scrcpyFiles = @((Join-Path $scrcpy 'scrcpy-server'), (Join-Path $scrcpy 'avcodec-63.dll'), (Join-Path $scrcpy 'avutil-61.dll'))
foreach ($file in ($adbFiles + $scrcpyFiles)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw '安卓调试桥或投屏组件不完整；请按使用说明准备指定版本。' }
    Assert-PlainPath $file
}
if ($config.ContainsKey('connect')) {
    if ($config.connect -isnot [array] -or $config.connect.Count -gt 32) { throw '网络设备配置应为最多三十二项的数组。' }
    foreach ($endpoint in $config.connect) {
        if ($endpoint -isnot [string] -or !$endpoint -or $endpoint.Length -gt 255 -or $endpoint -match '\s') { throw '网络设备配置无效。' }
    }
}
# Install only dependency binaries and their licenses, never the source machine's logs or mappings.
$decoderFiles = @(Get-ChildItem -LiteralPath $scrcpy -File | Where-Object { $_.Extension -eq '.dll' -or $_.Name -eq 'scrcpy-server' -or $_.Name -match '^(LICENSE|COPYING|NOTICE)' })
foreach ($file in $decoderFiles) { Assert-PlainPath $file.FullName }
$config.adb = 'runtime/platform-tools/adb.exe'; $config.scrcpy = 'runtime/scrcpy'
if (!$PSCmdlet.ShouldProcess($hostPath, '复制补丁、配置与依赖；下次后台正常启动时自动加载')) { return }
New-Item -ItemType Directory -Path (Join-Path $payload 'runtime/platform-tools'), (Join-Path $payload 'runtime/scrcpy') | Out-Null
Copy-Item -LiteralPath $adbFiles[0] -Destination (Join-Path $payload 'runtime/platform-tools/adb.exe')
foreach ($file in $adbFiles[1..2]) { Copy-Item -LiteralPath $file -Destination (Join-Path $payload 'runtime/platform-tools') }
foreach ($name in @('libusb-1.0.dll','NOTICE.txt','LICENSE','LICENSE.txt')) {
    $file = Join-Path $adbRoot $name
    if (Test-Path -LiteralPath $file -PathType Leaf) {
        Assert-PlainPath $file; Copy-Item -LiteralPath $file -Destination (Join-Path $payload 'runtime/platform-tools')
    }
}
foreach ($file in $decoderFiles) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $payload 'runtime/scrcpy') }
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $payload 'config.local.json') -Encoding utf8NoBOM
@{schema=1; dll_sha256=$dllHash; installed_at=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $receiptFile -Encoding utf8NoBOM
# Publish by same-directory rename only after the full copy and hash check.
$pending = $installed + '.pending-' + [guid]::NewGuid().ToString('N')
[IO.File]::Copy($candidate, $pending, $false)
if ((Get-FileHash -LiteralPath $pending).Hash -ne $dllHash) { throw '复制后的补丁摘要不符，临时文件已保留，未启用代理。' }
[IO.File]::Move($pending, $installed)
Write-Output '安装完成。依赖已复制，不再依赖解压目录或源码目录。后台下次正常启动时自动加载；当前连接没有重启。'
