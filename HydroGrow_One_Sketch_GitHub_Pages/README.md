# HydroGrow — ONE Uno sketch (ESP-01 AT firmware) + GitHub Pages + Supabase

This edition has **only one Arduino `.ino`**: `arduino/HydroGrow_One_Sketch/HydroGrow_One_Sketch.ino`. Upload **only to Arduino Uno**. ESP-01 keeps its existing AT firmware — **no ESP-01 bridge sketch or reflashing**.

## Network

Arduino Uno -- UART AT commands --> ESP-01 -- **HTTP on trusted private LAN** --> Windows Node gateway (port 8081) -- HTTPS / verified TLS --> Supabase Edge Function/Postgres <-- HTTPS --> GitHub Pages dashboard.

A local gateway is necessary with the presently verified AT firmware because SSL certificate validation has not been established. **The Uno-to-gateway LAN segment is not encrypted**: never port-forward gateway port 8081 or use this on untrusted Wi-Fi. Gateway machine must remain on. Do not store tokens on public GitHub. For an entirely computer-free solution, use an ESP32 or verify ESP-01 firmware with certificate-validated HTTPS; that is a separate implementation.

## Setup

1. Disconnect physical pump load. Make sure relay's OFF logic and float polarity are tested. `ENABLE_PUMP_CONTROL=false` in Uno firmware prevents remote ON by default. pH is shown as *voltage*, not calibrated pH.
2. Set up Supabase using `supabase/schema.sql` (run SQL editor), create one administrator via Supabase Auth, register device ID `hydrogrow-uno-01` with actual admin UUID. Deploy Supabase Edge Function `device` as described below.
3. On Windows with Node.js **18+**, open `gateway/` in PowerShell. Set `HYDRO_LAN_KEY` to a *new* random 24+ character secret, `HYDRO_DEVICE_TOKEN` to the matching Edge Function secret, and `HYDRO_FUNCTION_URL` to exact HTTPS endpoint (see `run-gateway.example.ps1`). Start `node gateway.js`. If secret values are put in a local script, keep it out of Git and preferably outside repo.
4. Allow inbound TCP **8081** for Node.js only on Windows **Private** network. No router port forwarding. `192.168.1.52` is your observed previous PC IP; edit if it changed. The old `server.js` and port 8080 are **not used** for this cloud edition.
5. Open the Uno sketch. Fill in `WIFI_SSID`, `WIFI_PASS`, `API_KEY` (**same as `HYDRO_LAN_KEY`**), and `PC_SERVER` (your PC LAN IPv4). NEVER upload your modified credentials-bearing sketch to GitHub; keep your public Git version with placeholders. Keep ESP-01 TX→Uno D10, Uno D11→level shifter→ESP RX, grounds tied, stable verified supply. ESP-01 AT firmware should respond at **9600 baud** to the sketch. DHT D2, float D3+GND, relay D7, pH A0, I2C A4/A5.
6. Upload Uno sketch in Arduino IDE 1.8.19. Open Serial Monitor at 9600. Expect `ESP-01 joined Wi-Fi`, `Command response: 0|NONE`, then `Server reply: OK`. Cloud API success depends on account setup.
7. Edit `docs/config.js` with your public **Supabase publishable** key and project URL (NEVER service-role key or device token). Create GitHub repo and publish **`/docs`** via Settings→Pages→Deploy from branch→main→/docs.
8. Sign in via browser and confirm sensor readings. Pump remains OFF until safety checks. Alerts show in dashboard; notifications outside website not implemented. Camera cannot stream securely to a public HTTPS Pages site with the ESP32-CAM's default local HTTP stream; a secure authenticated camera gateway is still needed.

### Supabase commands (CLI)

```sh
supabase login
supabase link --project-ref YOUR_PROJECT_REF
supabase secrets set HYDRO_DEVICE_ID=hydrogrow-uno-01 HYDRO_DEVICE_TOKEN=YOUR_NEW_24_PLUS_CHARACTER_CLOUD_SECRET
supabase functions deploy device --no-verify-jwt
```

The Edge Function checks `x-device-key` before database access. Its service-role key stays on Supabase server. Never embed it in the GitHub Pages files. Hosted Supabase environment must provide `SUPABASE_SERVICE_ROLE_KEY`.

### Test (without Arduino)

From a PowerShell window with the gateway running and the `HYDRO_LAN_KEY` environment variable set:

```powershell
Invoke-WebRequest -Uri "http://127.0.0.1:8081/api/command?key=$($env:HYDRO_LAN_KEY)" -UseBasicParsing
```

Expected `200` body `0|NONE` if there are no pending commands. Test the Arduino afterward. The gateway logs cloud errors without exposing tokens.

### Important notes

- **Only one `.ino`** in this package: both controller and ESP-01 AT protocol are coded in it.
- Sensor and pump safety operate on the Uno, not the cloud. Keep schedule disabled until RTC verification.
- `ON` blocked until `ENABLE_PUMP_CONTROL=true` AND float reports sufficient water.
- Command ACK is included with next telemetry and Supabase marks `executed` or `blocked`; network loss can leave status pending/expired. Cloud data is periodic, not instantaneous.
- New commands may be lost if the Uno restarts at a critical moment; this is a prototype, not a safety-certified controller.
- HTTP between ESP-01 and gateway exposes device LAN key to other machines capable of sniffing LAN traffic. Use a trusted isolated Wi-Fi/VLAN and rotate keys if exposed.
