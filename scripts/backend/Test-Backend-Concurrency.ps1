param(
    [string]$BaseUrl='http://127.0.0.1:8088',
    [string]$PostgresRoot='D:/program/1-web/PostgreSQL/18'
)
. (Join-Path $PSScriptRoot 'Common.ps1')
Import-MMOEnvironment
$project=Get-MMOProjectRoot
$checks=[Collections.Generic.List[object]]::new()
$pending=[Collections.Generic.List[object]]::new()
$http=[Net.Http.HttpClient]::new()
$http.Timeout=[TimeSpan]::FromSeconds(12)
$fixtureSuffix=[Guid]::NewGuid().ToString('N').Substring(0,12)
$fixtureUser="race_$fixtureSuffix"
$fixturePassword="Tutorial!$([Guid]::NewGuid().ToString('N'))"
$accountId=''
$fixtureLock=$null
$failure=$null

function Assert-Concurrency([bool]$Pass,[string]$Name) {
    $checks.Add([pscustomobject]@{name=$Name;passed=$Pass})
    if(-not $Pass) { throw "FAILED: $Name" }
    Write-Host "PASS $Name"
}
function Start-MMOCall([string]$Path,[object]$Body=$null,[string]$Token='',[string]$Method='POST') {
    $request=[Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::new($Method), "$BaseUrl$Path")
    if($Token) { $request.Headers.Authorization=[Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer',$Token) }
    if($null -ne $Body) { $request.Content=[Net.Http.StringContent]::new((ConvertTo-Json -InputObject $Body -Depth 12 -Compress),[Text.Encoding]::UTF8,'application/json') }
    $call=[pscustomobject]@{Task=$http.SendAsync($request);Request=$request}
    $pending.Add($call)
    return $call
}
function Complete-MMOCall([object]$Call) {
    try {
        $response=$Call.Task.GetAwaiter().GetResult()
        try {
            $body=$response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            return [pscustomobject]@{Status=[int]$response.StatusCode;Json=($body|ConvertFrom-Json)}
        } finally { $response.Dispose() }
    } finally { $Call.Request.Dispose(); [void]$pending.Remove($Call) }
}
function Call-MMO([string]$Path,[object]$Body=$null,[string]$Token='',[string]$Method='POST') {
    Complete-MMOCall (Start-MMOCall $Path $Body $Token $Method)
}
function Start-FixtureLock {
    # This helper locks only the newly-created test account. PGOPTIONS applies to this
    # child connection; no PostgreSQL service restart or global configuration change occurs.
    $psi=[Diagnostics.ProcessStartInfo]::new()
    $psi.FileName=Join-Path $PostgresRoot 'bin/psql.exe'
    foreach($arg in @('-X','-q','-t','-A','-v','ON_ERROR_STOP=1','-d','lyra_mmo_tutorial')) { $psi.ArgumentList.Add($arg) }
    $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true
    $psi.RedirectStandardInput=$true; $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
    $psi.Environment['PGAPPNAME']="mmo_fixture_$fixtureSuffix"
    $psi.Environment['PGOPTIONS']='-c statement_timeout=8000 -c idle_in_transaction_session_timeout=15000'
    $proc=[Diagnostics.Process]::Start($psi)
    $stderr=$proc.StandardError.ReadToEndAsync()
    try {
        $proc.StandardInput.WriteLine("BEGIN; SELECT id FROM accounts WHERE id='$accountId'::uuid FOR UPDATE; SELECT 'MMO_LOCK_READY:' || pg_backend_pid();")
        $proc.StandardInput.Flush()
        while($true) {
            $lineTask=$proc.StandardOutput.ReadLineAsync()
            if(-not $lineTask.Wait(5000)) { throw 'Test account lock did not become ready.' }
            $line=$lineTask.Result
            if($null -eq $line) { throw 'Test lock helper exited before acquiring its account row.' }
            if($line -match '^MMO_LOCK_READY:(\d+)$') { return [pscustomobject]@{Process=$proc;Pid=[int]$Matches[1];ErrorTask=$stderr} }
        }
    } catch {
        if(-not $proc.HasExited) { $proc.StandardInput.WriteLine('ROLLBACK;'); $proc.StandardInput.Close(); [void]$proc.WaitForExit(5000) }
        $proc.Dispose(); throw
    }
}
function Stop-FixtureLock([object]$Lock) {
    if(-not $Lock) { return }
    try {
        if(-not $Lock.Process.HasExited) {
            $Lock.Process.StandardInput.WriteLine('ROLLBACK;')
            $Lock.Process.StandardInput.Close()
            if(-not $Lock.Process.WaitForExit(5000)) { throw 'Test lock helper did not release normally; its session-local 15-second timeout remains active.' }
        }
        if($Lock.Process.ExitCode -ne 0) { throw 'Test account lock helper reported a SQL error.' }
    } finally { $Lock.Process.Dispose() }
}
function Wait-BlockedWorkers([object]$Lock,[int]$Count,[object[]]$Calls) {
    # PostgreSQL may report a second waiter blocked by the first waiter rather than
    # directly by our helper. Follow the blocker chain instead of assuming one edge.
    $deadline=[DateTime]::UtcNow.AddMilliseconds(1200)
    do {
        foreach($call in $Calls) {
            if($call.Task.IsCompleted) { throw 'A request completed before both account-lock waiters were established; check the rebuilt backend and lock timeouts.' }
        }
        $sql="WITH RECURSIVE blocked(pid) AS (SELECT $($Lock.Pid) UNION SELECT a.pid FROM pg_stat_activity a JOIN blocked b ON b.pid=ANY(pg_blocking_pids(a.pid))) SELECT count(*) FROM blocked JOIN pg_stat_activity a USING(pid) WHERE a.application_name='lyra_mmo_backend';"
        $observed=[int](Invoke-MMOPsql $sql 'lyra_mmo_tutorial' $PostgresRoot)
        if($observed -ge $Count) { return }
        Start-Sleep -Milliseconds 15
    } while([DateTime]::UtcNow -lt $deadline)
    throw "Could not establish $Count backend workers waiting on the test account within the backend's 3-second lock timeout."
}
function Invoke-OrderedPair([object]$First,[object]$Second) {
    $lock=Start-FixtureLock
    try {
        $a=Start-MMOCall @First
        Wait-BlockedWorkers $lock 1 @($a)
        $b=Start-MMOCall @Second
        Wait-BlockedWorkers $lock 2 @($a,$b)
    } finally { Stop-FixtureLock $lock }
    # Row-lock wait order is observed before release; no timing-only race assertion.
    return @((Complete-MMOCall $a),(Complete-MMOCall $b))
}
function Login-Fixture {
    $response=Call-MMO '/v1/auth/login' @{username=$fixtureUser;password=$fixturePassword}
    if($response.Status -ne 200) { throw "Fixture login failed (HTTP $($response.Status))." }
    return $response.Json.token
}

try {
    Assert-Concurrency ((Call-MMO '/health' $null '' 'GET').Status -eq 200) 'database-backed health before concurrency tests'
    $registered=Call-MMO '/v1/auth/register' @{username=$fixtureUser;password=$fixturePassword}
    Assert-Concurrency ($registered.Status -eq 201) 'create isolated concurrency account'
    $accountId=$registered.Json.accountId
    $token=Login-Fixture
    $character=Call-MMO '/v1/characters' @{name='Concurrency fixture'} $token
    Assert-Concurrency ($character.Status -eq 201) 'create owned concurrency character'
    $characterId=$character.Json.id
    $joinBody=@{characterId=$characterId;serverId='local-1'}

    foreach($scenario in @(
        @{Name='ticket issuance';Request=@{Path='/v1/join-ticket';Body=$joinBody;Method='POST'}},
        @{Name='character creation';Request=@{Path='/v1/characters';Body=@{name='Must not be created'};Method='POST'}},
        @{Name='character listing';Request=@{Path='/v1/characters';Body=$null;Method='GET'}}
    )) {
        $token=Login-Fixture
        $second=$scenario.Request.Clone(); $second.Token=$token
        $responses=Invoke-OrderedPair @{Path='/v1/auth/logout';Body=@{};Token=$token} $second
        Assert-Concurrency ($responses[0].Status -eq 200 -and $responses[1].Status -eq 401) "logout wins over already-waiting $($scenario.Name)"
        Assert-Concurrency ((Invoke-MMOPsql "SELECT count(*) FROM join_tickets WHERE account_id='$accountId'::uuid AND consumed_at IS NULL;" 'lyra_mmo_tutorial' $PostgresRoot) -eq '0') "no post-logout ticket after $($scenario.Name)"
    }

    $token=Login-Fixture
    $oldTicket=Call-MMO '/v1/join-ticket' $joinBody $token
    Assert-Concurrency ($oldTicket.Status -eq 200) 'issue ticket before replacement login'
    $token=Login-Fixture
    $rejected=Call-MMO '/v1/server/consume-ticket' @{ticket=$oldTicket.Json.ticket;serverId='local-1'} $env:MMO_SERVER_SECRET
    Assert-Concurrency ($rejected.Status -eq 401) 'replacement login revokes old unconsumed ticket'

    $oldTicket=Call-MMO '/v1/join-ticket' $joinBody $token
    $responses=Invoke-OrderedPair @{Path='/v1/join-ticket';Body=$joinBody;Token=$token} @{Path='/v1/server/consume-ticket';Body=@{ticket=$oldTicket.Json.ticket;serverId='local-1'};Token=$env:MMO_SERVER_SECRET}
    Assert-Concurrency ($responses[0].Status -eq 200 -and $responses[1].Status -eq 401) 'issuance before redemption revokes old ticket without a lock cycle'
    Assert-Concurrency ((Invoke-MMOPsql "SELECT count(*) FROM game_leases WHERE account_id='$accountId'::uuid;" 'lyra_mmo_tutorial' $PostgresRoot) -eq '0') 'rejected old ticket created no lease'

    $newTicket=$responses[0].Json.ticket
    $responses=Invoke-OrderedPair @{Path='/v1/server/consume-ticket';Body=@{ticket=$newTicket;serverId='local-1'};Token=$env:MMO_SERVER_SECRET} @{Path='/v1/join-ticket';Body=$joinBody;Token=$token}
    Assert-Concurrency ($responses[0].Status -eq 200 -and $responses[1].Status -eq 409) 'redemption before issuance creates exactly one active account lease'
    Assert-Concurrency ((Invoke-MMOPsql "SELECT count(*) FROM game_leases WHERE account_id='$accountId'::uuid;" 'lyra_mmo_tutorial' $PostgresRoot) -eq '1') 'one lease after concurrent redemption and issuance'
    $released=Call-MMO '/v1/server/release' @{characterId=$characterId;leaseToken=$responses[0].Json.leaseToken;serverId='local-1'} $env:MMO_SERVER_SECRET
    Assert-Concurrency ($released.Status -eq 200) 'release concurrency fixture lease'

    $oldTicket=Call-MMO '/v1/join-ticket' $joinBody $token
    $responses=Invoke-OrderedPair @{Path='/v1/auth/logout';Body=@{};Token=$token} @{Path='/v1/server/consume-ticket';Body=@{ticket=$oldTicket.Json.ticket;serverId='local-1'};Token=$env:MMO_SERVER_SECRET}
    Assert-Concurrency ($responses[0].Status -eq 200 -and $responses[1].Status -eq 401) 'ticket redemption rechecks revocation after waiting for logout'
} catch {
    $failure=$_.Exception.Message
    throw
} finally {
    # Dispose the test HTTP client, then remove only this randomly named account.
    $http.CancelPendingRequests(); $http.Dispose()
    foreach($call in $pending) { $call.Request.Dispose() }
    $pending.Clear()
    $report=[pscustomobject]@{at=(Get-Date).ToUniversalTime().ToString('o');baseUrl=$BaseUrl;checks=$checks;failure=$failure;method='Observed account-row lock waiters; no PostgreSQL global changes'}
    $local=Join-Path $project '.local'; New-Item -ItemType Directory -Path $local -Force | Out-Null
    $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $local 'backend-concurrency-results.json') -Encoding utf8
    Invoke-MMOPsql "DELETE FROM accounts WHERE username='$fixtureUser';" 'lyra_mmo_tutorial' $PostgresRoot | Out-Null
}
Write-Host "$($checks.Count) concurrency checks passed. Report: .local/backend-concurrency-results.json"
