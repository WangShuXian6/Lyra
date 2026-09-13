#Requires -Version 7.2
param(
    [string]$PackageReport=(Join-Path $PSScriptRoot '../verification/training-package.json'),
    [string]$Executable,
    [string]$ArtifactRoot='F:/UE/LyraDocLabs/Artifacts/LyraTrainingRuntime',
    [ValidateRange(120,300)][int]$TimeoutSeconds=150,
    [switch]$ShowCommand
)
$ErrorActionPreference='Stop'
$docsRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$packageReportPath=[IO.Path]::GetFullPath($PackageReport)
if(-not(Test-Path -LiteralPath $packageReportPath)){throw 'Run package-training.ps1 -Mode Package successfully before runtime verification.'}
$package=Get-Content -LiteralPath $packageReportPath -Raw | ConvertFrom-Json
if($package.passed -ne $true -or $package.mode -ne 'Package' -or $package.configuration -ne 'Development' -or $package.map -ne '/TrainingRange/Maps/L_TrainingRange') {
    throw 'A passed Development TrainingRange Package report is required; a Cook report cannot establish a packaged executable.'
}
$candidates=@($package.artifacts | Where-Object {
    $_.path.Replace('\','/') -match '/Binaries/Win64/LyraGame(?:-Win64-Development)?\.exe$'
})
if(-not $Executable) {
    if($candidates.Count -ne 1){throw 'Select the actual archived Binaries/Win64/LyraGame executable with -Executable; do not use the bootstrap or UnrealEditor executable.'}
    $Executable=$candidates[0].path
}
$exePath=[IO.Path]::GetFullPath($Executable)
if(-not($candidates | Where-Object { [IO.Path]::GetFullPath($_.path) -eq $exePath })) {
    throw 'The selected native executable is not listed in this successful package report.'
}
if(-not(Test-Path -LiteralPath $exePath)){throw "Packaged executable is missing: $exePath"}
$runId=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6)
$runRoot=Join-Path ([IO.Path]::GetFullPath($ArtifactRoot)) $runId
$nativeReport=Join-Path $runRoot 'runtime.json'
$screenshot=Join-Path $runRoot 'training-package.png'
$gameLog=Join-Path $runRoot 'game.log'
$arguments=@('/TrainingRange/Maps/L_TrainingRange','-LyraTrainingVerify',"-LyraTrainingVerifyOutput=$runRoot",
    '-windowed','-ResX=1920','-ResY=1080','-ForceRes','-d3d12','-NoSplash','-unattended',
    '-NoVSync',"-abslog=$gameLog")
