Import("env")

import json
from pathlib import Path


project_dir = Path(env["PROJECT_DIR"])
dotenv_path = project_dir / ".env"

if not dotenv_path.is_file():
    raise RuntimeError(
        f"Missing {dotenv_path}. Copy .env.example to .env and add your Wi-Fi credentials."
    )

values = {}
for raw_line in dotenv_path.read_text(encoding="utf-8").splitlines():
    line = raw_line.strip()
    if not line or line.startswith("#"):
        continue

    key, separator, value = line.partition("=")
    if not separator:
        raise RuntimeError(f"Invalid line in {dotenv_path}: {raw_line!r}")

    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        value = value[1:-1]
    values[key.strip()] = value

required = ("WIFI_SSID", "WIFI_PASSWORD")
missing = [key for key in required if not values.get(key)]
if missing:
    raise RuntimeError(f"Missing required value(s) in {dotenv_path}: {', '.join(missing)}")

generated_dir = project_dir / ".pio" / "generated"
generated_dir.mkdir(parents=True, exist_ok=True)
header_path = generated_dir / "wifi_secrets.h"
header_path.write_text(
    "#pragma once\n"
    f"static constexpr char WIFI_SSID[] = {json.dumps(values['WIFI_SSID'])};\n"
    f"static constexpr char WIFI_PASSWORD[] = {json.dumps(values['WIFI_PASSWORD'])};\n",
    encoding="utf-8",
)

env.Append(CPPPATH=[str(generated_dir)])
