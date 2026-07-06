// Pushling v0.2 — Milestone 4: the creature gets a face
//
// New in this milestone:
//   - Sprite rendering (pushling_sprites.h) replaces text faces
//   - Layout: creature on the left, status text on the right
//   - Eating animation when a new push is detected
//   - Flicker-free updates: full redraws only on state change,
//     text-only redraws for the ticking clock

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "secrets.h"
#include "pushling_sprites.h"

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

const unsigned long POLL_INTERVAL = 2UL * 60UL * 1000UL;
unsigned long lastPoll = 0;

const int HUNGRY_AFTER = 8;   // hours
const int SAD_AFTER    = 24;  // hours

time_t lastPushTime = 0;

// ---------- Creature state ----------

enum PushlingState { STATE_BOOT, STATE_HAPPY, STATE_HUNGRY, STATE_SAD };
PushlingState currentState = STATE_BOOT;

// Layout constants (240x135 landscape)
const int SPRITE_SCALE = 7;                 // 16px art -> 112px creature
const int SPRITE_X = 8;
const int SPRITE_Y = 11;
const int TEXT_X = 130;                     // right column

PushlingState stateForHours(long hours) {
  if (hours < HUNGRY_AFTER) return STATE_HAPPY;
  if (hours < SAD_AFTER)    return STATE_HUNGRY;
  return STATE_SAD;
}

const uint16_t *spriteForState(PushlingState s) {
  switch (s) {
    case STATE_HAPPY:  return SPRITE_HAPPY;
    case STATE_HUNGRY: return SPRITE_HUNGRY;
    default:           return SPRITE_SAD2;
  }
}

const char *moodLine(PushlingState s) {
  switch (s) {
    case STATE_HAPPY:  return "content";
    case STATE_HUNGRY: return "hungry...";
    default:           return "STARVING";
  }
}

uint16_t moodColor(PushlingState s) {
  switch (s) {
    case STATE_HAPPY:  return ST77XX_GREEN;
    case STATE_HUNGRY: return ST77XX_YELLOW;
    default:           return ST77XX_RED;
  }
}

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

// Redraw only the right-hand text column (keeps sprite untouched -> no flicker)
void drawStats() {
  tft.fillRect(TEXT_X - 4, 0, 240 - TEXT_X + 4, 135, ST77XX_BLACK);

  tft.setCursor(TEXT_X, 18);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println("Pushling");

  tft.setCursor(TEXT_X, 45);
  tft.setTextColor(moodColor(currentState));
  tft.setTextSize(1);
  tft.println(moodLine(currentState));

  tft.setCursor(TEXT_X, 65);
  tft.setTextColor(ST77XX_WHITE);
  if (lastPushTime == 0) {
    tft.println("no pushes seen");
  } else {
    long mins = (time(nullptr) - lastPushTime) / 60;
    long hours = mins / 60;
    char fedLine[24];
    if (hours > 0) {
      snprintf(fedLine, sizeof(fedLine), "fed %ldh %ldm ago", hours, mins % 60);
    } else {
      snprintf(fedLine, sizeof(fedLine), "fed %ldm ago", mins);
    }
    tft.println(fedLine);
  }

  tft.setCursor(TEXT_X, 110);
  tft.setTextColor(0x7BEF);  // dim gray
  tft.println(GITHUB_USER);
}

// Full scene redraw: creature + stats
void drawScene() {
  tft.fillScreen(ST77XX_BLACK);
  drawSprite(tft, spriteForState(currentState), SPRITE_X, SPRITE_Y, SPRITE_SCALE);
  drawStats();
}

// Blocking celebration — acceptable for now; keys arrive next milestone,
// at which point this becomes a non-blocking animation state.
void playEatingAnimation() {
  const uint16_t *frames[] = {SPRITE_EAT1, SPRITE_EAT2, SPRITE_EAT1,
                              SPRITE_EAT2, SPRITE_EAT3};
  tft.fillRect(TEXT_X - 4, 40, 240 - TEXT_X + 4, 40, ST77XX_BLACK);
  tft.setCursor(TEXT_X, 45);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.println("* NOM *");

  for (const uint16_t *f : frames) {
    drawSprite(tft, f, SPRITE_X, SPRITE_Y, SPRITE_SCALE);
    delay(400);
  }
  delay(800);  // linger on the sparkle frame
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
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // board runs UTC
  tftStatus("Pushling", "Syncing clock (NTP)...", ST77XX_YELLOW);
  Serial.print("Waiting for NTP");
  time_t now = time(nullptr);
  while (now < 8 * 3600) {
    delay(250);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.printf("\nClock synced: %s", ctime(&now));
}

// ---------- GitHub ----------

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
  return mktime(&t);  // valid because board TZ is UTC (see syncClock)
}

void pollGitHub() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWiFi();
    drawScene();
  }

  WiFiClientSecure client;
  client.setInsecure();  // TODO(hardening): pin GitHub root CA

  HTTPClient http;
  String url = "https://api.github.com/users/" + String(GITHUB_USER) +
               "/events/public?per_page=10";

  http.begin(client, url);
  http.addHeader("User-Agent", "Pushling/0.2");
  http.addHeader("Accept", "application/vnd.github+json");

  int code = http.GET();
  Serial.printf("GitHub HTTP: %d\n", code);

  if (code != 200) {
    // Don't panic the creature over a bad poll; note it and retry later
    Serial.println("Poll failed, will retry next interval.");
    http.end();
    return;
  }

  JsonDocument filter;
  filter[0]["type"] = true;
  filter[0]["created_at"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    Serial.printf("JSON parse failed: %s\n", err.c_str());
    return;
  }

  for (JsonObject ev : doc.as<JsonArray>()) {
    const char *type = ev["type"];
    if (type && strcmp(type, "PushEvent") == 0) {
      time_t parsed = parseISO8601(ev["created_at"]);
      Serial.printf("Latest push: %s\n", (const char *)ev["created_at"]);

      bool isNewMeal = (lastPushTime != 0 && parsed > lastPushTime);
      lastPushTime = parsed;

      if (isNewMeal) {
        Serial.println("NEW PUSH — nom nom nom!");
        playEatingAnimation();
        currentState = stateForHours(0);
        drawScene();
      }
      break;
    }
  }
}

// ---------- State upkeep ----------

void refreshState() {
  if (lastPushTime == 0) return;

  long hours = (time(nullptr) - lastPushTime) / 3600;
  PushlingState newState = stateForHours(hours);

  if (newState != currentState) {
    currentState = newState;
    Serial.printf("Mood change -> %s\n", moodLine(currentState));
    drawScene();          // sprite changed: full redraw
  } else {
    drawStats();          // just tick the clock text
  }
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

  // First scene: creature appears
  if (lastPushTime != 0) {
    long hours = (time(nullptr) - lastPushTime) / 3600;
    currentState = stateForHours(hours);
  } else {
    currentState = STATE_HUNGRY;  // never fed = born hungry
  }
  drawScene();
  lastPoll = millis();
}

void loop() {
  if (millis() - lastPoll >= POLL_INTERVAL) {
    lastPoll = millis();
    pollGitHub();
  }

  static unsigned long lastTick = 0;
  if (millis() - lastTick >= 60UL * 1000UL) {
    lastTick = millis();
    refreshState();
  }
}