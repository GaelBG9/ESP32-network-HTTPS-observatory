// SitePulse - Arduino ESP32 core 3.x, ArduinoJson 7.x
// Prototype: setInsecure() does not authenticate HTTPS servers. See README.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ctype.h>
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "config.h"

String pending[CAPACITY];
int pendingCount = 0;
uint32_t lostRows = 0;
uint32_t sequence = 0;
String bootId;
int64_t lastSlot = -1;
uint32_t lastRetry = 0;
uint32_t lastReconnect = 0;

bool portalReady = false;
uint32_t lastPortalAttempt = 0;
bool portalAttempted = false;
bool ntpStarted = false;
bool checkInternetAfterFailure = false;
uint32_t lastInternetCheck = 0;

String formEncode(const String& input) {
  const char hex[] = "0123456789ABCDEF";
  String output;
  for (size_t i = 0; i < input.length(); i++) {
    unsigned char c = input[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      output += (char)c;
    } else {
      output += '%';
      output += hex[c >> 4];
      output += hex[c & 15];
    }
  }
  return output;
}

// Recherche des attributs simples du champ cache, quel que soit leur ordre.
String attributeValue(const String& tag, const String& name) {
  size_t pos = 0;
  while ((pos = tag.indexOf(name, pos)) != (size_t)-1) {
    if (pos > 0 && !isspace((unsigned char)tag[pos - 1])) {
      pos += name.length();
      continue;
    }
    size_t cursor = pos + name.length();
    while (cursor < tag.length() && isspace((unsigned char)tag[cursor])) cursor++;
    if (cursor >= tag.length() || tag[cursor] != '=') {
      pos = cursor;
      continue;
    }
    cursor++;
    while (cursor < tag.length() && isspace((unsigned char)tag[cursor])) cursor++;
    char quote = tag[cursor++];
    if (quote != '"' && quote != '\'') return "";
    int end = tag.indexOf(quote, cursor);
    return end >= 0 ? tag.substring(cursor, end) : "";
  }
  return "";
}

String csrfToken(const String& html) {
  int pos = 0;
  while ((pos = html.indexOf("<input", pos)) >= 0) {
    int end = html.indexOf('>', pos);
    if (end < 0) return "";
    String tag = html.substring(pos, end + 1);
    if (attributeValue(tag, "name") == "_token") return attributeValue(tag, "value");
    pos = end + 1;
  }
  return "";
}

void connectionDiagnostic(WiFiClientSecure& client, const char* stage) {
  char detail[160] = {};
  int tlsError = client.lastError(detail, sizeof(detail));
  Serial.printf(
      "Diagnostic %s: WiFi=%d IP=%s RSSI=%d RAM=%u TLS=%d %s\n",
      stage, (int)WiFi.status(), WiFi.localIP().toString().c_str(),
      (int)WiFi.RSSI(), (unsigned int)ESP.getFreeHeap(), tlsError, detail);
}

bool internetAvailable() {
  WiFiClientSecure client;
  client.setInsecure(); // meme limitation TLS que le prototype de mesure
  client.setHandshakeTimeout(10);
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, "https://www.gstatic.com/generate_204")) return false;
  int code = http.GET();
  http.end();
  return code == 204; // une redirection seule ne vaut pas validation
}

