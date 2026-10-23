# Pendientes: Xbox PIN Controller

**Última actualización:** 2026-10-23

## Estado actual

El síntoma original (arranque en frío sin dígitos; pantalla de bloqueo que no despertaba con el mando) está resuelto y probado en el Legion Go: arranque en frío y Win+L, con y sin Handheld Companion. Ver `SOLUCION_Y_ARQUITECTURA.md`.

| Elemento | Estado |
|---|---|
| `Gamepad.cpp` reescrito (3 motores fusionados, flanco de subida, hilo activo, inyección en el escritorio de entrada) | Hecho, compilado, instalado y probado |
| `install.ps1` / `uninstall.ps1`: `LogonUI.exe` en la lista blanca de HidHide | Hecho y verificado |
| Imagen del overlay con A (Enter) y B (Borrar) | Hecho, instalada (el original está en el historial de git) |
| Build Tools 2022 + SDK 10.0.26100.0 | Reinstalados el 2026-10-23 |
| Log | **Sigue activo con `LogKeys=1`** (guarda el PIN de prueba). Ver tarea 1. |

## Tareas

### 1. Apagar el log de depuración
- Ejecutar `disable-logging.ps1` como administrador.
- Quitar `LogKeys` de `HKLM\SOFTWARE\XboxPINController` (o ponerlo en 0).
- Borrar `C:\Windows\Temp\XboxPINController.log` y `C:\Windows\Temp\XboxPIN_EnableLogging.txt`.

### 2. Pruebas que faltan
Solo se probaron arranque en frío y Win+L. Falta, con el log activo:
- Volver de suspensión / hibernación.
- Cambio de modo del Legion (`61EB` ↔ `61ED`) estando en la pantalla de bloqueo.
- **Modo `61ED` puro, sin HC:** pulsar A, B, X, Y, LB y RB y revisar las líneas `Boton crudo N` para confirmar el orden de botones por DirectInput (hoy solo se vieron A y RB).
- Varias credenciales / varios usuarios en la pantalla de inicio.
- Que HC no quite `LogonUI.exe` de la lista blanca de HidHide (`HidHideCLI --app-list`). Si lo pisara: tarea programada al arranque que ejecute `HidHideCLI --app-reg` o `--cloak-off`.

### 3. Mejoras pendientes de decidir
- **Leyenda en la imagen:** añadir "Pulsa cualquier botón para despertar la pantalla de bloqueo" (la primera pulsación solo despierta).
- **Coherencia de mapeos:** Start y L3 (→ `0`) y R3 (→ `9`) existen solo en XInput. Quitarlos o añadirlos a los otros motores.
- **Tile sin campo de texto:** mover la captura a `CProvider` y no mostrar tile propio, para que no pueda quedar seleccionado.
- **Log:** moverlo a una carpeta solo para administradores y eliminar el archivo bandera de `Windows\Temp` (cualquiera con acceso a esa carpeta puede activarlo).
- **Overlay:** el arrastre con el ratón no funciona (`WS_EX_TRANSPARENT` deja pasar los clics) y el PNG se decodifica en cada `WM_PAINT`.
- **`uninstall.ps1`** pone `ControllerToVKMapping=1` a la fuerza en vez de restaurar el valor previo.

## Notas de entorno (Legion Go, 2026-10)

- Handheld Companion 1.3.3.1 con `RunAtStartup`, `HIDcloakonconnect=True` y HidHide en `--cloak-on`.
- Lista blanca de HidHide: HC, `HidHideCLI.exe` y `LogonUI.exe` (agregado por `install.ps1`).
- `GameInputRedistService` sigue en Automatic / Running; no se ha comprobado que influya.
