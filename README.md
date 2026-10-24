
# Codex Usage Tracker

A physical Codex usage monitor built with an **ESP32-C3**, **1602 LCD**, and **rotary encoder**. It displays Codex usage limits.

## Features

- Displays 5-hour and weekly usage percentages
- Shows when usage limits reset
- Rotary encoder to switch between screens
- Automatic usage updates over Wi-Fi
- Rust web server using the Codex App Server API

## Hardware

- ESP32-C3 SuperMini
- 1602A LCD (16×2)
- KY-040 rotary encoder
- 10k Potentiometer
- Jumper wires and breadboard

## Tech Stack

- **Firmware:** C++ / PlatformIO
- **Backend:** Rust / Axum / Tokio

## Project Structure

```text
usage-limit-tracker/
├── firmware/      # ESP32 firmware
├── webserver/     # Rust API server
└── README.md
```

## Getting Started

1. Install and sign in to the [Codex CLI](https://github.com/openai/codex).
2. Start the Rust server:
   ```bash
   cd webserver
   cargo run
   ```
3. Check the usage API at `http://localhost:8765/usage`.
4. Configure the ESP32 firmware with your server's local IP address.
5. Upload the firmware using PlatformIO.

## How It Works

The Rust server retrieves Codex usage limits and exposes them as JSON. The ESP32 fetches the data over Wi-Fi and displays it on the LCD.

> The Rust server must be running for the ESP32 to receive updated usage data.
  
