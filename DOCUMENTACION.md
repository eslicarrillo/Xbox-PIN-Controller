# 🎮 Documentación Técnica: Xbox-PIN-Controller

## 1. Descripción General

**Xbox-PIN-Controller** es un módulo nativo en C++ compilado como biblioteca de vínculos dinámicos (DLL) que implementa las interfaces de **Windows Credential Provider** (`ICredentialProvider`, `ICredentialProviderCredential2`, `ICredentialProviderCredentialWithFieldOptions`).

Permite utilizar cualquier mando de Xbox (o compatible con XInput) para introducir el código PIN de Windows Hello en la pantalla de bloqueo o inicio de sesión de Windows 10 y Windows 11, emulando la experiencia de autenticación de las consolas Xbox.

* **Versión Actual:** `2.0.0.0`
* **Arquitectura:** `x64`
* **Archivo Binario:** `XboxPINController.dll`

---

## 2. Registro de Cambios (Changelog v2.0.0.0)

### 🚀 Nuevas Funcionalidades y Mejoras
* **Sistema de Diagnóstico y Logging Dinámico:**
  * Registro de eventos detallado configurable en caliente (sin necesidad de recompilar ni reiniciar).
  * Control mediante Registro de Windows (`HKLM\SOFTWARE\XboxPINController\EnableLogging`) o archivo flag en `Temp`.
  * Doble salida: archivo `C:\Windows\Temp\XboxPINController.log` y captura en vivo mediante `OutputDebugString` (compatible con *Sysinternals DebugView*).
  * Scripts de gestión incluidos: [`enable-logging.ps1`](enable-logging.ps1), [`disable-logging.ps1`](disable-logging.ps1) y [`view-log.ps1`](view-log.ps1).
* **Soporte Multi-Mando Concurrente (4 Slots):**
  * Sondeo simultáneo de los slots XInput `0..3`.
  * Seguimiento de estado y control de rebote (*debounce* de 300 ms) independiente para cada mando.
  * Compatibilidad con mandos USB, adaptadores inalámbricos Xbox y Bluetooth, así como consolas portátiles (ASUS ROG Ally, Lenovo Legion Go, etc.) con mandos integrados y externos al mismo tiempo.
* **Recurso de Imagen Embebido (`IDR_COMBO_PNG`):**
  * La imagen guía de combinaciones `combo.png` ahora viene **incorporada dentro del binario de la DLL**.
  * No requiere copiar archivos `.png` sueltos a `System32`. Mantiene *fallback* a disco si se desea personalizar.
* **Control de Versiones Semántico (`version.h` / `resources.rc`):**
  * Integración de `VS_VERSION_INFO` en la DLL con información formal de versión `2.0.0.0`, derechos de autor y metadatos del producto.
* **Scripts Automatizados de Instalación y Desinstalación:**
  * Inclusión de [`install.ps1`](install.ps1) / [`install.ps1`](install.ps1) y [`uninstall.ps1`](uninstall.ps1) / [`uninstall.ps1`](uninstall.ps1) con auto-elevación de permisos de Administrador y limpieza previa automática.

### 🐛 Correcciones de Bugs Críticos
* **Solución de Crash en LogonUI (Null Pointer Dereference):**
  * Se corrigió `CProvider::GetCredentialCount()` y `CProvider::GetCredentialAt()` para validar `_pCredential != nullptr`, evitando congelamientos o pantallas negras en Windows.
* **Seguridad en Concurrencia COM (Thread Safety):**
  * Migración de `AddRef()` y `Release()` en `CProvider` y `CCredential` a `InterlockedIncrement` e `InterlockedDecrement`, erradicando fallos de tipo *Use-After-Free*.
* **Mapeo Riguroso de XInput:**
  * Uso de constantes oficiales de Windows (`XINPUT_GAMEPAD_*`) y descarte estricto de botones no asignados (evitando que el botón `BACK` o clicks de joystick inserten números no deseados).
  * Rediseño seguro de `presskeys()` sin accesos fuera de límites en memoria.
* **Eliminación de Fugas de Memoria:**
  * Destrucción garantizada de `Gdiplus::Image` en todos los flujos de ejecución.
  * Invocación de `GdiplusShutdown` y `UnregisterClass` al descargar la DLL.

