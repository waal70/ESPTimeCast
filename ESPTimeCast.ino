#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "mfactoryfont.h"
#include <DNSServer.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN   12
#define DATA_PIN  15
#define CS_PIN    13
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

AsyncWebServer server(80);

char ssid[32] = "";
char password[32] = "";
char openWeatherApiKey[64] = "";
char openWeatherCity[64] = "";
char openWeatherCountry[64] = "";
char weatherUnits[12] = "metric";
long utcOffsetInSeconds = 32400;
unsigned long clockDuration = 10000;
unsigned long weatherDuration = 5000;

WiFiClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

const byte DNS_PORT = 53;
DNSServer dnsServer;

String currentTemp = "";
String weatherDescription = "";
bool weatherAvailable = false;
bool weatherFetched = false;
bool displayed = false;
bool isAPMode = false;
bool isFirstBoot = true;
bool timeSynced = false;
bool shouldReboot = false;  
char tempSymbol = 'C';

char daysOfTheWeek[7][12] = {"&", "-", "/", "?", "@", "=", "$"}; //custom font for days of the week.

void printConfigToSerial() {
  Serial.println(F("========= Loaded Configuration ========="));
  Serial.print(F("WiFi SSID: ")); Serial.println(ssid);
  Serial.print(F("WiFi Password: ")); Serial.println(password);
  Serial.print(F("OpenWeather City: ")); Serial.println(openWeatherCity);
  Serial.print(F("OpenWeather Country: ")); Serial.println(openWeatherCountry);
  Serial.print(F("OpenWeather API Key: ")); Serial.println(openWeatherApiKey);
  Serial.print(F("Temperature Unit: ")); Serial.println(weatherUnits);
  Serial.print(F("Clock duration: ")); Serial.println(clockDuration);
  Serial.print(F("Weather duration: ")); Serial.println(weatherDuration);
  Serial.print(F("UTC Offset: ")); Serial.println(utcOffsetInSeconds);
  Serial.println(F("========================================"));
}

void loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("[ERROR] LittleFS mount failed");
    return;
  }

  if (!LittleFS.exists("/config.json")) {
    Serial.println("[CONFIG] config.json not found, creating with defaults...");

    DynamicJsonDocument doc(512);
    doc["ssid"] = "";
    doc["password"] = "";
    doc["openWeatherApiKey"] = "";
    doc["openWeatherCity"] = "Osaka";
    doc["openWeatherCountry"] = "JP";
    doc["weatherUnits"] = "metric";
    doc["clockDuration"] = 10000;
    doc["weatherDuration"] = 5000;
    doc["utcOffsetInSeconds"] = 32400;

    File f = LittleFS.open("/config.json", "w");
    if (f) {
      serializeJsonPretty(doc, f);
      f.close();
      Serial.println("[CONFIG] Default config.json created.");
    } else {
      Serial.println("[ERROR] Failed to create default config.json");
    }
  }

  // Now load config as usual
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("[ERROR] Failed to open config.json");
    return;
  }

  size_t size = configFile.size();
  if (size == 0 || size > 1024) {
    Serial.println("[ERROR] Invalid config file size");
    configFile.close();
    return;
  }

  String jsonString = configFile.readString();
  configFile.close();

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    Serial.print("[ERROR] JSON parse failed: ");
    Serial.println(error.f_str());
    return;
  }

  if (doc.containsKey("ssid")) strlcpy(ssid, doc["ssid"], sizeof(ssid));
  if (doc.containsKey("password")) strlcpy(password, doc["password"], sizeof(password));
  if (doc.containsKey("openWeatherApiKey")) strlcpy(openWeatherApiKey, doc["openWeatherApiKey"], sizeof(openWeatherApiKey));
  if (doc.containsKey("openWeatherCity")) strlcpy(openWeatherCity, doc["openWeatherCity"], sizeof(openWeatherCity));
  if (doc.containsKey("openWeatherCountry")) strlcpy(openWeatherCountry, doc["openWeatherCountry"], sizeof(openWeatherCountry));
  if (doc.containsKey("weatherUnits")) strlcpy(weatherUnits, doc["weatherUnits"], sizeof(weatherUnits));
  if (doc.containsKey("clockDuration")) clockDuration = doc["clockDuration"];
  if (doc.containsKey("weatherDuration")) weatherDuration = doc["weatherDuration"];
  if (doc.containsKey("utcOffsetInSeconds")) utcOffsetInSeconds = doc["utcOffsetInSeconds"];


  if (strcmp(weatherUnits, "imperial") == 0)
    tempSymbol = 'F';
  else if (strcmp(weatherUnits, "standard") == 0)
    tempSymbol = 'K';
  else
    tempSymbol = 'C';

  timeClient.setTimeOffset(utcOffsetInSeconds);
  Serial.println("[CONFIG] Configuration loaded.");
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  timeClient.begin();
  delay(500);

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 15000;

  unsigned long animTimer = 0;
  int animFrame = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    unsigned long now = millis();
    if (now - animTimer > 750) {
      animTimer = now;
      P.setTextAlignment(PA_CENTER);
      switch (animFrame % 3) {
        case 0: P.print("WiFi ©"); break;
        case 1: P.print("WiFi ª"); break;
        case 2: P.print("WiFi «"); break;
      }
      animFrame++;
    }
    delay(10);
    yield(); // Keeps ESP responsive
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected: " + WiFi.localIP().toString());
    isAPMode = false;  // Connected normally
  } else {
    Serial.println("\r\n[WiFi] Failed. Starting AP mode...");
    WiFi.softAP("ESPTimeCast", "12345678");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());// Captive portal redirect
    isAPMode = true;  // AP mode enabled
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request){
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    request->send(500, "application/json", "{\"error\":\"Failed to open config.json\"}");
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    request->send(500, "application/json", "{\"error\":\"Failed to parse config.json\"}");
    return;
  }

  doc["mode"] = isAPMode ? "ap" : "sta";  // Add current mode

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
});


