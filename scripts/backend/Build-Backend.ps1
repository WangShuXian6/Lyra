param([string]$EngineRoot='E:/UnrealEngine', [string]$ProjectRoot='')
. (Join-Path $PSScriptRoot 'Common.ps1')
if (-not $ProjectRoot) { $ProjectRoot=Get-MMOProjectRoot }
$editors=Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'UnrealEditor.exe' -and $_.ExecutablePath -like "$EngineRoot*" }
if ($editors) { throw 'Save and normally close editors using this engine before a full UBT build. Live Coding may block Program builds too.' }
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') MMOBackendService Win64 Development "-Project=$ProjectRoot/MMORPG.uproject" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "UBT backend build failed ($LASTEXITCODE)." }