bool authenticateQuantic() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (internetAvailable()) {
    Serial.println("Acces Internet deja disponible.");
    return true;
  }
  if (String(QUANTIC_EMAIL) == "TON_EMAIL_QUANTIC" ||
      String(QUANTIC_PASSWORD) == "TON_MOT_DE_PASSE_QUANTIC") {
    Serial.println("Renseigner QUANTIC_EMAIL et QUANTIC_PASSWORD.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  CookieJar cookies;
  HTTPClient http;
  http.setReuse(false);
  http.setCookieJar(&cookies);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  // Ne pas suivre aveuglement un changement d'hote avant le POST.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, QUANTIC_URL)) return false;
  int code = http.GET();
  if (code < 0) connectionDiagnostic(client, "Quantic GET");
  String html;
  if (code == 200) html = http.getString();
  http.end();
  String token = csrfToken(html);
  if (code != 200 || token.isEmpty()) {
    Serial.printf("Quantic: formulaire inaccessible ou jeton absent (HTTP %d).\n", code);
    return false;
  }

  String body = "_token=" + formEncode(token) +
                "&email=" + formEncode(QUANTIC_EMAIL) +
                "&password=" + formEncode(QUANTIC_PASSWORD);
  const char* headerKeys[] = {"Location"};
  http.collectHeaders(headerKeys, 1);
  if (!http.begin(client, QUANTIC_URL)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Origin", "https://www.quantic-telecom.net");
  http.addHeader("Referer", QUANTIC_URL);
  code = http.POST(body);
  String location = http.header("Location");
  String destination = location;
  int query = destination.indexOf('?');
  if (query >= 0) destination = destination.substring(0, query);
  Serial.println("Quantic redirige vers: " + destination);
  http.end();
  // Ouvrir la page suivante avec les memes cookies.
  if (code == 302 || code == 303) {
    if (location.startsWith("/") && !location.startsWith("//")) {
      location = "https://www.quantic-telecom.net" + location;
    }
    bool allowed =
        location == "https://www.quantic-telecom.net" ||
        location.startsWith("https://www.quantic-telecom.net/") ||
        location.startsWith("https://auth.quantic-telecom.net/");
    if (!allowed || !http.begin(client, location)) {
      Serial.println("Quantic: redirection inattendue.");
      return false;
    }
    int pageCode = http.GET();
    String page = pageCode == 200 ? http.getString() : "";
    http.end();
    Serial.printf("Page apres connexion: HTTP %d\n", pageCode);
    if (page.indexOf("confirm_number_of_devices") >= 0) {
      if (!CONFIRM_MY_DEVICES) {
        Serial.println("Confirmation requise: regler CONFIRM_MY_DEVICES si applicable.");
        return false;
      }
      String confirmationToken = csrfToken(page);
      if (confirmationToken.isEmpty()) {
        Serial.println("Jeton de confirmation absent.");
        return false;
      }
      String confirmationBody =
          "_token=" + formEncode(confirmationToken) +
          "&confirm_number_of_devices=on";
      if (!http.begin(client, QUANTIC_URL)) return false;
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.addHeader("Origin", "https://www.quantic-telecom.net");
      http.addHeader("Referer", QUANTIC_URL);
      int confirmationCode = http.POST(confirmationBody);
      String nextLocation = http.header("Location");
      http.getString();
      http.end();
      Serial.printf("Confirmation appareils: HTTP %d\n", confirmationCode);
      if (confirmationCode == 302 || confirmationCode == 303) {
        if (nextLocation.startsWith("/") && !nextLocation.startsWith("//")) {
          nextLocation = "https://www.quantic-telecom.net" + nextLocation;
        }
        bool nextAllowed =
            nextLocation == "https://www.quantic-telecom.net" ||
            nextLocation.startsWith("https://www.quantic-telecom.net/") ||
            nextLocation.startsWith("https://auth.quantic-telecom.net/");
        if (nextAllowed && http.begin(client, nextLocation)) {
          int finalCode = http.GET();
          http.getString();
          http.end();
          Serial.printf("Apres confirmation: HTTP %d\n", finalCode);
        }
      }
    }
  }
  Serial.printf("Quantic: POST termine (HTTP %d). Verification Internet...\n", code);
  bool available = internetAvailable();
  Serial.println(available ? "Internet confirme." : "Acces non confirme. Aucun secret affiche.");
  return available;
}


struct PingResult {
  SemaphoreHandle_t done;
  int milliseconds;
};

void pingSuccess(esp_ping_handle_t handle, void* args);
void pingEnd(esp_ping_handle_t handle, void* args);

void pingSuccess(esp_ping_handle_t handle, void* args) {
  auto* result = static_cast<PingResult*>(args);
  uint32_t elapsed = 0;
  esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
  result->milliseconds = elapsed;
}

void pingEnd(esp_ping_handle_t handle, void* args) {
  auto* result = static_cast<PingResult*>(args);
  xSemaphoreGive(result->done);
}

// -1 timeout, -2 fallo interno. Un paquete por llamada.
int pingOnce(const IPAddress& address) {
  PingResult result;
  result.done = xSemaphoreCreateBinary();
  result.milliseconds = -1;
  if (!result.done) return -2;

  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  String ip = address.toString();
  if (!ipaddr_aton(ip.c_str(), &config.target_addr)) {
    vSemaphoreDelete(result.done);
    return -2;
  }
  config.count = 1;
  config.interval_ms = 10;
  config.timeout_ms = 1000;
  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = pingSuccess;
  callbacks.on_ping_end = pingEnd;
  callbacks.cb_args = &result;
  esp_ping_handle_t handle = nullptr;
  if (esp_ping_new_session(&config, &callbacks, &handle) != ESP_OK) {
    vSemaphoreDelete(result.done);
    return -2;
  }
  if (esp_ping_start(handle) != ESP_OK) {
    esp_ping_delete_session(handle);
    vSemaphoreDelete(result.done);
    return -2;
  }
  // La tarea ping siempre senala el final tras el timeout.
  xSemaphoreTake(result.done, portMAX_DELAY);
  esp_ping_stop(handle);
  esp_ping_delete_session(handle);
  vSemaphoreDelete(result.done);
  return result.milliseconds;
}

String utcTimestamp() {
  time_t now = time(nullptr);
  struct tm utc;
  gmtime_r(&now, &utc);
  char text[25];
  strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(text);
}

void enqueue(const String& row) {
  if (pendingCount == CAPACITY) {
    lostRows++;
    Serial.printf("BUFFER LLENO: medida descartada. Total: %lu\n",
                  (unsigned long)lostRows);
    return;
  }
  pending[pendingCount++] = row;
}

// Prototipo: HTTPS cifrado, sin validar el certificado del servidor.
// No imprime contrasenas ni tokens.
void measureHttps(JsonDocument& row) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, HTTPS_URL)) {
    row["http_code"] = -1;
    return;
  }
  uint32_t started = millis();
  int code = http.GET();
  row["http_ms"] = millis() - started; // hasta cabeceras, incluye TLS
  row["http_code"] = code; // negativos = error del cliente
  http.end();
}

