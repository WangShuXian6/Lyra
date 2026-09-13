param([string]$Project='F:/UE/LyraDocLabs/MMORPG/MMORPG.uproject',[string]$Engine='E:/UnrealEngine',[string]$ClientExecutable='',[string]$ServerExecutable='',[switch]$Visible)
$ErrorActionPreference='Stop'
$showRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$showPackaged=[bool]$ClientExecutable
if([bool]$ClientExecutable -ne [bool]$ServerExecutable){throw 'Packaged rendering requires both ClientExecutable and ServerExecutable.'}
$showEvidence=Join-Path $showRepo $(if($showPackaged){'verification/mmorpg-packaged-rendered-showcase.json'}else{'verification/mmorpg-rendered-showcase.json'})
$showManifest=$null
$showProbe=$null
$showReport=[ordered]@{date=[DateTime]::UtcNow.ToString('o');passed=$false;packaged=$showPackaged;captureAPI='FSlateApplication::TakeScreenshot(SViewport), game timer';physicalInputTested=$false;programmaticCultureSwitch=$true;clients=@();captures=@();selectedInputs=@()}
try {
    $showLab=Split-Path $Project -Parent
    if($showPackaged) {
        $ClientExecutable=(Resolve-Path -LiteralPath $ClientExecutable).Path
        $ServerExecutable=(Resolve-Path -LiteralPath $ServerExecutable).Path
        $showPresentation=(& (Join-Path $PSScriptRoot 'Test-StagedPresentation.ps1') -ClientExecutable $ClientExecutable -Engine $Engine|Select-Object -Last 1)
        $showReport.stagedPresentation=Get-Content -LiteralPath $showPresentation -Raw|ConvertFrom-Json
        foreach($showBinary in @($ClientExecutable,$ServerExecutable)){$showReport.selectedInputs+=@{path=$showBinary;sha256=(Get-FileHash -LiteralPath $showBinary).Hash.ToLowerInvariant()}}
        $showReport.selectedInputs+=@($showReport.stagedPresentation.containers)
    } else {
        foreach($showInput in @('Binaries/Win64/UnrealEditor-MMORPG.dll','Config/DefaultGame.ini','Config/Windows/WindowsEngine.ini','Content/UI/BP_MMOUIPolicy.uasset','Content/UI/WBP_MMOLayout.uasset','Content/UI/WBP_Login.uasset','Content/UI/WBP_PlayerHUD.uasset','Content/UI/WBP_ManaStatus.uasset','Content/UI/WBP_Inventory.uasset','Content/UI/WBP_Settings.uasset','Content/UI/WBP_Confirm.uasset','Content/Characters/MMO/ALI_MMOCharacter.uasset','Content/Characters/MMO/ABP_MMOCharacter.uasset','Content/Characters/MMO/ABP_MMOUnarmed.uasset')) {
            $showInputPath=Join-Path $showLab $showInput
            $showReport.selectedInputs+=@{relativePath=$showInput;sha256=(Get-FileHash -LiteralPath $showInputPath -Algorithm SHA256).Hash.ToLowerInvariant()}
        }
    }
    $showManifest=(& (Join-Path $PSScriptRoot 'Start-Lab.ps1') -Project $Project -Engine $Engine -ClientExecutable $ClientExecutable -ServerExecutable $ServerExecutable -Showcase -ShowcaseAutoQuit -RenderOffscreen:(-not $Visible) | Select-Object -Last 1)
    $showRun=Get-Content -LiteralPath $showManifest -Raw | ConvertFrom-Json
    $showDeadline=[DateTime]::UtcNow.AddMinutes(4)
    do {
        $showLive=@($showRun.processes | Where-Object role -like 'client-*' | Where-Object {Get-Process -Id $_.pid -ErrorAction SilentlyContinue})
        if($showLive.Count){Start-Sleep -Milliseconds 500}
    } while($showLive.Count -and [DateTime]::UtcNow -lt $showDeadline)
    if($showLive.Count){throw 'Rendered showcase did not complete within four minutes'}
    foreach($showEntry in @($showRun.processes | Where-Object role -like 'client-*')) {
        $showText=Get-Content -LiteralPath $showEntry.log -Raw
        if($showText -notmatch 'MMO_SHOWCASE_READY'){throw "Showcase did not finish: $($showEntry.role)"}
        $showHealthSpawn=$showText -match 'MMO_SHOWCASE_HEALTH_GRAPH spawned=1 class=/Game/Tutorial/BP_BackendHealth\.BP_BackendHealth_C'
        $showHealthResponse=[regex]::Match($showText,'LogBlueprintUserMessages: \[BP_BackendHealth_C_\d+\] (\{\s*"ok"\s*:\s*true\s*\})')
        if(-not $showHealthSpawn -or -not $showHealthResponse.Success){throw 'The actual health Blueprint did not spawn and complete GET /health successfully'}
        $showTaggedInput=& (Join-Path $PSScriptRoot 'Read-TaggedAbilityInputs.ps1') -Log $showEntry.log -Stage 'showcase-complete'
        $showMovementBases=& (Join-Path $PSScriptRoot 'Test-MovementBaseLogs.ps1') -Logs @($showEntry.log)
        $showTaggedInput|Add-Member -NotePropertyName movementBases -NotePropertyValue $showMovementBases
        $showQualityMatch=[regex]::Match($showText,'MMO_SHOWCASE_QUALITY_RESULT (\{[^\r\n]+\})')
        if(-not $showQualityMatch.Success){throw 'The actual Medium quality button was not exercised'}
        $showQuality=$showQualityMatch.Groups[1].Value|ConvertFrom-Json
        if(-not $showQuality.buttonApplied -or $showQuality.beforeWidth -ne 1920 -or $showQuality.beforeHeight -ne 1080 -or $showQuality.afterWidth -ne 1920 -or $showQuality.afterHeight -ne 1080){throw 'The Medium quality action changed the viewport resolution'}
        $showLocomotion=@([regex]::Matches($showText,'MMO_LOCOMOTION_RESULT (\{[^\r\n]+\})')|ForEach-Object{$_.Groups[1].Value|ConvertFrom-Json})
        if($showEntry.role -eq 'client-1') {
            if($showLocomotion.Count -ne 2 -or @($showLocomotion|Where-Object {-not $_.passed}).Count){throw 'The evaluated locomotion BlendSpace did not receive moving Speed on its Y axis'}
            if([Math]::Abs($showLocomotion[1].assetTime-$showLocomotion[0].assetTime) -lt .001){throw 'The evaluated BlendSpace animation playback time did not advance'}
        }
        $showObservations=@([regex]::Matches($showText,'MMO_CULTURE_RESULT (\{[^\r\n]+\})') | ForEach-Object {$_.Groups[1].Value | ConvertFrom-Json})
        if($showObservations.Count -ne 3){throw 'Expected first-switch, second-switch and final-hud culture records'}
        if($showEntry.initialCulture -eq $showEntry.expectedCulture){throw 'Initial culture must differ from the final persisted culture'}
        $showExpectedStages=@('first-switch','second-switch','final-hud')
        $showExpectedCultures=@($showEntry.expectedCulture,$showEntry.initialCulture,$showEntry.expectedCulture)
        for($showIndex=0;$showIndex -lt 3;$showIndex++) {
            if($showObservations[$showIndex].stage -cne $showExpectedStages[$showIndex] -or $showObservations[$showIndex].culture -cne $showExpectedCultures[$showIndex]){throw "Culture stage $showIndex did not perform the expected language transition"}
        }
        $showExpectedQualityKeys=@('ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading')
        foreach($showObservation in $showObservations) {
            if(-not $showObservation.persisted -or $showObservation.isEditor -or -not $showObservation.umgMVVMBinding){throw 'Culture persistence or live Designer ViewModel binding failed'}
            $showQualityKeys=@($showObservation.qualityGroups.psobject.Properties.Name)
            if($showQualityKeys.Count -ne 10 -or @(Compare-Object $showExpectedQualityKeys $showQualityKeys -CaseSensitive).Count -or @($showObservation.qualityGroups.psobject.Properties | Where-Object Value -ne 1).Count){throw 'Expected the exact ten Medium scalability groups, each with value 1'}
            if($showObservation.stage -ne 'final-hud') {
                $showExpectedTitle=if($showObservation.culture -eq 'zh-Hans'){'设置'}else{'Settings'}
                if($showObservation.menuTitle -ne $showExpectedTitle){throw 'Designer Settings title did not use the selected language'}
            }
            $showExpectedWord=if($showObservation.culture -eq 'zh-Hans'){'等级'}else{'Level'}
            if(-not $showObservation.statusText.Contains($showExpectedWord)){throw 'Runtime ViewModel status did not refresh its language'}
        }
        if($showObservations[-1].culture -ne $showEntry.expectedCulture){throw 'Client did not finish with its selected culture'}
        $showDiskMatch=[regex]::Match((Get-Content -LiteralPath $showEntry.settingsIni -Raw),'(?m)^Culture=([^\r\n]+)')
        if(-not $showDiskMatch.Success -or $showDiskMatch.Groups[1].Value -ne $showEntry.expectedCulture){throw 'Selected culture was not flushed to the isolated INI on disk'}
        # New independent -game process: no -culture override, no login fixture or server secret.
        $showProbeLog=[IO.Path]::ChangeExtension($showEntry.log,'.culture-probe.log')
        $showInfo=[Diagnostics.ProcessStartInfo]::new(); $showInfo.FileName=$showRun.executable
        $showInfo.WorkingDirectory=if($showPackaged){Split-Path $showRun.executable -Parent}else{Split-Path $Project -Parent}; $showInfo.UseShellExecute=$false; $showInfo.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        foreach($showKey in @($showInfo.Environment.Keys)){if($showKey -like 'PG*' -or $showKey -in @('MMO_SERVER_SECRET','MMO_TEST_PASSWORD')){[void]$showInfo.Environment.Remove($showKey)}}
        $showProbeArguments=@('/Engine/Maps/Entry','-NullRHI','-nosound','-unattended','-Multiprocess','-LiveCoding=false','-MMOLocalizationProbe',"-GameUserSettingsINI=$($showEntry.settingsIni)","-MMOExpectedCulture=$($showEntry.expectedCulture)","-abslog=$showProbeLog")
        if(-not $showPackaged){$showProbeArguments=@($Project)+$showProbeArguments+@('-game')}
        foreach($showArg in $showProbeArguments){$showInfo.ArgumentList.Add($showArg)}
        $showProbe=[Diagnostics.Process]::Start($showInfo)
        if(-not $showProbe.WaitForExit(60000)){throw 'Fresh-process culture probe did not exit'}
        if($showProbe.ExitCode -ne 0){throw 'Fresh-process culture probe failed'}
        $showMatch=[regex]::Match((Get-Content -LiteralPath $showProbeLog -Raw),'MMO_CULTURE_RESULT (\{[^\r\n]+\})')
        if(-not $showMatch.Success){throw 'Fresh process did not emit a culture observation'}
        $showFresh=$showMatch.Groups[1].Value|ConvertFrom-Json
        $showLoginTitle=if($showEntry.expectedCulture -eq 'zh-Hans'){'开启旅程'}else{'Begin your journey'}
        if(-not $showFresh.matchesExpectedCulture -or -not $showFresh.persisted -or $showFresh.menuTitle -ne $showLoginTitle){throw 'New process did not restore the saved culture and localized login UI'}
        if($showFresh.culture -eq $showEntry.initialCulture){throw 'Fresh process merely loaded the originally seeded culture'}
        $showReport.clients+=@{inputGrants=$showTaggedInput;role=$showEntry.role;initialCulture=$showEntry.initialCulture;expectedCulture=$showEntry.expectedCulture;settingsIni=$showEntry.settingsIni;diskCulture=$showDiskMatch.Groups[1].Value;observations=$showObservations;freshProcess=$showFresh;mediumQualityAction=$showQuality;locomotion=$showLocomotion;healthBlueprint=@{spawned=$showHealthSpawn;httpResponse=($showHealthResponse.Groups[1].Value|ConvertFrom-Json);method='Native Blueprint Print String after Request MMO Completed'}}
    }
    $showServerEntry=@($showRun.processes|Where-Object role -eq 'server')
    if($showServerEntry.Count -ne 1){throw 'Expected an independently started showcase server for input-tag dispatch evidence.'}
    $showReport.tagDrivenDispatch=& (Join-Path $PSScriptRoot 'Read-TaggedAbilityDispatch.ps1') -ServerLog $showServerEntry[0].log -RequiredSuccessfulTags @('InputTag.MMO.Spell')
    $showReport.movementBases=& (Join-Path $PSScriptRoot 'Test-MovementBaseLogs.ps1') -Logs @($showServerEntry[0].log)
    Add-Type -AssemblyName System.Drawing
    foreach($showPNG in @(Get-ChildItem -LiteralPath $showRun.captures -Filter '*.png')) {
        $showImage=[Drawing.Image]::FromFile($showPNG.FullName)
        try {if($showImage.Width -ne 1920 -or $showImage.Height -ne 1080){throw 'Native viewport capture was not 1920x1080'}; $showReport.captures+=@{path=$showPNG.FullName;width=$showImage.Width;height=$showImage.Height;sha256=(Get-FileHash -LiteralPath $showPNG.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
        finally {$showImage.Dispose()}
    }
    if($showReport.captures.Count -ne 22){throw 'Expected eleven native viewport captures per client'}
    $showReport.passed=$true
} catch {$showReport.error=$_.Exception.Message; throw}
finally {
    try {
        if($showProbe -and -not $showProbe.HasExited){$showProbe.Kill();$showProbe.WaitForExit()}
        if($showManifest){& (Join-Path $PSScriptRoot 'Stop-Lab.ps1') -Manifest $showManifest -Force}
        $showReport.cleanupCompleted=$true
    } catch {
        $showReport.passed=$false; $showReport.cleanupError=$_.Exception.Message; throw
    } finally {
        $showReport.completedAt=[DateTime]::UtcNow.ToString('o')
        $showReport|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $showEvidence -Encoding utf8
    }
}
Write-Host "Rendered Designer UI and independent-process localization persistence verified: $showEvidence"
