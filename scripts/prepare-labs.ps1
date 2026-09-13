param(
    [string]$LyraRoot = 'F:/UE/LyraStarterGame',
    [string]$LabsRoot = 'F:/UE/LyraDocLabs',
    [string]$EngineRoot = 'E:/UnrealEngine',
    [ValidateSet('All','Training','MMORPG')][string]$Lab = 'All'
)
$ErrorActionPreference = 'Stop'
$docsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceRoot = (Resolve-Path -LiteralPath $LyraRoot).Path
$destinationRoot = [IO.Path]::GetFullPath($LabsRoot)
if ($destinationRoot.TrimEnd('/','\') -eq $sourceRoot.TrimEnd('/','\')) { throw 'LabsRoot must differ from the baseline.' }
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'LyraStarterGame.uproject'))) { throw 'Official Lyra project is missing.' }
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
function Copy-Tree([string]$From, [string]$To) {
    $targetPath = [IO.Path]::GetFullPath($To)
    if (-not $targetPath.StartsWith($destinationRoot.TrimEnd('/','\') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Copy target must remain inside LabsRoot.' }
    & robocopy $From $targetPath /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP /XD Intermediate Saved DerivedDataCache .git .vs .idea .local .deps /XF *.sln *.slnx *.pdb | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copy failed: $From -> $targetPath (robocopy $LASTEXITCODE)" }
}
if ($Lab -in @('All','Training')) {
    Copy-Tree $sourceRoot (Join-Path $destinationRoot 'LyraTraining')
    $addition = Join-Path $docsRoot 'examples/LyraTraining'
    if (Test-Path -LiteralPath $addition) { Copy-Tree $addition (Join-Path $destinationRoot 'LyraTraining') }
    $trainingProject=Join-Path $destinationRoot 'LyraTraining/LyraStarterGame.uproject'
    $descriptor=Get-Content -LiteralPath $trainingProject -Raw | ConvertFrom-Json
    if (-not ($descriptor.Plugins | Where-Object Name -eq 'TrainingRange')) { $descriptor.Plugins += [pscustomobject]@{Name='TrainingRange';Enabled=$true} }
    $descriptor | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $trainingProject -Encoding utf8
    Write-Host 'Training lab prepared. Baseline files were read only.'
}
if ($Lab -in @('All','MMORPG')) {
    $mmoRoot = Join-Path $destinationRoot 'MMORPG'
    Copy-Tree (Join-Path $docsRoot 'examples/MMORPG') $mmoRoot
    $manifest = Get-Content -LiteralPath (Join-Path $docsRoot 'examples/MMORPG/dependencies.json') -Raw | ConvertFrom-Json
    foreach ($entry in $manifest.importDirectories) { Copy-Tree (Join-Path $sourceRoot $entry.from) (Join-Path $mmoRoot $entry.to) }
    foreach ($entry in $manifest.importEngineDirectories) {
        $engineContent = Join-Path $EngineRoot $entry.from
        if (-not (Test-Path -LiteralPath $engineContent)) { throw "Required UE template content is missing: $engineContent. Install the engine's Templates and Feature Packs, then rerun." }
        Copy-Tree $engineContent (Join-Path $mmoRoot $entry.to)
    }
    $capturePlugin = Join-Path $sourceRoot 'Plugins/BlueprintScreenshotTool'
    if (Test-Path -LiteralPath $capturePlugin) { Copy-Tree $capturePlugin (Join-Path $mmoRoot 'Plugins/BlueprintScreenshotTool') }
    Write-Host 'MMORPG lab prepared. Official dependencies were copied locally from your Lyra installation.'
}