server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
  // === Simulation flags ===
  bool simulateWriteFailure = false;      // Set to true to simulate write failure
  bool simulateCorruptConfig = false;     // Set to true to simulate corrupted config after write

  DynamicJsonDocument doc(2048);
  for (int i = 0; i < request->params(); i++) {
    const AsyncWebParameter* p = request->getParam(i);
    doc[p->name()] = p->value();
  }

  // Backup existing config
  if (LittleFS.exists("/config.json")) {
    LittleFS.rename("/config.json", "/config.bak");
  }

  File f = simulateWriteFailure ? File() : LittleFS.open("/config.json", "w");

  if (!f) {
    DynamicJsonDocument errorDoc(256);
    errorDoc["error"] = "Failed to write config";
    String response;
    serializeJson(errorDoc, response);
    request->send(500, "application/json", response);
    return;
  }

  serializeJson(doc, f);
  f.close();

  // === Optional: Simulate corrupted config by writing invalid content ===
  if (simulateCorruptConfig) {
    File corrupt = LittleFS.open("/config.json", "w");
    corrupt.print("{invalid json");  // Intentionally malformed
    corrupt.close();
  }

  // Verify config
  File verify = LittleFS.open("/config.json", "r");
  DynamicJsonDocument test(2048);
  DeserializationError err = deserializeJson(test, verify);
  verify.close();

  if (err) {
    DynamicJsonDocument errorDoc(256);
    errorDoc["error"] = "Config corrupted. Reboot cancelled.";
    String response;
    serializeJson(errorDoc, response);
    request->send(500, "application/json", response);
    return;
  }

  DynamicJsonDocument okDoc(128);
  okDoc["message"] = "Saved successfully. Rebooting...";
  String response;
  serializeJson(okDoc, response);
  request->send(200, "application/json", response);

  shouldReboot = true;
});

server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request){
  if (LittleFS.exists("/config.bak")) {
    LittleFS.remove("/config.json");
    if (LittleFS.rename("/config.bak", "/config.json")) {
      DynamicJsonDocument okDoc(128);
      okDoc["message"] = "Backup restored.";
      String response;
      serializeJson(okDoc, response);
      request->send(200, "application/json", response);
    } else {
      DynamicJsonDocument errorDoc(128);
      errorDoc["error"] = "Failed to restore backup.";
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
    }
  } else {
    DynamicJsonDocument errorDoc(128);
    errorDoc["error"] = "No backup found.";
    String response;
    serializeJson(errorDoc, response);
    request->send(404, "application/json", response);
  }
});

  server.begin();
}


