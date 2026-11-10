#!/usr/bin/env python3
"""Instala el hook de la Heltec en Claude Code (Windows o Linux).

Copia heltec-claude.py a ~/.claude/hooks/ y añade los hooks a ~/.claude/settings.json
sin tocar el resto de la configuración. Se puede ejecutar varias veces sin duplicar nada.

Uso:  python install.py
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

EVENTS = {
    "SessionStart": "done",
    "UserPromptSubmit": "thinking",
    "PostToolUse": "thinking",
    "PermissionDenied": "thinking",
    "PermissionRequest": "permission",
    "Stop": "done",
    "SessionEnd": "off",
}

here = Path(__file__).resolve().parent
claude_dir = Path.home() / ".claude"
hooks_dir = claude_dir / "hooks"
settings_path = claude_dir / "settings.json"
script = hooks_dir / "heltec-claude.py"

try:
    import serial  # noqa: F401
except ImportError:
    print("Instalando pyserial...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "--user", "pyserial"])

hooks_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(here / "heltec-claude.py", script)

# Rutas con / para que el comando funcione igual desde Git Bash, cmd o PowerShell
python = Path(sys.executable).as_posix()
target = script.as_posix()

settings = json.loads(settings_path.read_text(encoding="utf-8")) if settings_path.exists() else {}
if settings_path.exists():
    shutil.copy2(settings_path, settings_path.with_suffix(".json.bak"))

hooks = settings.setdefault("hooks", {})
for event, state in EVENTS.items():
    groups = hooks.setdefault(event, [])
    # Quita instalaciones anteriores de este hook
    for g in groups:
        g["hooks"] = [h for h in g.get("hooks", []) if "heltec-claude.py" not in h.get("command", "")]
    groups[:] = [g for g in groups if g["hooks"]]
    groups.append({"hooks": [{
        "type": "command",
        "command": f'"{python}" -I "{target}" {state}',
        "timeout": 5,
    }]})

settings_path.write_text(json.dumps(settings, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"Hook instalado en {script}")
print(f"Configuración actualizada: {settings_path}")
print("Reinicia Claude Code para que cargue los hooks.")
