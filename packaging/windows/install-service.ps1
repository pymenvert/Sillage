# Installs the Sillage engine as a Windows service with automatic recovery.
# Run from an ELEVATED PowerShell:
#   .\install-service.ps1 -BinaryPath "C:\Program Files\Sillage\sillage-engine.exe" `
#                         -ConfigPath "C:\ProgramData\Sillage\project.json"
#
# The engine must be started with --service: that is what makes it register a
# Service Control Manager dispatcher. Without it the SCM gets no answer and
# kills the service after 30 s with error 1053.
param(
    [Parameter(Mandatory = $true)][string]$BinaryPath,
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [int]$HttpPort = 8080,
    [string]$HttpBind = "127.0.0.1"
)

$ErrorActionPreference = "Stop"
$name = "Sillage"

# Fail early and clearly rather than half-installing.
if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "This script must be run from an elevated PowerShell (Run as administrator)."
}
if (-not (Test-Path -LiteralPath $BinaryPath)) { throw "Binary not found: $BinaryPath" }
if (-not (Test-Path -LiteralPath $ConfigPath)) { throw "Config not found: $ConfigPath" }

$binWithArgs = "`"$BinaryPath`" --service --config `"$ConfigPath`" --http-port $HttpPort --http-bind $HttpBind"

if (Get-Service -Name $name -ErrorAction SilentlyContinue) {
    Write-Host "Existing service found, removing it first..."
    try { Stop-Service $name -Force -ErrorAction Stop } catch {}
    sc.exe delete $name | Out-Null
    Start-Sleep -Seconds 2
}

New-Service -Name $name -BinaryPathName $binWithArgs `
    -DisplayName "Sillage tracking engine" `
    -Description "Multi-LiDAR people tracking for immersive spaces" `
    -StartupType Automatic | Out-Null

# Recovery: restart on failure (5s, 5s, then 30s; reset the counter daily).
sc.exe failure $name reset= 86400 actions= restart/5000/restart/5000/restart/30000 | Out-Null

Start-Service $name

# Do not trust Start-Service alone: confirm the service actually reached
# Running and stayed there, otherwise the technician must hear about it now —
# not discover a dead service on show day.
$deadline = (Get-Date).AddSeconds(20)
do {
    Start-Sleep -Milliseconds 500
    $svc = Get-Service -Name $name
} while ($svc.Status -ne "Running" -and (Get-Date) -lt $deadline)

if ($svc.Status -ne "Running") {
    Write-Error @"
The Sillage service was registered but did not start (state: $($svc.Status)).
Check the Windows event log:  Get-EventLog -LogName System -Source 'Service Control Manager' -Newest 5
Command line registered:      $binWithArgs
"@
    exit 1
}

# A running service is not necessarily a serving one: prove the API answers.
$healthy = $false
$deadline = (Get-Date).AddSeconds(15)
do {
    Start-Sleep -Milliseconds 500
    try {
        $r = Invoke-WebRequest -Uri "http://127.0.0.1:$HttpPort/api/status" -UseBasicParsing -TimeoutSec 2
        if ($r.StatusCode -eq 200) { $healthy = $true }
    } catch {}
} while (-not $healthy -and (Get-Date) -lt $deadline)

if (-not $healthy) {
    Write-Warning "Service is Running but http://127.0.0.1:$HttpPort/api/status did not answer. Check the port and the firewall."
    exit 1
}

Write-Host "Sillage service installed, started and answering. UI: http://127.0.0.1:$HttpPort"