// Apps Script devuelve un 302 a googleusercontent.
// Se sigue mediante GET sin reenviar el token ni el cuerpo del POST.
bool postBatch(const String& body, String& response) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(20);
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(15000);
  http.setTimeout(45000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* headers[] = {"Location"};
  http.collectHeaders(headers, 1);
  if (!http.begin(client, ENDPOINT)) return false;
  http.addHeader("Content-Type", "application/json");
  Serial.printf("Google POST: %u octets, RAM libre %u octets\n",
                (unsigned int)body.length(), (unsigned int)ESP.getFreeHeap());
  int code = http.POST(body);
  String location = http.header("Location");
  if (code == 200) response = http.getString();
  if (code < 0) {
    Serial.printf("Google POST echoue: %d (%s)\n",
                  code, HTTPClient::errorToString(code).c_str());
    connectionDiagnostic(client, "Google POST");
  } else {
    Serial.printf("Google POST: HTTP %d\n", code);
  }
  http.end();
  client.stop();

  if (code == 302 || code == 303) {
    if (!location.startsWith("https://script.googleusercontent.com/")) {
      Serial.println("Google Sheets: redirection inattendue.");
      return false;
    }
    // Nouvelle connexion TLS pour recuperer la confirmation Google.
    WiFiClientSecure redirectedClient;
    redirectedClient.setInsecure();
    redirectedClient.setHandshakeTimeout(20);
    HTTPClient redirected;
    redirected.setReuse(false);
    redirected.setConnectTimeout(15000);
    redirected.setTimeout(45000);
    if (!redirected.begin(redirectedClient, location)) return false;
    code = redirected.GET();
    if (code == 200) response = redirected.getString();
    if (code < 0) {
      Serial.printf("Google confirmation GET echoue: %d (%s)\n",
                    code, HTTPClient::errorToString(code).c_str());
      connectionDiagnostic(redirectedClient, "Google confirmation");
    } else {
      Serial.printf("Google confirmation GET: HTTP %d\n", code);
    }
    redirected.end();
    redirectedClient.stop();
  }
  return code == 200;
}

