# Data and API contract

All API operations are JSON POST requests to the deployed Apps Script `/exec` URL. `Content-Type` should be `application/json`. An HTTP 200 is not enough: inspect the JSON `ok` field.

## Fields

| Field | Meaning |
| --- | --- |
| `id` | Device/boot identifier plus sequence; 1–100 letters, digits, `_` or `-` |
| `timestamp` | UTC ISO 8601 timestamp; captured before the ping pair |
| `device` | Probe label; 1–40 letters, digits, `_` or `-` |
| `target_ip` | Resolved ping destination, or empty on failure |
| `status` | `ok`, `timeout`, `wifi_disconnected`, `dns_error`, or `error` |
| `rtt_ms` | Successful target ICMP round-trip time in milliseconds; null otherwise |
| `gateway_ms` | Successful default-gateway ICMP RTT; null on failure |
| `rssi_dbm` | Wi-Fi received signal strength at measurement time |
| `http_ms` | HTTPS request-to-headers duration, stored only on the last row of a session |
| `http_code` | HTTP response code, or negative HTTPClient error; null when not measured |

`status` describes the target ping path, not the HTTP request. A target ping can be `ok` while HTTP returns 403. Gateway failures currently have no separate status field. Missing numeric values become empty cells in Sheets and are converted back to missing values by the dashboard.

## Write

```json
{
  "token": "YOUR_WRITE_TOKEN",
  "rows": [{
    "id": "example_boot_0",
    "timestamp": "REPLACE_WITH_CURRENT_UTC_ISO_TIMESTAMP",
    "device": "esp32_home",
    "target_ip": "192.0.2.1",
    "status": "ok",
    "rtt_ms": 18,
    "gateway_ms": 2,
    "rssi_dbm": -48,
    "http_ms": null,
    "http_code": null
  }]
}
```

The IP above is a documentation placeholder, not a suggested live target. Replace the timestamp with a current value. Requests must contain 1–120 rows. The receiver validates IDs, status, numeric types and a seven-day timestamp window, and serializes access with a script lock.

```json
{"ok": true, "inserted": 1, "accepted_ids": ["example_boot_0"]}
```

Retrying the same row on the same UTC day returns its accepted ID with `inserted: 0`. Deduplication compares IDs; it does not verify that repeated content is identical. Preserve IDs and timestamps during retries. A multi-day write is not a database transaction across all sheets; retries remain necessary after partial failure.

## Read

```json
{"action": "read", "token": "YOUR_READ_TOKEN"}
```

Response: `{"ok": true, "rows": [...]}`. The receiver reads the latest eight `data_` tabs in descending name order; Streamlit sorts timestamps and applies its own selected time window. This is not a paginated historical-export API.

Failures have the form `{"ok": false, "error": "unauthorized"}` or another error string. Tokens belong in request bodies, not URLs. There is no browser GET interface.

Apps Script's content response can redirect to `script.googleusercontent.com`. The firmware follows only the expected HTTPS host with a separate GET to obtain the acknowledgement, without resending the write token/body. Python requests handles the ordinary POST-to-GET redirect for dashboard reads.
