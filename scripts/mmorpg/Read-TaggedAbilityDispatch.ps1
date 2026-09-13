param(
    [Parameter(Mandatory)][string]$ServerLog,
    [string[]]$RequiredSuccessfulTags=@('InputTag.MMO.Attack','InputTag.MMO.Spell')
)
$ErrorActionPreference='Stop'
$mmoText=Get-Content -LiteralPath $ServerLog -Raw
$mmoEvents=@([regex]::Matches($mmoText,'MMO_INPUT_DISPATCH tag=(InputTag\.MMO\.[A-Za-z]+) activated=([01]) specCount=(\d+) authority=([01])') | ForEach-Object {
    [pscustomobject]@{tag=$_.Groups[1].Value;activated=($_.Groups[2].Value -eq '1');matchingSpecCount=[int]$_.Groups[3].Value;authoritative=($_.Groups[4].Value -eq '1')}
})
if(-not $mmoEvents.Count){throw 'No real server input-tag dispatch observations were emitted.'}
foreach($mmoEvent in $mmoEvents) {
    if(-not $mmoEvent.authoritative -or $mmoEvent.matchingSpecCount -ne 1 -or $mmoEvent.tag -notin @('InputTag.MMO.Attack','InputTag.MMO.Spell')){throw 'A server dispatch did not match the two-grant tutorial configuration.'}
}
foreach($mmoTag in $RequiredSuccessfulTags) {
    if(-not @($mmoEvents|Where-Object {$_.tag -eq $mmoTag -and $_.activated}).Count){throw "No successful authoritative activation was observed for $mmoTag"}
}
[pscustomobject]@{passed=$true;scope='Actual server RPC dispatch and GAS activation by granted dynamic spec tag';requiredSuccessfulTags=$RequiredSuccessfulTags;events=$mmoEvents}
