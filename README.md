# SitePulse

**An ESP32 Network & HTTPS Observatory**

SitePulse uses a small, continuously powered ESP32 to sample the network path to a website and compare it with the local Wi-Fi gateway. Measurements flow through Google Apps Script into Google Sheets, then appear in a Streamlit dashboard.

It answers practical questions: Does latency vary by time of day? Do remote latency spikes coincide with gateway latency or weak Wi-Fi? Is an HTTPS endpoint replying, refusing requests, or failing to connect? Can the probe keep collecting samples when uploads temporarily fail?

This is an educational measurement prototype developed by **Gaël Boënnec**, initially for the MET project at INSA Lyon. It is useful for exploring connectivity from one location over time. It does **not** measure browser rendering, JavaScript execution, application correctness, download throughput, or the experience of every user of a website.

> **Prototype security limitation:** the firmware retains the original `WiFiClientSecure.setInsecure()` calls. HTTPS traffic is encrypted, but server certificates are not verified. This affects measurement requests, uploads and the optional portal login. Do not treat this as a production-secure monitoring agent or deploy valuable portal credentials on an untrusted network. Certificate validation is future work. Personal passwords, tokens and deployment URLs are not included in this repository.

## What is included

| Component | Location | Purpose |
| --- | --- | --- |
| ESP32 firmware | [`firmware/SitePulse/SitePulse.ino`](firmware/SitePulse/SitePulse.ino) | Paired gateway/target pings, Wi-Fi RSSI, HTTPS checks, RAM queue and upload retries |
| Firmware configuration template | [`config.example.h`](firmware/SitePulse/config.example.h) | Wi-Fi, target, sampling schedule and optional captive portal settings |
| Apps Script receiver | [`apps-script/Code.gs`](apps-script/Code.gs) | Token-protected JSON API, validation, daily Sheets tabs and deduplication |
| Streamlit dashboard | [`app.py`](app.py) | Session summaries, detailed charts, freshness indicator and CSV export |
| Dashboard settings | [`.streamlit/`](.streamlit/) | Theme and example secrets file |
| Captive portal guide | [`docs/captive-portal.md`](docs/captive-portal.md) | Quantic login flow, device confirmation and adaptation limits |
| Data/API reference | [`docs/data-and-api.md`](docs/data-and-api.md) | Fields, status definitions and request/response contract |
| Validation | [`tests/`](tests/) | Dashboard regression checks and receiver contract tests |

No additional backend server or database is required beyond the Apps Script/Sheets deployment. A computer is needed to flash the ESP32 and run the dashboard locally; the probe itself can then run from USB power. The dashboard interface currently retains its original French labels; this documentation is in English.

## How it works

1. The ESP32 joins a 2.4 GHz Wi-Fi network, optionally completes the supported captive portal flow, and obtains UTC time through NTP.
2. At startup after time synchronization, and subsequently in each scheduled time slot, it collects **60 pairs of pings**: first the default gateway, then the configured target.
3. It aims for **one pair per second**. The two pings are sequential; timeouts can make a pair take longer. This is not exact simultaneous sampling.
4. It makes **one HTTPS GET after the ping session** and attaches the result to the session's last buffered sample.
5. It sends queued measurements in **batches of five** to Apps Script. Rows are removed from RAM only after their IDs are acknowledged.
6. Apps Script stores rows in UTC daily tabs. Streamlit reads the API and refreshes while the dashboard is open.

The default schedule is every **30 minutes**, aligned to UTC half-hour slots. The startup session can occur partway through a slot. Long measurements or uploads can delay later sessions; missed slots are not replayed.

## Try the dashboard without hardware

Download the repository using **Code → Download ZIP**, or clone it:

```bash
git clone https://github.com/GaelBG9/web-site-study.git
cd web-site-study
python -m venv .venv
```

Activate the environment:

```bash
# Windows PowerShell
.venv\Scripts\Activate.ps1

# Linux / macOS
source .venv/bin/activate
```

