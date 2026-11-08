# Captive portal adapter: Quantic

Joining Wi-Fi and obtaining an IP address does not guarantee Internet access. Some networks redirect new devices to a login form or require an account declaration before enabling traffic.

The optional adapter in `firmware/SitePulse/SitePulse.ino` implements the Quantic form observed during the project. It is isolated logically in `authenticateQuantic()`, supported by `internetAvailable()`, `formEncode()`, `attributeValue()` and `csrfToken()`. Its settings live in `config.h`.

## Enable the existing adapter

1. First configure the normal Wi-Fi credentials, Google endpoint and write token.
2. Set `USE_QUANTIC_PORTAL = true`.
3. Fill `QUANTIC_EMAIL` and `QUANTIC_PASSWORD` with your own authorized account.
4. Keep the known `QUANTIC_URL` unless you are actually modifying the adapter.
5. Read any device-ownership declaration in your provider's portal. Set `CONFIRM_MY_DEVICES = true` only when that declaration is true for the devices associated with your account. This flag is not needed to use ordinary Wi-Fi.
6. Flash the board and watch Serial at 115200 baud. Do not post account credentials, raw form contents or session cookies in issue reports.

## Request sequence

1. `internetAvailable()` requests `https://www.gstatic.com/generate_204`. Only HTTP 204 counts as success; a redirect is not accepted as proof of Internet access.
2. If Internet access is already available, login is skipped.
3. The adapter creates an HTTP cookie jar, GETs the Quantic form and extracts its hidden `_token` field.
4. It POSTs URL-encoded `_token`, `email` and `password` fields while preserving cookies. Automatic redirects are disabled before this credential-bearing request.
5. It follows a returned 302/303 only to the explicit Quantic hosts allowed by the implementation. Relative same-host locations are handled too.
6. If the next page contains `confirm_number_of_devices`, the adapter stops unless `CONFIRM_MY_DEVICES` is enabled. If enabled, it extracts the new CSRF token and sends `_token` plus `confirm_number_of_devices=on` to the configured Quantic endpoint.
7. It checks Internet access again. A redirect or a successful POST alone is insufficient.

The loop spaces portal attempts by at least five minutes. After an upload transport failure, it checks Internet access before attempting portal authentication again. A failed Google request does not automatically trigger Wi-Fi disconnection. The connectivity check itself can be blocked independently, so a failed check is not definitive proof of portal expiration.

## Adapting to another form

Changing only the URL is insufficient. Inspect the actual form on a network/account you are authorized to use and identify its action URL, method, encoding, field names, hidden tokens, cookies, allowed redirect hosts and success signal. Update the adapter deliberately to match those details. Its token parser only recognizes simple quoted HTML attributes; it is not a complete HTML parser.

This code does not automate arbitrary questionnaires, infer truthful answers, execute JavaScript, complete CAPTCHA, handle MFA, implement OAuth flows, or solve enterprise WPA2 authentication. Those mechanisms are different from a simple captive web form. A provider-supported device registration method may be more suitable for unattended equipment.

## TLS limitation

The inherited prototype calls `setInsecure()`, including for portal credentials. Host allowlists reduce accidental redirect forwarding but do not replace certificate authentication. A production adaptation must validate the relevant server certificates (and arrange a trustworthy clock/bootstrap strategy) before sending credentials. Portal compatibility has not been re-tested against the live provider during this publication.
