#include <Arduino.h>
#include <freertos/task.h>

#include "webdata.h"
#include "led.h"

// Show wlan and other status via rgb led
#include <Adafruit_NeoPixel.h>

static uint8_t maxRed = 240;
static uint8_t maxGreen = 255;
static uint8_t maxBlue = 255;
static long maxHealth = 1000;

static const uint32_t neutralColor = Adafruit_NeoPixel::Color(0, 0, 0);
static const uint32_t startColor = Adafruit_NeoPixel::Color(maxRed, maxGreen, maxBlue);
static const uint32_t wlanErrorColor = Adafruit_NeoPixel::Color(0, maxGreen, maxBlue);
static const uint32_t influxErrorColor = Adafruit_NeoPixel::Color(maxRed / 2, 0, maxBlue);

static const uint8_t neopixelPin = 27;
static Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, neopixelPin, NEO_GRB + NEO_KHZ800);

static volatile uint32_t buttonPressed = 0;

const char *lastError = "OK";

long getSpO2Health() {
  if( webData.f7Data.spO2 <= 70 )
    return 0;

  if (webData.f7Data.spO2 >= 100 )
    return maxHealth;

  return map(webData.f7Data.spO2, 70, 100, 0, maxHealth);
}

long getPpmHealth() {
  if (webData.f7Data.ppm <= 40 || webData.f7Data.ppm > 180)
    return 0;

  if (webData.f7Data.ppm <= 60)
    return map(webData.f7Data.ppm, 40, 60, 0, maxHealth);

  if (webData.f7Data.spO2 <= 80)
    return maxHealth;

  return map(webData.f7Data.ppm, 80, 180, maxHealth, 0);
}

uint32_t piBlinkColor() {
  if (webData.f7Data.deziPI >= 8 || !webData.f7Connected)
    return neutralColor;

  if (webData.f7Data.deziPI <= 2)
    return Adafruit_NeoPixel::Color(maxRed, 0, maxBlue / 6);

  uint8_t red = map(webData.f7Data.deziPI, 2, 8, maxRed, 0);
  uint8_t green = map(webData.f7Data.deziPI, 2, 8, 0, maxGreen);

  return Adafruit_NeoPixel::Color(red, green, maxBlue / 6);
}

// Color range from healthy to dead...
uint32_t healthColor() {
  long worstHealth = getSpO2Health();
  long health = getPpmHealth();
  if (health < worstHealth)
    worstHealth = health;

  health *= health;
  uint8_t green = map(health, 0, maxHealth * maxHealth, 0, maxGreen*3/4);
  uint8_t red = map(health, 0, maxHealth * maxHealth, maxRed*3/4, 0);

  return Adafruit_NeoPixel::Color(red, green, 0);
}

// Blink at start, wlan error, influx error or pi low with different colors
// If F7 is connected, use health status for a solid color
uint32_t calculateColor() {
  static const uint32_t blinkInterval = 1000;
  static const uint32_t blinkDuty = 250;
  static uint32_t blinkOffset = 0;

  uint32_t now = millis();

  if( blinkOffset == 0 || now - blinkOffset < blinkDuty ) {
    if( !blinkOffset )
      blinkOffset = now;
    return startColor;
  }

  if ((now - blinkOffset) % blinkInterval < blinkDuty) {
    uint32_t pi = piBlinkColor();
    if( pi != neutralColor ) {
      lastError = "PI low";
      return pi;
    }
    if (!wlanConnected) {
      lastError = "No WLAN";
      return wlanErrorColor;
    }
    if (influxStatus && (influxStatus < 200 || influxStatus >= 300)) {
      lastError = "No Influx";
      return influxErrorColor;
    }
  }

  if( webData.f7Connected && webData.f7Data.deziPI && webData.f7Data.ppm && webData.f7Data.spO2 ) {
    lastError = "OK";
    return healthColor();
  }

  lastError = "No F7";
  return neutralColor;
}

// void IRAM_ATTR buttonISR() {
//     buttonPressed++;
// }

void ledTask( void *parms ) {
  (void)parms;

  Serial.printf("Task '%s' running on core %u\n", pcTaskGetTaskName(NULL), xPortGetCoreID());

  static uint32_t lastColor = !startColor;
  static bool useLed = true;

  pixel.begin(); // This initializes the NeoPixel library.
  pixel.setPixelColor(0, startColor);
  pixel.show();

  pinMode(39, INPUT_PULLUP);
  // attachInterrupt(39, buttonISR, FALLING);

  for (;;) {
    if( digitalRead(39) == LOW ) {
      buttonPressed++;
    } else {
      buttonPressed = 0;
    }

    if( buttonPressed == 20 ) {
      useLed = !useLed;
      if( useLed ) {
        Serial.println("LED on");
      } else {
        Serial.println("LED off");
        pixel.setPixelColor(0, 0);
        lastColor = 0;
        pixel.show();
      }
    }

    if( useLed ) {
      uint32_t newColor = calculateColor();
      if( newColor != lastColor ) {
        lastColor = newColor;
        pixel.setPixelColor(0, newColor);
        pixel.show();
        // Serial.println(lastError);
      }
    }
    // We run in lowest priority, no need for a delay()...
  }
}

void startLed() {
  uint32_t ledCpuId = 0;
  if (portNUM_PROCESSORS > 1 && xPortGetCoreID() == 0) {
    ledCpuId = (portNUM_PROCESSORS > 2) ? 2 : 1; // use another core...
  }
  xTaskCreatePinnedToCore(ledTask, "led", 5 * 1024, nullptr, 0, nullptr, ledCpuId);
}
