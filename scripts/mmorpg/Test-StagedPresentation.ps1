param(
    [Parameter(Mandatory)][string]$ClientExecutable,
    [string]$Engine='E:/UnrealEngine'
)
$ErrorActionPreference='Stop'
$mmoRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$mmoClient=(Resolve-Path -LiteralPath $ClientExecutable).Path
if([IO.Path]::GetFileName($mmoClient) -ne 'MMORPGClient.exe'){throw 'Expected the staged Development MMORPGClient.exe.'}
$mmoStage=[IO.Path]::GetFullPath((Join-Path (Split-Path $mmoClient -Parent) '../../..'))
$mmoPak=Join-Path $Engine 'Engine/Binaries/Win64/UnrealPak.exe'
if(-not(Test-Path -LiteralPath $mmoPak)){throw 'Build UnrealPak Win64 Development before inspecting packaged presentation resources.'}
$mmoListingPath=Join-Path $mmoRepo 'verification/mmorpg-staged-presentation-files.log'
$mmoEvidence=Join-Path $mmoRepo 'verification/mmorpg-staged-presentation.json'
$mmoInspectionId=[Guid]::NewGuid().ToString('N')
$mmoReport=[ordered]@{inspectionId=$mmoInspectionId;checkedAt=[DateTime]::UtcNow.ToString('o');passed=$false;client=$mmoClient;clientSHA256=(Get-FileHash -LiteralPath $mmoClient).Hash.ToLowerInvariant();stageRoot=$mmoStage;method='Native UnrealPak -List and -ListContainer plus exact loose-file checks in the staged directory';resources=@();containers=@()}
try {
    $mmoPaks=@(Get-ChildItem -LiteralPath (Join-Path $mmoStage 'MMORPG/Content/Paks') -File -Filter '*.pak')
    if(-not $mmoPaks.Count){throw 'No staged Pak files were found.'}
    $mmoListings=@{}
    $mmoCombined=[Text.StringBuilder]::new()
    foreach($mmoPakFile in $mmoPaks) {
        $mmoListText=(& $mmoPak $mmoPakFile.FullName -List 2>&1 | Out-String)
        if($LASTEXITCODE -ne 0){throw "UnrealPak failed to inspect $($mmoPakFile.Name)."}
        $mmoListings[$mmoPakFile.FullName]=$mmoListText.Replace('\','/')
        [void]$mmoCombined.AppendLine($mmoPakFile.FullName)
        [void]$mmoCombined.AppendLine($mmoListText)
    }
    $mmoIoStoreLists=@()
    foreach($mmoToc in @(Get-ChildItem -LiteralPath (Join-Path $mmoStage 'MMORPG/Content/Paks') -File -Filter '*.utoc')) {
        $mmoCsv=Join-Path $mmoRepo ("verification/mmorpg-staged-"+$mmoInspectionId+'-'+$mmoToc.BaseName+'.csv')
        if(Test-Path -LiteralPath $mmoCsv){throw 'This inspection requires a new, previously nonexistent CSV path.'}
        $mmoListingStartedAt=[DateTime]::UtcNow
        $mmoTocSHA256=(Get-FileHash -LiteralPath $mmoToc.FullName).Hash.ToLowerInvariant()
        $mmoIoText=(& $mmoPak "-ListContainer=$($mmoToc.FullName)" "-Csv=$mmoCsv" 2>&1 | Out-String)
        if($LASTEXITCODE -ne 0 -or $mmoIoText -match 'Failed to read container toc' -or -not(Test-Path -LiteralPath $mmoCsv)){throw "Native IoStore listing failed for $($mmoToc.Name)."}
        $mmoCsvFile=Get-Item -LiteralPath $mmoCsv
        if($mmoCsvFile.Length -le 0 -or $mmoCsvFile.LastWriteTimeUtc -lt $mmoListingStartedAt){throw 'Native IoStore listing did not write a fresh nonempty CSV in this run.'}
        if((Get-FileHash -LiteralPath $mmoToc.FullName).Hash.ToLowerInvariant() -ne $mmoTocSHA256){throw 'Container metadata changed during native inspection.'}
        $mmoCsvText=Get-Content -LiteralPath $mmoCsv -Raw
        if($mmoCsvText -notmatch '^OrderInContainer,\s*ChunkId,\s*PackageId,\s*PackageName,\s*Filename,'){throw 'Native IoStore CSV does not contain the expected filename-bearing schema.'}
        $mmoListings[$mmoToc.FullName]=$mmoCsvText.Replace('\','/')
        [void]$mmoCombined.AppendLine($mmoIoText)
        [void]$mmoCombined.AppendLine($mmoCsvText)
        $mmoIoStoreLists+=@{container=$mmoToc.FullName;containerSHA256=$mmoTocSHA256;csv=$mmoCsv;createdByThisRun=$true;listingStartedAt=$mmoListingStartedAt.ToString('o');csvWrittenAt=$mmoCsvFile.LastWriteTimeUtc.ToString('o');sha256=(Get-FileHash -LiteralPath $mmoCsv).Hash.ToLowerInvariant()}
    }
    if(-not $mmoIoStoreLists.Count){throw 'This lab expects staged IoStore containers as well as Pak files.'}
    [IO.File]::WriteAllText($mmoListingPath,$mmoCombined.ToString(),[Text.UTF8Encoding]::new($false))
    foreach($mmoResource in @('MMORPG/Content/Localization/MMO/MMO.locmeta','MMORPG/Content/Localization/MMO/en/MMO.locres','MMORPG/Content/Localization/MMO/zh-Hans/MMO.locres','Engine/Content/Slate/Fonts/Roboto-Regular.ttf','Engine/Content/Slate/Fonts/DroidSansFallback.ttf')) {
        $mmoMatches=@($mmoListings.Keys | Where-Object {$mmoListings[$_].Contains($mmoResource)})
        $mmoLoose=Join-Path $mmoStage $mmoResource
        $mmoFound=$mmoMatches.Count -gt 0 -or (Test-Path -LiteralPath $mmoLoose)
        $mmoReport.resources+=@{relativePath=$mmoResource;found=$mmoFound;pakFiles=$mmoMatches;loosePath=$(if(Test-Path -LiteralPath $mmoLoose){$mmoLoose}else{$null})}
        if(-not $mmoFound){throw "Required localized UI resource missing from actual staged output: $mmoResource"}
    }
    $mmoAllListings=$mmoCombined.ToString().Replace('\','/')
    $mmoHealthIncluded=$mmoAllListings -match 'MMORPG/Content/Tutorial/BP_BackendHealth\.uasset'
    $mmoLooseTestFiles=@()
    $mmoLooseTests=Join-Path $mmoStage 'MMORPG/Content/Tutorial/Tests'
    if(Test-Path -LiteralPath $mmoLooseTests){$mmoLooseTestFiles+=@(Get-ChildItem -LiteralPath $mmoLooseTests -Recurse -File | ForEach-Object FullName)}
    $mmoLooseTutorial=Join-Path $mmoStage 'MMORPG/Content/Tutorial'
    if(Test-Path -LiteralPath $mmoLooseTutorial){$mmoLooseTestFiles+=@(Get-ChildItem -LiteralPath $mmoLooseTutorial -Recurse -File -Filter 'BP_BackendHealth_RoundTrip_*' | ForEach-Object FullName)}
    $mmoLooseTestFiles=@($mmoLooseTestFiles | Select-Object -Unique)
    $mmoReport.looseRoundTripFiles=$mmoLooseTestFiles
    $mmoTestExcluded=$mmoAllListings -notmatch 'MMORPG/Content/Tutorial/Tests/|BP_BackendHealth_RoundTrip_' -and $mmoLooseTestFiles.Count -eq 0
    if(-not $mmoHealthIncluded -or -not $mmoTestExcluded){throw 'Cook must include the explicit health teaching Blueprint and exclude all Tutorial/Tests round-trip copies.'}
    $mmoReport.teachingBlueprint=@{healthIncluded=$mmoHealthIncluded;roundTripTestsExcluded=$mmoTestExcluded;ioStoreLists=$mmoIoStoreLists}
    foreach($mmoContainer in @(Get-ChildItem -LiteralPath (Join-Path $mmoStage 'MMORPG/Content/Paks') -File | Where-Object Extension -in @('.pak','.utoc','.ucas'))) {
        $mmoReport.containers+=@{path=$mmoContainer.FullName;bytes=$mmoContainer.Length;sha256=(Get-FileHash -LiteralPath $mmoContainer.FullName).Hash.ToLowerInvariant()}
    }
    $mmoReport.listingPath=$mmoListingPath
    $mmoReport.listingSHA256=(Get-FileHash -LiteralPath $mmoListingPath).Hash.ToLowerInvariant()
    $mmoReport.passed=$true
} catch {$mmoReport.error=$_.Exception.Message;throw}
finally {$mmoReport|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $mmoEvidence -Encoding utf8}
Write-Output $mmoEvidence
