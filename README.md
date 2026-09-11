# EcoFarm AI — Arduino UNO Q — Final Contest Package

Sustainable precision irrigation + rain/motion sensing + LoRa field alerts
for smallholder farmers. Built for the Arduino UNO Q + App Lab contest.

## What's Working (all in one synced package)
- 3-zone soil moisture monitoring + auto pump control
- Rain sensor → auto pump-OFF override (real water-saving proof)
- LDR light sensor → live sunlight % on dashboard
- PIR motion sensor → field security alert (buzzer + LoRa push)
- BMP280 (air temp + pressure), DS18B20 (soil temp), DHT11 (humidity)
- LED matrix growth animation + scrolling live data
- LoRa RA-02 (SX1278) → TX-only heartbeat every 30s + instant DRY/MOTION alerts
- Green/Red LED + buzzer — auto or dashboard-manual control
- Full web + mobile live dashboard (Socket.IO) with manual pump/LED override
- Mobile camera plant-leaf health scanner (rule-based, works instantly,
  no external model needed — see "Edge Impulse upgrade" note below)

## Pin Map (matches sketch.ino exactly — verified line by line)

| Component | Pin | Notes |
|---|---|---|
| Soil Zone 1 | A0 | |
| Soil Zone 2 | A1 | |
| Soil Zone 3 | A2 | |
| Rain sensor AOUT | A3 | read but unused (DOUT drives logic) |
| LDR AO | A4 | read but unused (used as %, DO drives day/night threshold logic if you want it) |
| Rain sensor DOUT | D2 | active LOW = rain |
| LDR DO | D3 | active LOW = dark |
| PIR (HC-SR501) OUT | D4 | HIGH = motion |
| Relay (pump) | D5 | |
| Buzzer | D6 | |
| DS18B20 | D7 | needs 4.7kΩ pull-up |
| DHT11 | D8 | |
| Green LED | D9 | |
| Red LED | D10 | |
| LoRa NSS (CS) | SS (board's dedicated SPI CS) | |
| LoRa MOSI/MISO/SCK | D11/D12/D13 | hardware SPI, automatic |
| LoRa RESET, DIO0 | not wired | TX-only demo, code uses -1 |
| BMP280 + OLED | SDA/SCL (I2C) | shared bus, dedicated pins |

⚠️ **RA-02 is 3.3V logic only** — never connect it to 5V.
⚠️ **PIR needs 5V** and ~30-60s warm-up after power-on before readings are reliable.

## Folder Structure
```
EcoFarm_AI/
├── app.yaml                 <- App Lab manifest (name, bricks used)
├── sketch/
│   ├── sketch.ino            <- MCU (STM32) side — all sensors, pump/LED
│   │                            logic, OLED, LED matrix, LoRa, Bridge
│   └── sketch.yaml           <- Arduino library list for this sketch
├── python/
│   ├── main.py                <- MPU (Linux) side — dashboard server,
│   │                              Bridge polling, leaf photo AI
│   └── requirements.txt       <- Python deps (Pillow)
└── assets/
    ├── index.html              <- Dashboard web page (served to browser)
    └── libs/socket.io.min.js   <- Socket.IO client library
```

## How to load this into Arduino App Lab (step by step)

1. **Open Arduino App Lab** on your computer (or the UNO Q's own web
   interface if you're using it standalone).
2. **Create a new App / Project** — give it a name, e.g. `EcoFarmAI`.
   App Lab will generate an empty folder with the same structure shown
   above (`app.yaml`, `sketch/`, `python/`, `assets/`).
3. **Replace the generated files** with the ones from this package:
   - Copy `app.yaml` → overwrite the project root `app.yaml`.
   - Copy everything inside `sketch/` → into the project's `sketch/`
     folder (overwrite `sketch.ino` and `sketch.yaml`).
   - Copy everything inside `python/` → into the project's `python/`
     folder (overwrite `main.py`, add `requirements.txt`).
   - Copy everything inside `assets/` → into the project's `assets/`
     folder (overwrite `index.html`, add the `libs/` folder with
     `socket.io.min.js` inside it).
4. **Install the sketch libraries.** In App Lab / Arduino IDE, open
   Library Manager and install each library listed in `sketch/sketch.yaml`
   (Adafruit BMP280, Adafruit Unified Sensor, OneWire, DallasTemperature,
   U8g2, DHT sensor library, and **LoRa by Sandeep Mistry**).
5. **Wire everything** per the pin map above.
6. **Build & Deploy** from App Lab — it flashes `sketch.ino` to the STM32
   side and starts `python/main.py` + the web dashboard on the Linux/MPU
   side automatically.
7. **Open the dashboard** — App Lab shows a local URL/QR code; open it on
   your laptop browser or scan with your phone. You should see live
   zone data, pump/LED controls, rain/motion/light status, and the leaf
   photo scanner.
8. **Check Serial Monitor** (via Arduino IDE, separately, while the UNO Q
   is connected via USB) to confirm each sensor printed `OK` at boot —
   this is the fastest way to catch a wiring mistake before demo day.

## Edge Impulse AI upgrade (optional, for higher accuracy disease detection)

Your Edge Impulse project — https://studio.edgeimpulse.com/studio/1089108 —
already gave you 88.9% accuracy. The dashboard right now ships with a
**rule-based** leaf analyzer (`analyze_leaf_image()` in `main.py`) so the
scanner works immediately with zero setup. To swap in your trained model:

1. In Edge Impulse Studio → **Deployment** → search "Arduino library" →
   Build. This downloads a `.zip` Arduino library of your trained model.
2. Import that `.zip` into Arduino IDE (Sketch → Include Library → Add
   .ZIP Library).
3. Replace the body of `analyze_leaf_image()` in `python/main.py` with a
   call into your exported model's inference function instead of the
   green/yellow pixel-ratio heuristic — the rest of the app (Socket.IO
   wiring, UI, alert triggering) stays exactly the same.

This two-stage design (working rule-based scanner today, clean upgrade
path to your real trained model) is intentional — it means the project
demos reliably right now, and the AI upgrade is a drop-in swap, not a
rewrite.

## Still Pending
- Swap in the trained Edge Impulse model (see above) if you want the
  88.9%-accuracy classifier instead of the rule-based fallback
- Demo video
- Final Hackster.io submission text update
