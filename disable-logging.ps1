# Requires Administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Elevando permisos de Administrador..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Deshabilitar Registro de Logs (XboxPINController)" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$regPath = "HKLM:\SOFTWARE\XboxPINController"
if (Test-Path $regPath) {
    Set-ItemProperty -Path $regPath -Name "EnableLogging" -Value 0 -Type DWord
}

# Eliminar archivo flag
Remove-Item -Path "C:\Windows\Temp\XboxPIN_EnableLogging.txt" -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host " [OK] Registro de logs DESHABILITADO con exito." -ForegroundColor Green
Write-Host " No se escribiran mas eventos en disco ni OutputDebugString." -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Presiona cualquier tecla para salir..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
