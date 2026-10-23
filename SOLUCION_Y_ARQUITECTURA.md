# 🎮 Xbox PIN Controller: Arquitectura, Diagnóstico y Solución Técnica

**Proyecto:** Credential Provider V2 de Windows para escribir el PIN de Windows Hello con un mando  
**Hardware principal:** Lenovo Legion Go (controles integrados) y mandos Xbox / compatibles  
**Sistema operativo:** Windows 11 x64  
**Última actualización:** 2026-10-23 (probado en el equipo: arranque en frío, Win+L, con y sin Handheld Companion)

---

## 1. Resumen ejecutivo

La DLL `XboxPINController.dll` se carga dentro de `LogonUI.exe` (cuenta SYSTEM), lee el mando con tres motores en paralelo (DirectInput, Windows.Gaming.Input y XInput) y escribe el PIN con `SendInput` usando scan codes de hardware.

Comportamiento verificado en el Legion Go:

- **Cualquier botón despierta la pantalla de bloqueo.** Esa primera pulsación solo la despierta; no entra como dígito.
- Cada pulsación escribe **un solo dígito**, también si varios motores o dispositivos ven la misma pulsación.
- Funciona en **arranque en frío** (sin Handheld Companion) y en **Win+L** (con o sin HC).
- Convive con Steam y Handheld Companion.

---

## 2. Qué se descubrió (causas raíz)

### 2.1 La pantalla de bloqueo recibe la entrada en otro escritorio (hallazgo clave)

En la pantalla de bloqueo (antes de que exista la caja del PIN) el escritorio que recibe entrada es **`Default`**, no `Winlogon`, que es donde corre el hilo de captura de `LogonUI`.

- `SendInput` y `GetGUIThreadInfo` devolvían `ERROR_ACCESS_DENIED` (5) hasta que la caja del PIN aparecía por otro medio (teclado, táctil).
- El mando se leía bien; lo que fallaba era la inyección. Esto parecía "carga pero no escribe nada".
- **Solución:** si `SendInput` es denegado, se abre el escritorio de entrada (`OpenInputDesktop`) en un hilo auxiliar de vida corta, se asocia con `SetThreadDesktop` y se inyecta desde ahí. Hace falta un hilo auxiliar porque `SetThreadDesktop` no funciona en hilos con ventanas o hooks (el de captura los tiene por DirectInput/WGI).
- Tras esa primera pulsación, `Winlogon` acepta las teclas con normalidad.

### 2.2 Estado todo en ceros al reconectarse el mando (dígitos fantasma)

Un dispositivo DirectInput recién adquirido, o que cambia de modo, devuelve un estado con todos los ejes en 0 hasta su primer reporte real. Eso se leía como cruceta arriba (POV = 0) y ambos gatillos pulsados (distancia al reposo de 32767), es decir, `1`, `5` y `6` a la vez.

- **Solución:** se ignoran los estados con los seis ejes en 0.

### 2.3 Gatillos en DirectInput

En DirectInput los ejes de los gatillos (Z y Rz) descansan en **32767**, no en 0. El mando tipo Xbox (interfaz `IG_`) comparte un solo eje Z entre LT y RT.

- Los gatillos se calibran con la posición de reposo, que se acepta solo tras 3 lecturas estables y con datos reales; se disparan con un delta de 20000 y se sueltan con 10000.
- Si un gatillo parece apretado más de 5 s, se recalibra.
- En dispositivos tipo Xbox, los gatillos se toman de XInput/WGI y no de DirectInput.

### 2.4 Teclas con scan codes de hardware

La caja del PIN en el escritorio seguro descarta eventos `KEYEVENTF_UNICODE`. Se traduce cada VK a scan code con `MapVirtualKeyW` y se rellena `wScan`.

### 2.5 HidHide oculta el mando a LogonUI

Handheld Companion deja HidHide con `--cloak-on` y una lista blanca que solo contiene HC y `HidHideCLI.exe`. Los dispositivos `VID_17EF&PID_61EB&IG_*` quedan ocultos para todo lo demás, incluido `LogonUI`.

- Editar el registro de HidHide no basta: el driver no lo relee. Hay que usar `HidHideCLI`.
- **Solución:** `install.ps1` agrega `C:\Windows\System32\LogonUI.exe` a la lista blanca con `HidHideCLI --app-reg`; `uninstall.ps1` lo quita.

### 2.6 Modos del Legion Go y mando virtual de HC

