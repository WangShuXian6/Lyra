$ErrorActionPreference = 'Stop'
function Get-MMOProjectRoot {
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../examples/MMORPG'))
}
function Get-MMOSecretPath { Join-Path (Get-MMOProjectRoot) '.local/backend.secrets.clixml' }
function ConvertFrom-MMOSecure([Security.SecureString]$Value) {
    [Net.NetworkCredential]::new('', $Value).Password
}
function Import-MMOEnvironment {
    $secretPath = Get-MMOSecretPath
    if (-not (Test-Path -LiteralPath $secretPath)) { throw 'Run Initialize-Database.ps1 once first.' }
    $secrets = Import-Clixml -LiteralPath $secretPath
    $env:PGHOST = '127.0.0.1'; $env:PGPORT = '5432'; $env:PGDATABASE = 'lyra_mmo_tutorial'; $env:PGUSER = 'lyra_mmo_service'
    $env:PGPASSWORD = ConvertFrom-MMOSecure $secrets.DatabasePassword
    $env:PGSSLMODE = 'prefer'
    $env:MMO_SERVER_SECRET = ConvertFrom-MMOSecure $secrets.ServerSecret
    $env:MMO_SERVER_ID = 'local-1'; $env:MMO_GAME_ADDRESS = '127.0.0.1:7777'
}
function Import-MMOGameServerEnvironment {
    # A Dedicated Server needs only its service identity, never PostgreSQL credentials.
    $secrets = Import-Clixml -LiteralPath (Get-MMOSecretPath)
    $env:MMO_SERVER_SECRET = ConvertFrom-MMOSecure $secrets.ServerSecret
    $env:MMO_SERVER_ID = 'local-1'; $env:MMO_GAME_ADDRESS = '127.0.0.1:7777'
}
function Invoke-MMOPsql([string]$Sql, [string]$Database = 'lyra_mmo_tutorial', [string]$PostgresRoot = 'D:/program/1-web/PostgreSQL/18') {
    # Feed SQL through stdin; credentials and generated passwords never become command arguments.
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = Join-Path $PostgresRoot 'bin/psql.exe'
    $psi.Arguments = "-X -q -t -A -v ON_ERROR_STOP=1 -d $Database"
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($psi)
    $p.StandardInput.WriteLine($Sql); $p.StandardInput.Close()
    $output = $p.StandardOutput.ReadToEnd(); $errorOutput = $p.StandardError.ReadToEnd(); $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "PostgreSQL command failed: $errorOutput" }
    $output.Trim()
}
