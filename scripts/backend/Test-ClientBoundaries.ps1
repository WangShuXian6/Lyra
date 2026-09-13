param([string]$ClientReceipt='', [string]$ClientBinary='')
. (Join-Path $PSScriptRoot 'Common.ps1')
$project=Get-MMOProjectRoot
$clientRules=Get-Content -LiteralPath (Join-Path $project 'Plugins/MMOIntegration/Source/MMOBackendClient/MMOBackendClient.Build.cs') -Raw
$contractsRules=Get-Content -LiteralPath (Join-Path $project 'Source/MMOContracts/MMOContracts.Build.cs') -Raw
$plugin=Get-Content -LiteralPath (Join-Path $project 'Plugins/MMOIntegration/MMOIntegration.uplugin') -Raw | ConvertFrom-Json
foreach($forbidden in @('MMOPersistence','MMOThirdParty','libpq','sodium','MMOBackendServer')) {
    if($clientRules.Contains($forbidden) -or $contractsRules.Contains($forbidden)) { throw "Client-visible module references forbidden dependency: $forbidden" }
}
$serverModule=$plugin.Modules | Where-Object Name -eq 'MMOBackendServer'
if($serverModule.Type -ne 'ServerOnly') { throw 'Backend server integration must remain ServerOnly.' }
$thirdParty=Get-Content -LiteralPath (Join-Path $project 'Source/ThirdParty/MMOThirdParty/MMOThirdParty.Build.cs') -Raw
if(-not $thirdParty.Contains('Target.Type != TargetType.Program')) { throw 'Missing Program-only third-party dependency guard.' }
Write-Host 'PASS source dependency boundary (Client/Contracts have no persistence libraries; service integration ServerOnly).'
$artifactStatus='pending'
if($ClientReceipt) {
    $receipt=Get-Content -LiteralPath $ClientReceipt -Raw | ConvertFrom-Json
    if($receipt.TargetName -ne 'MMORPGClient') { throw 'Supply the MMORPGClient receipt, not Editor/Game/Server.' }
    foreach($dep in $receipt.RuntimeDependencies) {
        if($dep.Path -match '(?i)(libpq|libsodium|MMOBackendService|MMOPersistence|MMOBackendServer)') { throw "Private dependency found in client receipt: $($dep.Path)" }
    }
    $artifactStatus='receipt passed'
    Write-Host 'PASS Client Target runtime receipt contains no backend private dependencies.'
}
if($ClientBinary) {
    $bytes=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ClientBinary).Path)
    foreach($encoding in @([Text.Encoding]::ASCII,[Text.Encoding]::Unicode)) {
        $text=$encoding.GetString($bytes)
        foreach($forbidden in @('MMO_SERVER_SECRET','PGPASSWORD','PQconnectdb','crypto_pwhash_str')) {
            if($text.Contains($forbidden)) { throw "Private backend marker found in Client binary: $forbidden" }
        }
    }
    $artifactStatus+='; binary passed'
    Write-Host 'PASS Client binary contains no private backend configuration or DB/password API markers.'
}
@{at=(Get-Date).ToUniversalTime().ToString('o');sourceBoundary='passed';clientArtifact=$artifactStatus}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $project 'backend/client-boundary-review.json')
