# PowerShell script to kill web server on port 8000
$proc = Get-NetTCPConnection -LocalPort 8000 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty OwningProcess -First 1

if ($proc) {
    Write-Host "Killing web server (PID: $proc) on port 8000..."
    Stop-Process -Id $proc -Force
    Write-Host "Web server killed."
} else {
    Write-Host "No process found running on port 8000."
}
