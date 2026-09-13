param(
    [string]$Project='F:/UE/LyraDocLabs/MMORPG/MMORPG.uproject',
    [string]$Engine='E:/UnrealEngine',
    [string]$ClientExecutable='',
    [string]$ServerExecutable='',
    [switch]$UseRunningServer,
    [switch]$KeepServer,
    [switch]$Visible,
    [switch]$RenderOffscreen,
    [switch]$RestartBackendBetweenRounds,
    [switch]$DeathLogoutOnly
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../backend/Common.ps1')
$mmoRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$mmoLab=Split-Path ([IO.Path]::GetFullPath($Project)) -Parent
$mmoRawLogs=Join-Path $mmoLab 'Saved/Logs/Smoke'
$mmoPackaged=[bool]$ClientExecutable
if($mmoPackaged -and -not $ServerExecutable -and -not $UseRunningServer){throw 'Packaged smoke requires both ClientExecutable and ServerExecutable'}
if($ServerExecutable -and -not $mmoPackaged){throw 'Provide ClientExecutable with ServerExecutable'}
$mmoEvidence=Join-Path $mmoRepo $(if($DeathLogoutOnly){if($mmoPackaged){'verification/mmorpg-packaged-death-logout.json'}else{'verification/mmorpg-death-logout.json'}}elseif($mmoPackaged){'verification/mmorpg-packaged-smoke.json'}else{'verification/mmorpg-smoke.json'})
New-Item -ItemType Directory -Path $mmoRawLogs -Force | Out-Null
$mmoExe=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor.exe'
if($mmoPackaged) {
    $ClientExecutable=(Resolve-Path -LiteralPath $ClientExecutable).Path
    if($ServerExecutable){$ServerExecutable=(Resolve-Path -LiteralPath $ServerExecutable).Path}
    if([IO.Path]::GetFileName($ClientExecutable) -ne 'MMORPGClient.exe'){throw 'Expected a Development MMORPGClient.exe'}
    if($ServerExecutable -and [IO.Path]::GetFileName($ServerExecutable) -ne 'MMORPGServer.exe'){throw 'Expected a Development MMORPGServer.exe'}
}
$mmoRunId=[Guid]::NewGuid().ToString('N').Substring(0,10)
$mmoPassword=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$mmoUsers=@("smoke_a_$mmoRunId","smoke_b_$mmoRunId")
$mmoResults=[Collections.Generic.List[object]]::new()
$mmoPersistedDeaths=[Collections.Generic.List[object]]::new()
$mmoCharacterIDs=[Collections.Generic.List[string]]::new()
$mmoCompletedRounds=0
$mmoServer=$null
$mmoTaggedDispatch=$null

function Start-MMOChild([string[]]$Arguments,[bool]$Server,[string]$Password='') {
    $mmoStartInfo=[Diagnostics.ProcessStartInfo]::new()
    $mmoStartInfo.FileName=if($mmoPackaged){if($Server){$ServerExecutable}else{$ClientExecutable}}else{$mmoExe}
    $mmoStartInfo.UseShellExecute=$false
    $mmoStartInfo.WorkingDirectory=if($mmoPackaged){Split-Path $mmoStartInfo.FileName -Parent}else{$mmoLab}
    $mmoStartInfo.WindowStyle=if($Server -or -not $Visible){[Diagnostics.ProcessWindowStyle]::Hidden}else{[Diagnostics.ProcessWindowStyle]::Normal}
    foreach($mmoArg in $Arguments){$mmoStartInfo.ArgumentList.Add($mmoArg)}
    foreach($mmoEnvKey in @($mmoStartInfo.Environment.Keys)) {
        if($mmoEnvKey -like 'PG*' -or $mmoEnvKey -in @('MMO_SERVER_SECRET','MMO_TEST_PASSWORD')){[void]$mmoStartInfo.Environment.Remove($mmoEnvKey)}
    }
    if($Server){$mmoStartInfo.Environment['MMO_SERVER_SECRET']=$env:MMO_SERVER_SECRET}
    else {$mmoStartInfo.Environment['MMO_TEST_PASSWORD']=$Password}
    [Diagnostics.Process]::Start($mmoStartInfo)
}
function Invoke-MMOTestAPI([string]$Path,[object]$Body,[string]$Token='') {
    $mmoHeaders=@{}; if($Token){$mmoHeaders.Authorization="Bearer $Token"}
    Invoke-RestMethod -Uri ('http://127.0.0.1:8088'+$Path) -Method Post -Headers $mmoHeaders -ContentType 'application/json' -Body ($Body|ConvertTo-Json -Depth 12 -Compress)
}

try {
    $null=Invoke-RestMethod 'http://127.0.0.1:8088/health'
    foreach($mmoUser in $mmoUsers) {
        $null=Invoke-MMOTestAPI '/v1/auth/register' @{username=$mmoUser;password=$mmoPassword}
        $mmoLogin=Invoke-MMOTestAPI '/v1/auth/login' @{username=$mmoUser;password=$mmoPassword}
        $mmoCharacter=Invoke-MMOTestAPI '/v1/characters' @{name=$mmoUser} $mmoLogin.token
        $mmoCharacterIDs.Add($mmoCharacter.id)
        $null=Invoke-MMOTestAPI '/v1/auth/logout' @{} $mmoLogin.token
    }
    if(-not $UseRunningServer) {
        Import-MMOGameServerEnvironment
        $mmoServerLog=Join-Path $mmoRawLogs "server-$mmoRunId.log"
        $mmoServerArgs=@('/Engine/Maps/Entry','-server','-port=7777','-unattended','-nosound','-LiveCoding=false','-Multiprocess',"-abslog=$mmoServerLog")
        if(-not $mmoPackaged){$mmoServerArgs=@($Project)+$mmoServerArgs}
        $mmoServer=Start-MMOChild $mmoServerArgs $true
        $mmoDeadline=[DateTime]::UtcNow.AddMinutes(3)
        while([DateTime]::UtcNow -lt $mmoDeadline) {
            if($mmoServer.HasExited){throw "Server exited with code $($mmoServer.ExitCode)"}
            if(Test-Path -LiteralPath $mmoServerLog) {
                $mmoLogText=Get-Content -LiteralPath $mmoServerLog -Raw
                if($mmoLogText -match 'GameNetDriver.*listening on port 7777' -and $mmoLogText -match 'MMOCore: zone added'){break}
            }
            Start-Sleep -Milliseconds 500
        }
        if([DateTime]::UtcNow -ge $mmoDeadline){throw 'Server did not become ready in three minutes'}
    }
    $mmoRounds=if($DeathLogoutOnly){@('combat','death-logout','death-restore')}else{@('combat','restore','death-respawn','ui-return')}
    foreach($mmoRound in $mmoRounds) {
        $mmoClients=@(); $mmoRoundLogs=@()
        for($mmoIndex=0;$mmoIndex -lt 2;$mmoIndex++) {
            $mmoRole=if($mmoIndex -eq 0){'A'}else{'B'}
            $mmoClientLog=Join-Path $mmoRawLogs "$mmoRound-$mmoRole-$mmoRunId.log"
            $mmoArgs=@($Project,'/Engine/Maps/Entry','-game','-windowed','-ResX=1920','-ResY=1080','-ForceRes','-unattended','-nosound','-LiveCoding=false','-Multiprocess',"-MMOSmokeUser=$($mmoUsers[$mmoIndex])","-MMOSmokeRole=$mmoRole","-abslog=$mmoClientLog",'-ExecCmds=sg.ViewDistanceQuality 1,sg.AntiAliasingQuality 1,sg.ShadowQuality 1,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.PostProcessQuality 1,sg.TextureQuality 1,sg.EffectsQuality 1,sg.FoliageQuality 1,sg.ShadingQuality 1,t.MaxFPS 30')
            if($mmoPackaged){$mmoArgs=@($mmoArgs[1..($mmoArgs.Count-1)]|Where-Object {$_ -ne '-game'})}
            if($RenderOffscreen){$mmoArgs+='-RenderOffscreen'}
            elseif(-not $Visible){$mmoArgs+='-NullRHI'}
            if($mmoRound -eq 'restore'){$mmoArgs+='-MMOSmokeRestore'}
            if($mmoRound -eq 'death-respawn'){$mmoArgs+='-MMOSmokeDeath'}
            if($mmoRound -eq 'death-logout'){$mmoArgs+='-MMOSmokeDeadLogout'}
            if($mmoRound -eq 'death-restore'){$mmoArgs+='-MMOSmokeDeadRestore'}
            if($DeathLogoutOnly){$mmoArgs+='-MMOSmokeKeepPotion'}
            if($mmoRound -eq 'ui-return'){$mmoArgs+='-MMOSmokeUIReturn'}
            $mmoClients+=Start-MMOChild $mmoArgs $false $mmoPassword; $mmoRoundLogs+=$mmoClientLog
        }
        $mmoDeadline=[DateTime]::UtcNow.AddMinutes(5)
        while(@($mmoClients|Where-Object{-not $_.HasExited}).Count -gt 0 -and [DateTime]::UtcNow -lt $mmoDeadline){Start-Sleep -Milliseconds 500}
        foreach($mmoClient in $mmoClients){if(-not $mmoClient.HasExited){$mmoClient.CloseMainWindow()|Out-Null; throw 'Client smoke test timed out; inspect local raw logs'}}
        foreach($mmoLog in $mmoRoundLogs) {
            $mmoMatch=[regex]::Match((Get-Content -LiteralPath $mmoLog -Raw),'MMO_SMOKE_RESULT (\{[^\r\n]+\})')
            if(-not $mmoMatch.Success){throw "No gameplay result in local log $mmoLog"}
            $mmoResult=$mmoMatch.Groups[1].Value|ConvertFrom-Json
            $mmoTagStage=switch($mmoRound){'ui-return'{'before-ui-return'} 'death-logout'{'before-death-logout'} 'death-respawn'{'death-respawn'} default{'gameplay-result'}}
            $mmoTagInput=& (Join-Path $PSScriptRoot 'Read-TaggedAbilityInputs.ps1') -Log $mmoLog -Stage $mmoTagStage
            $mmoResult|Add-Member -NotePropertyName inputGrants -NotePropertyValue $mmoTagInput
            $mmoMovementBases=& (Join-Path $PSScriptRoot 'Test-MovementBaseLogs.ps1') -Logs @($mmoLog)
            $mmoResult|Add-Member -NotePropertyName movementBases -NotePropertyValue $mmoMovementBases
            if(-not $mmoResult.tagDrivenInput){throw "Gameplay did not verify the tag-based grants for $mmoRound"}
            $mmoResults.Add($mmoResult)
            if(-not $mmoResult.passed){throw "Gameplay assertions failed for $mmoRound role $($mmoResult.role)"}
            if($mmoRound -eq 'death-logout' -and ((Get-Content -LiteralPath $mmoLog -Raw) -notmatch 'MMO_DEAD_LOGOUT_SNAPSHOT ')){throw 'Exit did not follow a replicated GAS death'}
        }
        # Wait for the actual final-save success callbacks before restarting persistence.
        $mmoCompletedRounds++
        if($mmoServer) {
            $mmoDeadline=[DateTime]::UtcNow.AddSeconds(40)
            do {
                $mmoServerText=Get-Content -LiteralPath $mmoServerLog -Raw
                $mmoFinalsComplete=@($mmoCharacterIDs|Where-Object{
                    [regex]::Matches($mmoServerText,'MMO saved character '+[regex]::Escape($_)+' at version \d+ \(final\)').Count -lt $mmoCompletedRounds -or
                    [regex]::Matches($mmoServerText,'MMO released character '+[regex]::Escape($_)).Count -lt $mmoCompletedRounds
                }).Count -eq 0
                if(-not $mmoFinalsComplete){Start-Sleep -Milliseconds 300}
            }while(-not $mmoFinalsComplete -and [DateTime]::UtcNow -lt $mmoDeadline)
            if(-not $mmoFinalsComplete){throw 'Final-save callbacks did not complete before the next round'}
        }
        Start-Sleep -Seconds 2 # Allow the subsequent release HTTP response to return.
        if($mmoRound -eq 'death-logout') {
            for($mmoIndex=0;$mmoIndex -lt 2;$mmoIndex++) {
                $mmoSnapshotMatch=[regex]::Match((Get-Content -LiteralPath $mmoRoundLogs[$mmoIndex] -Raw),'MMO_DEAD_LOGOUT_SNAPSHOT (\{[^\r\n]+\})')
                $mmoBeforeExit=$mmoSnapshotMatch.Groups[1].Value|ConvertFrom-Json
                $mmoLogin=Invoke-MMOTestAPI '/v1/auth/login' @{username=$mmoUsers[$mmoIndex];password=$mmoPassword}
                try {
                    $mmoList=Invoke-RestMethod 'http://127.0.0.1:8088/v1/characters' -Headers @{Authorization="Bearer $($mmoLogin.token)"}
                    $mmoStored=($mmoList.characters|Where-Object id -eq $mmoCharacterIDs[$mmoIndex]).state
                    if(-not $mmoStored -or $mmoBeforeExit.health -ne 0 -or $mmoStored.health -ne 0){throw 'The actual death snapshot was not saved as health zero before fresh admission'}
                    foreach($mmoField in @('mana','level','xp')){if($mmoStored.$mmoField -ne $mmoBeforeExit.$mmoField){throw "Death logout changed persisted $mmoField"}}
                    if(($mmoStored.inventory|ConvertTo-Json -Compress) -ne ($mmoBeforeExit.inventory|ConvertTo-Json -Compress)){throw 'Death logout changed persisted inventory'}
                    $mmoPersistedDeaths.Add(@{role=$(if($mmoIndex -eq 0){'A'}else{'B'});clientSnapshot=$mmoBeforeExit;storedState=$mmoStored;finalSaveAndReleaseVerified=[bool]$mmoServer})
                } finally {$null=Invoke-MMOTestAPI '/v1/auth/logout' @{} $mmoLogin.token}
            }
        }
        if($mmoRound -eq 'combat' -and $RestartBackendBetweenRounds) {
            & (Join-Path $PSScriptRoot '../backend/Stop-Backend.ps1')
            & (Join-Path $PSScriptRoot '../backend/Start-Backend.ps1')
        }
    }
    if($mmoServer){
        $mmoTaggedDispatch=& (Join-Path $PSScriptRoot 'Read-TaggedAbilityDispatch.ps1') -ServerLog $mmoServerLog
        $mmoServerMovementBases=& (Join-Path $PSScriptRoot 'Test-MovementBaseLogs.ps1') -Logs @($mmoServerLog)
        $mmoTaggedDispatch|Add-Member -NotePropertyName movementBases -NotePropertyValue $mmoServerMovementBases
    }
    @{tagDrivenDispatch=$mmoTaggedDispatch;date=[DateTime]::UtcNow.ToString('o');requestedResolution='1920x1080';scalability='Medium (1)';backendRestarted=[bool]$RestartBackendBetweenRounds;deathLogout=[bool]$DeathLogoutOnly;persistedDeaths=$mmoPersistedDeaths;finalSaveCallbacksVerified=[bool]$mmoServer;releaseCallbacksVerified=[bool]$mmoServer;rendering=$(if($Visible -or $RenderOffscreen){'Rendered'}else{'NullRHI headless gameplay validation'});mode=$(if($mmoPackaged){'Two cooked Development MMORPGClient executables + MMORPGServer'}else{'Two independent editor game clients + dedicated server'});clientExecutable=$(if($mmoPackaged){$ClientExecutable}else{$mmoExe});serverExecutable=$(if($mmoPackaged){$ServerExecutable}else{$mmoExe});results=$mmoResults;passed=$true}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $mmoEvidence -Encoding utf8
    Write-Host "$($mmoResults.Count) gameplay client results passed. Evidence: $mmoEvidence"
}
catch {
    @{date=[DateTime]::UtcNow.ToString('o');passed=$false;error=$_.Exception.Message;results=$mmoResults}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $mmoEvidence -Encoding utf8
    throw
}
finally {
    foreach($mmoClient in @($mmoClients)) {
        if($mmoClient -and -not $mmoClient.HasExited){
            $null=$mmoClient.CloseMainWindow()
            if(-not $mmoClient.WaitForExit(5000)){$mmoClient.Kill();$mmoClient.WaitForExit()}
        }
    }
    if($mmoServer -and -not $KeepServer -and -not $mmoServer.HasExited){
        # All clients have already exited; their final-save callbacks were allowed to finish.
        $null=$mmoServer.CloseMainWindow()
        if(-not $mmoServer.WaitForExit(5000)){$mmoServer.Kill();$mmoServer.WaitForExit()}
    }
    # Only this run's validated fixture IDs are removed, after clients have stopped.
    if(@($mmoUsers|Where-Object{$_ -notmatch '^smoke_[ab]_[a-f0-9]{10}$'}).Count -eq 0) {
        Import-MMOEnvironment
        $null=Invoke-MMOPsql "DELETE FROM accounts WHERE username IN ('$($mmoUsers[0])','$($mmoUsers[1])');"
    }
    # Persist only sanitized assertions. Raw local logs may contain one-time UE travel tickets.
    $mmoPassword=$null
}
