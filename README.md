# Heltech LoRa projects

Proyectos para la **Heltec WiFi LoRa 32 V3** (ESP32-S3 + OLED + BME280).

| Carpeta | Qué es |
|---|---|
| `heltec-lora-v3/` | Firmware (PlatformIO): estación meteorológica con BME280 + indicador de estado de Claude Code |
| `claude-hooks/` | Hooks de Claude Code que avisan a la placa por USB serie (`S:thinking`, `S:permission`, `S:done`, `S:off`) |

## Instalación en Windows

### 1. Requisitos
- [Python 3](https://www.python.org/downloads/) (marca *Add python.exe to PATH*)
- [Claude Code](https://claude.com/claude-code) (necesita Git for Windows)
- VS Code + extensión **PlatformIO IDE** (solo para flashear el firmware)
- Driver del CP2102: Windows 10/11 suele instalarlo solo. Si la placa no aparece como `COMx` en el Administrador de dispositivos, instala el [driver CP210x de Silicon Labs](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).

### 2. Clonar
```powershell
git clone https://github.com/DavidEguaras/heltech-lora-projects.git
cd heltech-lora-projects
```

### 3. Flashear el firmware (si la placa no lo tiene ya)
Abre `heltec-lora-v3` en VS Code con PlatformIO y pulsa **Upload**, o desde terminal:
```powershell
cd heltec-lora-v3
pio run -t upload
```

### 4. Instalar los hooks
```powershell
python claude-hooks\install.py
```
El instalador:
- instala `pyserial` si falta,
- copia `heltec-claude.py` a `%USERPROFILE%\.claude\hooks\`,
- añade los hooks a `%USERPROFILE%\.claude\settings.json` (guarda una copia `settings.json.bak` y no toca el resto).

Reinicia Claude Code y listo.

## Estados

| Evento de Claude Code | Estado | En la placa |
|---|---|---|
| `SessionStart`, `Stop` | `done` | Clawd terminado (vuelve a la estación tras 1 min) |
| `UserPromptSubmit`, `PostToolUse`, `PermissionDenied` | `thinking` | Clawd trabajando |
| `PermissionRequest` | `permission` | Clawd pidiendo permiso + LED parpadeando |
| `SessionEnd` | `off` | Estación meteorológica |

## Problemas
- **No pasa nada**: comprueba que la placa sale como *Silicon Labs CP210x (COMx)* y prueba a mano:
  `python %USERPROFILE%\.claude\hooks\heltec-claude.py thinking`
- **Solo un programa puede abrir el puerto COM a la vez en Windows**: cierra el monitor serie de PlatformIO/Arduino mientras usas Claude Code.