void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WEATHER] Skipped: WiFi not connected");
    return;
  }

  if (!openWeatherApiKey || strlen(openWeatherApiKey) != 32) {
    Serial.println("[WEATHER] Skipped: Invalid API key (must be exactly 32 characters)");
    weatherAvailable = false;
    weatherFetched = false;
    return;
  }

  if (!(strlen(openWeatherCity) > 0 && strlen(openWeatherCountry) > 0)) {
    Serial.println("[WEATHER] Skipped: City or Country is empty.");
    return;
  }

  Serial.println("[WEATHER] Connecting to OpenWeatherMap...");
  const char* host = "api.openweathermap.org";
  String url = "/data/2.5/weather?q=" + String(openWeatherCity) + "," + String(openWeatherCountry) + "&appid=" + openWeatherApiKey + "&units=" + String(weatherUnits);

  Serial.println("[WEATHER] URL: " + url);

  if (!client.connect(host, 80)) {
    Serial.println("[WEATHER] Connection failed");
    weatherAvailable = false;
    return;
  }

  client.setTimeout(5000); 

  Serial.println("[WEATHER] Connected, sending request...");
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  bool isBody = false;
  String payload = "";

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (!isBody && line == "\r") {
      isBody = true;
    } else if (isBody) {
      payload += line;
    }
  }

  client.stop();
  Serial.println("[WEATHER] Response received.");

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("[WEATHER] JSON parse error: ");
    Serial.println(error.f_str());
    weatherAvailable = false;
    return;
  }

  float temp = doc["main"]["temp"];
  const char* desc = doc["weather"][0]["main"];

  currentTemp = String((int)round(temp)) + "º" + tempSymbol;
  weatherDescription = String(desc);
  weatherAvailable = true;
  weatherFetched = true;

  Serial.print("[CONFIG] Weather units: ");
  Serial.println(weatherUnits);
  Serial.printf("[WEATHER] Temp: %s / Description: %s\n", currentTemp.c_str(), weatherDescription.c_str());
}



void setup() {
  Serial.begin(74880);
  P.begin();
  P.setFont(mFactory);
  P.setIntensity(8);        // Set brightness level here (0–15)
  Serial.println("[BOOT] Starting setup...");
  loadConfig();
  connectWiFi();
  setupWebServer();
  printConfigToSerial();
  weatherFetched = false;
  fetchWeather();
}