bool uploadBatch() {
  if (!pendingCount || WiFi.status() != WL_CONNECTED) return false;
  int count = min(pendingCount, UPLOAD_BATCH_SIZE);
  JsonDocument request;
  request["token"] = WRITE_TOKEN;
  JsonArray rows = request["rows"].to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonDocument row;
    if (deserializeJson(row, pending[i])) return false;
    rows.add(row.as<JsonObject>());
  }
  String body;
  serializeJson(request, body);
  String response;
  if (!postBatch(body, response)) {
    checkInternetAfterFailure = true;
    Serial.printf("Envoi non confirme: %d lignes conservees en RAM.\n", pendingCount);
    return false;
  }
  checkInternetAfterFailure = false;
  JsonDocument reply;
  if (deserializeJson(reply, response)) {
    Serial.println("Respuesta sin JSON. Revisa despliegue /exec.");
    return false;
  }
  if (reply["ok"] != true) {
    Serial.printf("Apps Script: %s\n", reply["error"] | "error");
    return false;
  }
  // Borrar solo las filas confirmadas. Un reintento conserva los mismos IDs.
  JsonArray accepted = reply["accepted_ids"].as<JsonArray>();
  if (accepted.size() != (size_t)count) return false;
  for (int i = 0; i < count; i++) {
    JsonDocument row;
    deserializeJson(row, pending[i]);
    bool found = false;
    for (JsonVariant id : accepted) {
      if (row["id"].as<String>() == id.as<String>()) found = true;
    }
    if (!found) return false;
  }
  for (int i = count; i < pendingCount; i++) pending[i - count] = pending[i];
  int previousCount = pendingCount;
  pendingCount -= count;
  for (int i = pendingCount; i < previousCount; i++) pending[i] = "";
  portalReady = true;
  checkInternetAfterFailure = false;
  Serial.printf("Google Sheets OK: %d confirmadas, %d pendientes\n", count, pendingCount);
  return true;
}

