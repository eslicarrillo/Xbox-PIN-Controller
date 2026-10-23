# Requires Administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Elevando permisos de Administrador..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Desinstalador de Xbox-PIN-Controller" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$clsid = "{5fd3d285-0dd9-4362-8855-e0abaacd4af6}"

# 1. Eliminar claves de registro
Write-Host "[1/4] Eliminando claves de registro COM, Credential Provider y Logs..." -ForegroundColor Gray
Remove-Item -Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCR:\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKLM:\SOFTWARE\XboxPINController" -Recurse -Force -ErrorAction SilentlyContinue

# 2. Eliminar archivos en System32 y Temporales
Write-Host "[2/4] Eliminando archivos en System32 y logs..." -ForegroundColor Gray
Remove-Item -Path "C:\Windows\System32\XboxPINController.dll" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "C:\Windows\System32\combo.png" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "C:\Windows\System32\SampleV2CredentialProvider.dll" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "C:\Windows\Temp\XboxPINController.log" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "C:\Windows\Temp\XboxPIN_EnableLogging.txt" -Force -ErrorAction SilentlyContinue

# 3. Restaurar configuracion nativa de Windows 11
Write-Host "[3/4] Restaurando configuracion del sistema..." -ForegroundColor Gray
$controllerMappingKey = "HKLM:\SOFTWARE\Microsoft\Input\Settings\ControllerProcessor\ControllerToVKMapping"
if (Test-Path $controllerMappingKey) {
    Set-ItemProperty -Path $controllerMappingKey -Name "Enabled" -Value 1 -Type DWord -ErrorAction SilentlyContinue
}

# 4. Quitar LogonUI de la lista blanca de HidHide
Write-Host "[4/4] Quitando LogonUI.exe de la lista blanca de HidHide..." -ForegroundColor Gray
$hidHideCli = "C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe"
if (Test-Path $hidHideCli) {
    & $hidHideCli --app-unreg "C:\Windows\System32\LogonUI.exe" | Out-Null
}

Write-Host ""
Write-Host "==================================================" -ForegroundColor Green
Write-Host " [OK] Desinstalacion completada con exito!" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Presiona cualquier tecla para salir..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
