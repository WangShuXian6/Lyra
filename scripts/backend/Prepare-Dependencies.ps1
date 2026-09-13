param([string]$ProjectRoot = '', [string]$PostgresRoot = 'D:/program/1-web/PostgreSQL/18')
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $ProjectRoot) { $ProjectRoot=Get-MMOProjectRoot }
if (-not (Test-Path -LiteralPath (Join-Path $PostgresRoot 'lib/libpq.lib'))) { throw 'Install PostgreSQL 18 command line tools and development headers first.' }
$deps=Join-Path $ProjectRoot '.deps'; New-Item -ItemType Directory -Path $deps -Force | Out-Null
$archive=Join-Path $deps 'libsodium-1.0.22-msvc.zip'
$url='https://download.libsodium.org/libsodium/releases/libsodium-1.0.22-msvc.zip'
$expected='3E03A726FAC4BC09CB61D8F29D658EF7A5ECA0811DE59082130414F7CA2E4279'
if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $url -OutFile $archive }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'libsodium archive hash mismatch. Stop and verify the upstream release.' }
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $deps 'sodium') -Force
$env:MMO_POSTGRES_ROOT=$PostgresRoot
$env:MMO_SODIUM_ROOT=Join-Path $deps 'sodium/libsodium'
Write-Host 'libpq headers/import library located; libsodium 1.0.22 archive hash verified.'
