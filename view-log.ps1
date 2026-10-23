$logFile = "C:\Windows\Temp\XboxPINController.log"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Visor de Logs de XboxPINController" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

if (-not (Test-Path $logFile)) {
    Write-Host "No se encontro el archivo de log en '$logFile'." -ForegroundColor Yellow
    Write-Host "Asegurate de haber habilitado los logs con 'enable-logging.ps1'." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Presiona cualquier tecla para salir..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit
}

Write-Host "Mostrando las ultimas lineas de '$logFile':" -ForegroundColor Gray
Write-Host "--------------------------------------------------" -ForegroundColor Gray
Get-Content -Path $logFile -Tail 40
Write-Host "--------------------------------------------------" -ForegroundColor Gray
Write-Host ""
Write-Host "Presiona cualquier tecla para salir..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