void loop() {
  // Handle scheduled reboot early
  if (shouldReboot) {
    Serial.println("[SYSTEM] Preparing to reboot...");
    if (isAPMode) {
      Serial.println("[SYSTEM] Stopping DNS server...");
      dnsServer.stop();
      Serial.print("[DNS] Server stopped:");
    }
    delay(2000);
    Serial.println("[SYSTEM] Rebooting now.");
    ESP.restart();
  }

  if (isAPMode) {
    dnsServer.processNextRequest();
    static unsigned long lastUpdate = 0;
    static int animFrame = 0;
    unsigned long now = millis();
    if (now - lastUpdate > 750) {
      lastUpdate = now;
      P.setTextAlignment(PA_CENTER);
      switch (animFrame % 3) {
        case 0: P.print("AP©"); break;
        case 1: P.print("APª"); break;
        case 2: P.print("AP«"); break;
      }
      animFrame++;
    }
    delay(10);
    return;
  }

  static unsigned long lastSwitch = 0;
  static unsigned long lastFetch = 0;
  static unsigned long lastTimeUpdate = 0;
  static uint8_t displayMode = 0;
  const unsigned long fetchInterval = 300000;      // 5 minutes
  const unsigned long timeUpdateInterval = 60000;  // 1 minute

  // Periodically fetch weather
  if (millis() - lastFetch > fetchInterval) {
    Serial.println("[LOOP] Fetching weather data...");
    fetchWeather();
    lastFetch = millis();
  }

  // Periodically update time
  if (millis() - lastTimeUpdate > timeUpdateInterval) {
    Serial.println("[TIME] Updating time from NTP server...");
    if (WiFi.status() == WL_CONNECTED) {
      if (timeClient.update()) {
        Serial.println("[TIME] NTP time updated successfully");
        timeSynced = true;  // ✅ Time is synced
      } else {
        Serial.println("[TIME] Failed to update NTP time, using last known time");
      }
    }
    lastTimeUpdate = millis();
  }

  unsigned long currentInterval = (displayMode == 0 || !weatherFetched) ? clockDuration : weatherDuration;

  if (millis() - lastSwitch > currentInterval) {
    displayMode = (displayMode + 1) % 2;
    lastSwitch = millis();
    displayed = false;
    Serial.printf("[LOOP] Switching to display mode: %s\n", displayMode == 0 ? "CLOCK" : "WEATHER");
  }

  if (isFirstBoot) {
    // Try immediate NTP sync on first boot
    Serial.println("[BOOT] Attempting initial NTP sync...");
    if (WiFi.status() == WL_CONNECTED && timeClient.update()) {
      Serial.println("[BOOT] Initial NTP sync successful");
      timeSynced = true;
    } else {
      Serial.println("[BOOT] Initial NTP sync failed");
    }

    if (!timeSynced) {
      Serial.println("[DISPLAY] Waiting for NTP sync...");
      unsigned long start = millis();
      static int syncAnimFrame = 0;

      while (millis() - start < clockDuration) {
        P.setTextAlignment(PA_CENTER);
        switch (syncAnimFrame % 3) {
          case 0: P.print("sync®"); break;
          case 1: P.print("sync¯"); break;
          case 2: P.print("sync°"); break;
        }
        syncAnimFrame++;
        delay(750);
        yield();
      }
    } else {
      const char* day = daysOfTheWeek[timeClient.getDay()];
      int hour = timeClient.getHours();
      int minute = timeClient.getMinutes();

      Serial.printf("[DISPLAY] First boot clock: %s %02d:%02d\n", day, hour, minute);

      unsigned long start = millis();
      while (millis() - start < clockDuration) {
        if ((millis() / 750) % 2 == 0)
          P.printf("%.3s%02d;%02d", day, hour, minute);
        else
          P.printf("%.3s%02d:%02d", day, hour, minute);
        delay(50);
        yield();
      }
    }

    isFirstBoot = false;
    lastSwitch = millis();
  } else {
    if (displayMode == 0 || !weatherFetched) {
      if (!timeSynced) {
        Serial.println("[DISPLAY] Waiting for NTP sync...");
        unsigned long start = millis();
        static int syncAnimFrame = 0;

        while (millis() - start < clockDuration) {
          P.setTextAlignment(PA_CENTER);
          switch (syncAnimFrame % 3) {
            case 0: P.print("sync®"); break;
            case 1: P.print("sync¯"); break;
            case 2: P.print("sync°"); break;
          }
          syncAnimFrame++;
          delay(750);
          yield();
        }
      } else {
        const char* day = daysOfTheWeek[timeClient.getDay()];
        int hour = timeClient.getHours();
        int minute = timeClient.getMinutes();

        Serial.printf("[DISPLAY] Clock: %s %02d:%02d\n", day, hour, minute);

        unsigned long start = millis();
        while (millis() - start < clockDuration) {
          if ((millis() / 750) % 2 == 0)
            P.printf("%.3s%02d;%02d", day, hour, minute);
          else
            P.printf("%.3s%02d:%02d", day, hour, minute);
          delay(50);
          yield();
        }
      }
    } else if (displayMode == 1) {
      if (!displayed) {
        Serial.println("[DISPLAY] Showing weather info...");
        String msg = currentTemp;
        Serial.printf("[WEATHER] Display message: %s\n", msg.c_str());
        P.displayClear();
        P.setTextAlignment(PA_CENTER);
        P.printf(msg.c_str());
        displayed = true;
      }
    }
  }
}