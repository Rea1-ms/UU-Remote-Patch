[CmdletBinding()]
param(
    [ValidateSet('Status','Install','Disable')][string]$Action = 'Status',
    [string]$HostDirectory = 'C:/Program Files/Netease/GameViewer/bin',
    [string]$BuildDirectory = 'build/candidate/Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$hostPath = [IO.Path]::GetFullPath($HostDirectory)
$candidate = Join-Path ([IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))) 'version.dll'
$installed = Join-Path $hostPath 'version.dll'
$expected = @{
    'GameViewerServer.exe' = '91cfe3bfc5582776a56a2cf6294c45afb7d65d68abe8c4a483e99a8549b00b5b'
    'nrd_renderer_ipc.dll' = 'd3960b8b14ec0020b89ccbc51f7414cdbfa67897aa5fa29b2014ed8e84e6aa1a'
    'nemu-api.dll' = '40f44159d6fd637e2226a133508d032470e5caf22f35ef1065c86438fdd90abb'
}
if ($Action -eq 'Install') {
    throw '崩溃修复已完成独立验证，但官方手机端接入尚未通过；持久化安装暂不开放。'
}
if ($Action -eq 'Disable') {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw '停用安装目录中的库需要管理员权限。' }
    if (!(Test-Path -LiteralPath $installed)) { Write-Output '安装目录没有同名代理库。'; return }
    if (!(Test-Path -LiteralPath $candidate)) { throw '候选库不存在，无法确认同名文件的归属；保留原文件。' }
    if ((Get-FileHash -LiteralPath $candidate).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) {
        throw '安装目录存在未知或其他版本的同名库，不能自动改名；保留原文件。'
    }
    $backup = $installed + '.disabled-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    Move-Item -LiteralPath $installed -Destination $backup
    Write-Output '已改名保留补丁文件。正在运行的进程不会卸载补丁；脚本没有停止或重启程序。'
    return
}
foreach ($name in ($expected.Keys | Sort-Object)) {
    $file = Join-Path $hostPath $name
    $matches = (Test-Path -LiteralPath $file) -and ((Get-FileHash -LiteralPath $file).Hash -eq $expected[$name])
    [pscustomobject]@{ Component=$name; MatchesSupportedBuild=$matches }
}
Write-Output ('安装目录中有同名库：' + (Test-Path -LiteralPath $installed))
Write-Output '当前阶段：离线排查。此脚本不会停止、启动或重启远程程序。'
