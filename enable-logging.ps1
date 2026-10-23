# Requires Administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Elevando permisos de Administrador..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Habilitar Registro de Logs (XboxPINController)" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$regPath = "HKLM:\SOFTWARE\XboxPINController"
if (-not (Test-Path $regPath)) {
    New-Item -Path $regPath -Force | Out-Null
}

Set-ItemProperty -Path $regPath -Name "EnableLogging" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath -Name "LogLevel" -Value 0 -Type DWord # 0 = DEBUG (Detallado)
Set-ItemProperty -Path $regPath -Name "LogFilePath" -Value "C:\Windows\Temp\XboxPINController.log" -Type String

# Crear archivo flag en Temp como respaldo
New-Item -Path "C:\Windows\Temp\XboxPIN_EnableLogging.txt" -ItemType File -Force | Out-Null

Write-Host ""
Write-Host " [OK] Logs DETALLADOS habilitados correctamente." -ForegroundColor Green
Write-Host " Archivo de salida : C:\Windows\Temp\XboxPINController.log" -ForegroundColor Green
Write-Host " Registro en vivo  : OutputDebugString habilitado (compatible con DebugView)" -ForegroundColor Green
Write-Host " Nivel de log      : DEBUG (Detallado)" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Presiona cualquier tecla para salir..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
