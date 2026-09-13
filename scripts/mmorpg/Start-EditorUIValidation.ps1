param(
    [string]$Project='F:/UE/LyraDocLabs/MMORPG/MMORPG.uproject',
    [string]$Engine='E:/UnrealEngine'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../backend/Common.ps1')
$uiProject=[IO.Path]::GetFullPath($Project)
$uiRoot=Split-Path $uiProject -Parent
if(-not $uiRoot.Replace('\','/').EndsWith('/LyraDocLabs/MMORPG',[StringComparison]::OrdinalIgnoreCase)){throw 'Use the isolated MMORPG lab.'}
$uiExe=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor.exe'
foreach($uiRequired in @($uiProject,$uiExe,(Join-Path $uiRoot 'Binaries/Win64/UnrealEditor-MMORPG.dll'))) {
    if(-not (Test-Path -LiteralPath $uiRequired)){throw "Prepare and build MMORPGEditor first: $uiRequired"}
}
if(Get-NetTCPConnection -LocalPort 8000 -State Listen -ErrorAction SilentlyContinue){throw 'Port 8000 already belongs to an editor. Save and close it normally before changing projects.'}
if(Get-NetUDPEndpoint -LocalPort 7777 -ErrorAction SilentlyContinue){throw 'Port 7777 is already occupied; this isolated UI run requires its own game server.'}
$null=Invoke-RestMethod 'http://127.0.0.1:8088/health'
$uiRun=[Guid]::NewGuid().ToString('N').Substring(0,10)
$uiUser="showcase_a_$uiRun"
$uiPassword=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$uiLogs=Join-Path $uiRoot "Saved/Logs/UIValidation/$uiRun"
New-Item -ItemType Directory -Path $uiLogs -Force | Out-Null
$uiChildren=[Collections.Generic.List[object]]::new()
$uiCreated=$false
$uiOldSecret=$env:MMO_SERVER_SECRET

function Start-UIProcess([string[]]$Arguments,[bool]$Server) {
    $uiInfo=[Diagnostics.ProcessStartInfo]::new()
    $uiInfo.FileName=$uiExe; $uiInfo.WorkingDirectory=$uiRoot; $uiInfo.UseShellExecute=$false
    $uiInfo.WindowStyle=if($Server){[Diagnostics.ProcessWindowStyle]::Hidden}else{[Diagnostics.ProcessWindowStyle]::Normal}
    foreach($uiArg in $Arguments){$uiInfo.ArgumentList.Add($uiArg)}
    foreach($uiKey in @($uiInfo.Environment.Keys)) {
        if($uiKey -like 'PG*' -or $uiKey -in @('MMO_SERVER_SECRET','MMO_TEST_PASSWORD')){[void]$uiInfo.Environment.Remove($uiKey)}
    }
    if($Server){$uiInfo.Environment['MMO_SERVER_SECRET']=$env:MMO_SERVER_SECRET}
    else {$uiInfo.Environment['MMO_TEST_PASSWORD']=$uiPassword; $uiInfo.Environment['MMO_TEST_USERNAME']=$uiUser}
    [Diagnostics.Process]::Start($uiInfo)
}

try {
    $uiBody=@{username=$uiUser;password=$uiPassword}|ConvertTo-Json -Compress
    $null=Invoke-RestMethod 'http://127.0.0.1:8088/v1/auth/register' -Method Post -ContentType 'application/json' -Body $uiBody
    $uiCreated=$true
    $uiAuth=Invoke-RestMethod 'http://127.0.0.1:8088/v1/auth/login' -Method Post -ContentType 'application/json' -Body $uiBody
    $uiHeaders=@{Authorization="Bearer $($uiAuth.token)"}
    foreach($uiName in @('教学旅人·一','教学旅人·二')) {
        $null=Invoke-RestMethod 'http://127.0.0.1:8088/v1/characters' -Method Post -Headers $uiHeaders -ContentType 'application/json; charset=utf-8' -Body (@{name=$uiName}|ConvertTo-Json -Compress)
    }
    $null=Invoke-RestMethod 'http://127.0.0.1:8088/v1/auth/logout' -Method Post -Headers $uiHeaders -ContentType 'application/json' -Body '{}'
    Import-MMOGameServerEnvironment
    $uiServerLog=Join-Path $uiLogs 'server.log'
    $uiServer=Start-UIProcess @($uiProject,'/Engine/Maps/Entry','-server','-port=7777','-NullRHI','-nosound','-unattended','-Multiprocess','-LiveCoding=false',"-abslog=$uiServerLog") $true
    $uiChildren.Add(@{role='server';process=$uiServer;log=$uiServerLog})
    $uiDeadline=[DateTime]::UtcNow.AddMinutes(3)
    do {
        if($uiServer.HasExited){throw 'The UI test game server exited during startup.'}
        $uiReady=(Test-Path -LiteralPath $uiServerLog) -and ((Get-Content -LiteralPath $uiServerLog -Raw) -match 'GameNetDriver.*listening on port 7777')
        if(-not $uiReady){Start-Sleep -Milliseconds 500}
    } while(-not $uiReady -and [DateTime]::UtcNow -lt $uiDeadline)
    if(-not $uiReady){throw 'The UI test game server did not become ready.'}
    $uiEditorLog=Join-Path $uiLogs 'editor.log'
    $uiEditor=Start-UIProcess @($uiProject,'/Engine/Maps/Entry','-Multiprocess','-LiveCoding=false',
        '-EnablePlugins=ModelContextProtocol,ToolsetRegistry,AllToolsets',
        '-MMOShowcase','-MMONoAutoCapture','-MMOSmokeRole=A',"-MMOSmokeUser=$uiUser",
        '-ExecCmds=Scalability 1,t.MaxFPS 30',"-abslog=$uiEditorLog") $false
    $uiChildren.Add(@{role='client-1';process=$uiEditor;log=$uiEditorLog})
    $uiManifest=Join-Path $uiLogs 'processes.json'
    @{project=$uiProject;executable=$uiExe;fixtures=@($uiUser);captures=$null;
        purpose='Native Slate input and ViewModel lifecycle validation; test fixture credentials remain in editor process environment.';
        processes=@($uiChildren|ForEach-Object{@{role=$_.role;pid=$_.process.Id;started=$_.process.StartTime.ToUniversalTime().ToString('o');log=$_.log}})
    }|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $uiManifest
    Write-Host "UI validation editor PID $($uiEditor.Id). Verify MCP owner and project, set Floating PIE to actual 1920x1080 / Medium, then start PIE."
    Write-Host "The first character autojoins this disposable server. After MMO_SHOWCASE_READY, use native Slate keyboard/gamepad events to validate the real panels. Manifest: $uiManifest"
    Write-Host 'Stop PIE, save work, and close the editor normally; run Stop-Lab.ps1 with this manifest to check final saves and remove only these fixtures.'
} catch {
    foreach($uiChild in $uiChildren) {
        if(-not $uiChild.process.HasExited){$null=$uiChild.process.CloseMainWindow();if(-not $uiChild.process.WaitForExit(5000)){$uiChild.process.Kill()}}
    }
    if($uiCreated){Import-MMOEnvironment;$null=Invoke-MMOPsql "DELETE FROM accounts WHERE username='$uiUser';"}
    throw
} finally {
    $env:MMO_SERVER_SECRET=$uiOldSecret
    $uiPassword=$null; $uiAuth=$null; $uiHeaders=$null; $uiBody=$null
}
