# Usage Limit Tracker

A local usage dashboard built with an ESP32-C3 SuperMini, 16x2 LCD, KY-040
rotary encoder, Rust/Axum server, and Firefox extension.

It displays:

- Current New Zealand date and time
- Codex five-hour usage window
- Codex weekly usage window
- An observed ChatGPT image-generation restriction and its reset countdown

The image tracker does not estimate remaining images. It records a deadline
only after Firefox observes an image-specific rejection from ChatGPT. Passing
that deadline produces `reset_time_passed`; it does not claim that image
generation is available.

## Project layout

```text
usage-limit-tracker/
├── platformio.ini
├── firmware/          # ESP32 source, headers, and environment loader
├── webserver/         # Rust read/write APIs and persisted state
└── firefox-extension/ # Local Firefox response observer
```

## Configuration

### ESP32 Wi-Fi

Copy `.env.example` to `.env` and set:

```dotenv
WIFI_SSID=your_wifi_name
WIFI_PASSWORD=your_wifi_password
```

The `.env` file is ignored by Git. PlatformIO generates an ignored header at
build time, so Wi-Fi credentials are not embedded in tracked source files.

If the laptop address changes, update `SERVER_URL` in
`firmware/include/AppConfig.h`.

### Local image-update token

Generate a strong token and export it in the terminal that starts the server:

```bash
export IMAGE_TRACKER_TOKEN="$(openssl rand -hex 32)"
cd webserver
cargo run
```

Open the Firefox extension popup and save the same token. Firefox stores it in
privileged extension storage; ChatGPT page JavaScript cannot access it.

## Firefox setup

1. Open `about:debugging#/runtime/this-firefox`.
2. Choose **Load Temporary Add-on**.
3. Select `firefox-extension/manifest.json`.
4. Open the extension popup and save `IMAGE_TRACKER_TOKEN`.
5. Keep the extension enabled while using ChatGPT.

The extension observes new POST responses to ChatGPT's conversation endpoints.
It forwards every response byte unchanged, keeps a bounded temporary scan
buffer, and sends only parsed reset metadata to `127.0.0.1:8766`. A rendered
message observer provides a fallback when ChatGPT changes its network path.
Neither observer stores or logs conversation bodies.

Because ChatGPT's internal response wording is undocumented, detection is
best-effort. Supported reset descriptions include:

- `Try again tomorrow at 6:27 PM`
- `Your limit resets in 19 hours`
- `Try again in 45 minutes`
- Explicit ISO timestamps with a timezone when tied to reset wording

## Server APIs

The LAN-facing server listens on `0.0.0.0:8765`:

```text
GET /health
GET /usage
GET /image-limit
```

The authenticated write server listens only on `127.0.0.1:8766`:

```text
POST   /images/limit-update
DELETE /images/limit-update
```

Image-limit state is persisted in `image-limit-state.json` by default. Override
the location with `IMAGE_LIMIT_STATE_FILE`. The state file is ignored by Git.

## Manual image-limit fallback

If ChatGPT changes its response format, submit a known deadline manually:

```bash
read -rsp "Rust token: " TOKEN
echo

RESET_AT=$(TZ=Pacific/Auckland \
  date -d '2026-10-28 18:27:00' +%s)

curl -sS -X POST \
  http://127.0.0.1:8766/images/limit-update \
  -H "Content-Type: application/json" \
  -H "x-tracker-token: $TOKEN" \
  -d "{\"reset_at\":$RESET_AT}" \
  | python3 -m json.tool

unset TOKEN RESET_AT
```

The date above is only an example. To clear image-limit test data without
touching Codex usage state:

```bash
read -rsp "Rust token: " TOKEN
echo

curl -i -X DELETE \
  http://127.0.0.1:8766/images/limit-update \
  -H "x-tracker-token: $TOKEN"

unset TOKEN
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
