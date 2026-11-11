#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$HostDirectory = 'C:/Program Files/Netease/GameViewer/bin',
    [string]$BuildDirectory = 'build/portable/Release',
    [string]$ConfigFile = 'config.local.json'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path $root ('artifacts/install-check-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $fixture | Out-Null
foreach ($name in @('GameViewerServer.exe','nrd_renderer_ipc.dll','nemu-api.dll')) {
    Copy-Item -LiteralPath (Join-Path $HostDirectory $name) -Destination $fixture
}
$script = Join-Path $PSScriptRoot 'install.ps1'
$arguments = @{ HostDirectory=$fixture; BuildDirectory=$BuildDirectory; ConfigFile=[IO.Path]::GetFullPath($ConfigFile,$root) }
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
& $script @arguments -Action Install -WhatIf | Out-Null
Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'version.dll'))) '预演写入了补丁库。'
Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'uu-adb-patch'))) '预演写入了依赖。'
& $script @arguments -Action Install | Out-Null
$dll = Join-Path $fixture 'version.dll'
$payload = Join-Path $fixture 'uu-adb-patch'
$config = Get-Content -LiteralPath (Join-Path $payload 'config.local.json') -Raw | ConvertFrom-Json
Assert ($config.adb -eq 'runtime/platform-tools/adb.exe' -and $config.scrcpy -eq 'runtime/scrcpy') '安装配置仍包含原机依赖路径。'
foreach ($relative in @($config.adb, ($config.scrcpy + '/scrcpy-server'), ($config.scrcpy + '/avcodec-63.dll'), ($config.scrcpy + '/avutil-61.dll'))) {
    Assert (Test-Path -LiteralPath (Join-Path $payload $relative)) '依赖复制不完整。'
}
Assert (!(Test-Path -LiteralPath (Join-Path $payload 'private'))) '安装复制了原机私有目录。'
$hash = (Get-FileHash -LiteralPath $dll).Hash
& $script @arguments -Action Install | Out-Null
Assert ((Get-FileHash -LiteralPath $dll).Hash -eq $hash) '重复安装更改了补丁。'
& $script @arguments -Action Disable -WhatIf | Out-Null
Assert (Test-Path -LiteralPath $dll) '停用预演改变了补丁。'
& $script @arguments -Action Disable | Out-Null
Assert (!(Test-Path -LiteralPath $dll)) '停用没有移走代理入口。'
Assert (@(Get-ChildItem -LiteralPath $fixture -Filter 'version.dll.disabled-*').Count -eq 1) '停用未保留备份。'
Assert (Test-Path -LiteralPath (Join-Path $payload 'config.local.json')) '停用没有保留配置。'
[IO.File]::WriteAllText($dll, 'unknown-library-fixture')
$rejected = $false
try { & $script @arguments -Action Disable | Out-Null } catch { $rejected = $true }
Assert $rejected '停用接受了未知同名库。'
$rejected = $false
try { & $script @arguments -Action Install | Out-Null } catch { $rejected = $true }
Assert $rejected '安装覆盖了未知同名库。'
[IO.File]::WriteAllText((Join-Path $fixture 'nrd_renderer_ipc.dll'), 'unsupported-host-fixture')
$rejected = $false
try { & $script @arguments -Action Install -WhatIf | Out-Null } catch { $rejected = $true }
Assert $rejected '安装接受了不匹配的宿主。'
Write-Output ('隔离安装、重复安装、停用、未知文件和版本保护检查通过。资料保留在：' + $fixture)
