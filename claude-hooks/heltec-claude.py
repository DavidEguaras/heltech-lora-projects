#!/usr/bin/env python3
"""Envía el estado de Claude Code (thinking/permission/done/off) a la Heltec por USB serie.

Funciona en Windows (COMx) y Linux (/dev/ttyUSBx): busca el puerto por el chip CP2102 de la placa.
"""
import sys

import serial
from serial.tools import list_ports

CP2102_VID, CP2102_PID = 0x10C4, 0xEA60


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == (CP2102_VID, CP2102_PID) or "CP210" in (p.description or ""):
            return p.device
    return None


port_name = find_port()
if len(sys.argv) < 2 or not port_name:
    sys.exit(0)

try:
    # Valores por defecto de DTR/RTS: abrir y cerrar el puerto no reinicia la placa
    with serial.Serial(port_name, 115200, timeout=0.5, write_timeout=0.5) as port:
        port.write(f"S:{sys.argv[1]}\n".encode())
        port.flush()
except Exception:
    pass
