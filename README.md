# ESP32 Crypto // Stock Terminal

A touchscreen market dashboard for an **ESP32 CYD (Cheap Yellow Display)**. It presents live cryptocurrency, stock, precious-metal, weather, and Hong Kong time information in a cyber-style interface.

The firmware is designed for a 320 × 240 ESP32 display with an XPT2046 touch controller. Wi-Fi credentials and the watchlist are configured directly on the device and persisted in ESP32 flash.

> The current application source is [`wifi_stocks_4.cpp`](wifi_stocks_4.cpp).

## Features

- **Crypto dashboard** — BTC/USDT and ETH/USDT prices plus 24-hour percentage change.
- **Stock watchlist** — Displays live quotes, supports up to 20 symbols, and includes `0700.HK`, `0941.HK`, and `AAPL` by default.
- **Hong Kong stock names** — Fetches and caches Chinese names for `.HK` symbols when available.
- **Precious metals** — COMEX gold (`GC=F`), silver (`SI=F`), and platinum (`PL=F`) futures.
- **Hong Kong weather and time** — Current temperature/weather for Hong Kong and NTP-synchronised local time (UTC+8).
- **On-device Wi-Fi configuration** — Scan networks and enter SSID/password through the touchscreen keyboard.
- **Persistent settings** — Wi-Fi credentials and the stock watchlist are saved using ESP32 `Preferences` (NVS).
- **Touch controls** — Add/delete stocks, scroll the watchlist, and switch between Crypto, Stock, Metals, and Setup tabs.
- **RGB status LED** — After a BTC update, green represents non-negative BTC 24-hour change and red represents a negative change.

## Hardware

### Required

- ESP32 development board with a 320 × 240 TFT display
- XPT2046 resistive touch controller
- CYD-style board with onboard RGB LED is the intended target
- Wi-Fi network with internet access

### Touch wiring / pin mapping

The firmware configures the XPT2046 touch controller on VSPI using these pins:

| Signal | ESP32 GPIO |
| --- | ---: |
| Touch IRQ | 36 |
| Touch MOSI | 32 |
| Touch MISO | 39 |
| Touch CLK | 25 |
| Touch CS | 33 |

Touch calibration values in the current firmware are:

```cpp
TOUCH_X_MIN = 306    TOUCH_X_MAX = 3697
TOUCH_Y_MIN = 438    TOUCH_Y_MAX = 3781
```

These values are hardware-specific. If touch positions do not line up with the display, recalibrate them for your board. [`define_setting.c`](define_setting.c) contains the same calibration reference values.

## Software requirements

Use the Arduino framework for ESP32 and install these libraries through Arduino Library Manager (or your preferred Arduino-compatible workflow):

- `ArduinoJson`
- `TFT_eSPI`
- `XPT2046_Touchscreen`
- `U8g2_for_TFT_eSPI`

The following are provided by the ESP32 Arduino core:

- `WiFi`
- `HTTPClient`
- `WiFiClientSecure`
- `Preferences`
- `SPI`
- `time`

## Installation and upload

1. Install the **ESP32 by Espressif Systems** board package in the Arduino IDE.
2. Install the libraries listed in [Software requirements](#software-requirements).
3. Configure `TFT_eSPI` for your specific display driver and display pins. This configuration is intentionally external to this repository because `TFT_eSPI` stores it in its library setup files.
4. Arduino sketches normally require a folder and `.ino` file with matching names. Create a folder named `wifi_stocks_4`, copy `wifi_stocks_4.cpp` into it, and rename the copy to `wifi_stocks_4.ino`.
5. Open `wifi_stocks_4.ino` in Arduino IDE.
6. Select the ESP32 board and the correct serial port, then build and upload.
7. On first boot, open the **Setup** tab:
   - Tap the SSID field and enter your Wi-Fi network name.
   - Tap the password field and enter the password.
   - Tap **Save & Connect** (or finish password entry) to connect.

Once connected, the firmware synchronises time and retrieves data automatically.

## Using the dashboard

| Tab | Purpose |
| --- | --- |
| **Crypto** | BTC and ETH price information and 24-hour change. |
| **Stock** | Scroll the watchlist; use **Add** to enter a ticker such as `0700.HK` or `TSLA`; tap the trash icon to remove a symbol. |
| **Metals** | Gold, silver, and platinum COMEX futures quotes. |
| **Setup** | Set saved Wi-Fi credentials or scan nearby Wi-Fi networks. |

Stock symbols are normalised to uppercase. Hong Kong symbols can be entered with fewer than four digits (for example, `700.HK` becomes `0700.HK`).

## Data sources and refresh intervals

| Data | Source | Normal refresh interval |
| --- | --- | ---: |
| Weather | [Open-Meteo](https://open-meteo.com/) | 10 minutes after success; retries every 30 seconds after failure |
| Crypto | [Binance public data API](https://www.binance.com/) | 15 seconds |
| Stocks and metals | [Yahoo Finance](https://finance.yahoo.com/) chart/search endpoints | 60 seconds |
| Time | `pool.ntp.org` / `time.google.com` | Set when Wi-Fi connects |

The weather location is fixed in the firmware to **Hong Kong** (`22.30, 114.17`).

## Notes and limitations

- This is a personal/educational project, not financial advice. Market data may be delayed, unavailable, or inaccurate.
- The code uses `WiFiClientSecure::setInsecure()` for HTTPS requests. This accepts certificates without validation and is convenient for prototyping but is **not appropriate for a security-sensitive production deployment**.
- Internet data sources may rate-limit requests or change their APIs without notice.
- Wi-Fi credentials are stored in ESP32 NVS through `Preferences`; erase flash/NVS before giving the device to another person.
- The repository currently contains source files only; no PlatformIO project configuration is included.

## Repository contents

| File | Description |
| --- | --- |
| [`wifi_stocks_4.cpp`](wifi_stocks_4.cpp) | Current full market-terminal firmware. |
| [`define_setting.c`](define_setting.c) | Touch calibration constants/reference mapping. |

## License

No license has been declared. Add a license file before distributing or reusing this project under defined terms.
