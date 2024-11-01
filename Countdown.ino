#include <Wire.h>
#include <RtcDS3231.h>                        // Include RTC library by Makuna
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdate.h>
#include <FastLED.h>
#include <SPIFFS.h>                            // For ESP32 SPIFFS filesystem
#include <ESPmDNS.h>                           // mDNS Server
#define countof(a) (sizeof(a) / sizeof(a[0]))
#define NUM_LEDS 86                            // Total of 86 LED's     
#define DATA_PIN 16                            // Change this to your ESP32 GPIO pin
#define MILLI_AMPS 2400 
#define COUNTDOWN_OUTPUT 17                    // Change this to your ESP32 GPIO pin

// Wi-Fi Credentials
const char* ssid = "YourWiFiName";           // Replace with your Wi-Fi network SSID
const char* password = "YouWiFiPW";           // Replace with your Wi-Fi network password

// Access Point Credentials
const char* APssid = "CountdownAPName";         // Replace with your desired AP name
const char* APpassword = "CountdownAPPW";       // Replace with your desired AP password

RtcDS3231<TwoWire> Rtc(Wire);
WebServer server(80);
CRGB LEDs[NUM_LEDS];

// Settings
unsigned long prevTime = 0;
byte r_val = 255;
byte g_val = 0;
byte b_val = 0;
bool dotsOn = true;
byte brightness = 255;
float temperatureCorrection = -3.0;
byte temperatureSymbol = 12;                  // 12=Celcius, 13=Fahrenheit check 'numbers'
byte clockMode = 0;                           // Clock modes: 0=Clock, 1=Countdown, 2=Temperature, 3=Scoreboard
unsigned long countdownMilliSeconds;
unsigned long endCountDownMillis;
byte hourFormat = 24;                         // Change this to 12 if you want default 12 hours format instead of 24               
CRGB countdownColor = CRGB::Green;
byte scoreboardLeft = 0;
byte scoreboardRight = 0;
CRGB scoreboardColorLeft = CRGB::Green;
CRGB scoreboardColorRight = CRGB::Red;
CRGB alternateColor = CRGB::Black; 
long numbers[] = {
  0b000111111111111111111,  // [0] 0
  0b000111000000000000111,  // [1] 1
  0b111111111000111111000,  // [2] 2
  0b111111111000000111111,  // [3] 3
  0b111111000111000000111,  // [4] 4
  0b111000111111000111111,  // [5] 5
  0b111000111111111111111,  // [6] 6
  0b000111111000000000111,  // [7] 7
  0b111111111111111111111,  // [8] 8
  0b111111111111000111111,  // [9] 9
  0b000000000000000000000,  // [10] off
  0b111111111111000000000,  // [11] degrees symbol
  0b000000111111111111000,  // [12] C(elsius)
  0b111000111111111000000,  // [13] F(ahrenheit)
};

void setup() {
  pinMode(COUNTDOWN_OUTPUT, OUTPUT);
  digitalWrite(COUNTDOWN_OUTPUT, LOW);
  Serial.begin(115200);
  delay(200);

  // RTC DS3231 Setup
  Rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  if (!Rtc.IsDateTimeValid()) {
      if (Rtc.LastError() != 0) {
          Serial.print("RTC communications error = ");
          Serial.println(Rtc.LastError());
      } else {
          Serial.println("RTC lost confidence in the DateTime!");
          Rtc.SetDateTime(compiled);
      }
  }

  WiFi.setSleep(false);
  delay(200);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(LEDs, NUM_LEDS);
  FastLED.setDither(false);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  fill_solid(LEDs, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Try to connect to Wi-Fi network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  byte count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 60) {
    delay(500);
    Serial.print(".");
    LEDs[count % NUM_LEDS] = CRGB::Green;
    FastLED.show();
    count++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi network.");
    Serial.println(WiFi.localIP());

    // Initialize mDNS
    if (!MDNS.begin("odpocet")) {  // Replace "odpocet" with your desired hostname
      Serial.println("Error setting up mDNS responder!");
    } else {
      Serial.println("mDNS responder started");
    }
    // Advertise the web server over mDNS
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("\nCould not connect to WiFi network, starting AP mode.");
    // Switch to AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(APssid, APpassword);
    Serial.println("AP IP address: ");
    Serial.println(WiFi.softAPIP());
  }

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }

  // Web server handlers

   //homepage
