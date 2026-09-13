param(
    [Security.SecureString]$AdminPassword,
    [string]$PostgresRoot='D:/program/1-web/PostgreSQL/18'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
if(-not $AdminPassword){$AdminPassword=Read-Host 'Local PostgreSQL administrator password (create/drop the new verification database only)' -AsSecureString}
$backupRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$backupSource='lyra_mmo_tutorial'
$backupTarget='lyra_mmo_restore_'+[Guid]::NewGuid().ToString('N').Substring(0,16)
$backupDirectory=Join-Path (Get-MMOProjectRoot) '.local/backups'
$null=New-Item -ItemType Directory -Path $backupDirectory -Force
$backupFile=Join-Path $backupDirectory ($backupTarget+'.dump')
$backupCreated=$false
$backupOID=$null
$backupChecks=[Collections.Generic.List[object]]::new()
$backupReport=[ordered]@{startedAt=[DateTime]::UtcNow.ToString('o');passed=$false;sourceDatabase=$backupSource;restoreDatabase=$backupTarget;checks=$backupChecks;scope='Local logical backup/restore into a newly created database. Source is read-only. No service failover or point-in-time recovery is claimed.'}
$backupPrevious=@{}
foreach($backupKey in @('PGHOST','PGPORT','PGDATABASE','PGUSER','PGPASSWORD','PGSSLMODE','PGCLIENTENCODING','MMO_SERVER_SECRET','MMO_SERVER_ID','MMO_GAME_ADDRESS')){
    $backupPrevious[$backupKey]=[Environment]::GetEnvironmentVariable($backupKey,'Process')
}
function Invoke-BackupTool([string]$Name,[string[]]$Arguments,[switch]$Administrator) {
    $backupStart=[Diagnostics.ProcessStartInfo]::new()
    $backupStart.FileName=Join-Path $PostgresRoot ('bin/'+$Name+'.exe')
    $backupStart.UseShellExecute=$false; $backupStart.CreateNoWindow=$true
    $backupStart.RedirectStandardOutput=$true; $backupStart.RedirectStandardError=$true
    $backupStart.Environment.Remove('MMO_SERVER_SECRET')|Out-Null
    if($Administrator){
        $backupStart.Environment['PGUSER']='postgres'
        $backupStart.Environment['PGPASSWORD']=ConvertFrom-MMOSecure $AdminPassword
    }
    foreach($backupArgument in $Arguments){$backupStart.ArgumentList.Add($backupArgument)}
    $backupProcess=[Diagnostics.Process]::Start($backupStart)
    try {
        $backupOutput=$backupProcess.StandardOutput.ReadToEndAsync()
        $backupError=$backupProcess.StandardError.ReadToEndAsync()
        if(-not $backupProcess.WaitForExit(60000)){$backupProcess.Kill();$backupProcess.WaitForExit();throw "$Name exceeded the local verification timeout"}
        $backupText=$backupOutput.GetAwaiter().GetResult(); $backupErrorText=$backupError.GetAwaiter().GetResult()
        if($backupProcess.ExitCode -ne 0){throw "$Name failed ($($backupProcess.ExitCode)): $backupErrorText"}
        if($backupErrorText.Trim()){throw "$Name emitted diagnostics; inspect locally before recording acceptance: $backupErrorText"}
        return $backupText.Trim()
    } finally {$backupProcess.Dispose();$backupStart.Environment.Remove('PGPASSWORD')|Out-Null}
}
function Get-BackupSnapshot([string]$Database) {
    $backupParts=foreach($backupTable in @('schema_migrations','accounts','sessions','characters','join_tickets','game_leases','save_requests')) {
        "SELECT '$backupTable' AS name, count(*) AS rows, encode(sha256(convert_to(COALESCE(string_agg(row_to_json(r)::text,E'\n' ORDER BY row_to_json(r)::text),''),'UTF8')),'hex') AS digest FROM public.$backupTable r"
    }
    $backupTableSQL='SELECT json_agg(t ORDER BY name) FROM ('+($backupParts -join ' UNION ALL ')+') t;'
    $backupSchemaSQL=@'
SELECT encode(sha256(convert_to(COALESCE(string_agg(value,E'\n' ORDER BY value),''),'UTF8')),'hex') FROM (
 SELECT concat_ws('|','column',table_name,column_name,ordinal_position,data_type,is_nullable,COALESCE(column_default,'')) AS value
 FROM information_schema.columns WHERE table_schema='public'
 UNION ALL SELECT concat_ws('|','constraint',c.relname,co.conname,pg_get_constraintdef(co.oid))
 FROM pg_constraint co JOIN pg_class c ON c.oid=co.conrelid JOIN pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname='public'
 UNION ALL SELECT concat_ws('|','index',tablename,indexname,indexdef) FROM pg_indexes WHERE schemaname='public'
) definitions;
'@
    @{tables=@((Invoke-MMOPsql $backupTableSQL $Database $PostgresRoot)|ConvertFrom-Json);
      schemaDigest=Invoke-MMOPsql $backupSchemaSQL $Database $PostgresRoot;
      migrations=Invoke-MMOPsql 'SELECT string_agg(version::text,'','' ORDER BY version) FROM schema_migrations;' $Database $PostgresRoot;
      encoding=Invoke-MMOPsql 'SHOW server_encoding;' $Database $PostgresRoot}
}
try {
    Import-MMOEnvironment
    $env:PGCLIENTENCODING='UTF8'
    $backupBefore=Get-BackupSnapshot $backupSource
    $backupReport.postgresVersion=Invoke-MMOPsql 'SHOW server_version;' $backupSource $PostgresRoot
    $null=Invoke-BackupTool 'pg_dump' @('--no-password','--format=custom',"--file=$backupFile","--dbname=$backupSource")
    # createdb fails if the random name already exists. Never reuse an existing database.
    $null=Invoke-BackupTool 'createdb' @('--no-password','--maintenance-db=postgres','--template=template0','--encoding=UTF8','--owner=lyra_mmo_service',$backupTarget) -Administrator
    $backupCreated=$true
    $backupOID=Invoke-MMOPsql "SELECT oid FROM pg_database WHERE datname='$backupTarget';" 'postgres' $PostgresRoot
    $null=Invoke-BackupTool 'pg_restore' @('--no-password','--no-owner','--no-acl','--exit-on-error','--single-transaction',"--dbname=$backupTarget",$backupFile)
    $backupAfter=Get-BackupSnapshot $backupSource
    $backupRestored=Get-BackupSnapshot $backupTarget
    # A busy source is not an accepted fixture. pg_dump itself is consistent, but a
    # separately sampled source digest must describe the same revision to compare it.
    if(($backupBefore|ConvertTo-Json -Depth 8 -Compress) -cne ($backupAfter|ConvertTo-Json -Depth 8 -Compress)){
        throw 'Source changed during the test. Let clients finish saving, then rerun with a fresh target.'
    }
    foreach($backupTable in $backupBefore.tables){
        $backupMatch=@($backupRestored.tables|Where-Object name -eq $backupTable.name)
        $backupPass=$backupMatch.Count -eq 1 -and $backupMatch[0].rows -eq $backupTable.rows -and $backupMatch[0].digest -ceq $backupTable.digest
        $backupChecks.Add(@{name=$backupTable.name;rows=$backupTable.rows;sha256=$backupTable.digest;passed=$backupPass})
        if(-not $backupPass){throw "Restored data differs: $($backupTable.name)"}
    }
    foreach($backupField in @('schemaDigest','migrations','encoding')){
        $backupPass=$backupBefore[$backupField] -ceq $backupRestored[$backupField]
        $backupChecks.Add(@{name=$backupField;value=$backupRestored[$backupField];passed=$backupPass})
        if(-not $backupPass){throw "Restored database differs: $backupField"}
    }
    $backupReport.sourceStableDuringTest=$true
    $backupReport.backup=@{path=$backupFile;bytes=(Get-Item -LiteralPath $backupFile).Length;sha256=(Get-FileHash -LiteralPath $backupFile -Algorithm SHA256).Hash.ToLowerInvariant();tracked=$false}
    $backupReport.passed=$true
} catch {
    $backupReport.error=$_.Exception.Message
    throw
} finally {
    try {
        if($backupCreated){
            if($backupTarget -notmatch '^lyra_mmo_restore_[a-f0-9]{16}$' -or $backupTarget -eq $backupSource){throw 'Restore cleanup target guard failed'}
            $backupCurrentOID=Invoke-MMOPsql "SELECT oid FROM pg_database WHERE datname='$backupTarget';" 'postgres' $PostgresRoot
            if(-not $backupOID -or $backupOID -cne $backupCurrentOID){throw 'Restore database identity changed; no cleanup was attempted'}
            $null=Invoke-BackupTool 'dropdb' @('--no-password','--maintenance-db=postgres',$backupTarget) -Administrator
            $backupReport.restoreDatabaseRemoved=$true
        }
    } catch {$backupReport.passed=$false;$backupReport.cleanupError=$_.Exception.Message;throw}
    finally {
        foreach($backupKey in $backupPrevious.Keys){[Environment]::SetEnvironmentVariable($backupKey,$backupPrevious[$backupKey],'Process')}
        $backupReport.completedAt=[DateTime]::UtcNow.ToString('o')
        $backupReport|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $backupRepo 'verification/backend-backup-restore.json') -Encoding utf8
    }
}
Write-Host "Logical backup/restore passed $($backupChecks.Count) checks. Source unchanged; temporary restore database removed; backup retained in ignored .local/backups."