### 🔄 Revisión de octubre de 2026 (mismo `2.0.0.0`)
Reescritura de `Gamepad.cpp`; el detalle y las causas están en [`SOLUCION_Y_ARQUITECTURA.md`](SOLUCION_Y_ARQUITECTURA.md). Esta sección reemplaza lo que más abajo se dice del debounce de 300 ms y del sondeo solo XInput.
* **Tres motores fusionados** (DirectInput, Windows.Gaming.Input y XInput) con una sola tecla por pulsación (flanco de subida de la máscara unida). Ya no hay *debounce* temporal por mando.
* **Pantalla de bloqueo:** la entrada va al escritorio `Default`; si `SendInput` es denegado, se reintenta en el escritorio de entrada desde un hilo auxiliar. Así cualquier botón despierta la pantalla de bloqueo.
* **HidHide:** `install.ps1` agrega `LogonUI.exe` a la lista blanca; `uninstall.ps1` lo quita.
* **Robustez:** re-enumeración de DirectInput cada 2,5 s, estados todo en ceros ignorados, cooldown de 60 s para interfaces ilegibles, registro de hilos de captura activos.
* **Privacidad del log:** los dígitos solo se registran con `HKLM\SOFTWARE\XboxPINController\LogKeys = 1`.
* **Imagen del overlay:** añade A = Iniciar sesión y B = Borrar dígito.

---

## 3. Diagrama de Arquitectura y Flujo

```
 ┌────────────────────────────────────────────────────────────┐
 │                  LogonUI / Winlogon                        │
 │  (Muestra interfaz de inicio de sesión en Secure Desktop)   │
 └───────────────────────▲─────────────────────────────┬──────┘
                         │                             │
               Carga DLL │ COM Provider                │ Inyección de Teclas
                         │                             │ (SendInput)
 ┌───────────────────────┴─────────────────────────────▼──────┐
 │               XboxPINController.dll (v2.0.0.0)             │
 │                                                            │
 │  ┌──────────────────────────────────────────────────────┐  │
 │  │ CClassFactory & CProvider (ICredentialProvider)      │  │
 │  │ - Registra el proveedor en Windows LogonUI           │  │
 │  │ - Proporciona el Tile visual y mensaje guía          │  │
 │  │ - Conteo de referencias atómicas (Interlocked)       │  │
 │  └──────────────────────────┬───────────────────────────┘  │
 │                             │ Instancia                    │
 │  ┌──────────────────────────▼───────────────────────────┐  │
 │  │ CCredential (ICredentialProviderCredential2)         │  │
 │  │ - Crea ventana overlay GDI+ con imagen embebida      │  │
 │  │ - Gestión segura del ciclo de vida de GDI+           │  │
 │  │ - Controla ciclo de vida del hilo de captura         │  │
 │  └──────────────────────────┬───────────────────────────┘  │
 │                             │ Inicia hilo                  │
 │  ┌──────────────────────────▼───────────────────────────┐  │
 │  │ XInputController (Gamepad.cpp)                       │  │
 │  │ - Motores: DirectInput + WGI + XInput (fusionados)   │  │
 │  │ - Una tecla por pulsación (flanco de la máscara)     │  │
 │  │ - SendInput; si es denegado, escritorio de entrada   │  │
 │  └──────────────────────────┬───────────────────────────┘  │
 │                             │                              │
 │  ┌──────────────────────────▼───────────────────────────┐  │
 │  │ Logger Module (Logger.cpp / Logger.h)                │  │
 │  │ - Formato: [YYYY-MM-DD HH:MM:SS.mmm] [PID/TID]       │  │
 │  │ - Salida a C:\Windows\Temp\XboxPINController.log    │  │
 │  │ - Salida en vivo vía OutputDebugStringW              │  │
 │  └──────────────────────────────────────────────────────┘  │
 └─────────────────────────────┼──────────────────────────────┘
                               │ XInputGetState()
 ┌─────────────────────────────┴──────────────────────────────┐
 │           Cualquier Mando Xbox Conectado (Slots 0..3)      │
 └────────────────────────────────────────────────────────────┘
```

---

## 4. Esquema de Mapeo Oficial (Xbox PIN Schema)

Las teclas se inyectan como VK con scan code de hardware (`MapVirtualKeyW`), no como caracteres Unicode: la caja del PIN descarta `KEYEVENTF_UNICODE`. La columna de constantes es la de XInput; DirectInput y WGI usan los equivalentes.

