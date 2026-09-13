param([string]$ProjectRoot='', [int]$Port=8088)
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $ProjectRoot) { $ProjectRoot=Get-MMOProjectRoot }
Import-MMOEnvironment
$local=Join-Path $ProjectRoot '.local'; New-Item -ItemType Directory -Path $local -Force | Out-Null
$stopFile=Join-Path $local 'backend.stop'
if (Test-Path -LiteralPath $stopFile) { Remove-Item -LiteralPath $stopFile }
$exe=Join-Path $ProjectRoot 'Binaries/Win64/MMOBackendService.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build MMOBackendService first.' }
$args=@("-Port=$Port", "-ShutdownFile=`"$stopFile`"", '-unattended', '-NoSound')
$proc=Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $ProjectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $local 'backend.stdout.log') -RedirectStandardError (Join-Path $local 'backend.stderr.log')
Set-Content -LiteralPath (Join-Path $local 'backend.pid') -Value $proc.Id
for ($attempt=0;$attempt -lt 40;$attempt++) {
    if ($proc.HasExited) { throw "Backend exited ($($proc.ExitCode)); inspect .local/backend.stdout.log." }
    try { $health=Invoke-RestMethod "http://127.0.0.1:$Port/health" -TimeoutSec 2; if ($health.ok) { Write-Host "Backend ready on 127.0.0.1:$Port (PID $($proc.Id))."; return } } catch {}
    Start-Sleep -Milliseconds 250
}
throw 'Backend did not become healthy; inspect .local logs and database connectivity.'
