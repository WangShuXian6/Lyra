param([string]$BaseUrl='http://127.0.0.1:8088')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$i18nUser='i18n_'+[Guid]::NewGuid().ToString('N').Substring(0,12)
$i18nPassword='多语言-'+[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(24))
$i18nNames=@('教学旅人·一','Élodie des bois','旅人の記録')
$i18nChecks=[Collections.Generic.List[object]]::new()
$i18nCreated=$false
$i18nReport=@{startedAt=[DateTime]::UtcNow.ToString('o');passed=$false;checks=$i18nChecks;
    scope='Actual UTF-8 HTTP/JSON and PostgreSQL round trip; does not claim glyph rendering, IME input or Unicode normalization.'}
function Invoke-I18n([string]$Route,[object]$Body=$null,[string]$Token='',[string]$Method='POST') {
    $i18nArgs=@{Uri=$BaseUrl+$Route;Method=$Method;TimeoutSec=15;Headers=@{}}
    if($Token){$i18nArgs.Headers.Authorization='Bearer '+$Token}
    if($null -ne $Body){$i18nArgs.Body=$Body|ConvertTo-Json -Depth 8 -Compress;$i18nArgs.ContentType='application/json; charset=utf-8'}
    Invoke-RestMethod @i18nArgs
}
try {
    $null=Invoke-I18n '/v1/auth/register' @{username=$i18nUser;password=$i18nPassword}
    $i18nCreated=$true
    $i18nToken=(Invoke-I18n '/v1/auth/login' @{username=$i18nUser;password=$i18nPassword}).token
    Import-MMOEnvironment
    foreach($i18nName in $i18nNames) {
        $i18nCharacter=Invoke-I18n '/v1/characters' @{name=$i18nName} $i18nToken
        $i18nId=[Guid]::Parse($i18nCharacter.id).ToString()
        $i18nHex=Invoke-MMOPsql "SELECT encode(convert_to(name,'UTF8'),'hex') FROM characters WHERE id='$i18nId'::uuid;"
        $i18nExpected=[Convert]::ToHexString([Text.Encoding]::UTF8.GetBytes($i18nName)).ToLowerInvariant()
        $i18nPass=($i18nCharacter.name -ceq $i18nName) -and ($i18nHex -ceq $i18nExpected)
        $i18nChecks.Add(@{name=$i18nName;createResponseMatches=($i18nCharacter.name -ceq $i18nName);databaseUTF8Matches=($i18nHex -ceq $i18nExpected);passed=$i18nPass})
        if(-not $i18nPass){throw 'Character name changed between HTTP and PostgreSQL.'}
    }
    $null=Invoke-I18n '/v1/auth/logout' @{} $i18nToken
    $i18nToken=(Invoke-I18n '/v1/auth/login' @{username=$i18nUser;password=$i18nPassword}).token
    $i18nList=(Invoke-I18n '/v1/characters' $null $i18nToken 'GET').characters
    $i18nListed=$i18nList.Count -eq $i18nNames.Count
    foreach($i18nName in $i18nNames){$i18nListed=$i18nListed -and (@($i18nList|Where-Object { $_.name -ceq $i18nName }).Count -eq 1)}
    $i18nChecks.Add(@{name='Names survive logout and a new login session';passed=$i18nListed})
    if(-not $i18nListed){throw 'Names changed after a new login session.'}
    $i18nReport.passed=$true
} finally {
    if($i18nCreated){Import-MMOEnvironment;$null=Invoke-MMOPsql "DELETE FROM accounts WHERE username='$i18nUser';"}
    $i18nPassword=$null;$i18nToken=$null
    $i18nReport.completedAt=[DateTime]::UtcNow.ToString('o')
    $i18nReport|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $PSScriptRoot '../../verification/backend-internationalized-data.json') -Encoding utf8
}
Write-Host "Internationalized data: $($i18nChecks.Count) checks passed; disposable fixtures removed."