| Control en Mando | Constante XInput | Acción / Tecla Inyectada | Función en PIN |
| :--- | :--- | :--- | :--- |
| **D-Pad Arriba** | `XINPUT_GAMEPAD_DPAD_UP` | `VK '1'` | **1** |
| **D-Pad Izquierda** | `XINPUT_GAMEPAD_DPAD_LEFT` | `VK '2'` | **2** |
| **D-Pad Abajo** | `XINPUT_GAMEPAD_DPAD_DOWN` | `VK '3'` | **3** |
| **D-Pad Derecha** | `XINPUT_GAMEPAD_DPAD_RIGHT` | `VK '4'` | **4** |
| **Gatillo Izquierdo (LT)** | `bLeftTrigger > THRESHOLD` | `VK '5'` | **5** |
| **Gatillo Derecho (RT)** | `bRightTrigger > THRESHOLD` | `VK '6'` | **6** |
| **LB (Left Bumper)** | `XINPUT_GAMEPAD_LEFT_SHOULDER` | `VK '7'` | **7** |
| **RB (Right Bumper)** | `XINPUT_GAMEPAD_RIGHT_SHOULDER` | `VK '8'` | **8** |
| **Botón X** | `XINPUT_GAMEPAD_X` | `VK '0'` | **0** |
| **Botón Y** | `XINPUT_GAMEPAD_Y` | `VK '9'` | **9** |
| **Start / L3** (solo XInput) | `XINPUT_GAMEPAD_START` / `LEFT_THUMB` | `VK '0'` | **0** (alternativo, no existe en DirectInput/WGI) |
| **R3** (solo XInput) | `XINPUT_GAMEPAD_RIGHT_THUMB` | `VK '9'` | **9** (alternativo, no existe en DirectInput/WGI) |
| **Botón A** | `XINPUT_GAMEPAD_A` | `VK_RETURN` (Enter) | **Aceptar / Iniciar sesión** |
| **Botón B** | `XINPUT_GAMEPAD_B` | `VK_BACK` (Backspace) | **Borrar último dígito** |

---

## 5. Sistema de Logging y Diagnóstico

### Cómo Activar los Logs
1. Ejecutar [`enable-logging.ps1`](enable-logging.ps1) como **Administrador**.
2. O configurar manualmente en el Registro de Windows:
   * Ruta: `HKLM\SOFTWARE\XboxPINController`
   * Valor: `EnableLogging` = `1` (DWORD)
   * Valor: `LogLevel` = `0` (DWORD: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)
   * Valor opcional: `LogKeys` = `1` (DWORD). Registra qué dígito/botón se pulsó, lo que equivale a registrar el PIN; solo para pruebas con un PIN de prueba. Por defecto los dígitos se registran como "PIN digito".
   * Edita los valores con `Set-ItemProperty`: `New-Item -Force` sobre una clave existente la recrea vacía.

### Cómo Desactivar los Logs
1. Ejecutar [`disable-logging.ps1`](disable-logging.ps1) como **Administrador**.
2. O configurar en el Registro: `EnableLogging` = `0`.

### Cómo Ver los Logs
* **Archivo de log:** Ejecutar [`view-log.ps1`](view-log.ps1) o abrir `C:\Windows\Temp\XboxPINController.log`. Lo escribe SYSTEM, así que hace falta elevar para leerlo.
* **En Vivo:** Abrir **Sysinternals DebugView** (como Administrador con captura global Win32 habilitada).

---

## 6. Instalación, Desinstalación y Compilación

### Opción A: Instalación Automática (Recomendada)
1. Ejecutar [`install.ps1`](install.ps1) (o [`install.ps1`](install.ps1)) como **Administrador**.
2. El script automáticamente:
   * Elimina cualquier rastro de instalaciones previas (`combo.png`, DLLs obsoletas y claves COM huérfanas).
   * Copia `x64\Release\XboxPINController.dll` a `C:\Windows\System32\`.
   * Registra el Credential Provider en el Registro de Windows.
   * Configura la compatibilidad para Windows 11 (`ControllerToVKMapping -> Enabled = 0`).

### Opción B: Desinstalación Automática
1. Ejecutar [`uninstall.ps1`](uninstall.ps1) (o [`uninstall.ps1`](uninstall.ps1)) como **Administrador**.
2. El script elimina la DLL de `System32` y limpia completamente el Registro de Windows y temporales.

### Compilación Manual:
```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "Xbox Pin Controller.sln" /p:Configuration=Release /p:Platform=x64
```
La DLL compilada se genera en: `C:\Desarrollo\Xbox-PIN-Controller\x64\Release\XboxPINController.dll`
