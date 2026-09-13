param(
    [string]$Project='F:/UE/LyraDocLabs/MMORPG/MMORPG.uproject',
    [string]$Engine='E:/UnrealEngine',
    [switch]$SkipCook
)
$ErrorActionPreference='Stop'
$mmoRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$mmoProject=(Resolve-Path -LiteralPath $Project).Path
$mmoLab=Split-Path $mmoProject -Parent
if([IO.Path]::GetFileName($mmoProject) -ne 'MMORPG.uproject'){throw 'This packager is for the MMORPG lab'}
$mmoEvidence=Join-Path $mmoRepo 'verification/mmorpg-package.json'
$mmoOriginalNoProxy=[Environment]::GetEnvironmentVariable('NO_PROXY','Process')
$mmoReport=[ordered]@{startedAt=[DateTime]::UtcNow.ToString('o');passed=$false;project=$mmoProject;engine=$Engine;skipCook=[bool]$SkipCook;precompiledAutomationTools=$true;gameplayExecuted=$false}
try {
$mmoEngineRoot=[IO.Path]::GetFullPath($Engine).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
$mmoBusy=@(Get-CimInstance Win32_Process|Where-Object{
    $_.Name -in @('UnrealEditor.exe','UnrealEditor-Cmd.exe','LiveCodingConsole.exe') -and $_.ExecutablePath -and
    [IO.Path]::GetFullPath($_.ExecutablePath).StartsWith($mmoEngineRoot,[StringComparison]::OrdinalIgnoreCase)
})
if($mmoBusy.Count){throw 'Save and normally close editors and Live Coding sessions using this engine before packaging.'}
foreach($mmoAutomationFile in @('Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll','Engine/Binaries/DotNET/AutomationTool/AutomationUtils/net10.0/AutomationUtils.Automation.dll')) {
    if(-not(Test-Path -LiteralPath (Join-Path $Engine $mmoAutomationFile))){throw "Build the source engine AutomationTool scripts before packaging: missing $mmoAutomationFile"}
}
if(-not(Test-Path -LiteralPath (Join-Path $Engine 'Engine/Binaries/Win64/UnrealPak.exe'))){throw 'Build UnrealPak Win64 Development with this source engine before running Cook/Stage.'}
if(-not(Test-Path -LiteralPath (Join-Path $Engine 'Engine/Binaries/Win64/BootstrapPackagedGame-Win64-Shipping.exe'))){throw 'Build BootstrapPackagedGame Win64 Shipping so Stage can create the Windows launch executable.'}
foreach($mmoTarget in @('MMORPGClient','MMORPGServer')) {
    if(-not(Test-Path -LiteralPath (Join-Path $mmoLab "Binaries/Win64/$mmoTarget.target"))){throw "Build $mmoTarget Win64 Development first"}
}
if(-not(Test-Path -LiteralPath (Join-Path $mmoLab 'Content/UI/WBP_ManaStatus.uasset'))){throw 'Run the latest prepare_content.py before cooking'}
foreach($mmoCulture in @('en','zh-Hans')) {
    if(-not(Test-Path -LiteralPath (Join-Path $mmoLab "Content/Localization/MMO/$mmoCulture/MMO.locres"))){throw "Compile the $mmoCulture localization resource before packaging"}
}
$mmoLog=Join-Path $mmoRepo 'verification/mmorpg-package.log'
$mmoCookerOptions='-NullRHI -Multiprocess -CookProcessCount=1 -corelimit=3 -ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=1,[DevOptions.Shaders]:NumUnusedShaderCompilingThreadsDuringGame=1,[DevOptions.Shaders]:bForceUseSCWMemoryPressureLimits=False,[SystemSettings]:r.ForceAllCoresForShaderCompiling=0 -ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False'
# This verified source engine already contains its AutomationTool assemblies and local Windows SDK.
# These three native switches avoid rebuilding UAT/script modules or starting AutoSDK installers.
# They do not skip asset Cook, shader compilation, staging, Pak or IoStore generation.
$mmoArguments=@('-NoCompileUAT','-NoCompile','-NoAutoSDK','BuildCookRun',"-project=$mmoProject",'-nop4','-unattended','-utf8output','-nocompileeditor','-client','-server','-platform=Win64','-serverplatform=Win64','-clientconfig=Development','-serverconfig=Development','-stage','-pak','-iostore','-ubtargs=-NoUBA -MaxParallelActions=3',"-additionalcookeroptions=$mmoCookerOptions")
# Actual game shaders must be compiled during the first cook. NullRHI only avoids rendering the commandlet.
$mmoArguments+=if($SkipCook){'-skipcook'}else{'-cook'}
$mmoReport.arguments=$mmoArguments
$mmoReport.log=$mmoLog
# .NET's environment proxy matches IPv6 URI hosts with brackets. Preserve existing bypasses
# and keep the local Zen connection direct in this process and its UAT child only.
$mmoLocalBypasses=@('localhost','127.0.0.1','::1','[::1]')
$mmoNoProxy=(@(($mmoOriginalNoProxy -split ',' | Where-Object {$_})+$mmoLocalBypasses) | Select-Object -Unique) -join ','
[Environment]::SetEnvironmentVariable('NO_PROXY',$mmoNoProxy,'Process')
$mmoReport.localServiceProxyBypasses=$mmoLocalBypasses
$mmoReport.systemProxyConfigurationChanged=$false
& (Join-Path $Engine 'Engine/Build/BatchFiles/RunUAT.bat') @mmoArguments 2>&1 | Tee-Object -FilePath $mmoLog
$mmoExit=$LASTEXITCODE
$mmoReport.uatExitCode=$mmoExit
if($mmoExit -ne 0){throw "BuildCookRun failed ($mmoExit); see $mmoLog"}
$mmoClient=Join-Path $mmoLab 'Saved/StagedBuilds/WindowsClient/MMORPG/Binaries/Win64/MMORPGClient.exe'
$mmoServer=Join-Path $mmoLab 'Saved/StagedBuilds/WindowsServer/MMORPG/Binaries/Win64/MMORPGServer.exe'
foreach($mmoBinary in @($mmoClient,$mmoServer)){if(-not(Test-Path -LiteralPath $mmoBinary)){throw "Expected staged binary missing: $mmoBinary"}}
& (Join-Path $PSScriptRoot '../backend/Test-ClientBoundaries.ps1') -ClientReceipt (Join-Path $mmoLab 'Binaries/Win64/MMORPGClient.target') -ClientBinary $mmoClient
$mmoClientRoot=Join-Path $mmoLab 'Saved/StagedBuilds/WindowsClient'
$mmoPrivate=@(Get-ChildItem -LiteralPath $mmoClientRoot -Recurse -File | Where-Object { $_.Name -match '(?i)(libpq|libsodium|backend\.secrets|MMOBackendService|MMOPersistence)' })
if($mmoPrivate.Count){throw 'Private backend files were included in the staged Client directory'}
$mmoReport.client=$mmoClient
$mmoReport.server=$mmoServer
$mmoReport.clientSHA256=(Get-FileHash -LiteralPath $mmoClient -Algorithm SHA256).Hash.ToLowerInvariant()
$mmoReport.serverSHA256=(Get-FileHash -LiteralPath $mmoServer -Algorithm SHA256).Hash.ToLowerInvariant()
$mmoReport.privateClientFiles=0
$mmoReport.passed=$true
Write-Host 'Cook and Stage completed. Run Invoke-Smoke.ps1 with the two executable paths to verify actual packaged gameplay.'
Write-Host "ClientExecutable: $mmoClient"
Write-Host "ServerExecutable: $mmoServer"
} catch {$mmoReport.error=$_.Exception.Message;throw}
finally {
    [Environment]::SetEnvironmentVariable('NO_PROXY',$mmoOriginalNoProxy,'Process')
    $mmoReport.completedAt=[DateTime]::UtcNow.ToString('o')
    $mmoReport|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $mmoEvidence -Encoding utf8
}