- El mando integrado cambia entre `PID_61EB` (con interfaz XInput `xusb22`) y `PID_61ED` (solo HID; únicamente DirectInput lo lee). El cambio ocurre, por ejemplo, al despertar o cuando HC arranca.
- El mando virtual Xbox 360 (`VID_045E&PID_028E`) solo existe mientras HC corre en la sesión de usuario; **no existe en el arranque en frío**.
- Por eso DirectInput debe poder funcionar solo, y se vuelve a enumerar periódicamente.

### 2.7 Defectos del código anterior (ya corregidos)

1. DirectInput enumeraba una sola vez; si el mando no estaba listo, no se buscaba de nuevo.
2. La calibración de gatillos usaba la primera lectura (podía ser todo ceros → '5' en bucle).
3. El "anti-duplicados" de 280 ms repetía el dígito al mantener un botón.
4. RT escribía '5' en mandos Xbox leídos por DirectInput.
5. Estado `static` compartido entre hilos sin protección.
6. Faltaba `Poll()` antes de `GetDeviceState`.
7. Si LogonUI destruía la credencial nueva antes que la vieja, la superviviente se consideraba inactiva y no escribía nada (ver 3.4).

---

## 3. Arquitectura

```
                        LogonUI.exe (SYSTEM, escritorio Winlogon)
                                       │
                         XboxPINController.dll
        CProvider ── CCredential ── XInputController::CaptureInput (hilo)
                                       │
          ┌────────────────────────────┼────────────────────────────┐
          ▼                            ▼                            ▼
   DirectInputEngine           WgiEngine                     XInputEngine
   (re-enumera cada 2,5 s)     (Windows.Gaming.Input)        (xinput1_4/1_3/9_1_0)
          └────────────┬───────────────┴──────────────┬─────────────┘
                       ▼  máscara de acciones lógicas (bits 0-9 dígitos, 10 Enter, 11 Borrar)
              flanco de subida de la máscara unida (una tecla por pulsación)
                       ▼
                 SendKeyStroke
                       ├─ SendInput en el escritorio actual (Winlogon)
                       └─ si ACCESS_DENIED → hilo auxiliar en el escritorio de entrada (Default)
```

### 3.1 Fusión de motores y un solo dígito por pulsación
Cada motor devuelve una máscara de acciones. El bucle (cada 15 ms) une las tres y escribe **solo en el flanco de subida** de la máscara unida. Así no hay autorepetición al mantener un botón ni duplicados entre motores o dispositivos (mando físico más el virtual de HC). Una guarda de 100 ms por acción cubre el caso de un motor que ve un toque corto con retraso.

### 3.2 DirectInput
- Re-enumeración cada 2,5 s (mando que aparece tarde, reanudación, cambio de modo).
- `Poll()` antes de `GetDeviceState`; se descarta un dispositivo que falla durante 1,5 s.
- Una interfaz que **nunca** entregó un estado (algunas colecciones HID del Legion se enumeran como joystick pero no se pueden leer) queda 60 s en cooldown, en lugar de reintentarse en cada re-escaneo.
- Se ignoran los estados con todos los ejes en 0 (ver 2.2).
- Mapeo de botones asumido: 0..5 = A, B, X, Y, LB, RB (igual que el código que funcionó en agosto).

### 3.3 XInput y WGI
XInput prueba los slots vacíos cada 1 s (sondear slots vacíos es lento) y refresca `XInputEnable(TRUE)` cada 1,5 s. WGI cuenta los mandos y aplica histéresis a los gatillos (0,5 / 0,3).

### 3.4 Hilo activo
Solo inyecta teclas el hilo de captura **más reciente que siga en ejecución**. LogonUI puede mantener viva una credencial vieja mientras crea otra, y también destruir la nueva primero; un registro (`s_runningControllers`, protegido por mutex) hace que la superviviente tome el relevo en vez de quedarse muda.

### 3.5 Logging
Ver sección 5. Los dígitos y botones concretos se consideran material del PIN y solo se registran con `LogKeys=1`.

---

## 4. Mapeo de teclas

| Botón del mando | Dígito / acción | VK | Scan code |
|---|---|---|---|
| D-Pad Arriba | `1` | `'1'` | `0x02` |
| D-Pad Izquierda | `2` | `'2'` | `0x03` |
| D-Pad Abajo | `3` | `'3'` | `0x04` |
| D-Pad Derecha | `4` | `'4'` | `0x05` |
| Gatillo Izquierdo (LT) | `5` | `'5'` | `0x06` |
| Gatillo Derecho (RT) | `6` | `'6'` | `0x07` |
| Bumper Izquierdo (LB) | `7` | `'7'` | `0x08` |
| Bumper Derecho (RB) | `8` | `'8'` | `0x09` |
| Botón Y | `9` | `'9'` | `0x0A` |
| Botón X | `0` | `'0'` | `0x0B` |
| Botón A | Iniciar sesión | `VK_RETURN` | `0x1C` |
| Botón B | Borrar dígito | `VK_BACK` | `0x0E` |

