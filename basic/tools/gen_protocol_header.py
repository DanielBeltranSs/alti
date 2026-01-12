#!/usr/bin/env python3
"""
Genera un header C++ con los UUID y versión del protocolo BLE a partir de
alti-protocol/protocol.json. Se escribe en src/include/bluetooth_protocol.h
para que firmware y app compartan las mismas constantes.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROTO_JSON = ROOT / "alti-protocol" / "protocol.json"
OUT_HDR = ROOT / "src" / "include" / "bluetooth_protocol.h"


def expand_uuid(base: str, hex_id: str) -> str:
    """Reemplaza 'xxxx' en base con el valor del id (ej. 0x1001 -> 1001)."""
    # hex_id viene como "0x1001"
    suffix = hex_id.lower().replace("0x", "").zfill(4)
    return base.replace("xxxx", suffix)


def main() -> None:
    data = json.loads(PROTO_JSON.read_text(encoding="utf-8"))
    base_uuid = data["base_uuid"]
    version = data.get("version", "0.0.0")

    # Tomamos el primer servicio como principal
    service = data["services"][0]
    svc_uuid = expand_uuid(base_uuid, service["id"])

    chars = {c["name"]: expand_uuid(base_uuid, c["id"]) for c in service["characteristics"]}

    lines = [
        "// Auto-generado por tools/gen_protocol_header.py desde alti-protocol/protocol.json",
        "#pragma once",
        '#include <Arduino.h>',
        "",
        "namespace BtProtocol {",
        f'constexpr const char* kVersion = "{version}";',
        f'constexpr const char* kBaseUuid = "{base_uuid}";',
        f'constexpr const char* kServiceMainUuid = "{svc_uuid}";',
    ]

    for name, uuid in chars.items():
        lines.append(f'constexpr const char* kChar{ name.capitalize() }Uuid = "{uuid}";')

    lines.append("}  // namespace BtProtocol")
    lines.append("")

    OUT_HDR.write_text("\n".join(lines), encoding="utf-8")
    print(f"[gen] Escrito {OUT_HDR}")


if __name__ == "__main__":
    main()
