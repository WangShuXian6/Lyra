param([int]$Port=8089)
. (Join-Path $PSScriptRoot 'Common.ps1')
Import-MMOEnvironment
$project=Get-MMOProjectRoot; $local=Join-Path $project '.local'
$stop=Join-Path $local 'backend-fault.stop'
if(Test-Path -LiteralPath $stop) { Remove-Item -LiteralPath $stop }
$existing=Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if($existing) { throw "Fault-test port $Port is already occupied." }
$oldPort=$env:PGPORT
try {
    # Isolated child process points at a closed DB port; the user's PostgreSQL service is untouched.
    $env:PGPORT='1'
    $exe=Join-Path $project 'Binaries/Win64/MMOBackendService.exe'
    $proc=Start-Process -FilePath $exe -WorkingDirectory $project -ArgumentList @("-Port=$Port","-ShutdownFile=`"$stop`"",'-unattended') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $local 'backend-fault.stdout.log') -RedirectStandardError (Join-Path $local 'backend-fault.stderr.log')
} finally { $env:PGPORT=$oldPort }
try {
    $response=$null
    for($i=0;$i -lt 30;$i++) {
        if($proc.HasExited) { throw 'Fault backend unexpectedly exited.' }
        try { $response=Invoke-WebRequest "http://127.0.0.1:$Port/health" -SkipHttpErrorCheck -TimeoutSec 8; break } catch { Start-Sleep -Milliseconds 250 }
    }
    if(-not $response -or [int]$response.StatusCode -ne 503 -or ($response.Content|ConvertFrom-Json).error.code -ne 'database_unavailable') { throw 'Expected structured database_unavailable 503.' }
    $bad=Invoke-WebRequest "http://127.0.0.1:$Port/v1/auth/login" -Method Post -ContentType 'application/json' -Body '{broken' -SkipHttpErrorCheck -TimeoutSec 5
    if([int]$bad.StatusCode -ne 400) { throw 'HTTP thread did not continue to reject invalid JSON while DB was unavailable.' }
    Write-Host 'PASS isolated database failure returns 503; HTTP main loop remains responsive.'
} finally {
    Set-Content -LiteralPath $stop -Value 'drain'
    if(-not $proc.WaitForExit(30000)) { throw 'Fault backend did not drain normally.' }
}
if($proc.ExitCode -ne 0) { throw "Fault backend failed normal shutdown ($($proc.ExitCode))." }
@{at=(Get-Date).ToUniversalTime().ToString('o');databaseUnavailable=$true;httpResponsive=$true;gracefulExitCode=$proc.ExitCode}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $local 'backend-fault-results.json')
Write-Host 'PASS normal shutdown after database failure.'