Solo por **XInput** existen además Start y L3 (→ `0`) y R3 (→ `9`). DirectInput y WGI no los mapean; es una incoherencia heredada.

La imagen del overlay (`combo.png`, embebida como `IDR_COMBO_PNG`) es una ilustración propia (sin capturas de la consola) que muestra los dígitos con su botón, **A = Iniciar sesión (Enter)**, **B = Borrar dígito** y el aviso de que cualquier botón despierta la pantalla de bloqueo.

---

## 5. Logging y diagnóstico

| Valor en `HKLM\SOFTWARE\XboxPINController` | Efecto |
|---|---|
| `EnableLogging` (DWORD) | `1` activa el log. También se activa si existe `C:\Windows\Temp\XboxPIN_EnableLogging.txt`. |
| `LogLevel` (DWORD) | `0` DEBUG, `1` INFO, `2` WARN, `3` ERROR. |
| `LogFilePath` (cadena) | Ruta del log (por defecto `C:\Windows\Temp\XboxPINController.log`). |
| `LogKeys` (DWORD) | `1` registra **qué dígito/botón** se pulsó (equivale a registrar el PIN). Solo para pruebas con un PIN de prueba. Con `0` o ausente, los dígitos se registran como "PIN digito". |

- **Leer el log:** lo escribe SYSTEM, así que sin elevar da "acceso denegado". Usa `view-log.ps1` (como administrador) o copia el archivo con permisos de administrador.
- **Cuidado al editar el registro con PowerShell:** `New-Item -Force` sobre una clave existente la recrea vacía y borra sus valores; usa `Set-ItemProperty` sobre la clave ya creada.
- `install.ps1` borra el log anterior.

Líneas útiles del log:

| Línea | Significado |
|---|---|
| `Proceso=… Sesion=… Escritorio='Winlogon'` | Contexto del hilo de captura. |
| `Mandos visibles -> DirectInput=N XInput=N WGI=N` | Qué ve LogonUI. Todo en 0 = HidHide u oculto / mando sin inicializar. |
| `Accion PIN … [DirectInput=.. WGI=.. XInput=..]` | Qué motor vio cada pulsación. |
| `SendInput denegado en el escritorio 'Winlogon'; reintentando…` + `Escritorio de entrada='Default'` | Primera pulsación en la pantalla de bloqueo; es lo esperado. |
| `Destino de teclas: … clase='LogonUI Logon Window'` | Dónde caerá la tecla (solo con `LogKeys=1`). |
| `Boton crudo N presionado` / `POV crudo` | Botones DirectInput sin interpretar (solo con `LogKeys=1`); sirve para verificar el orden de botones de un mando. |
| `Interfaz ilegible … se ignora 60 s` | Interfaz HID que no se puede leer; es normal. |

---

## 6. Compilación e instalación

**Requisitos de compilación:** Visual Studio Build Tools 2022 (workload C++ / `v143`) y Windows SDK 10.0.26100.0.

```
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended"
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "Xbox Pin Controller.sln" /p:Configuration=Release /p:Platform=x64
```

Salida: `x64\Release\XboxPINController.dll`. Instalación: `install.ps1` como administrador. Copia la DLL, registra el provider, aplica la compatibilidad con Windows 11 y agrega `LogonUI.exe` a la lista blanca de HidHide (si está instalado).

---

## 7. Notas de uso y limitaciones

- La **primera pulsación** en la pantalla de bloqueo solo despierta; la caja del PIN aparece después.
- Con **Handheld Companion**, no hace falta desactivar "Ocultar mando físico" siempre que `LogonUI.exe` siga en la lista blanca de HidHide. Si HC la pisara, vuelve a ejecutar `install.ps1`.
- El orden de botones del Legion en modo `PID_61ED` puro (sin HC) solo está confirmado para A y RB por DirectInput; el resto se validó por XInput/WGI.
- El tile "XboxPINController" no tiene campo de texto. Si quedara seleccionado, los dígitos no irían a la caja del PIN; en las pruebas el foco siempre estuvo en `LogonUI Logon Window`.
- El log puede contener el PIN con `LogKeys=1`, y vive en `C:\Windows\Temp`. Apágalo al terminar de depurar (`disable-logging.ps1`, quitar `LogKeys` y borrar el archivo).

---
