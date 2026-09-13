param([string]$BaseUrl='http://127.0.0.1:8088', [switch]$IncludeRestart)
. (Join-Path $PSScriptRoot 'Common.ps1')
Import-MMOEnvironment
$checks=[Collections.Generic.List[object]]::new()
function Assert-MMO([bool]$Pass,[string]$Name) {
    $checks.Add([pscustomobject]@{name=$Name;passed=$Pass})
    if(-not $Pass) { throw "FAILED: $Name" }
    Write-Host "PASS $Name"
}
function Call-MMO([string]$Path,[object]$Body=$null,[string]$Token='',[string]$Method='POST') {
    $headers=@{}; if($Token) {$headers.Authorization="Bearer $Token"}
    $args=@{Uri="$BaseUrl$Path";Method=$Method;Headers=$headers;SkipHttpErrorCheck=$true;TimeoutSec=15}
    if($null -ne $Body) {$args.Body=ConvertTo-Json -InputObject $Body -Depth 12 -Compress; $args.ContentType='application/json; charset=utf-8'}
    $r=Invoke-WebRequest @args
    [pscustomobject]@{Status=[int]$r.StatusCode;Json=($r.Content|ConvertFrom-Json)}
}
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,12)
$user="test_$suffix"; $password="Tutorial!$([Guid]::NewGuid().ToString('N'))"
$serverToken=$env:MMO_SERVER_SECRET
try {
    Assert-MMO ((Call-MMO '/health' $null '' 'GET').Status -eq 200) 'database-backed health'
    $r=Call-MMO '/v1/auth/register' @{username=$user;password=$password}
    Assert-MMO ($r.Status -eq 201) 'register account'
    Assert-MMO ((Call-MMO '/v1/auth/register' @{username=$user;password=$password}).Status -eq 409) 'duplicate registration rejected'
    Assert-MMO ((Call-MMO '/v1/auth/login' @{username=$user;password='wrong-password-12'}).Status -eq 401) 'wrong password rejected'
    $login=Call-MMO '/v1/auth/login' @{username=$user;password=$password}; $token=$login.Json.token
    Assert-MMO ($login.Status -eq 200 -and $token.Length -eq 64) 'opaque login session'
    Assert-MMO ((Call-MMO '/v1/characters' $null '' 'GET').Status -eq 401) 'anonymous character access rejected'
    $c=Call-MMO '/v1/characters' @{name='测试旅人'} $token; $id=$c.Json.id
    Assert-MMO ($c.Status -eq 201 -and $c.Json.version -eq 0) 'create character with initial state'
    Assert-MMO ((Call-MMO '/v1/characters' @{name='测试旅人'} $token).Status -eq 409) 'duplicate character name rejected'
    $list=Call-MMO '/v1/characters' $null $token 'GET'
    Assert-MMO ($list.Status -eq 200 -and $list.Json.characters.Count -eq 1) 'list owned characters'
    $other=Call-MMO '/v1/auth/register' @{username="other_$suffix";password=$password}
    $otherToken=(Call-MMO '/v1/auth/login' @{username="other_$suffix";password=$password}).Json.token
    Assert-MMO ((Call-MMO '/v1/join-ticket' @{characterId=$id;serverId='local-1'} $otherToken).Status -eq 404) 'cross-account character access rejected'
    $join=Call-MMO '/v1/join-ticket' @{characterId=$id;serverId='local-1'} $token
    Assert-MMO ($join.Status -eq 200 -and $join.Json.expiresIn -eq 60) 'issue short-lived ticket'
    Assert-MMO ((Call-MMO '/v1/server/consume-ticket' @{ticket=$join.Json.ticket;serverId='local-1'} $token).Status -eq 401) 'client token cannot invoke server API'
    Assert-MMO ((Call-MMO '/v1/server/consume-ticket' @{ticket=$join.Json.ticket;serverId='wrong'} $serverToken).Status -eq 403) 'ticket cannot be redeemed by wrong server'
    $redeem=Call-MMO '/v1/server/consume-ticket' @{ticket=$join.Json.ticket;serverId='local-1'} $serverToken
    Assert-MMO ($redeem.Status -eq 200 -and $redeem.Json.character.id -eq $id) 'consume ticket and load state atomically'
    $lease=$redeem.Json.leaseToken
    Assert-MMO ((Call-MMO '/v1/server/consume-ticket' @{ticket=$join.Json.ticket;serverId='local-1'} $serverToken).Status -eq 401) 'ticket replay rejected'
    Assert-MMO ((Call-MMO '/v1/join-ticket' @{characterId=$id;serverId='local-1'} $token).Status -eq 409) 'duplicate online account rejected'
    $leaseBody=@{characterId=$id;leaseToken=$lease;serverId='local-1'}
    Assert-MMO ((Call-MMO '/v1/server/heartbeat' $leaseBody $serverToken).Status -eq 200) 'heartbeat extends lease'
    $state=@{level=2;xp=42;health=75;mana=80;inventory=@(@{itemId='potion';quantity=3});position=@{x=12;y=34;z=150}}
    $save=@{characterId=$id;leaseToken=$lease;serverId='local-1';expectedVersion=0;requestId=[Guid]::NewGuid().ToString();state=$state}
    $saved=Call-MMO '/v1/server/save' $save $serverToken
    Assert-MMO ($saved.Status -eq 200 -and $saved.Json.version -eq 1) 'transactionally save inventory and progression'
    $retry=Call-MMO '/v1/server/save' $save $serverToken
    Assert-MMO ($retry.Status -eq 200 -and $retry.Json.version -eq 1) 'identical save retry does not increment version'
    $state.xp=43
    Assert-MMO ((Call-MMO '/v1/server/save' $save $serverToken).Status -eq 409) 'reused requestId with changed payload rejected'
    $save.requestId=[Guid]::NewGuid().ToString()
    Assert-MMO ((Call-MMO '/v1/server/save' $save $serverToken).Status -eq 409) 'stale save version rejected'
    $save.expectedVersion=1; $state.inventory=@(@{itemId='potion';quantity=-1})
    Assert-MMO ((Call-MMO '/v1/server/save' $save $serverToken).Status -eq 400) 'invalid inventory rejected'
    $loaded=Call-MMO '/v1/server/load' $leaseBody $serverToken
    Assert-MMO ($loaded.Json.character.state.xp -eq 42 -and $loaded.Json.character.state.inventory[0].quantity -eq 3) 'failed saves leave committed state intact'
    if($IncludeRestart) {
        & (Join-Path $PSScriptRoot 'Stop-Backend.ps1')
        & (Join-Path $PSScriptRoot 'Start-Backend.ps1')
        $loaded=Call-MMO '/v1/server/load' $leaseBody $serverToken
        Assert-MMO ($loaded.Status -eq 200 -and $loaded.Json.character.state.xp -eq 42) 'backend restart preserves sessions lease and saved state'
    }
    Assert-MMO ((Call-MMO '/v1/server/release' $leaseBody $serverToken).Status -eq 200) 'release active lease'
    Assert-MMO ((Call-MMO '/v1/server/save' $save $serverToken).Status -eq 403) 'released lease cannot save'
    $join=Call-MMO '/v1/join-ticket' @{characterId=$id;serverId='local-1'} $token
    # This test owns the newly created account and deliberately expires only its test ticket.
    Invoke-MMOPsql "UPDATE join_tickets SET expires_at=now()-interval '1 second' WHERE character_id='$id'::uuid;" | Out-Null
    Assert-MMO ((Call-MMO '/v1/server/consume-ticket' @{ticket=$join.Json.ticket;serverId='local-1'} $serverToken).Status -eq 401) 'expired ticket rejected'
    $join=Call-MMO '/v1/join-ticket' @{characterId=$id;serverId='local-1'} $token
    $rejoined=Call-MMO '/v1/server/consume-ticket' @{ticket=$join.Json.ticket;serverId='local-1'} $serverToken
    Assert-MMO ($rejoined.Status -eq 200 -and $rejoined.Json.character.state.level -eq 2) 'rejoin restores persisted progression'
    $leaseBody.leaseToken=$rejoined.Json.leaseToken
    Invoke-MMOPsql "UPDATE game_leases SET expires_at=now()-interval '1 second' WHERE character_id='$id'::uuid;" | Out-Null
    Assert-MMO ((Call-MMO '/v1/server/heartbeat' $leaseBody $serverToken).Status -eq 403) 'expired lease cannot be revived'
    Assert-MMO ((Call-MMO '/v1/auth/logout' @{} $token).Status -eq 200) 'logout revokes session'
    Assert-MMO ((Call-MMO '/v1/characters' $null $token 'GET').Status -eq 401) 'revoked session cannot read characters'
} finally {
    $report=[pscustomobject]@{at=(Get-Date).ToUniversalTime().ToString('o');baseUrl=$BaseUrl;includeRestart=[bool]$IncludeRestart;checks=$checks}
    $reportPath=Join-Path (Get-MMOProjectRoot) '.local/backend-test-results.json'
    $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $reportPath -Encoding utf8
    # Remove only accounts generated by this invocation, including dependent fixtures via FK cascades.
    Invoke-MMOPsql "DELETE FROM accounts WHERE username IN ('$user','other_$suffix');" | Out-Null
}
Write-Host "$($checks.Count) backend integration checks passed. Report: .local/backend-test-results.json"
