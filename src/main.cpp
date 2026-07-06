// Pushling v0.1 — Milestone 2: WiFi + first contact with GitHub
//
// What this does:
//   1. Connects to WiFi (credentials in secrets.h)
//   2. Makes an HTTPS GET to the GitHub events API
//   3. Prints the raw response beginning to serial
//   4. Shows connection status on the TFT
//
// Not yet: JSON parsing, creature sprites, hunger. One thing at a time.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "secrets.h"

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// How often to poll GitHub (ms). 2 min = 30 req/hour,
// safely under the 60/hour unauthenticated rate limit.
const unsigned long POLL_INTERVAL = 2UL * 60UL * 1000UL;
unsigned long lastPoll = 0;

// ---------- Display helpers ----------

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

// ---------- WiFi ----------

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
  tftStatus("WiFi OK", WiFi.localIP().toString().c_str(), ST77XX_GREEN);
}

// ---------- GitHub ----------

void pollGitHub() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    connectWiFi();
  }

  WiFiClientSecure client;
  // TODO(hardening): pin GitHub's root CA cert instead of setInsecure().
  // setInsecure() skips certificate verification — fine for a desk toy's
  // first milestone, not fine to leave forever. Tracked in README.
  client.setInsecure();

  HTTPClient http;
  String url = "https://api.github.com/users/" + String(GITHUB_USER) +
               "/events/public?per_page=5";

  Serial.printf("GET %s\n", url.c_str());
  tftStatus("Polling GitHub...", GITHUB_USER, ST77XX_CYAN);

  http.begin(client, url);
  // GitHub's API rejects requests without a User-Agent. This header
  // is mandatory, not optional. Ask me how many hours people lose here.
  http.addHeader("User-Agent", "Pushling/0.1");
  http.addHeader("Accept", "application/vnd.github+json");

  int code = http.GET();
  Serial.printf("HTTP status: %d\n", code);

  if (code == 200) {
    String body = http.getString();
    Serial.printf("Response length: %d bytes\n", body.length());
    Serial.println("First 500 chars:");
    Serial.println(body.substring(0, 500));

    char msg[32];
    snprintf(msg, sizeof(msg), "HTTP 200 (%d bytes)", body.length());
    tftStatus("GitHub OK!", msg, ST77XX_GREEN);
  } else {
    // 403 usually means rate-limited; negative codes are connection errors
    tftStatus("GitHub error", String(code).c_str(), ST77XX_RED);
  }

  http.end();
}

// ---------- Arduino lifecycle ----------

void setup() {
  Serial.begin(115200);
  delay(1000);  // give the serial monitor a moment to attach

  // Switched power rail for TFT + STEMMA QT
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  connectWiFi();
  pollGitHub();          // first poll immediately on boot
  lastPoll = millis();
}

void loop() {
  // Non-blocking poll timer — no delay() so future input/animation
  // code can live in this loop without being starved.
  if (millis() - lastPoll >= POLL_INTERVAL) {
    lastPoll = millis();
    pollGitHub();
  }
}