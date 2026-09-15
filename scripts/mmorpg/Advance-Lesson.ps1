param(
    [Parameter(Mandatory)][ValidateRange(0,3)][int]$Stage,
    [Parameter(Mandatory)][string]$Destination,
    [ValidateSet('Apply','Check')][string]$Action = 'Check',
    [string]$LyraRoot = 'F:/UE/LyraStarterGame'
)
$ErrorActionPreference = 'Stop'
$courseRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$lessonRoot = [IO.Path]::GetFullPath($Destination).TrimEnd('/','\')
if ([IO.Path]::GetFileName($lessonRoot) -ne 'MMORPG') { throw 'Destination must be a new directory named MMORPG.' }
foreach ($protected in @($courseRoot, [IO.Path]::GetFullPath($LyraRoot).TrimEnd('/','\'))) {
    if ($lessonRoot.Equals($protected,[StringComparison]::OrdinalIgnoreCase) -or $protected.StartsWith($lessonRoot + '\',[StringComparison]::OrdinalIgnoreCase) -or $lessonRoot.StartsWith($protected + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose a separate lesson directory outside the docs and baseline projects.' }
}
$manifest = Get-Content -LiteralPath (Join-Path $courseRoot 'curriculum/mmorpg/stages.json') -Raw | ConvertFrom-Json
$stateFile = Join-Path $lessonRoot '.lesson-state.json'
$previous = $null
if (Test-Path -LiteralPath $stateFile) {
    $previous = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    if ($previous.destination -ne $lessonRoot) { throw 'Lesson state belongs to another directory.' }
}
if ($Action -eq 'Apply') {
    if (-not $previous) {
        if ($Stage -ne 0) { throw 'Start with Stage 0 in a new folder.' }
        if ((Test-Path -LiteralPath $lessonRoot) -and (Get-ChildItem -LiteralPath $lessonRoot -Force | Select-Object -First 1)) { throw 'Stage 0 requires an empty destination; existing projects are not overwritten.' }
    } elseif ($Stage -gt $previous.stage + 1 -or $Stage -lt $previous.stage) { throw 'Advance one stage at a time; an existing lesson cannot be rolled back by this helper.' }
}
$planned = @{}
function Add-LessonFile([string]$Source, [string]$Relative) {
    $absolute = [IO.Path]::GetFullPath((Join-Path $lessonRoot $Relative))
    if (-not $absolute.StartsWith($lessonRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Lesson file escaped Destination.' }
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing source: $Source" }
    $planned[$Relative] = [pscustomobject]@{source=$Source; target=$absolute; sha256=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash}
}
foreach ($phase in $manifest.stages | Where-Object id -le $Stage) {
    foreach ($file in $phase.files) {
        $source = [IO.Path]::GetFullPath((Join-Path $courseRoot $file.source))
        if (-not $source.StartsWith($courseRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Lesson source escaped the package.' }
        Add-LessonFile $source $file.target
    }
    foreach ($entry in $phase.imports) {
        $source = [IO.Path]::GetFullPath((Join-Path $LyraRoot $entry.from))
        if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Missing official plugin: $source" }
        foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File) {
            $relative = [IO.Path]::GetRelativePath($source,$file.FullName)
            if ($relative -match '(^|[\\/])(Binaries|Intermediate|Saved|DerivedDataCache|\.git|\.vs|\.idea)([\\/]|$)') { continue }
            Add-LessonFile $file.FullName (Join-Path $entry.to $relative)
        }
    }
}
$issues = [Collections.Generic.List[string]]::new()
foreach ($relative in $planned.Keys) {
    $file = $planned[$relative]
    if (-not (Test-Path -LiteralPath $file.target -PathType Leaf)) {
        if ($Action -eq 'Check') { $issues.Add("Missing: $relative") }
        continue
    }
    $actual = (Get-FileHash -LiteralPath $file.target -Algorithm SHA256).Hash
    if ($actual -eq $file.sha256) { continue }
    if ($Action -eq 'Check') { $issues.Add("Different: $relative"); continue }
    $oldHash = if ($previous) { $previous.files.PSObject.Properties[$relative].Value } else { $null }
    if (-not $oldHash -or $actual -ne $oldHash) { $issues.Add("Learner edit preserved: $relative") }
}
if ($issues.Count) { throw ($issues -join [Environment]::NewLine) }
if ($Action -eq 'Apply') {
    # Every potential conflict was checked before the first copy. No directories are deleted.
    foreach ($file in $planned.Values) {
        New-Item -ItemType Directory -Path (Split-Path $file.target -Parent) -Force | Out-Null
        if (-not (Test-Path -LiteralPath $file.target) -or (Get-FileHash -LiteralPath $file.target -Algorithm SHA256).Hash -ne $file.sha256) { Copy-Item -LiteralPath $file.source -Destination $file.target }
    }
    $hashes = [ordered]@{}
    foreach ($relative in $planned.Keys | Sort-Object) { $hashes[$relative] = $planned[$relative].sha256 }
    [ordered]@{schemaVersion=1;destination=$lessonRoot;stage=$Stage;files=$hashes} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $stateFile -Encoding utf8
}
Write-Host "Stage $Stage $Action passed: $($planned.Count) source/config/dependency files. This checks files; build and runtime checks are separate."
