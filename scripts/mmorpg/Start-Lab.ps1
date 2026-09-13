param(
    [string]$Project='F:/UE/LyraDocLabs/MMORPG/MMORPG.uproject',
    [string]$Engine='E:/UnrealEngine',
    [string]$ClientExecutable='',
    [string]$ServerExecutable='',
    [ValidateRange(1,2)][int]$ClientCount=2,
    [switch]$UseRunningServer,
    [switch]$Showcase,
    [switch]$ShowcaseAutoQuit,
    [switch]$RenderOffscreen
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../backend/Common.ps1')
$mmoLab=Split-Path ([IO.Path]::GetFullPath($Project)) -Parent
$mmoExe=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor.exe'
$mmoServerExe=$mmoExe
$mmoPackaged=[bool]$ClientExecutable
if([bool]$ClientExecutable -ne [bool]$ServerExecutable){throw 'Provide both staged ClientExecutable and ServerExecutable, or neither.'}
if(-not (Test-Path -LiteralPath $Project)){throw "Missing prepared project: $Project"}
if($mmoPackaged) {
    $mmoExe=(Resolve-Path -LiteralPath $ClientExecutable).Path
    $mmoServerExe=(Resolve-Path -LiteralPath $ServerExecutable).Path
    if([IO.Path]::GetFileName($mmoExe) -ne 'MMORPGClient.exe' -or [IO.Path]::GetFileName($mmoServerExe) -ne 'MMORPGServer.exe'){throw 'Expected Development MMORPGClient.exe and MMORPGServer.exe.'}
    foreach($mmoBinary in @($mmoExe,$mmoServerExe)) {
        $mmoCookedProject=[IO.Path]::GetFullPath((Join-Path (Split-Path $mmoBinary -Parent) '../..'))
        if(-not (Test-Path -LiteralPath (Join-Path $mmoCookedProject 'Content/Paks'))){throw 'Launch staged executables with their cooked Content/Paks directory, not loose build binaries.'}
    }
} elseif(-not (Test-Path -LiteralPath (Join-Path $mmoLab 'Binaries/Win64/UnrealEditor-MMORPG.dll'))){throw 'Build MMORPGEditor before launching the lab.'}
$null=Invoke-RestMethod 'http://127.0.0.1:8088/health'
$mmoLogs=Join-Path $mmoLab 'Saved/Logs/Interactive'
New-Item -ItemType Directory -Path $mmoLogs -Force | Out-Null
$mmoRun=[Guid]::NewGuid().ToString('N').Substring(0,10)
$mmoChildren=[Collections.Generic.List[object]]::new()
$mmoPreviousSecret=$env:MMO_SERVER_SECRET
$mmoFixtures=@()
$mmoShowcasePassword=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$mmoCaptures=Join-Path $mmoLab "Saved/Screenshots/MMOShowcase/$mmoRun"
$mmoProfiles=Join-Path $mmoLab "Saved/Config/Showcase/$mmoRun"

function Start-MMOInteractiveProcess([string[]]$Arguments,[bool]$Server) {
    $mmoProcessInfo=[Diagnostics.ProcessStartInfo]::new()
    $mmoProcessInfo.FileName=if($Server){$mmoServerExe}else{$mmoExe}
    $mmoProcessInfo.WorkingDirectory=if($mmoPackaged){Split-Path $mmoProcessInfo.FileName -Parent}else{$mmoLab}
    $mmoProcessInfo.UseShellExecute=$false
    $mmoProcessInfo.WindowStyle=if($Server -or $RenderOffscreen){[Diagnostics.ProcessWindowStyle]::Hidden}else{[Diagnostics.ProcessWindowStyle]::Normal}
    foreach($mmoArgument in $Arguments){$mmoProcessInfo.ArgumentList.Add($mmoArgument)}
    foreach($mmoVariable in @($mmoProcessInfo.Environment.Keys)) {
        if($mmoVariable -like 'PG*' -or $mmoVariable -in @('MMO_SERVER_SECRET','MMO_TEST_PASSWORD')) {
            [void]$mmoProcessInfo.Environment.Remove($mmoVariable)
        }
    }
    if($Server){$mmoProcessInfo.Environment['MMO_SERVER_SECRET']=$env:MMO_SERVER_SECRET}
    elseif($Showcase){$mmoProcessInfo.Environment['MMO_TEST_PASSWORD']=$mmoShowcasePassword}
    [Diagnostics.Process]::Start($mmoProcessInfo)
}

try {
    if($Showcase) {
        for($mmoIndex=0;$mmoIndex -lt $ClientCount;$mmoIndex++) {
            $mmoRole=if($mmoIndex -eq 0){'a'}else{'b'}
            $mmoUser="showcase_${mmoRole}_$mmoRun"
            $mmoAuthBody=@{username=$mmoUser;password=$mmoShowcasePassword}|ConvertTo-Json -Compress
            $null=Invoke-RestMethod 'http://127.0.0.1:8088/v1/auth/register' -Method Post -ContentType 'application/json' -Body $mmoAuthBody
            $mmoFixtures+=$mmoUser
            $mmoAuth=Invoke-RestMethod 'http://127.0.0.1:8088/v1/auth/login' -Method Post -ContentType 'application/json' -Body $mmoAuthBody
            $mmoHeaders=@{Authorization="Bearer $($mmoAuth.token)"}
            $null=Invoke-RestMethod 'http://127.0.0.1:8088/v1/characters' -Method Post -Headers $mmoHeaders -ContentType 'application/json' -Body (@{name="Traveler $($mmoRole.ToUpper())"}|ConvertTo-Json -Compress)
            $null=Invoke-RestMethod 'http://127.0.0.1:8088/v1/auth/logout' -Method Post -Headers $mmoHeaders -ContentType 'application/json' -Body '{}'
        }
    }
    if(-not $UseRunningServer) {
        if(Get-NetUDPEndpoint -LocalPort 7777 -ErrorAction SilentlyContinue){throw 'Port 7777 is already occupied. Use -UseRunningServer only for the intended MMO server.'}
        Import-MMOGameServerEnvironment
        $mmoServerLog=Join-Path $mmoLogs "server-$mmoRun.log"
        $mmoServerArguments=@('/Engine/Maps/Entry','-server','-port=7777','-NullRHI','-nosound','-unattended','-Multiprocess','-LiveCoding=false',"-abslog=$mmoServerLog")
        if(-not $mmoPackaged){$mmoServerArguments=@($Project)+$mmoServerArguments}
        $mmoServer=Start-MMOInteractiveProcess $mmoServerArguments $true
        $mmoChildren.Add(@{role='server';process=$mmoServer;log=$mmoServerLog;executable=$mmoServerExe})
        $mmoDeadline=[DateTime]::UtcNow.AddMinutes(3)
        do {
            if($mmoServer.HasExited){throw "Game server exited with code $($mmoServer.ExitCode). Inspect $mmoServerLog"}
            $mmoReady=$false
            if(Test-Path -LiteralPath $mmoServerLog) {
                $mmoServerOutput=Get-Content -LiteralPath $mmoServerLog -Raw
                $mmoReady=$mmoServerOutput -match 'GameNetDriver.*listening on port 7777' -and $mmoServerOutput -match 'MMOCore: zone added'
            }
            if(-not $mmoReady){Start-Sleep -Milliseconds 500}
        } while(-not $mmoReady -and [DateTime]::UtcNow -lt $mmoDeadline)
        if(-not $mmoReady){throw "Game server startup timed out. Inspect $mmoServerLog"}
    }
    for($mmoIndex=1;$mmoIndex -le $ClientCount;$mmoIndex++) {
        $mmoClientLog=Join-Path $mmoLogs "client-$mmoIndex-$mmoRun.log"
        $mmoArguments=@('/Engine/Maps/Entry','-windowed','-ResX=1920','-ResY=1080','-ForceRes','-Multiprocess','-LiveCoding=false',"-abslog=$mmoClientLog",'-ExecCmds=sg.ViewDistanceQuality 1,sg.AntiAliasingQuality 1,sg.ShadowQuality 1,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.PostProcessQuality 1,sg.TextureQuality 1,sg.EffectsQuality 1,sg.FoliageQuality 1,sg.ShadingQuality 1,t.MaxFPS 30')
        if(-not $mmoPackaged){$mmoArguments=@($Project)+$mmoArguments+@('-game')}
        if($RenderOffscreen){$mmoArguments+='-RenderOffscreen'}
        if($Showcase) {
            $mmoRole=if($mmoIndex -eq 1){'A'}else{'B'}
            New-Item -ItemType Directory -Path $mmoProfiles -Force | Out-Null
            $mmoProfile=Join-Path $mmoProfiles "$mmoRole.ini"
            $mmoCulture=if($mmoRole -eq 'A'){'zh-Hans'}else{'en'}
            $mmoInitialCulture=if($mmoRole -eq 'A'){'en'}else{'zh-Hans'}
            [IO.File]::WriteAllText($mmoProfile,"[Internationalization]`nCulture=$mmoInitialCulture`n",[Text.UTF8Encoding]::new($false))
            $mmoArguments+=@('-MMOShowcase','-MultiprocessSaveConfig',"-MMOSmokeUser=$($mmoFixtures[$mmoIndex-1])","-MMOSmokeRole=$mmoRole","-MMOCaptureDir=$mmoCaptures","-GameUserSettingsINI=$mmoProfile")
            if($ShowcaseAutoQuit){$mmoArguments+='-MMOShowcaseAutoQuit'}
        } else {
            # Normal interactive runs keep one persistent profile per local client.
            $mmoInteractiveProfiles=Join-Path $mmoLab 'Saved/Config/InteractiveProfiles'
            New-Item -ItemType Directory -Path $mmoInteractiveProfiles -Force | Out-Null
            $mmoProfile=Join-Path $mmoInteractiveProfiles "client-$mmoIndex.ini"
            if(-not (Test-Path -LiteralPath $mmoProfile)){Copy-Item -LiteralPath (Join-Path $mmoLab 'Config/DefaultGameUserSettings.ini') -Destination $mmoProfile}
            $mmoArguments+=@('-MultiprocessSaveConfig',"-GameUserSettingsINI=$mmoProfile")
        }
        $mmoClient=Start-MMOInteractiveProcess $mmoArguments $false
        $mmoChildren.Add(@{role="client-$mmoIndex";process=$mmoClient;log=$mmoClientLog;executable=$mmoExe;settingsIni=$mmoProfile;expectedCulture=$(if($Showcase){$mmoCulture}else{''});initialCulture=$(if($Showcase){$mmoInitialCulture}else{''})})
    }
    $mmoManifest=Join-Path $mmoLogs "processes-$mmoRun.json"
    @{project=$Project;packaged=$mmoPackaged;executable=$mmoExe;serverExecutable=$mmoServerExe;fixtures=$mmoFixtures;captures=$mmoCaptures;processes=@($mmoChildren|ForEach-Object{@{role=$_.role;pid=$_.process.Id;started=$_.process.StartTime.ToUniversalTime().ToString('o');log=$_.log;executable=$_.executable;settingsIni=$_.settingsIni;expectedCulture=$_.expectedCulture;initialCulture=$_.initialCulture}})}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $mmoManifest
    Write-Host "Started $ClientCount client(s), 1920x1080, Medium quality. Process manifest: $mmoManifest"
    Write-Host 'Create separate accounts and characters in each client. Exit clients before stopping the game server, and verify final save/release in its local log.'
    if($Showcase){Write-Host "Showcase uses dedicated fixtures and captures real viewports with UI to $mmoCaptures. AutoQuit=$ShowcaseAutoQuit; this does not test physical keyboard/gamepad navigation."}
    Write-Output $mmoManifest
} catch {
    # These are only processes created by this invocation; no pre-existing server is stopped.
    foreach($mmoChild in $mmoChildren) {
        if(-not $mmoChild.process.HasExited){$null=$mmoChild.process.CloseMainWindow();if(-not $mmoChild.process.WaitForExit(5000)){$mmoChild.process.Kill()}}
    }
    if($mmoFixtures.Count) {
        Import-MMOEnvironment
        foreach($mmoFixture in $mmoFixtures){if($mmoFixture -match '^showcase_[ab]_[a-f0-9]{10}$'){$null=Invoke-MMOPsql "DELETE FROM accounts WHERE username='$mmoFixture';"}}
    }
    throw
} finally {
    $env:MMO_SERVER_SECRET=$mmoPreviousSecret
    $mmoShowcasePassword=$null
}
