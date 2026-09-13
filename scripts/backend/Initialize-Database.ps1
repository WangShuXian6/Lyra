param([Security.SecureString]$AdminPassword, [string]$PostgresRoot = 'D:/program/1-web/PostgreSQL/18')
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $AdminPassword) { $AdminPassword = Read-Host 'PostgreSQL administrator password' -AsSecureString }
$names = @('PGHOST','PGPORT','PGDATABASE','PGUSER','PGPASSWORD','PGSSLMODE')
$previous = @{}; foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:PGHOST='127.0.0.1'; $env:PGPORT='5432'; $env:PGUSER='postgres'; $env:PGPASSWORD=ConvertFrom-MMOSecure $AdminPassword
    $secretPath=Get-MMOSecretPath
    if (Test-Path -LiteralPath $secretPath) { $secrets=Import-Clixml -LiteralPath $secretPath }
    else {
        $roleExists=Invoke-MMOPsql "SELECT 1 FROM pg_roles WHERE rolname='lyra_mmo_service';" 'postgres' $PostgresRoot
        if ($roleExists -eq '1') { throw 'Role exists but local credentials are missing; restore the original encrypted credentials before rerunning.' }
        $randomBytes=New-Object byte[] 32; [Security.Cryptography.RandomNumberGenerator]::Fill($randomBytes)
        $dbPassword=[Convert]::ToHexString($randomBytes)
        [Security.Cryptography.RandomNumberGenerator]::Fill($randomBytes)
        $serverSecret=[Convert]::ToHexString($randomBytes)
        $secrets=[pscustomobject]@{DatabasePassword=(ConvertTo-SecureString $dbPassword -AsPlainText -Force);ServerSecret=(ConvertTo-SecureString $serverSecret -AsPlainText -Force)}
        New-Item -ItemType Directory -Path (Split-Path -Parent $secretPath) -Force | Out-Null
        $secrets | Export-Clixml -LiteralPath $secretPath
    }
    $dbPassword=ConvertFrom-MMOSecure $secrets.DatabasePassword
    if ((Invoke-MMOPsql "SELECT 1 FROM pg_roles WHERE rolname='lyra_mmo_service';" 'postgres' $PostgresRoot) -ne '1') {
        Invoke-MMOPsql "CREATE ROLE lyra_mmo_service LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE PASSWORD '$dbPassword';" 'postgres' $PostgresRoot | Out-Null
    }
    if ((Invoke-MMOPsql "SELECT 1 FROM pg_database WHERE datname='lyra_mmo_tutorial';" 'postgres' $PostgresRoot) -ne '1') {
        Invoke-MMOPsql 'CREATE DATABASE lyra_mmo_tutorial OWNER lyra_mmo_service;' 'postgres' $PostgresRoot | Out-Null
    }
    $env:PGUSER='lyra_mmo_service'; $env:PGPASSWORD=$dbPassword
    $migrationRoot=Join-Path (Get-MMOProjectRoot) 'backend/migrations'
    foreach ($file in (Get-ChildItem -LiteralPath $migrationRoot -Filter '*.sql' | Sort-Object Name)) {
        $version=[int]($file.Name.Split('_')[0])
        $exists=Invoke-MMOPsql "SELECT to_regclass('public.schema_migrations') IS NOT NULL;" 'lyra_mmo_tutorial' $PostgresRoot
        $applied=if ($exists -eq 't') { Invoke-MMOPsql "SELECT 1 FROM schema_migrations WHERE version=$version;" 'lyra_mmo_tutorial' $PostgresRoot } else { '' }
        if ($applied -ne '1') {
            Invoke-MMOPsql (Get-Content -LiteralPath $file.FullName -Raw) 'lyra_mmo_tutorial' $PostgresRoot | Out-Null
            Write-Host "Applied $($file.Name)"
        }
    }
    Write-Host 'Dedicated tutorial database is ready. Credentials stored with Windows user DPAPI in ignored .local/.'
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$previous[$name]) }
    $dbPassword=$null; $serverSecret=$null
}
