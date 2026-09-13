param([string]$ProjectRoot='F:/UE/LyraDocLabs/LyraTraining')
$ErrorActionPreference='Stop'
$projectPath=(Resolve-Path -LiteralPath $ProjectRoot).Path
function Set-IniValue([string]$Text,[string]$Section,[string]$Key,[string]$Value) {
    $heading='['+$Section+']'
    $pattern='(?ms)^'+[regex]::Escape($heading)+'.*?(?=^\[|\z)'
    $match=[regex]::Match($Text,$pattern)
    if (-not $match.Success) { return $Text+"`r`n$heading`r`n$Key=$Value`r`n" }
    $body=$match.Value
    $keyPattern='(?m)^'+[regex]::Escape($Key)+'=[^\r\n]*'
    if ([regex]::IsMatch($body,$keyPattern)) { $body=[regex]::Replace($body,$keyPattern,"$Key=$Value") }
    else { $body=$body.TrimEnd()+"`r`n$Key=$Value`r`n`r`n" }
    return $Text.Substring(0,$match.Index)+$body+$Text.Substring($match.Index+$match.Length)
}
$quality=@('ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading','Landscape')
foreach ($platform in @('WindowsEditor','Windows')) {
    $folder=Join-Path $projectPath "Saved/Config/$platform"
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    $file=Join-Path $folder 'GameUserSettings.ini'
    $text=if (Test-Path -LiteralPath $file) { Get-Content -LiteralPath $file -Raw } else { '' }
    if ($text -and -not (Test-Path -LiteralPath "$file.before-medium.bak")) { Copy-Item -LiteralPath $file -Destination "$file.before-medium.bak" }
    $section="[ScalabilityGroups]`r`nsg.ResolutionQuality=71`r`n"
    foreach ($group in $quality) { $section += "sg.${group}Quality=1`r`n" }
    if ($text -match '(?m)^\[ScalabilityGroups\]') { $text=[regex]::Replace($text,'(?ms)^\[ScalabilityGroups\].*?(?=^\[|\z)',$section+"`r`n") }
    else { $text += "`r`n"+$section }
    $settingsClass=if (Test-Path -LiteralPath (Join-Path $projectPath 'LyraStarterGame.uproject')) { '/Script/LyraGame.LyraSettingsLocal' } else { '/Script/Engine.GameUserSettings' }
    foreach ($key in @('ResolutionSizeX','LastUserConfirmedResolutionSizeX','DesiredScreenWidth','LastUserConfirmedDesiredScreenWidth')) { $text=Set-IniValue $text $settingsClass $key '1920' }
    foreach ($key in @('ResolutionSizeY','LastUserConfirmedResolutionSizeY','DesiredScreenHeight','LastUserConfirmedDesiredScreenHeight')) { $text=Set-IniValue $text $settingsClass $key '1080' }
    $text=Set-IniValue $text $settingsClass 'FullscreenMode' '2'
    $text=Set-IniValue $text $settingsClass 'FrameRateLimit' '60.000000'
    [IO.File]::WriteAllText($file,$text,[Text.UTF8Encoding]::new($false))
}
$playFile=Join-Path $projectPath 'Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini'
$play=if (Test-Path -LiteralPath $playFile) { Get-Content -LiteralPath $playFile -Raw } else { '' }
if ($play -and -not (Test-Path -LiteralPath "$playFile.before-medium.bak")) { Copy-Item -LiteralPath $playFile -Destination "$playFile.before-medium.bak" }
$play=Set-IniValue $play '/Script/UnrealEd.LevelEditorPlaySettings' 'NewWindowWidth' '1920'
$play=Set-IniValue $play '/Script/UnrealEd.LevelEditorPlaySettings' 'NewWindowHeight' '1080'
[IO.File]::WriteAllText($playFile,$play,[Text.UTF8Encoding]::new($false))
Write-Host "1920x1080 Medium quality saved for $projectPath (Scalability group 1; resolution scale 71%)."