server.on("/", HTTP_GET, []() {
  if (!SPIFFS.exists("/index.html.gz")) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  File file = SPIFFS.open("/index.html.gz", "r");
  server.streamFile(file, "text/html");
  file.close();
});

  
  server.on("/color", HTTP_POST, []() {
    r_val = server.arg("r").toInt();
    g_val = server.arg("g").toInt();
    b_val = server.arg("b").toInt();
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/setdate", HTTP_POST, []() {
    String datearg = server.arg("date");
    String timearg = server.arg("time");
    Serial.println(datearg);
    Serial.println(timearg);
    char d[12];
    char t[9];
    datearg.toCharArray(d, 12);
    timearg.toCharArray(t, 9);
    RtcDateTime compiled = RtcDateTime(d, t);
    Rtc.SetDateTime(compiled);
    clockMode = 0;
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/brightness", HTTP_POST, []() {
    brightness = server.arg("brightness").toInt();
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/countdown", HTTP_POST, []() {
    countdownMilliSeconds = server.arg("ms").toInt();
    byte cd_r_val = server.arg("r").toInt();
    byte cd_g_val = server.arg("g").toInt();
    byte cd_b_val = server.arg("b").toInt();
    digitalWrite(COUNTDOWN_OUTPUT, LOW);
    countdownColor = CRGB(cd_r_val, cd_g_val, cd_b_val);
    endCountDownMillis = millis() + countdownMilliSeconds;
    allBlank();
    clockMode = 1;
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/temperature", HTTP_POST, []() {
    temperatureCorrection = server.arg("correction").toInt();
    temperatureSymbol = server.arg("symbol").toInt();
    clockMode = 2;
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/scoreboard", HTTP_POST, []() {
    scoreboardLeft = server.arg("left").toInt();
    scoreboardRight = server.arg("right").toInt();
    scoreboardColorLeft = CRGB(server.arg("rl").toInt(),server.arg("gl").toInt(),server.arg("bl").toInt());
    scoreboardColorRight = CRGB(server.arg("rr").toInt(),server.arg("gr").toInt(),server.arg("br").toInt());
    clockMode = 3;
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/scoreboardmirror", HTTP_POST, []() {
    scoreboardLeft = server.arg("left").toInt();
    scoreboardRight = server.arg("right").toInt();
    scoreboardColorLeft = CRGB(server.arg("rl").toInt(),server.arg("gl").toInt(),server.arg("bl").toInt());
    scoreboardColorRight = CRGB(server.arg("rr").toInt(),server.arg("gr").toInt(),server.arg("br").toInt());
    clockMode = 3;
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/hourformat", HTTP_POST, []() {
    hourFormat = server.arg("hourformat").toInt();
    clockMode = 0;
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/clock", HTTP_POST, []() {
    clockMode = 0;
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  // Handler for getting remaining countdown time
  server.on("/getremainingtime", HTTP_GET, []() {
    unsigned long restMillis = (endCountDownMillis > millis()) ? endCountDownMillis - millis() : 0;
    unsigned long hours = ((restMillis / 1000) / 60) / 60;
    unsigned long minutes = (restMillis / 1000) / 60;
    unsigned long seconds = restMillis / 1000;
    int remSeconds = seconds - (minutes * 60);
    int remMinutes = minutes - (hours * 60);
    char json[100];
    snprintf(json, sizeof(json), "{\"hours\": %lu, \"minutes\": %lu, \"seconds\": %lu}", hours, remMinutes, remSeconds);
    server.sendHeader("Access-Control-Allow-Origin", "*"); // Add this line
    server.send(200, "application/json", json);
});

  // Serve static files from SPIFFS
  server.serveStatic("/", SPIFFS, "/", "max-age=86400");
  server.begin();

  // List files in SPIFFS (optional)
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("FS File: %s, size: %u\n", file.name(), file.size());
    file = root.openNextFile();
  }
  Serial.println();

  digitalWrite(COUNTDOWN_OUTPUT, LOW);
}

void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();
  if (currentMillis - prevTime >= 1000) {
    prevTime = currentMillis;

    if (clockMode == 0) {
      updateClock();
    } else if (clockMode == 1) {
      updateCountdown();
    } else if (clockMode == 2) {
      updateTemperature();
    } else if (clockMode == 3) {
      updateScoreboard();
    }

    FastLED.setBrightness(brightness);
    FastLED.show();
  }
}

void displayNumber(byte number, byte segment, CRGB color) {
  // Segment from left to right: 3, 2, 1, 0
  byte startindex = 0;
  switch (segment) {
    case 0:
      startindex = 0;
      break;
    case 1:
      startindex = 21;
      break;
    case 2:
      startindex = 44;
      break;
    case 3:
      startindex = 65;
      break;
  }

  for (byte i = 0; i < 21; i++) {
    yield();
    LEDs[i + startindex] = ((numbers[number] & 1 << i) == 1 << i) ? color : alternateColor;
  }
}

void allBlank() {
  for (int i = 0; i < NUM_LEDS; i++) {
    LEDs[i] = CRGB::Black;
  }
  FastLED.show();
}

