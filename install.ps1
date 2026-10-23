# Requires Administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Elevando permisos de Administrador..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Instalador de Xbox-PIN-Controller v2.0.0.0" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# La DLL puede venir junto al script (descargada de una release) o de una compilacion local.
$dllSource = @(
    (Join-Path $scriptDir "XboxPINController.dll"),
    (Join-Path $scriptDir "x64\Release\XboxPINController.dll")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
$dllDest = "C:\Windows\System32\XboxPINController.dll"
$clsid = "{5fd3d285-0dd9-4362-8855-e0abaacd4af6}"

if (-not $dllSource) {
    Write-Host "Error: No se encontro XboxPINController.dll junto a este script ni en x64\Release." -ForegroundColor Red
    Write-Host "Descargala de la pagina de Releases del repositorio, o compila la solucion (Release x64) y vuelve a ejecutar." -ForegroundColor Red
    Pause
    exit 1
}
Write-Host "DLL de origen: $dllSource" -ForegroundColor Gray
Write-Host "SHA-256:       $((Get-FileHash $dllSource -Algorithm SHA256).Hash)" -ForegroundColor Gray

# 1. Limpieza de rastros anteriores
Write-Host "[1/5] Eliminando rastros de instalaciones anteriores..." -ForegroundColor Gray
Remove-Item -Path "C:\Windows\System32\combo.png" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "C:\Windows\System32\SampleV2CredentialProvider.dll" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCR:\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "C:\Windows\Temp\XboxPINController.log" -Force -ErrorAction SilentlyContinue

# 2. Copia de la nueva DLL
Write-Host "[2/5] Copiando XboxPINController.dll (v2.0.0.0) a System32..." -ForegroundColor Gray
Copy-Item -Path $dllSource -Destination $dllDest -Force

# 3. Registro COM y Credential Provider
Write-Host "[3/5] Registrando Credential Provider en el Registro de Windows..." -ForegroundColor Gray
$credProvKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid"
New-Item -Path $credProvKey -Force | Out-Null
Set-ItemProperty -Path $credProvKey -Name "(default)" -Value "XboxPINController"

$clsidKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
New-Item -Path $clsidKey -Force | Out-Null
Set-ItemProperty -Path $clsidKey -Name "(default)" -Value "XboxPINController"

$inprocKey = "$clsidKey\InprocServer32"
New-Item -Path $inprocKey -Force | Out-Null
Set-ItemProperty -Path $inprocKey -Name "(default)" -Value "XboxPINController.dll"
Set-ItemProperty -Path $inprocKey -Name "ThreadingModel" -Value "Apartment"

# 4. Compatibilidad Windows 11 (Deshabilitar navegacion nativa en Winlogon)
Write-Host "[4/5] Configurando compatibilidad para Windows 11..." -ForegroundColor Gray
$controllerMappingKey = "HKLM:\SOFTWARE\Microsoft\Input\Settings\ControllerProcessor\ControllerToVKMapping"
if (!(Test-Path $controllerMappingKey)) {
    New-Item -Path $controllerMappingKey -Force | Out-Null
}
Set-ItemProperty -Path $controllerMappingKey -Name "Enabled" -Value 0 -Type DWord

# 5. HidHide: permitir que LogonUI vea el mando fisico.
# Handheld Companion deja HidHide ocultando el mando; al arrancar el equipo HC aun no corre
# (no existe su mando virtual), asi que sin esto la pantalla de inicio no ve ningun mando.
# Se usa HidHideCLI porque aplica el cambio en el driver en vivo (editar el registro no basta).
Write-Host "[5/5] Configurando HidHide para LogonUI..." -ForegroundColor Gray
$hidHideCli = "C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe"
$logonUi = "C:\Windows\System32\LogonUI.exe"
if (Test-Path $hidHideCli) {
    $registered = & $hidHideCli --app-list | Where-Object { $_ -like "*LogonUI.exe*" }
    if ($registered) {
        Write-Host "      LogonUI.exe ya estaba en la lista blanca de HidHide." -ForegroundColor Gray
    } else {
        & $hidHideCli --app-reg $logonUi | Out-Null
        if (& $hidHideCli --app-list | Where-Object { $_ -like "*LogonUI.exe*" }) {
            Write-Host "      LogonUI.exe agregado a la lista blanca de HidHide." -ForegroundColor Gray
        } else {
            Write-Host "      [AVISO] No se pudo agregar LogonUI.exe a la lista blanca de HidHide." -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "      HidHide no esta instalado, nada que configurar." -ForegroundColor Gray
}

$versionInfo = (Get-Item $dllDest).VersionInfo
Write-Host ""
Write-Host "==================================================" -ForegroundColor Green
Write-Host " [OK] Instalacion completada con exito!" -ForegroundColor Green
Write-Host " Version instalada: $($versionInfo.FileVersion)" -ForegroundColor Green
Write-Host " Ubicacion: $dllDest" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Presiona cualquier tecla para salir..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
