# 🎮 Xbox-PIN-Controller (fork)

🌐 **Español** · [English](README.md)

Escribe tu **PIN de Windows Hello en la pantalla de bloqueo / inicio de sesión con un mando**, con una distribución de botones inspirada en la pantalla de passkey de la consola Xbox.

Este es un fork de [MikeCoder96/Xbox-PIN-Controller](https://github.com/MikeCoder96/Xbox-PIN-Controller). Se reescribió y se probó en una **Lenovo Legion Go** (controles integrados, con y sin Handheld Companion), en Windows 11 x64. Otros mandos y dispositivos siguen los mismos caminos de código, pero no se han probado.

> ⚠️ **Lee esto antes de instalar.** `XboxPINController.dll` es un Credential Provider: Windows lo carga dentro de `LogonUI.exe` y se ejecuta como **SYSTEM** en la pantalla de inicio de sesión, donde lee tu mando e inyecta las teclas de tu PIN. Instala solo una DLL en la que confíes. Es preferible compilarla tú mismo (ver más abajo) o, como mínimo, verificar el SHA-256 del archivo de la release.

## Qué hace

- **Cualquier botón del mando despierta la pantalla de bloqueo** (la primera pulsación solo la despierta); después la caja del PIN recibe los dígitos.
- Lee el mando con **DirectInput, Windows.Gaming.Input y XInput** a la vez, así que funciona en portátiles cuyo mando integrado es solo HID o solo XInput. Una pulsación física escribe exactamente un dígito, aunque varios motores o dispositivos la vean.
- Funciona en **arranque en frío** (sin Handheld Companion en ejecución) y con **Win+L**.
- Guía en pantalla incrustada con la correspondencia botón → dígito.
- Registro de diagnóstico opcional (desactivado por defecto).

| Mando | Escribe |
|---|---|
| Cruceta ↑ ← ↓ → | `1` `2` `3` `4` |
| LT / RT | `5` / `6` |
| LB / RB | `7` / `8` |
| Y / X | `9` / `0` |
| A | Enter (iniciar sesión) |
| B | Retroceso (borrar) |

## Instalar desde una release (sin compilador)

1. Abre la [última release](../../releases/latest) y descarga **`XboxPINController.dll`**, **`install.ps1`** y **`uninstall.ps1`** en la misma carpeta.
2. Verifica la DLL: `Get-FileHash .\XboxPINController.dll -Algorithm SHA256` debe coincidir con el hash de las notas de la release.
3. Clic derecho en `install.ps1` → *Ejecutar con PowerShell* (pide permisos de administrador por sí solo). Copia la DLL a `C:\Windows\System32`, registra el Credential Provider, aplica el valor de compatibilidad de la navegación con mando de Windows 11 y, **si [HidHide](https://github.com/nefarius/HidHide) está instalado, agrega `LogonUI.exe` a su lista blanca de aplicaciones**, para que la pantalla de inicio de sesión vea el mando (si no, Handheld Companion deja a HidHide ocultando el mando físico).
4. Bloquea el equipo (Win+L) y pulsa un botón del mando.

**Desinstalar:** ejecuta `uninstall.ps1` (administrador). Elimina la DLL y las claves de registro, restaura el valor de compatibilidad y quita `LogonUI.exe` de la lista blanca de HidHide.

Mientras pruebas, ten a mano otra forma de iniciar sesión (teclado, táctil, contraseña). Un PIN erróneo cuenta para el bloqueo de Windows Hello como cualquier otro.

## Compilar desde el código fuente

Requiere Visual Studio Build Tools 2022 (carga de trabajo de C++, toolset `v143`) y Windows SDK 10.0.26100.0:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended"
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "Xbox Pin Controller.sln" /p:Configuration=Release /p:Platform=x64
```

La salida es `x64\Release\XboxPINController.dll`; `install.ps1` la toma de ahí.

## Registro y diagnóstico

El registro está desactivado por defecto.

- `enable-logging.ps1` / `disable-logging.ps1` (administrador) lo activan y desactivan. `view-log.ps1` muestra `C:\Windows\Temp\XboxPINController.log`; lo escribe SYSTEM, así que leerlo requiere administrador.
- La salida en vivo también va a `OutputDebugStringW` (Sysinternals DebugView).
- Por defecto el registro **no** anota qué dígito o botón se pulsó. `HKLM\SOFTWARE\XboxPINController\LogKeys = 1` los anota, lo que equivale a registrar tu PIN: úsalo solo con un PIN de prueba y desactívalo después.

## Documentación

La documentación técnica está en español:

- [`SOLUCION_Y_ARQUITECTURA.md`](SOLUCION_Y_ARQUITECTURA.md): causas raíz (la pantalla de bloqueo recibe la entrada en el escritorio `Default`, HidHide ocultando el mando a LogonUI, la Legion Go cambiando de modo USB, estados todo en ceros de DirectInput), arquitectura y referencia del log.
- [`DOCUMENTACION.md`](DOCUMENTACION.md): documentación técnica original, con una nota de actualización al principio del changelog.
- [`PENDIENTES.md`](PENDIENTES.md): puntos abiertos y escenarios sin probar.

## Créditos y licencia

- Proyecto original: **MikeCoder96 y colaboradores** ([repositorio original](https://github.com/MikeCoder96/Xbox-PIN-Controller)). El esqueleto del credential provider deriva del ejemplo de Windows Credential Provider de Microsoft (Copyright © Microsoft Corporation).
- **Licencia:** el repositorio original no declara ninguna licencia, así que aquí tampoco se concede ninguna: todos los derechos sobre el código original siguen siendo de sus autores. Los cambios de este fork se publican como referencia y para uso personal; si quieres redistribuirlo o reutilizarlo, pídelo al autor original.
- `combo.png` es una ilustración propia de la distribución de botones, inspirada en la pantalla de passkey de la consola Xbox. Xbox es una marca de Microsoft; este proyecto no está afiliado ni respaldado por Microsoft ni Xbox.
