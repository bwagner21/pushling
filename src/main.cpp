// Pushling v0.1 — Milestone 3: the creature learns hunger
//
// New in this milestone:
//   - NTP time sync (we need "now" to compute "how long since fed")
//   - Parse the GitHub events JSON with ArduinoJson (filtered — we keep
//     only 3 fields per event and discard the rest as it parses)
//   - Find the most recent PushEvent and compute minutes since
//   - Display hunger state as text + color (sprites come next milestone)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "secrets.h"

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

const unsigned long POLL_INTERVAL = 2UL * 60UL * 1000UL;  // 2 min
unsigned long lastPoll = 0;

// Hunger thresholds (hours). Tune to taste / self-discipline.
const int HUNGRY_AFTER = 8;
const int SAD_AFTER    = 24;

// Epoch seconds of the most recent PushEvent; 0 = not yet known
time_t lastPushTime = 0;

// ---------- Display ----------

void tftStatus(const char *line1, const char *line2, uint16_t color) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 30);
  tft.setTextColor(color);
  tft.setTextSize(2);
  tft.println(line1);
  tft.setCursor(10, 60);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println(line2);
}

// Render the creature's current state (text edition — sprites are M4)
void drawPushling() {
  if (lastPushTime == 0) {
    tftStatus("Pushling", "No pushes found yet...", ST77XX_MAGENTA);
    return;
  }

  time_t now = time(nullptr);
  long mins = (now - lastPushTime) / 60;
  long hours = mins / 60;

  char fedLine[40];
  if (hours > 0) {
    snprintf(fedLine, sizeof(fedLine), "Fed %ldh %ldm ago", hours, mins % 60);
  } else {
    snprintf(fedLine, sizeof(fedLine), "Fed %ldm ago", mins);
  }

  if (hours < HUNGRY_AFTER) {
    tftStatus(":) content", fedLine, ST77XX_GREEN);
  } else if (hours < SAD_AFTER) {
    tftStatus(":| hungry...", fedLine, ST77XX_YELLOW);
  } else {
    tftStatus(":( starving", fedLine, ST77XX_RED);
  }

  Serial.printf("Pushling state: %s\n", fedLine);
}

// ---------- WiFi + time ----------

void connectWiFi() {
  Serial.printf("Connecting to %s", WIFI_SSID);
  tftStatus("Pushling", "Connecting to WiFi...", ST77XX_YELLOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
}

void syncClock() {
  // TZ offset 0,0 = the board's clock runs in UTC. GitHub timestamps
  // are UTC too, so all our math stays in one timezone. Display-local
  // time is a later problem.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  tftStatus("Pushling", "Syncing clock (NTP)...", ST77XX_YELLOW);
  Serial.print("Waiting for NTP");

  time_t now = time(nullptr);
  while (now < 8 * 3600) {  // until we get a plausible time (not 1970)
    delay(250);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.printf("\nClock synced: %s", ctime(&now));
}

// ---------- GitHub ----------

// Parse "2026-07-05T21:14:09Z" (ISO 8601, always UTC) into epoch seconds
time_t parseISO8601(const char *ts) {
  struct tm t = {};
  int y, mo, d, h, mi, s;
  if (sscanf(ts, "%d-%d-%dT%d:%d:%dZ", &y, &mo, &d, &h, &mi, &s) != 6) {
    return 0;
  }
  t.tm_year = y - 1900;
  t.tm_mon  = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min  = mi;
  t.tm_sec  = s;
  // mktime() assumes local time — but our board's TZ is UTC (see
  // syncClock), so local IS UTC and this is correct. If we ever set a
  // real timezone, this function must switch to a UTC-aware conversion.
  return mktime(&t);
}

void pollGitHub() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWiFi();
  }

  WiFiClientSecure client;
  client.setInsecure();  // TODO(hardening): pin GitHub root CA

  HTTPClient http;
  String url = "https://api.github.com/users/" + String(GITHUB_USER) +
               "/events/public?per_page=10";

  http.begin(client, url);
  http.addHeader("User-Agent", "Pushling/0.1");
  http.addHeader("Accept", "application/vnd.github+json");

  int code = http.GET();
  Serial.printf("GitHub HTTP: %d\n", code);

  if (code == 200) {
    // Filter: of each event, keep ONLY type + created_at. Everything
    // else (payloads, repo objects, actor avatars) is discarded during
    // parsing instead of ever occupying RAM. This is the trick that
    // keeps a multi-KB API response cheap on a microcontroller.
    JsonDocument filter;
    filter[0]["type"] = true;
    filter[0]["created_at"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::Filter(filter));

    if (err) {
      Serial.printf("JSON parse failed: %s\n", err.c_str());
      tftStatus("Parse error", err.c_str(), ST77XX_RED);
    } else {
      // Events arrive newest-first; find the first PushEvent
      bool found = false;
      for (JsonObject ev : doc.as<JsonArray>()) {
        const char *type = ev["type"];
        if (type && strcmp(type, "PushEvent") == 0) {
          const char *ts = ev["created_at"];
          time_t parsed = parseISO8601(ts);
          Serial.printf("Latest push: %s (epoch %ld)\n", ts, (long)parsed);

          // A push newer than any we've seen = the creature eats
          if (lastPushTime != 0 && parsed > lastPushTime) {
            Serial.println("NEW PUSH — nom nom nom!");
            tftStatus("* NOM *", "Pushling has been fed!", ST77XX_GREEN);
            delay(2000);  // brief celebration; animation comes later
          }
          lastPushTime = parsed;
          found = true;
          break;
        }
      }
      if (!found) {
        Serial.println("No PushEvent in the last 10 events.");
      }
    }
  } else {
    tftStatus("GitHub error", String(code).c_str(), ST77XX_RED);
    http.end();
    return;
  }

  http.end();
  drawPushling();
}

// ---------- Arduino lifecycle ----------

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  connectWiFi();
  syncClock();
  pollGitHub();
  lastPoll = millis();
}

void loop() {
  if (millis() - lastPoll >= POLL_INTERVAL) {
    lastPoll = millis();
    pollGitHub();
  }

  // Redraw once a minute so "fed Xm ago" ticks up between polls
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw >= 60UL * 1000UL) {
    lastDraw = millis();
    drawPushling();
  }
}