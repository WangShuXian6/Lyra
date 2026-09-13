param([Parameter(Mandatory)][string]$Manifest,[switch]$Force)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../backend/Common.ps1')
$mmoRun=Get-Content -LiteralPath $Manifest -Raw|ConvertFrom-Json
if([IO.Path]::GetFileName($mmoRun.project) -ne 'MMORPG.uproject'){throw 'This is not an MMORPG process manifest.'}
foreach($mmoFixture in $mmoRun.fixtures){if($mmoFixture -notmatch '^showcase_[ab]_[a-f0-9]{10}$'){throw 'Unexpected fixture name; refusing cleanup.'}}

function Get-MMORecordedProcess($Entry) {
    $mmoProcess=Get-Process -Id $Entry.pid -ErrorAction SilentlyContinue
    if(-not $mmoProcess){return $null}
    $mmoExpectedExecutable=if($Entry.executable){$Entry.executable}else{$mmoRun.executable}
    if($mmoProcess.StartTime.ToUniversalTime().Ticks -ne ([DateTime]$Entry.started).ToUniversalTime().Ticks -or [IO.Path]::GetFullPath($mmoProcess.Path) -ne [IO.Path]::GetFullPath($mmoExpectedExecutable)){throw "PID $($Entry.pid) no longer matches the recorded process."}
    $mmoProcess
}
foreach($mmoEntry in @($mmoRun.processes|Where-Object role -like 'client-*')) {
    $mmoProcess=Get-MMORecordedProcess $mmoEntry
    if($mmoProcess) {
        $null=$mmoProcess.CloseMainWindow()
        if(-not $mmoProcess.WaitForExit(15000)) {
            if(-not $Force){throw "Client $($mmoEntry.pid) did not quit normally. Close it, or use -Force for this disposable showcase fixture."}
            $mmoProcess.Kill();$mmoProcess.WaitForExit()
        }
    }
}
$mmoServerEntry=$mmoRun.processes|Where-Object role -eq 'server'|Select-Object -First 1
if($mmoServerEntry) {
    $mmoServer=Get-MMORecordedProcess $mmoServerEntry
    if($mmoServer) {
        # Confirm async final snapshots before stopping this otherwise idle server.
        $mmoDeadline=[DateTime]::UtcNow.AddSeconds(30)
        do {
            $mmoServerText=Get-Content -LiteralPath $mmoServerEntry.log -Raw
            $mmoAdmitted=@([regex]::Matches($mmoServerText,'MMO admitted character ([a-f0-9-]+)')|ForEach-Object{$_.Groups[1].Value}|Select-Object -Unique)
            $mmoPending=@($mmoAdmitted|Where-Object{$mmoServerText -notmatch ('MMO saved character '+[regex]::Escape($_)+' at version \d+ \(final\)')})
            if($mmoPending.Count){Start-Sleep -Milliseconds 500}
        } while($mmoPending.Count -and [DateTime]::UtcNow -lt $mmoDeadline)
        if($mmoPending.Count -and -not $Force){throw 'Final saves did not finish. The game server remains running so retries can continue.'}
        Start-Sleep -Seconds 2
        $null=$mmoServer.CloseMainWindow()
        if(-not $mmoServer.WaitForExit(5000)){$mmoServer.Kill();$mmoServer.WaitForExit()}
    }
} else {
    # A pre-existing server belongs to its original launcher and remains running.
    Start-Sleep -Seconds 3
}
if($mmoRun.fixtures.Count) {
    Import-MMOEnvironment
    foreach($mmoFixture in $mmoRun.fixtures){$null=Invoke-MMOPsql "DELETE FROM accounts WHERE username='$mmoFixture';"}
}
Write-Host 'Recorded clients stopped, final saves checked, and only exact showcase fixtures removed. A separately launched server is preserved.'