void runSession() {
  IPAddress target;
  bool resolved = WiFi.status() == WL_CONNECTED && WiFi.hostByName(TARGET, target);
  Serial.printf("Sesion UTC %s, destino %s\n",
                utcTimestamp().c_str(), resolved ? target.toString().c_str() : "DNS fallido");
  for (int i = 0; i < SAMPLES; i++) {
    uint32_t started = millis();
    JsonDocument row;
    row["id"] = bootId + "_" + String(sequence++);
    row["timestamp"] = utcTimestamp();
    row["device"] = DEVICE;
    row["rtt_ms"] = nullptr;
    row["gateway_ms"] = nullptr;
    row["rssi_dbm"] = nullptr;
    row["http_ms"] = nullptr;
    row["http_code"] = nullptr;
    row["target_ip"] = resolved ? target.toString() : "";
    const char* status = "wifi_disconnected";
    if (WiFi.status() == WL_CONNECTED) {
      row["rssi_dbm"] = WiFi.RSSI();
      int gateway = pingOnce(WiFi.gatewayIP());
      if (gateway >= 0) row["gateway_ms"] = gateway;
      status = "dns_error";
      if (resolved) {
        int rtt = pingOnce(target);
        status = rtt >= 0 ? "ok" : (rtt == -1 ? "timeout" : "error");
        if (rtt >= 0) row["rtt_ms"] = rtt;
      }
    }
    row["status"] = status;
    String json;
    serializeJson(row, json);
    enqueue(json);
    Serial.printf("%d/%d %s RTT=%s box=%s\n", i + 1, SAMPLES, status,
                  row["rtt_ms"].as<String>().c_str(),
                  row["gateway_ms"].as<String>().c_str());
    // Solo pausamos si ambas respuestas llegaron antes del intervalo.
    uint32_t elapsed = millis() - started;
    if (elapsed < PAIR_INTERVAL_MS) delay(PAIR_INTERVAL_MS - elapsed);
  }
  // HTTPS al final para no interrumpir el intervalo de los pings.
  // Se anade a la ultima fila de la sesion cuando cabe en el buffer.
  if (WiFi.status() == WL_CONNECTED && pendingCount > 0) {
    JsonDocument last;
    deserializeJson(last, pending[pendingCount - 1]);
    if (last["id"].as<String>() == bootId + "_" + String(sequence - 1)) {
      measureHttps(last);
      pending[pendingCount - 1] = "";
      serializeJson(last, pending[pendingCount - 1]);
    }
  }
  uint32_t uploadStarted = millis();
  while (uploadBatch()) {
    delay(300);
    if (millis() - uploadStarted > 90000) break;
  }
  lastRetry = millis();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  char id[64];
  snprintf(id, sizeof(id), "%08lx_%08lx_%08lx",
           (unsigned long)(ESP.getEfuseMac() >> 32),
           (unsigned long)ESP.getEfuseMac(), (unsigned long)esp_random());
  bootId = id;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false); // Keep the radio awake for latency measurements.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Conectando WiFi...");
  Serial.println("MAC ESP32: " + WiFi.macAddress());
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    portalReady = false;
    if (millis() - lastReconnect > 30000) {
      lastReconnect = millis();
      // Eviter une commande concurrente avec la reconnexion automatique.
      if (WiFi.status() != WL_IDLE_STATUS) WiFi.reconnect();
      Serial.println("Wi-Fi indisponible, reconnexion en cours...");
    }
  }

  // Les sessions de mesure restent prioritaires une fois l'heure connue.
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    int64_t slot = (int64_t)now / SESSION_INTERVAL_SECONDS;
    if (lastSlot < 0 || slot > lastSlot) {
      lastSlot = slot;
      runSession();
      return;
    }
  }

  // Un echec Google ne prouve pas une expiration du portail.
  // Verifier Internet avant de demander une nouvelle authentification.
  if (connected && checkInternetAfterFailure &&
      millis() - lastInternetCheck >= 60000) {
    lastInternetCheck = millis();
    checkInternetAfterFailure = false;
    if (!internetAvailable()) {
      portalReady = false;
      Serial.println("Acces Internet non confirme, verification Quantic prevue.");
    } else {
      Serial.println("Internet disponible: reessai Google sans couper le Wi-Fi.");
    }
  }

  if (connected && USE_QUANTIC_PORTAL && !portalReady) {
    if (!portalAttempted || millis() - lastPortalAttempt >= 300000) {
      portalAttempted = true;
      portalReady = authenticateQuantic();
      lastPortalAttempt = millis();
    }
  }

  if (WiFi.status() == WL_CONNECTED && !ntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpStarted = true;
  }

  if (time(nullptr) < 1700000000) {
    static uint32_t lastNotice = 0;
    if (millis() - lastNotice > 10000) {
      lastNotice = millis();
      Serial.println("Attente acces Internet / heure NTP...");
    }
    delay(100);
    return;
  }

  // Reessayer Google meme si le test du portail a echoue.
  // Une connexion Google confirmee etablit aussi l'acces Internet.
  if (WiFi.status() == WL_CONNECTED && pendingCount &&
      millis() - lastRetry > 60000) {
    uint32_t started = millis();
    while (uploadBatch()) {
      delay(300);
      if (millis() - started > 90000) break;
    }
    lastRetry = millis();
  }
  delay(100);
}
