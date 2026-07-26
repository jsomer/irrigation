# Firmware Deploy (VS Code + PlatformIO)

Flash the Arduino UNO R4 WiFi from this repo using **VS Code** and the **PlatformIO** extension.

## One-time setup

1. Install [VS Code](https://code.visualstudio.com/) and the **PlatformIO IDE** extension (`platformio.platformio-ide`).
2. Open the repo folder (`irrigation/`), or open `firmware/` as the workspace root.
3. Create secrets (gitignored — never commit):

```bash
cp firmware/src/secrets.h.example firmware/src/secrets.h
```

4. Edit `firmware/src/secrets.h`:
   - WiFi SSID / password
   - MQTT broker IP (Home Assistant), user, password
   - **`IRRIGATION_INSTANCE_ID`** — unique on the broker (`raised_bed`, `vegetable_garden`, …)
   - **`IRRIGATION_INSTANCE_NAME`** — human-readable device name

The instance ID must match an entry in `homeassistant/instances.yaml` and the
HA packages for that controller. See [mqtt_topics.md](../schemas/mqtt_topics.md).

## Build & upload

1. Plug the UNO R4 WiFi in via USB.
2. In VS Code’s PlatformIO toolbar (bottom status bar):
   - **Build** — compile (`uno_r4_wifi` is the default env; verbose serial logging).
   - **Upload** — flash over USB.
3. **Serial Monitor** (115200 baud) — confirm boot, WiFi, and `MQTT connected`.
   Topics should be under `irrigation/<instance_id>/...`.

### CLI equivalent

```bash
cd firmware
pio run -e uno_r4_wifi -t upload
pio device monitor          # 115200
```

Release build (logging off, smaller binary):

```bash
pio run -e uno_r4_wifi_release -t upload
```

## After flashing

1. In Home Assistant, confirm
   `binary_sensor.irrigation_<instance_id>_controller_online` is **on**.
2. On that instance’s dashboard, tap **Sync** (or run
   `script.irrigation_<instance_id>_sync_firmware`).
3. Mode should be **auto** only when you intend HA to request pulses for that valve.

## Second controller

1. Add the id/name to `homeassistant/instances.yaml` if needed; run
   `python3 scripts/render_ha_instances.py` and deploy packages + dashboard.
2. On the second board, use a **different** `IRRIGATION_INSTANCE_ID` (never reuse).
3. Flash and Sync that instance only.

## Troubleshooting

| Symptom | What to check |
|---------|----------------|
| Upload fails / no port | USB cable (data, not charge-only); close other serial monitors; pick the correct port in PlatformIO |
| Builds but no WiFi | `WIFI_SSID` / `WIFI_PASSWORD` in `secrets.h`; 2.4 GHz network |
| WiFi OK, no MQTT | Broker IP, port `1883`, MQTT user/password; HA MQTT add-on running |
| Online entity missing | HA packages deployed for that id; firmware id matches `instances.yaml` |
| Online but odd timings | Run **Sync** after flash; helpers with no `initial:` keep last HA values |

Board and library config live in `firmware/platformio.ini`. Pin map: [hardware.md](hardware.md).
