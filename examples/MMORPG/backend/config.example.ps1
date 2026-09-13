# Documentation only: copy values into your process/service environment or run Initialize-Database.ps1.
# Never commit a populated config. The local helper uses ignored .local/backend.secrets.clixml (Windows DPAPI).
$env:PGHOST='127.0.0.1'
$env:PGPORT='5432'
$env:PGDATABASE='lyra_mmo_tutorial'
$env:PGUSER='lyra_mmo_service'
$env:PGPASSWORD='<dedicated-role-password>'
$env:PGSSLMODE='verify-full' # local helper uses prefer; remote deployments require trusted TLS certificates
$env:MMO_SERVER_SECRET='<at-least-32-random-characters>'
$env:MMO_SERVER_ID='local-1'
$env:MMO_GAME_ADDRESS='127.0.0.1:7777'
