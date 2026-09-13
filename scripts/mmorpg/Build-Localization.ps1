param(
    [string]$Project='F:/UE/LyraDocLabs/MMORPG/MMORPG.uproject',
    [string]$Engine='E:/UnrealEngine'
)
$ErrorActionPreference='Stop'
$locProject=(Resolve-Path -LiteralPath $Project).Path
$locRoot=Split-Path $locProject -Parent
$locDocs=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$locExe=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if(-not (Test-Path -LiteralPath $locExe)){throw "Source editor executable missing: $locExe"}
if(-not (Test-Path -LiteralPath (Join-Path $locRoot 'Content/Localization/ST_MMO.uasset'))){throw 'Run the Designer asset builder and save ST_MMO before gathering.'}
$locBusy=@(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('UnrealEditor.exe','UnrealEditor-Cmd.exe') -and
    $_.CommandLine -and $_.CommandLine.Replace('/','\').Contains($locProject.Replace('/','\'),[StringComparison]::OrdinalIgnoreCase)
})
if($locBusy.Count){throw 'Save and close this project normally before the localization commandlets load its assets.'}
$locLogRoot=Join-Path $locRoot ('Saved/Logs/Localization/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $locLogRoot -Force | Out-Null
$locReport=[ordered]@{schemaVersion=2;project=$locProject;startedAt=[DateTime]::UtcNow.ToString('o');passed=$false;steps=@();artifacts=@()}
$locReportPath=Join-Path $locDocs 'verification/mmorpg-localization.json'
try {
    foreach($locStep in @('Gather','Export','Translate','Import','Compile','GenerateReports')){
        if($locStep -eq 'Translate'){
            $locTranslation=& python (Join-Path $PSScriptRoot 'translate-localization.py') --project $locRoot --examples (Join-Path $locDocs 'examples/MMORPG')
            if($LASTEXITCODE -ne 0){throw 'Translation coverage or source/format validation failed.'}
            $locReport.translation=($locTranslation -join [Environment]::NewLine)|ConvertFrom-Json
            continue
        }
        $locConfig="Config/Localization/MMO_$locStep.ini"
        if(-not (Test-Path -LiteralPath (Join-Path $locRoot $locConfig))){throw "Missing localization configuration: $locConfig"}
        $locLog=Join-Path $locLogRoot "$locStep.log"
        $locStart=[DateTime]::UtcNow
        & $locExe $locProject '-run=GatherText' "-config=$locConfig" '-unattended' '-nop4' '-NullRHI' '-nosound' '-Multiprocess' '-corelimit=2' '-UTF8Output' "-abslog=$locLog" '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False'
        $locExit=$LASTEXITCODE
        $locReport.steps+=@{name=$locStep;exitCode=$locExit;seconds=([DateTime]::UtcNow-$locStart).TotalSeconds;log=$locLog}
        if($locExit -ne 0){throw "$locStep failed ($locExit); inspect $locLog"}
        # GenerateTextLocalizationReport can return 0 after logging a report-write warning.
        if((Get-Content -LiteralPath $locLog -Raw) -match 'Failed to generate (word count|localization conflict) report'){
            throw "$locStep did not write its native report; inspect $locLog"
        }
        if($locStep -eq 'Gather'){
            $locConflictPath=Join-Path $locRoot 'Content/Localization/MMO/MMO_Conflicts.txt'
            if(-not(Test-Path -LiteralPath $locConflictPath)){throw 'Gather did not create MMO_Conflicts.txt in its actual gathering context.'}
            $locConflictText=[IO.File]::ReadAllText($locConflictPath)
            # Native FLocTextConflicts emits one unindented namespace/key header per conflict.
            $locConflictCount=[regex]::Matches($locConflictText,'(?m)^[^\t\r\n]+ - [^\r\n]+$').Count
            $locReport.conflicts=@{path=$locConflictPath;count=$locConflictCount;bytes=(Get-Item -LiteralPath $locConflictPath).Length;sha256=(Get-FileHash -LiteralPath $locConflictPath).Hash.ToLowerInvariant();method='Native report at the end of the same Source/Assets Gather process'}
            if($locConflictText.Length -gt 0 -or $locConflictCount -gt 0){throw "Gather found conflicting localization source identities; inspect $locConflictPath"}
        }
        Write-Host "Localization $locStep completed."
    }
    $locCSVPath=Join-Path $locRoot 'Content/Localization/MMO/MMO.csv'
    if(-not(Test-Path -LiteralPath $locCSVPath)){throw 'GenerateReports did not create the native MMO.csv.'}
    $locRows=@(Import-Csv -LiteralPath $locCSVPath)
    if(-not $locRows.Count){throw 'Native word count CSV has no data rows.'}
    $locLatest=$locRows[-1]
    foreach($locColumn in @('Date/Time','Word Count','en','zh-Hans')){
        if($locColumn -notin $locLatest.psobject.Properties.Name){throw "Native word count CSV is missing column $locColumn"}
    }
    foreach($locColumn in @('Word Count','en','zh-Hans')){
        if($locLatest.$locColumn -notmatch '^\d+$'){throw "Native word count CSV has an invalid $locColumn value."}
    }
    $locSourceWords=[int]$locLatest.'Word Count'
    $locCultureWords=[ordered]@{en=[int]$locLatest.en;'zh-Hans'=[int]$locLatest.'zh-Hans'}
    $locReport.wordCounts=@{path=$locCSVPath;reportedAt=$locLatest.'Date/Time';source=$locSourceWords;cultures=$locCultureWords;method='Native source word counts, separate from translation key counts'}
    if($locSourceWords -le 0 -or $locCultureWords.en -ne $locSourceWords -or $locCultureWords.'zh-Hans' -ne $locSourceWords){throw 'Expected both fully translated cultures to cover every native source word.'}
    foreach($locRelative in @('MMO.manifest','en/MMO.archive','zh-Hans/MMO.archive','en/MMO.locres','zh-Hans/MMO.locres','MMO.locmeta','MMO.csv','MMO_Conflicts.txt')){
        $locArtifact=Join-Path $locRoot "Content/Localization/MMO/$locRelative"
        if(-not (Test-Path -LiteralPath $locArtifact)){throw "Missing native localization output: $locArtifact"}
        $locReport.artifacts+=@{path=$locArtifact;bytes=(Get-Item -LiteralPath $locArtifact).Length;sha256=(Get-FileHash -LiteralPath $locArtifact -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $locReport.passed=$true
} catch {
    $locReport.error=$_.Exception.Message
    throw
} finally {
    $locReport.completedAt=[DateTime]::UtcNow.ToString('o')
    $locReport|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $locReportPath -Encoding utf8
}
Write-Host "Native localization resources compiled. Runtime language/layout checks are separate: $locReportPath"
