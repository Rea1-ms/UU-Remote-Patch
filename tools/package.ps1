[CmdletBinding()]
param([string]$BuildDirectory = 'build/revision2/Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
$gitOptions = @('-c', ('safe.directory=' + $root.Replace('\','/')), '-C', $root)
$revision = & git @gitOptions rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw '无法读取源码提交。' }
$dirty = & git @gitOptions status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw '先提交并检查当前源码，再打包对应版本。' }
foreach ($name in @('version.dll','patch_load.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $build $name))) { throw ('构建产物缺失：' + $name) }
}
$tag = 'uu-adb-patch-r2-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $revision.Substring(0,7)
$destination = Join-Path $root ('dist/' + $tag)
if (Test-Path -LiteralPath $destination) { throw '候选目录已存在，保留原文件。' }
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($name in @('version.dll','patch_load.exe')) { Copy-Item -LiteralPath (Join-Path $build $name) -Destination $destination }
foreach ($name in @('config.example.json','README.md','THIRD_PARTY.md')) { Copy-Item -LiteralPath (Join-Path $root $name) -Destination $destination }
Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $destination -Recurse
$licenses = Join-Path $destination 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'third_party/minhook/LICENSE.txt') -Destination (Join-Path $licenses 'minhook.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party/json/LICENSE.MIT') -Destination (Join-Path $licenses 'json.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party/ffmpeg/COPYING.LGPLv2.1') -Destination (Join-Path $licenses 'ffmpeg.txt')
& git @gitOptions archive --format=zip ('--output=' + (Join-Path $destination 'source.zip')) HEAD
if ($LASTEXITCODE -ne 0) { throw '生成源码归档失败，保留当前目录。' }
$entries = @(Get-ChildItem -LiteralPath $destination -File -Recurse | ForEach-Object {
    @{path=[IO.Path]::GetRelativePath($destination,$_.FullName).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{revision=$revision; stage='candidate-unverified-on-phone'; files=$entries} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
$archive = $destination + '.zip'
Compress-Archive -LiteralPath $destination -DestinationPath $archive
Write-Output $archive
Get-FileHash -LiteralPath $archive -Algorithm SHA256