void updateClock() {
  RtcDateTime now = Rtc.GetDateTime();
  // printDateTime(now);

  int hour = now.Hour();
  int mins = now.Minute();
  int secs = now.Second();

  if (hourFormat == 12 && hour > 12)
    hour = hour - 12;

  byte h1 = hour / 10;
  byte h2 = hour % 10;
  byte m1 = mins / 10;
  byte m2 = mins % 10;
  byte s1 = secs / 10;
  byte s2 = secs % 10;

  CRGB color = CRGB(r_val, g_val, b_val);

  if (h1 > 0)
    displayNumber(h1, 3, color);
  else
    displayNumber(10, 3, color);  // Blank

  displayNumber(h2, 2, color);
  displayNumber(m1, 1, color);
  displayNumber(m2, 0, color);

  displayDots(color);
}

void updateCountdown() {
  if (countdownMilliSeconds == 0 && endCountDownMillis == 0)
    return;

  unsigned long restMillis = endCountDownMillis - millis();
  unsigned long hours = ((restMillis / 1000) / 60) / 60;
  unsigned long minutes = (restMillis / 1000) / 60;
  unsigned long seconds = restMillis / 1000;
  int remSeconds = seconds - (minutes * 60);
  int remMinutes = minutes - (hours * 60);

  Serial.print(restMillis);
  Serial.print(" ");
  Serial.print(hours);
  Serial.print(" ");
  Serial.print(minutes);
  Serial.print(" ");
  Serial.print(seconds);
  Serial.print(" | ");
  Serial.print(remMinutes);
  Serial.print(" ");
  Serial.println(remSeconds);

  byte h1 = hours / 10;
  byte h2 = hours % 10;
  byte m1 = remMinutes / 10;
  byte m2 = remMinutes % 10;
  byte s1 = remSeconds / 10;
  byte s2 = remSeconds % 10;

  CRGB color = countdownColor;
  if (restMillis <= 300000) {
    color = CRGB::Orange;
  }
  if (restMillis <= 60000) {
    color = CRGB::Red;
  }

  if (hours > 0) {
    // hh:mm
    displayNumber(h1, 3, color);
    displayNumber(h2, 2, color);
    displayNumber(m1, 1, color);
    displayNumber(m2, 0, color);
  } else {
    // mm:ss
    displayNumber(m1, 3, color);
    displayNumber(m2, 2, color);
    displayNumber(s1, 1, color);
    displayNumber(s2, 0, color);
  }

  displayDots(color);

  if (hours <= 0 && remMinutes <= 0 && remSeconds <= 0) {
    Serial.println("Countdown timer ended.");
    // endCountdown();
    countdownMilliSeconds = 0;
    endCountDownMillis = 0;
    digitalWrite(COUNTDOWN_OUTPUT, HIGH);
    return;
  }
}

void endCountdown() {
  allBlank();
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i > 0)
      LEDs[i - 1] = CRGB::Black;

    LEDs[i] = CRGB::Red;
    FastLED.show();
    delay(25);
  }
}

void displayDots(CRGB color) {
  if (dotsOn) {
    LEDs[42] = color;
    LEDs[43] = color;
  } else {
    LEDs[42] = CRGB::Black;
    LEDs[43] = CRGB::Black;
  }

  dotsOn = !dotsOn;
}

void hideDots() {
  LEDs[42] = CRGB::Black;
  LEDs[43] = CRGB::Black;
}

void updateTemperature() {
  RtcTemperature temp = Rtc.GetTemperature();
  float ftemp = temp.AsFloatDegC();
  float ctemp = ftemp + temperatureCorrection;
  Serial.print("Sensor temp: ");
  Serial.print(ftemp);
  Serial.print(" Corrected: ");
  Serial.println(ctemp);

  if (temperatureSymbol == 13)
    ctemp = (ctemp * 1.8000) + 32;

  byte t1 = int(ctemp) / 10;
  byte t2 = int(ctemp) % 10;
  CRGB color = CRGB(r_val, g_val, b_val);
  displayNumber(t1, 3, color);
  displayNumber(t2, 2, color);
  displayNumber(11, 1, color);
  displayNumber(temperatureSymbol, 0, color);
  hideDots();
}

void updateScoreboard() {
  byte sl1 = scoreboardLeft / 10;
  byte sl2 = scoreboardLeft % 10;
  byte sr1 = scoreboardRight / 10;
  byte sr2 = scoreboardRight % 10;

  displayNumber(sl1, 3, scoreboardColorLeft);
  displayNumber(sl2, 2, scoreboardColorLeft);
  displayNumber(sr1, 1, scoreboardColorRight);
  displayNumber(sr2, 0, scoreboardColorRight);
  hideDots();
}

void printDateTime(const RtcDateTime& dt) {
  char datestring[20];

  snprintf(datestring, 
           countof(datestring),
           "%02u/%02u/%04u %02u:%02u:%02u",
           dt.Month(),
           dt.Day(),
           dt.Year(),
           dt.Hour(),
           dt.Minute(),
           dt.Second());
  Serial.println(datestring);
}
