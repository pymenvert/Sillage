# Installs the Sillage engine as a Windows service with automatic recovery.
# Run from an elevated PowerShell:
#   .\install-service.ps1 -BinaryPath "C:\Sillage\sillage-engine.exe" -ConfigPath "C:\ProgramData\Sillage\project.json"
param(
    [Parameter(Mandatory = $true)][string]$BinaryPath,
    [Parameter(Mandatory = $true)][string]$ConfigPath
)

$name = "Sillage"
$binWithArgs = "`"$BinaryPath`" --config `"$ConfigPath`""

if (Get-Service -Name $name -ErrorAction SilentlyContinue) {
    Write-Host "Service exists, removing first..."
    Stop-Service $name -ErrorAction SilentlyContinue
    sc.exe delete $name | Out-Null
    Start-Sleep -Seconds 1
}

New-Service -Name $name -BinaryPathName $binWithArgs `
    -DisplayName "Sillage tracking engine" `
    -Description "Multi-LiDAR people tracking for immersive spaces" `
    -StartupType Automatic

# Recovery: restart on failure (5s, 5s, then 30s; reset counter daily).
sc.exe failure $name reset= 86400 actions= restart/5000/restart/5000/restart/30000 | Out-Null
Start-Service $name
Write-Host "Sillage service installed and started. UI: http://localhost:8080"
