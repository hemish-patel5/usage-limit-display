# Usage Limit Tracker

A local usage dashboard built with an ESP32-C3 SuperMini, 16x2 LCD, KY-040
rotary encoder, Rust/Axum server, and Firefox extension.

It displays:

- Current New Zealand date and time
- Codex five-hour usage window
- Codex weekly usage window
- An observed ChatGPT image-generation restriction and its reset countdown


## Project layout

```text
usage-limit-tracker/
├── platformio.ini
├── firmware/          # ESP32 source, headers, and environment loader
├── webserver/         # Rust read/write APIs and persisted state
└── firefox-extension/ # Local Firefox response observer
```


## Firmware controls

- Rotate clockwise: next screen
- Rotate anticlockwise: previous screen
- Short press: refresh both server endpoints
- Long press: return to date/time
- 30 seconds without input: return to date/time

The ESP32 polls every 30 seconds. Between polls, an active image countdown is
decreased locally using `millis()` and updates on the LCD without extra HTTP
requests.

## Build and test

From the repository root:

```bash
# Firmware
platformio run

# Rust formatting, tests, and linting
cd webserver
cargo fmt --check
cargo test
cargo clippy --all-targets -- -D warnings

# Firefox parser tests
cd ..
node firefox-extension/tests/limit-parser.test.js
```

Upload the firmware with PlatformIO after the server address and Wi-Fi settings
are correct.
