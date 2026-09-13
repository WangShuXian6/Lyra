param(
    [Parameter(Mandatory)][string]$Log,
    [Parameter(Mandatory)][string]$Stage
)
$ErrorActionPreference='Stop'
$mmoText=Get-Content -LiteralPath $Log -Raw
$mmoRecords=@([regex]::Matches($mmoText,'MMO_INPUT_GRANTS_RESULT (\{[^\r\n]+\})') | ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json} | Where-Object stage -eq $Stage)
if($mmoRecords.Count -ne 1){throw "Expected exactly one actual ASC input-grant observation for $Stage in $Log"}
$mmoRecord=$mmoRecords[0]
if(-not $mmoRecord.passed -or -not $mmoRecord.ownerAndAvatarMatch -or $mmoRecord.authoritative -or $mmoRecord.ascClass -ne '/Script/MMOFramework.MMOAbilitySystemComponent' -or $mmoRecord.abilityCount -ne 2){throw 'The client did not observe the expected replicated MMO ASC, owner/avatar and two grants.'}
if(@($mmoRecord.bindings).Count -ne 2){throw 'Expected two input-tag bindings in the actual ASC snapshot.'}
foreach($mmoTag in @('InputTag.MMO.Attack','InputTag.MMO.Spell')) {
    $mmoBindings=@($mmoRecord.bindings | Where-Object tag -eq $mmoTag)
    if($mmoBindings.Count -ne 1){throw "Input tag is missing or duplicated in the observation: $mmoTag"}
    $mmoBinding=$mmoBindings[0]
    if(-not $mmoBinding.passed -or $mmoBinding.matchingSpecCount -ne 1 -or @($mmoBinding.specs).Count -ne 1){throw "The tutorial requires exactly one granted spec per tag: $mmoTag"}
    $mmoSpec=$mmoBinding.specs[0]
    $mmoClass=if($mmoTag -eq 'InputTag.MMO.Attack'){'/Script/MMORPG.MMOAttackAbility'}else{'/Game/Tutorial/GA_ArcaneBolt.GA_ArcaneBolt_C'}
    if(-not $mmoSpec.handleValid -or $mmoSpec.inputID -ne -1 -or $mmoSpec.abilityClass -ne $mmoClass){throw "The actual granted spec must have its expected class, a valid handle and InputID=-1: $mmoTag"}
}
# This is a validation rule for the two tutorial grants, not a limit imposed by the reusable ASC.
Write-Output $mmoRecord
