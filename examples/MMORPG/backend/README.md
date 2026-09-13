# UE MMORPG Backend

Independent UE Program, Core/Json/HTTPServer only. Runtime gameplay modules link MMOContracts and HTTP; libpq/libsodium only enter this Program.

From the LyraDoc repository in PowerShell 7:

```powershell
./scripts/backend/Prepare-Dependencies.ps1
./scripts/backend/Initialize-Database.ps1 # prompts for local postgres admin password once
./scripts/backend/Build-Backend.ps1
./scripts/backend/Start-Backend.ps1
./scripts/backend/Test-Backend.ps1
./scripts/backend/Test-Backend-Faults.ps1
./scripts/backend/Stop-Backend.ps1
```

DB initialization creates `lyra_mmo_tutorial` and a non-superuser `lyra_mmo_service`, applies numbered SQL migrations, and stores randomly generated credentials with Windows DPAPI in ignored `.local/`. No credentials are in this repository. Build requires a source UE engine and normally closed Live Coding editors. Dependencies are fetched into ignored `.deps/`; the libsodium release is SHA-256 pinned by the preparation script. PostgreSQL headers/import libraries come from the local installation.

The local script starts a hidden process, polls `/health`, and stops it through a sentinel file so accepted transactions drain before exit. Production: run as a managed service under a dedicated OS identity, terminate TLS at a reverse proxy, apply auth rate limits/request size limits and log redaction there. The built-in listener is bound to loopback. One configured game server identity is supported in this lesson; adding a cluster requires a per-server credential registry.

See the published Chinese `10-backend` chapters and `openapi.json` for the complete protocol. Validation status is recorded in `validation.json`; source existence is not a successful runtime test.
