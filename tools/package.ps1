#Requires -Version 7.0
[CmdletBinding()]
param([string]$BuildDirectory = 'build/dropin/Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDirectory, $root)
$gitOptions = @('-c', ('safe.directory=' + $root.Replace('\','/')), '-C', $root)
$revision = & git @gitOptions rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw '无法读取源码提交。' }
$dirty = & git @gitOptions status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw '先提交并检查当前源码，再打包对应版本。' }
$dll = Join-Path $build 'version.dll'
if (!(Test-Path -LiteralPath $dll -PathType Leaf)) { throw '构建产物缺失：version.dll' }
# Always derive the shipped config from the public template, never this computer's local config.
$template = Join-Path $root 'config.example.json'
$config = Get-Content -LiteralPath $template -Raw | ConvertFrom-Json
if ($config.adb -ne 'runtime/scrcpy/adb.exe' -or $config.scrcpy -ne 'runtime/scrcpy' -or @($config.connect).Count) {
    throw '公开配置应使用默认相对路径和空设备列表，未生成部署包。'
}
$tag = 'uu-adb-patch-drop-in-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $revision.Substring(0,7)
$destination = Join-Path $root ('dist/' + $tag)
if (Test-Path -LiteralPath $destination) { throw '候选目录已存在，保留原文件。' }
$payload = Join-Path $destination 'uu-adb-patch'
$licenses = Join-Path $payload 'licenses'
New-Item -ItemType Directory -Path $licenses, (Join-Path $payload 'runtime/scrcpy') | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $destination 'version.dll')
Copy-Item -LiteralPath $template -Destination (Join-Path $payload 'config.local.json')
Copy-Item -LiteralPath (Join-Path $root 'packaging/README.md') -Destination (Join-Path $payload 'README.md')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $payload
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY.md') -Destination $payload
Copy-Item -LiteralPath (Join-Path $root 'third_party/minhook/LICENSE.txt') -Destination (Join-Path $licenses 'minhook.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party/json/LICENSE.MIT') -Destination (Join-Path $licenses 'json.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party/ffmpeg/COPYING.LGPLv2.1') -Destination (Join-Path $licenses 'ffmpeg.txt')
$entries = @(Get-ChildItem -LiteralPath $destination -File -Recurse | Sort-Object FullName | ForEach-Object {
    @{path=[IO.Path]::GetRelativePath($destination,$_.FullName).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{revision=$revision; source_url=('https://github.com/Rea1-ms/UU-Remote-Patch/tree/' + $revision); layout='version.dll + uu-adb-patch/'; config_origin='config.example.json'; stage='persistent-candidate-host-restart-pending'; architecture='windows-x64'; host_version='4.46.0.4716'; files=$entries} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $payload 'manifest.json') -Encoding utf8NoBOM
$archive = $destination + '.zip'
Compress-Archive -LiteralPath (Join-Path $destination 'version.dll'), $payload -DestinationPath $archive
Write-Output $archive
Get-FileHash -LiteralPath $archive -Algorithm SHA256
