param([string]$ProjectRoot='')
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $ProjectRoot) { $ProjectRoot=Get-MMOProjectRoot }
$local=Join-Path $ProjectRoot '.local'
Set-Content -LiteralPath (Join-Path $local 'backend.stop') -Value 'drain'
$pidFile=Join-Path $local 'backend.pid'
if (Test-Path -LiteralPath $pidFile) {
    $backendPid=[int](Get-Content -LiteralPath $pidFile)
    $proc=Get-Process -Id $backendPid -ErrorAction SilentlyContinue
    if ($proc -and -not $proc.WaitForExit(30000)) { throw 'Backend is still draining; inspect logs. No process was forcibly stopped.' }
}
Write-Host 'Backend stopped after draining accepted work.'