if($ShowCommand) {
    Write-Host "Native executable: $exePath"
    foreach($argument in $arguments){Write-Host "  $argument"}
    Write-Host 'Display only. No process, directory, or verification report created.'
    return
}
if(Test-Path -LiteralPath $runRoot){throw 'A fresh runtime artifact directory is required.'}
New-Item -ItemType Directory -Path $runRoot | Out-Null
$reportFile=Join-Path $docsRoot 'verification/training-package-runtime.json'
$report=[ordered]@{
    startedAt=[DateTime]::UtcNow.ToString('o');status='running';passed=$false;scope='packaged-single-player'
    executable=$exePath;executableSha256=(Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLowerInvariant()
    packageReport=$packageReportPath;packageReportSha256=(Get-FileHash -LiteralPath $packageReportPath -Algorithm SHA256).Hash.ToLowerInvariant()
    sourceMapSha256=$package.sourceMapSha256;arguments=$arguments;runDirectory=$runRoot
    nativeReport=$nativeReport;nativeReportSha256=$null;log=$gameLog;screenshot=$screenshot
    screenshotSha256=$null;screenshotWidth=$null;screenshotHeight=$null
    processId=$null;exitCode=$null;timeoutSeconds=$TimeoutSeconds;checks=@();error=$null
    notCovered='Packaged networking, pickup and respawn. Existing PIE reports remain separate evidence.'
}
$process=$null
try {
    $report|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $reportFile -Encoding utf8
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$exePath
    $start.WorkingDirectory=Split-Path $exePath -Parent
    $start.UseShellExecute=$false
    # This is the requested visible graphical game validation, not a background helper.
    foreach($argument in $arguments){$start.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::Start($start)
    $report.processId=$process.Id
    Write-Host "Packaged game PID $($process.Id); leave its window focused while native forced-key input runs."
    $timer=[Diagnostics.Stopwatch]::StartNew()
    $nextProgress=15
    while(-not $process.WaitForExit(1000)) {
        if($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds){throw "Packaged game exceeded the launcher timeout of $TimeoutSeconds seconds."}
        if($timer.Elapsed.TotalSeconds -ge $nextProgress){Write-Host "Waiting for native runtime report and screenshot: $([int]$timer.Elapsed.TotalSeconds)s";$nextProgress+=15}
    }
    $process.Refresh()
    $report.exitCode=$process.ExitCode
    if(-not(Test-Path -LiteralPath $nativeReport)){throw "The actual executable exited without runtime.json (exit $($report.exitCode)); inspect $gameLog for feature/module loading."}
    $native=Get-Content -LiteralPath $nativeReport -Raw | ConvertFrom-Json
    $report.nativeReportSha256=(Get-FileHash -LiteralPath $nativeReport -Algorithm SHA256).Hash.ToLowerInvariant()
    $report.checks=@($native.checks)
    if($native.processId -ne $process.Id -or [IO.Path]::GetFullPath($native.executable) -ne $exePath -or $native.scope -ne 'packaged-single-player') {
        throw 'Native runtime report does not identify the process and packaged scope launched by this script.'
    }
    $expected=@('packaged-world','experience','pawn-asc','training-dash-granted','hud','display-medium-1080','movement','dash-input-activation','fire-ammo','reload-ammo','native-screenshot')
    foreach($id in $expected) {
        $checks=@($native.checks | Where-Object { $_.id -eq $id })
        if($checks.Count -ne 1 -or $checks[0].passed -ne $true){throw "The native check '$id' did not pass exactly once. Inspect $nativeReport"}
    }
    if($native.passed -ne $true -or @($native.checks | Where-Object { $_.passed -ne $true }).Count -or $report.exitCode -ne 0) {
        throw "The native runtime failed or did not exit normally (exit $($report.exitCode))."
    }
    $imageCheck=@($native.checks | Where-Object {$_.id -eq 'native-screenshot'})[0].evidence
    if([IO.Path]::GetFullPath($imageCheck.path) -ne $screenshot -or $imageCheck.processedCallback -ne $true -or
        $imageCheck.showUI -ne $true -or $imageCheck.restrictToGameViewport -ne $true) {
        throw 'Screenshot evidence does not establish the native processed viewport request with UI.'
    }
    $bytes=[IO.File]::ReadAllBytes($screenshot)
    if($bytes.Length -lt 45 -or ($bytes[0..7] -join ',') -ne '137,80,78,71,13,10,26,10' -or
        [Text.Encoding]::ASCII.GetString($bytes,12,4) -ne 'IHDR' -or
        ($bytes[($bytes.Length-12)..($bytes.Length-1)] -join ',') -ne '0,0,0,0,73,69,78,68,174,66,96,130') {
        throw 'The native screenshot is not a complete PNG with IHDR and final IEND chunks.'
    }
    $width=([uint32]$bytes[16]*16777216)+([uint32]$bytes[17]*65536)+([uint32]$bytes[18]*256)+[uint32]$bytes[19]
    $height=([uint32]$bytes[20]*16777216)+([uint32]$bytes[21]*65536)+([uint32]$bytes[22]*256)+[uint32]$bytes[23]
    $report.screenshotWidth=$width;$report.screenshotHeight=$height
    if($width -ne 1920 -or $height -ne 1080){throw "Native screenshot size is $width x $height, expected 1920 x 1080. No resizing is performed."}
    $report.screenshotSha256=(Get-FileHash -LiteralPath $screenshot -Algorithm SHA256).Hash.ToLowerInvariant()
    $report.passed=$true;$report.status='passed'
} catch {
    $report.status='failed';$report.error=$_.Exception.Message
    throw
} finally {
    # Only the process created above may be closed; never enumerate/stop other UE sessions.
    if($process -and -not $process.HasExited) {
        $report.normalCloseRequested=$process.CloseMainWindow()
        if(-not $process.WaitForExit(10000)){$process.Kill();$process.WaitForExit();$report.forcedExitAfterTimeout=$true}
    }
    if($process -and $process.HasExited){$report.exitCode=$process.ExitCode}
    $report.finishedAt=[DateTime]::UtcNow.ToString('o')
    $report|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $reportFile -Encoding utf8
    $report|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $runRoot 'report.json') -Encoding utf8
    if($process){$process.Dispose()}
}
Write-Host "Packaged single-player verification passed: $reportFile"
