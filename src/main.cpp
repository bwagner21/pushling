#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// These pin names (TFT_CS, TFT_DC, TFT_RST, TFT_BACKLITE, TFT_I2C_POWER)
// are pre-defined by the board variant for the Feather ESP32-S2 TFT.
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);

  // The display (and STEMMA QT port) are on a switched power rail.
  // It must be turned on or the screen stays black!
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // Turn on the backlight
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // Init display: 240x135 ST7789
  tft.init(135, 240);
  tft.setRotation(3);          // landscape, USB port on the left
  tft.fillScreen(ST77XX_BLACK);

  tft.setCursor(10, 20);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(3);
  tft.println("Hello World");

  tft.setCursor(10, 60);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println("Pushling v0.1");

  tft.setCursor(10, 90);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.println("If you can read this, the");
  tft.setCursor(10, 100);
  tft.println("display + toolchain work!");

  Serial.println("Display initialized.");
}

void loop() {
  // Heartbeat in the serial monitor so you know the loop is alive
  Serial.println("alive");
  delay(2000);
}