Then install and run:

```bash
python -m pip install -r requirements.txt
python -m streamlit run app.py
```

Use Python 3.11 or 3.12. The default **Démonstration** source needs no secrets, Google account or ESP32. Its generated data are explicitly labeled **DONNÉES SIMULÉES**. The synthetic latency spike, loss rate and HTTP 403 responses are examples, not published experimental results.

## Set up your own measurements

### 1. Create the Apps Script receiver

1. Create a standalone project at [Google Apps Script](https://script.google.com/).
2. Replace its `Code.gs` with [`apps-script/Code.gs`](apps-script/Code.gs).
3. Run `setup()` in the editor and authorize the requested spreadsheet access. It creates a spreadsheet if `SHEET_ID` is absent and generates separate `WRITE_TOKEN` and `READ_TOKEN` properties. Re-running it preserves existing values.
4. Open **Project Settings → Script properties** and retrieve `SHEET_ID`, `WRITE_TOKEN` and `READ_TOKEN`. Keep these private. The execution log prints the spreadsheet link, not the tokens.
5. Choose **Deploy → New deployment → Web app**, execute as yourself, and allow access by **Anyone** so the ESP32 can call it without an interactive Google login. Access to data is checked using the tokens inside the JSON body. Some managed accounts do not permit this deployment mode.
6. Copy the deployment URL ending in **`/exec`**. Do not use `/dev`.
7. After changing the receiver, update the deployment to a new version; saving the editor alone does not update a versioned deployment.

The spreadsheet itself need not be public. The API deployment is reachable publicly, so keep its tokens private. The implementation is intended for small experiments: reads scan up to the latest eight daily tabs, and writes scan IDs in the affected tabs. Quotas and execution limits still apply; this is not an unlimited telemetry database.

### 2. Configure and flash the ESP32

Hardware used in the original project: **ESP32-WROOM-32D**, configured as **ESP32 Dev Module**, connected to 2.4 GHz Wi-Fi. Other ESP32 variants have not been validated here.

1. Install Arduino IDE and the **esp32 by Espressif Systems** board package, using a **3.x** version. Install **ArduinoJson 7.x** through Library Manager. The ping implementation uses ESP-IDF bundled with the board package; no separate ESP32Ping library is required.
2. Open `firmware/SitePulse/SitePulse.ino`. Keep the sketch in its `SitePulse` directory.
3. Copy `config.example.h` to **`config.h` in the same directory**. The latter is ignored by Git.
4. Fill in `WIFI_SSID`, `WIFI_PASSWORD`, `ENDPOINT` and the receiver's `WRITE_TOKEN`.
5. Set a distinct `DEVICE`, using only letters, digits, `_` or `-` (maximum 40 characters).
6. Set `TARGET` for ICMP and `HTTPS_URL` for the application-level check. Keep them conceptually aligned with the service you want to study.
7. Select your board and serial port, upload, and open Serial Monitor at **115200 baud**.
8. Wait for Wi-Fi and NTP. A session should start and successful uploads should report confirmed rows. Check the spreadsheet for a `data_YYYY_MM_DD` tab.

`TARGET` can be a hostname, resolved once per session, or an IPv4 address for a fixed destination. This firmware is designed around IPv4; full IPv6 measurement support is not implemented. A fixed address can stop representing the service as infrastructure changes. With a hostname, different sessions can resolve to different addresses; inspect `target_ip` before comparing them. HTTPS uses its own URL and may resolve to a different frontend.

The radio is kept awake using `WiFi.setSleep(false)`, matching the adjustment used during the project. This trades power consumption for avoiding Wi-Fi sleep effects in the measurements. Place the board where the Wi-Fi signal is stable and use a reliable USB supply.

### 3. Connect Streamlit to your data

From the repository root, copy `.streamlit/secrets.example.toml` to `.streamlit/secrets.toml`:

```toml
endpoint = "https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec"
read_token = "YOUR_READ_TOKEN"
```

Start Streamlit and select **Google Sheets** in the sidebar. The dashboard requires only the read token. Never give it the write token.

For Streamlit Community Cloud, select `app.py` as the entry point and paste those settings into the app's Secrets settings. Do not commit the filled-in secrets file. A publicly accessible dashboard exposes the displayed measurements to its visitors even though the API token stays server-side; use demo mode or suitable app access restrictions for a public showcase.

### 4. Optional: Wi-Fi with a captive portal

Leave `USE_QUANTIC_PORTAL = false` on ordinary Wi-Fi. For the supported Quantic form, set it to `true` and configure your own account in `config.h`. If the portal asks you to confirm ownership of associated devices, `CONFIRM_MY_DEVICES` controls that explicit confirmation and defaults to `false`.

Read the [complete captive portal guide](docs/captive-portal.md) before enabling it. This is a provider-specific form adapter, not a general questionnaire solver. CAPTCHA, MFA, JavaScript-driven login and other providers need different handling.

## Sampling settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `SESSION_INTERVAL_SECONDS` | `1800` | Start a session in each 30-minute UTC slot |
| `SAMPLES` | `60` | Number of paired gateway/target samples in a session |
| `PAIR_INTERVAL_MS` | `1000` | Minimum intended interval between pair starts |
| `UPLOAD_BATCH_SIZE` | `5` | Rows per upload; receiver accepts 1–120 |
| `CAPACITY` | `360` | Maximum queued rows in RAM |

To sample every five minutes, set `SESSION_INTERVAL_SECONDS = 300` and reflash. Keep `SAMPLES = 60` if you want approximately one minute of measurements per slot. This gives **17,280 rows/day** and, with batches of five, **3,456 write requests/day**, before retries. At 30-minute intervals the corresponding steady-state figures are **2,880 rows** and **576 write requests/day**. These are workload estimates, not quota guarantees.

In Streamlit, change **Alerte après · minutes** from 35 to a threshold appropriate for the new interval, for example 7–10 minutes. Upload delays can legitimately exceed this threshold. The demo remains a 30-minute synthetic schedule.

Avoid intervals shorter than the time needed for a session and its uploads. The dashboard separates sessions using gaps greater than 120 seconds, not an explicit firmware session ID. Very frequent or unusually long sessions can therefore merge visually.

## Read the results correctly

| Observation | Useful interpretation | What it does not prove |
| --- | --- | --- |
| Gateway and remote RTT rise together | The local wireless/access path may contribute | Wi-Fi is definitively the only cause |
| Remote RTT rises while gateway stays stable | The change may be beyond the local gateway or at the target | Which router or provider caused it |
| Target ping times out | No ICMP reply arrived within the probe timeout | The website is down; ICMP may be filtered or rate-limited |
| HTTPS returns `403` | An HTTP server responded but refused access | A network connection failure or successful page access |
| `http_ms` increases | This ESP32's request-to-response-header duration increased | Browser page-load time or a pure server-processing time |
| No recent rows arrive | Recorded measurements are stale | Whether the probe, Wi-Fi, portal, API or power failed |

The dashboard shows median and p95 RTT for successful target pings. Its no-reply percentage is `timeouts / (successful pings + timeouts) × 100`; Wi-Fi disconnections, DNS failures and internal errors are distinct statuses and excluded from that denominator. Scheduled gaps are not counted as packet losses. The p95 of a 60-sample session is descriptive and sensitive to a few slow replies.

`http_ms` covers the synchronous GET until response headers, including connection/TLS work and followed redirects. It is neither a full body-download measurement nor separate DNS, TCP, TLS and server timings. The HTTPS result is attached after the last ping, so that row's timestamp precedes the HTTP check. Gateway and target probes are also sequential.

## Example: the original MET experiment

The project was used to observe connectivity towards **ChatGPT from a home Wi-Fi connection**, using an ESP32 as a dedicated probe. The original experiment used a fixed destination address, `172.64.155.209`, alongside requests to `https://chatgpt.com/`; the public configuration instead defaults to the hostname so that this historical address is not mistaken for a permanent endpoint.

The useful comparison was between target RTT, gateway RTT and Wi-Fi RSSI across repeated sessions. HTTPS responses included `403`, illustrating why a reachable HTTP endpoint and access to the application are separate questions. Moving the board closer to the router and disabling Wi-Fi sleep were practical adjustments during debugging; those observations alone do not establish a single cause for earlier upload failures.

A sensible workflow is to collect several sessions, inspect session median/p95 and no-reply rates, open an unusual session in detail, compare gateway latency and RSSI, then inspect the HTTPS status separately. Export the filtered CSV for further analysis. Record hardware placement and configuration changes alongside the experiment so that different conditions are not compared blindly.

This repository does not publish private measurements or claim a general performance result about ChatGPT. The demo is a reproducible way to explore the dashboard without disclosing the original dataset.

## Reliability and known limitations

- The queue is **RAM-only**. Power loss/reset discards pending samples. At capacity, new rows are dropped and `lostRows` is printed to Serial; it is not uploaded as a separate metric.
- IDs survive upload retries but not reboots. Receiver deduplication is per daily tab. Reusing the same ID with altered content is not a supported update mechanism.
- Before the first successful NTP synchronization, no timestamped measurements are collected. After synchronization, disconnected sessions can still be recorded and uploaded later.
- Upload retries retain unacknowledged rows; a response lost after a successful write can be retried with the same IDs. Retries run approximately once a minute when connected, but blocking requests can extend delays.
- The receiver accepts timestamps from the previous seven days and at most five minutes in the future. Daily tabs remain stored; automatic retention cleanup is not implemented.
- The dashboard freshness check runs only while the app is active. It is not an email, push notification or independent monitoring service.
- Rows whose device ends in `_recovered` are displayed with their original probe and marked as reconstructed. Their timestamp can represent session start rather than individual packet time. The private one-off recovery scripts and hardcoded recovered measurements are deliberately not distributed as generic tools.
- This release is assembled from the project's firmware and dashboard, with configuration extraction, configurable scheduling/HTTPS target, Wi-Fi sleep disabled and dashboard deduplication. It has not been compiled/flashed on hardware as part of repository preparation. See the tests below for the validation that can run without a board.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `config.h` missing | Copy `config.example.h` beside the `.ino` file |
| `CookieJar` or `setCookieJar` compilation error | Use a recent Arduino-ESP32 3.x package exposing these APIs; older versions are not interchangeable |
| Waiting for NTP | Wi-Fi access, captive portal completion and reachability of the configured NTP servers |
| `unauthorized` | Correct token type and value; updated script deployment |
| Response is HTML or not JSON | `/exec` URL, public web-app access, and redirect handling |
| Google upload fails but browsing works | Read the serial HTTP/TLS diagnostics; a failed upload does not itself prove Wi-Fi or portal failure |
| Portal form/token not found | Provider form may have changed; review the portal guide |
| Rows stop after reset | RAM buffering does not provide persistent storage |
| Dashboard is stale | Probe power, serial output, queued uploads and latest Sheets rows; also check the selected time range |

## Validation

```bash
python -m unittest discover -s tests -p 'test_*.py'
node tests/receiver.test.cjs
```

Tests exercise the real dashboard using Streamlit's test harness and the receiver using mocked Apps Script services. They do not validate real Google deployment, radio timing, portal behavior or physical hardware.

## Setup references

- [Espressif Arduino-ESP32 installation](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
- [Google Apps Script web apps](https://developers.google.com/apps-script/guides/web)
- [Streamlit Community Cloud secrets](https://docs.streamlit.io/deploy/streamlit-community-cloud/deploy-your-app/secrets-management)

## Author

Gaël Boënnec
