param(
    [ValidateSet('Cook','Package')][string]$Mode='Cook',
    [string]$ProjectRoot='F:/UE/LyraDocLabs/LyraTraining',
    [string]$EngineRoot='E:/UnrealEngine',
    [string]$ArtifactRoot='F:/UE/LyraDocLabs/Artifacts/LyraTraining',
    [switch]$ShowCommand
)
$ErrorActionPreference='Stop'
$docsRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectPath=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('/','\')
$enginePath=[IO.Path]::GetFullPath($EngineRoot).TrimEnd('/','\')
$artifactPath=[IO.Path]::GetFullPath($ArtifactRoot).TrimEnd('/','\')
if(-not $projectPath.Replace('\','/').EndsWith('/LyraDocLabs/LyraTraining',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use the isolated LyraDocLabs/LyraTraining project; the official baseline is not a package-test workspace.'
}
$projectFile=Join-Path $projectPath 'LyraStarterGame.uproject'
$uat=Join-Path $enginePath 'Engine/Build/BatchFiles/RunUAT.bat'
$editor=Join-Path $enginePath 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$map='/TrainingRange/Maps/L_TrainingRange'
$renderingPatch=Join-Path $docsRoot 'examples/LyraTraining/Config/TutorialWindowsSM6.ini'
foreach($required in @(
    $projectFile,$uat,$editor,$renderingPatch,
    (Join-Path $projectPath 'Source/LyraGame.Target.cs'),
    (Join-Path $projectPath 'Plugins/GameFeatures/TrainingRange/Content/Maps/L_TrainingRange.umap'),
    (Join-Path $projectPath 'Plugins/GameFeatures/TrainingRange/Content/Experiences/B_TrainingRange.uasset'),
    (Join-Path $projectPath 'Plugins/GameFeatures/TrainingRange/Content/TrainingRange.uasset')
)) { if(-not (Test-Path -LiteralPath $required)) { throw "Prepare and save the training assets/editor first. Missing: $required" } }
$descriptor=Get-Content -LiteralPath (Join-Path $projectPath 'Plugins/GameFeatures/TrainingRange/TrainingRange.uplugin') -Raw | ConvertFrom-Json
foreach($dependency in @('ShooterCore','LyraExampleContent')) {
    if(-not ($descriptor.Plugins | Where-Object { $_.Name -eq $dependency -and $_.Enabled })) {
        throw "TrainingRange must explicitly depend on $dependency before Cook."
    }
}
$projectDescriptor=Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json
if(-not ($projectDescriptor.Plugins | Where-Object { $_.Name -eq 'TrainingRange' -and $_.Enabled })) {
    throw 'Enable TrainingRange in the isolated .uproject before invoking UAT.'
}
$verificationSource='Plugins/GameFeatures/TrainingRange/Source/TrainingRangeVerification/Private/TrainingRangeVerification.cpp'
if($Mode -eq 'Package') {
    foreach($automationFile in @(
        'Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll',
        'Engine/Binaries/DotNET/AutomationTool/AutomationUtils/net10.0/AutomationUtils.Automation.dll'
    )) {
        if(-not(Test-Path -LiteralPath (Join-Path $enginePath $automationFile))) {
            throw "Build the source engine AutomationTool and script modules before packaging: missing $automationFile. With the shared-engine build window free, run RunUAT.bat -CompileOnly first; Package intentionally does not rebuild these tools."
        }
    }
    foreach($packagingTool in @(
        @{ RelativePath='Engine/Binaries/Win64/UnrealPak.exe'; Target='UnrealPak'; Configuration='Development' },
        @{ RelativePath='Engine/Binaries/Win64/BootstrapPackagedGame-Win64-Shipping.exe'; Target='BootstrapPackagedGame'; Configuration='Shipping' }
    )) {
        if(-not(Test-Path -LiteralPath (Join-Path $enginePath $packagingTool.RelativePath) -PathType Leaf)) {
            $buildBatch=Join-Path $enginePath 'Engine/Build/BatchFiles/Build.bat'
            throw "Missing packaging tool $($packagingTool.RelativePath). With the shared-engine build window free, run: & '$buildBatch' $($packagingTool.Target) Win64 $($packagingTool.Configuration) -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=3. Follow content/docs/07-training/multiplayer-and-package.mdx before Package; UAT's -ubtargs does not limit these tool targets."
        }
    }
    if(-not ($descriptor.Modules | Where-Object { $_.Name -eq 'TrainingRangeVerification' -and $_.TargetConfigurationAllowList -contains 'Development' })) {
        throw 'Copy the current TrainingRange descriptor and original Source into the lab before packaging its Development runtime verifier.'
    }
    foreach($relative in @($verificationSource,'Plugins/GameFeatures/TrainingRange/Source/TrainingRangeVerification/TrainingRangeVerification.Build.cs')) {
        $labSource=Join-Path $projectPath $relative
        $repoSource=Join-Path $docsRoot ('examples/LyraTraining/'+$relative)
        if(-not(Test-Path -LiteralPath $labSource) -or (Get-FileHash -LiteralPath $labSource -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $repoSource -Algorithm SHA256).Hash) {
            throw "The lab's runtime verifier source is missing or stale: $relative. Copy the current original teaching delta before Package."
        }
    }
}
$runId=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6)
$runRoot=Join-Path $artifactPath $runId
$stageRoot=Join-Path $runRoot 'Stage'
$archiveRoot=Join-Path $runRoot 'Archive'
$cookRoot=Join-Path $runRoot 'Cooked'
$log=Join-Path $runRoot ('training-'+$Mode.ToLowerInvariant()+'.log')
$shaderIni='-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=1,[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame=1,[DevOptions.Shaders]:bForceUseSCWMemoryPressureLimits=False,[SystemSettings]:r.ForceAllCoresForShaderCompiling=0'
$mcpIni='-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False'
$limitedCookerOptions="-CookProcessCount=1 -corelimit=3 -Multiprocess $shaderIni $mcpIni"
$uatArgs=@(
    'BuildCookRun',"-project=$projectFile",'-target=LyraGame',
    '-platform=Win64','-clientconfig=Development','-cook',"-MapsToCook=$map",
    '-skipbuildeditor','-unattended','-utf8output','-nop4'
)
if($Mode -eq 'Package') {
    $uatArgs+=@('-build','-stage','-pak','-iostore','-package','-archive',
        '-ubtargs=-NoUBA -MaxParallelActions=3',"-AdditionalCookerOptions=$limitedCookerOptions",
        "-stagingdirectory=$stageRoot","-archivedirectory=$archiveRoot")
    $runner=$uat
    # Reuse the already-built source engine AutomationTool and script assemblies.
    # These native switches do not skip the requested LyraGame build, Cook, or Stage.
    $runnerArgs=@('-NoCompileUAT','-NoCompile','-NoAutoSDK')+$uatArgs
} else {
    # Native commandlet: no AutomationTool compilation and no UBT invocation.
    # CoreLimit=3 gives exactly two SCWs in CalculateNumberOfCompilingThreads' <=4-core branch.
    # The INI overrides also prevent the percentage/memory policies from raising this limit.
    $runner=$editor
    $runnerArgs=@($projectFile,'-run=Cook','-TargetPlatform=Windows',"-Map=$map",
        "-OutputDir=$cookRoot",'-SkipZenStore','-CookProcessCount=1','-corelimit=3','-Multiprocess',
        $shaderIni,$mcpIni,
        '-unattended','-nop4','-NullRHI','-UTF8Output','-stdout','-FullStdOutLogOutput',"-abslog=$log")
}
if($ShowCommand) {
    Write-Host ('& '''+$runner.Replace("'","''")+'''')
    foreach($argument in $runnerArgs) { Write-Host ('  '''+$argument.Replace("'","''")+'''') }
    Write-Host 'When executed, the tutorial D3D12/SM6 delta is appended to the isolated project WindowsEngine.ini if not already present.'
    Write-Host 'Display only: no UAT, native build, Cook, output directory, or validation report was created.'
    return
}

# Respect the shared engine build boundary without stopping anyone's process.
$busy=@(Get-CimInstance Win32_Process | Where-Object {
    ($_.Name -in @('UnrealEditor.exe','UnrealEditor-Cmd.exe','LiveCodingConsole.exe') -and
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($enginePath+'\',[StringComparison]::OrdinalIgnoreCase) -and
        ($Mode -eq 'Package' -or $_.Name -eq 'LiveCodingConsole.exe' -or
            ($_.CommandLine -and $_.CommandLine.Replace('/','\').IndexOf($projectPath,[StringComparison]::OrdinalIgnoreCase) -ge 0))) -or
    ($Mode -eq 'Package' -and $_.Name -in @('dotnet.exe','UnrealBuildTool.exe') -and $_.CommandLine -and
        $_.CommandLine.Contains('UnrealBuildTool') -and
        $_.CommandLine.Replace('/','\').IndexOf($enginePath,[StringComparison]::OrdinalIgnoreCase) -ge 0)
})
if($busy.Count) {
    $labels=($busy | ForEach-Object { "$($_.Name) PID=$($_.ProcessId)" }) -join ', '
    throw "Save work, close the relevant editors normally, and let the existing shared-engine build finish first: $labels"
}

# Append the original teaching delta to the clone's platform layer; never replace baseline settings.
$windowsConfig=Join-Path $projectPath 'Config/Windows/WindowsEngine.ini'
$profile=Get-Content -LiteralPath $renderingPatch -Raw
if(-not (Test-Path -LiteralPath $windowsConfig)) { throw 'The cloned WindowsEngine.ini is missing.' }
$windowsText=Get-Content -LiteralPath $windowsConfig -Raw
if(-not $windowsText.Contains('; BEGIN LYRADOC WINDOWS SM6')) {
    [IO.File]::AppendAllText($windowsConfig,[Environment]::NewLine+$profile,[Text.UTF8Encoding]::new($false))
}

if(Test-Path -LiteralPath $runRoot) { throw 'The unique artifact run directory already exists; retry to create a fresh output path.' }
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$verification=Join-Path $docsRoot 'verification'
New-Item -ItemType Directory -Path $verification -Force | Out-Null
$reportFile=Join-Path $verification ('training-'+$Mode.ToLowerInvariant()+'.json')
$report=[ordered]@{
    startedAt=(Get-Date).ToUniversalTime().ToString('o');status='running';passed=$false
    mode=$Mode;project=$projectFile;engine=$enginePath;target='LyraGame';configuration='Development';platform='Win64'
    map=$map;nativeBuildRequested=($Mode -eq 'Package');runner=$runner;arguments=$runnerArgs;log=$log
    sourceMapSha256=(Get-FileHash -LiteralPath (Join-Path $projectPath 'Plugins/GameFeatures/TrainingRange/Content/Maps/L_TrainingRange.umap') -Algorithm SHA256).Hash.ToLowerInvariant()
    trainingPluginSha256=(Get-FileHash -LiteralPath (Join-Path $projectPath 'Plugins/GameFeatures/TrainingRange/TrainingRange.uplugin') -Algorithm SHA256).Hash.ToLowerInvariant()
    runtimeVerifierSourceSha256=if($Mode -eq 'Package'){(Get-FileHash -LiteralPath (Join-Path $projectPath $verificationSource) -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null}
    renderingProfile='Windows D3D12 / PCD3D_SM6 only';renderingConfig=$windowsConfig;renderingPatchSha256=(Get-FileHash -LiteralPath $renderingPatch -Algorithm SHA256).Hash.ToLowerInvariant()
    cookDirectory=if($Mode -eq 'Cook'){$cookRoot}else{$null};shaderWorkerLimit=2
    ubtParallelActionLimit=if($Mode -eq 'Package'){3}else{$null};ubaEnabled=$false
    runDirectory=$runRoot;archiveDirectory=if($Mode -eq 'Package'){$archiveRoot}else{$null}
    localServiceProxyBypasses=@();systemProxyConfigurationChanged=$false
    exitCode=$null;runtimeVerification='pending';artifacts=@();error=$null
}
$trainingOriginalNoProxy=[Environment]::GetEnvironmentVariable('NO_PROXY','Process')
$trainingProxyBypassApplied=$false
try {
    $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $reportFile -Encoding utf8
    if($Mode -eq 'Cook') {
        # The engine owns -abslog. Do not also write this same file through Tee-Object.
        & $runner @runnerArgs 2>&1 | ForEach-Object { Write-Host $_ }
    } else {
        # Native ZenUtils uses .NET HttpClient's environment proxy. Its IPv6 URI host
        # requires the bracketed [::1] bypass as well as the unbracketed form.
        # Preserve the existing domains and restore the exact original process value below.
        $trainingLocalBypasses=@('localhost','127.0.0.1','::1','[::1]')
        $trainingNoProxy=(@(($trainingOriginalNoProxy -split ',' | Where-Object {$_})+$trainingLocalBypasses) | Select-Object -Unique) -join ','
        [Environment]::SetEnvironmentVariable('NO_PROXY',$trainingNoProxy,'Process')
        $trainingProxyBypassApplied=$true
        $report.localServiceProxyBypasses=$trainingLocalBypasses
        & $runner @runnerArgs 2>&1 | Tee-Object -FilePath $log | ForEach-Object { Write-Host $_ }
    }
    $uatExit=$LASTEXITCODE
    $report.exitCode=$uatExit
    if($uatExit -ne 0) { throw "$Mode failed with exit code $uatExit. Read $log" }
    if($Mode -eq 'Cook') {
        $cookedMap=Get-ChildItem -LiteralPath $cookRoot -Recurse -File -Filter 'L_TrainingRange.umap' | Select-Object -First 1
        if(-not $cookedMap) { throw 'Cook returned success without writing the requested training map.' }
        $report.artifacts=@([ordered]@{path=$cookedMap.FullName;bytes=$cookedMap.Length})
        $report.shaderWorkerLog=@(Select-String -LiteralPath $log -Pattern 'Using .*local workers for shader compilation' | ForEach-Object { $_.Line })
        if(-not ($report.shaderWorkerLog -match 'Using [12] local workers')) { throw 'The Cook log did not confirm the requested maximum of two shader workers.' }
    }
    if($Mode -eq 'Package') {
        $report.shaderWorkerLog=@(Select-String -LiteralPath $log -Pattern 'Using .*local workers for shader compilation' | ForEach-Object { $_.Line })
        foreach($workerLine in $report.shaderWorkerLog) {
            if($workerLine -match 'Using\s+(\d+)\s+local workers' -and [int]$Matches[1] -gt 2) {
                throw 'The native Cook log exceeded the maximum of two shader workers.'
            }
        }
        $report.artifacts=@(Get-ChildItem -LiteralPath $archiveRoot -Recurse -File | Where-Object {
            $_.Extension -in @('.exe','.pak','.utoc','.ucas')
        } | ForEach-Object { [ordered]@{path=$_.FullName;bytes=$_.Length} })
        if(-not $report.artifacts.Count) { throw 'UAT returned success, but the requested archive contains no package artifacts.' }
    }
    $report.passed=$true
    $report.status='passed'
} catch {
    $report.status='failed';$report.error=$_.Exception.Message
    throw
} finally {
    if($trainingProxyBypassApplied) {
        [Environment]::SetEnvironmentVariable('NO_PROXY',$trainingOriginalNoProxy,'Process')
    }
    $report.finishedAt=(Get-Date).ToUniversalTime().ToString('o')
    $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $reportFile -Encoding utf8
    $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runRoot 'report.json') -Encoding utf8
}
Write-Host "$Mode completed. Report: $reportFile"
Write-Host 'Cook/packaging success does not verify gameplay. Launch the archived executable with /TrainingRange/Maps/L_TrainingRange and retain a separate runtime report.'
