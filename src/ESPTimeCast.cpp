#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <sntp.h>
#include <time.h>

#include "mfactoryfont.h" // Replace with your font, or comment/remove if not using custom
#include "tz_lookup.h"    // Timezone lookup, do not duplicate mapping here!
#include "weather_icons.h" // Weather icons, do not duplicate mapping here!

#define DEBUG 1 // Set to 1 to enable debug prints, anything else to disable

#if DEBUG == 1
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(fmt,...) Serial.printf(fmt, __VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt,...)
#endif

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN 5
#define DATA_PIN 4
#define CS_PIN 2
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

AsyncWebServer server(80);

// TODO: Make the doorbell event configurable via web interface (time of event, text to display)
// int doorbellDuration = 10; // Duration of doorbell animation in seconds
// char doorbellText[8] = "DE BEL!";

char ssid[32] = "";
char password[32] = "";
char openWeatherApiKey[64] = "";
char openWeatherCity[64] = "";
char openWeatherCountry[64] = "";
char weatherUnits[12] = "metric";
char timeZone[64] = "";
unsigned long clockDuration = 10000;
unsigned long weatherDuration = 5000;

int DOORBELL_EVENT = 0;

// ADVANCED SETTINGS
int brightness = 7;
bool flipDisplay = false;
bool twelveHourToggle = false;
bool showDayOfWeek = true;
bool showHumidity = false;
char ntpServer1[64] = "pool.ntp.org";
char ntpServer2[64] = "time.nist.gov";

WiFiClient client;
const byte DNS_PORT = 53;
DNSServer dnsServer;

String currentTemp = "";
bool weatherAvailable = false;
bool weatherFetched = false;
bool weatherFetchInitiated = false;
bool isAPMode = false;
char tempSymbol = 'C';

unsigned long lastSwitch = 0;
unsigned long lastColonBlink = 0;
int displayMode = 0;
int currentHumidity = -1;
int currentPressure = -1; // New variable for pressure
uint8_t currentWeatherIcon[8]; // Placeholder for current weather icon

bool ntpSyncSuccessful = false;

char daysOfTheWeek[7][12] = {"&", "*", "/", "?", "@", "=", "$"};

// NTP Synchronization State Machine
enum NtpState
{
  NTP_IDLE,
  NTP_SYNCING,
  NTP_SUCCESS,
  NTP_FAILED
};

NtpState ntpState = NTP_IDLE;
unsigned long ntpStartTime = 0;
const int ntpTimeout = 30000; // 30 seconds
const int maxNtpRetries = 30;
int ntpRetryCount = 0;

void performDoorbellAnimation();

void printConfigToSerial()
{
  DEBUG_PRINTLN(F("========= Loaded Configuration ========="));
  DEBUG_PRINT(F("WiFi SSID: "));
  DEBUG_PRINTLN(ssid);
  DEBUG_PRINT(F("WiFi Password: "));
  DEBUG_PRINTLN(password);
  DEBUG_PRINT(F("OpenWeather City: "));
  DEBUG_PRINTLN(openWeatherCity);
  DEBUG_PRINT(F("OpenWeather Country: "));
  DEBUG_PRINTLN(openWeatherCountry);
  DEBUG_PRINT(F("OpenWeather API Key: "));
  DEBUG_PRINTLN(openWeatherApiKey);
  DEBUG_PRINT(F("Temperature Unit: "));
  DEBUG_PRINTLN(weatherUnits);
  DEBUG_PRINT(F("Clock duration: "));
  DEBUG_PRINTLN(clockDuration);
  DEBUG_PRINT(F("Weather duration: "));
  DEBUG_PRINTLN(weatherDuration);
  DEBUG_PRINT(F("TimeZone (IANA): "));
  DEBUG_PRINTLN(timeZone);
  DEBUG_PRINT(F("Brightness: "));
  DEBUG_PRINTLN(brightness);
  DEBUG_PRINT(F("Flip Display: "));
  DEBUG_PRINTLN(flipDisplay ? "Yes" : "No");
  DEBUG_PRINT(F("Show 12h Clock: "));
  DEBUG_PRINTLN(twelveHourToggle ? "Yes" : "No");
  DEBUG_PRINT(F("Show Day of the Week: "));
  DEBUG_PRINTLN(showDayOfWeek ? "Yes" : "No");
  DEBUG_PRINT(F("Show Humidity "));
  DEBUG_PRINTLN(showHumidity ? "Yes" : "No");
  DEBUG_PRINT(F("NTP Server 1: "));
  DEBUG_PRINTLN(ntpServer1);
  DEBUG_PRINT(F("NTP Server 2: "));
  DEBUG_PRINTLN(ntpServer2);
  DEBUG_PRINTLN(F("========================================"));
  DEBUG_PRINTLN();
}

void loadConfig()
{
  DEBUG_PRINTLN(F("[CONFIG] Loading configuration..."));
  if (!LittleFS.begin())
  {
    DEBUG_PRINTLN(F("[ERROR] LittleFS mount failed"));
    return;
  }

  if (!LittleFS.exists("/config.json"))
  {
    DEBUG_PRINTLN(F("[CONFIG] config.json not found, creating with defaults..."));
    JsonDocument doc;
    doc[F("ssid")] = "";
    doc[F("password")] = "";
    doc[F("openWeatherApiKey")] = "";
    doc[F("openWeatherCity")] = "";
    doc[F("openWeatherCountry")] = "";
    doc[F("weatherUnits")] = "metric";
    doc[F("clockDuration")] = 10000;
    doc[F("weatherDuration")] = 5000;
    doc[F("timeZone")] = "";
    doc[F("brightness")] = brightness;
    doc[F("flipDisplay")] = flipDisplay;
    doc[F("twelveHourToggle")] = twelveHourToggle;
    doc[F("showDayOfWeek")] = showDayOfWeek;
    doc[F("showHumidity")] = showHumidity;
    doc[F("ntpServer1")] = ntpServer1;
    doc[F("ntpServer2")] = ntpServer2;
    File f = LittleFS.open("/config.json", "w");
    if (f)
    {
      serializeJsonPretty(doc, f);
      f.close();
      DEBUG_PRINTLN(F("[CONFIG] Default config.json created."));
    }
    else
    {
      DEBUG_PRINTLN(F("[ERROR] Failed to create default config.json"));
    }
  }
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile)
  {
    DEBUG_PRINTLN(F("[ERROR] Failed to open config.json"));
    return;
  }
  size_t size = configFile.size();
  if (size == 0 || size > 1024)
  {
    DEBUG_PRINTLN(F("[ERROR] Invalid config file size"));
    configFile.close();
    return;
  }
  String jsonString = configFile.readString();
  configFile.close();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error)
  {
    DEBUG_PRINT(F("[ERROR] JSON parse failed: "));
    DEBUG_PRINTLN(error.f_str());
    return;
  }
  if (doc["ssid"].is<const char *>())
    strlcpy(ssid, doc["ssid"], sizeof(ssid));
  if (doc["password"].is<const char *>())
    strlcpy(password, doc["password"], sizeof(password));
  if (doc["openWeatherApiKey"].is<const char *>())
    strlcpy(openWeatherApiKey, doc["openWeatherApiKey"], sizeof(openWeatherApiKey));
  if (doc["openWeatherCity"].is<const char *>())
    strlcpy(openWeatherCity, doc["openWeatherCity"], sizeof(openWeatherCity));
  if (doc["openWeatherCountry"].is<const char *>())
    strlcpy(openWeatherCountry, doc["openWeatherCountry"], sizeof(openWeatherCountry));
  if (doc["weatherUnits"].is<const char *>())
    strlcpy(weatherUnits, doc["weatherUnits"], sizeof(weatherUnits));
  if (doc["clockDuration"].is<unsigned long>())
    clockDuration = doc["clockDuration"];
  if (doc["weatherDuration"].is<unsigned long>())
    weatherDuration = doc["weatherDuration"];
  if (doc["timeZone"].is<const char *>())
    strlcpy(timeZone, doc["timeZone"], sizeof(timeZone));
  if (doc["brightness"].is<int>())
    brightness = doc["brightness"];
  if (doc["flipDisplay"].is<bool>())
    flipDisplay = doc["flipDisplay"];
  if (doc["twelveHourToggle"].is<bool>())
    twelveHourToggle = doc["twelveHourToggle"];
  if (doc["showDayOfWeek"].is<bool>())
    showDayOfWeek = doc["showDayOfWeek"]; // <-- NEW
  if (doc["showHumidity"].is<bool>())
    showHumidity = doc["showHumidity"];
  else
    showHumidity = false;
  if (doc["ntpServer1"].is<const char *>())
    strlcpy(ntpServer1, doc["ntpServer1"], sizeof(ntpServer1));
  if (doc["ntpServer2"].is<const char *>())
    strlcpy(ntpServer2, doc["ntpServer2"], sizeof(ntpServer2));
  if (strcmp(weatherUnits, "imperial") == 0)
    tempSymbol = 'F';
  else if (strcmp(weatherUnits, "standard") == 0)
    tempSymbol = 'K';
  else
    tempSymbol = 'C';
  DEBUG_PRINTLN(F("[CONFIG] Configuration loaded."));
}

void connectWiFi()
{
  DEBUG_PRINTLN(F("[WIFI] Connecting to WiFi..."));
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 15000;
  unsigned long animTimer = 0;
  int animFrame = 0;
  bool animating = true;
  while (animating)
  {
    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED)
    {
      DEBUG_PRINTLN(F("[WiFi] Connected: ") + WiFi.localIP().toString());
      isAPMode = false;
      animating = false;
      break;
    }
    else if (now - startAttemptTime >= timeout)
    {
      DEBUG_PRINTLN(F("\r\n[WiFi] Failed. Starting AP mode..."));
      WiFi.softAP("ESPTimeCast", "12345678");
      DEBUG_PRINT(F("AP IP address: "));
      DEBUG_PRINTLN(WiFi.softAPIP());
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
      isAPMode = true;
      animating = false;
      DEBUG_PRINTLN(F("[WIFI] AP Mode Started"));
      break;
    }
    if (now - animTimer > 750)
    {
      animTimer = now;
      P.setTextAlignment(PA_CENTER);
      switch (animFrame % 3)
      {
      case 0:
        P.print("WiFi©");
        break;
      case 1:
        P.print("WiFiª");
        break;
      case 2:
        P.print("WiFi«");
        break;
      }
      animFrame++;
    }
    yield();
  }
}

void setupTime()
{
  sntp_stop();
  DEBUG_PRINTLN(F("[TIME] Starting NTP sync..."));
  configTime(0, 0, ntpServer1, ntpServer2); // Use custom NTP servers
  setenv("TZ", ianaToPosix(timeZone), 1);
  tzset();
  ntpState = NTP_SYNCING;
  ntpStartTime = millis();
  ntpRetryCount = 0;
  ntpSyncSuccessful = false; // Reset the flag
}

void setupWebServer()
{
  DEBUG_PRINTLN(F("[WEBSERVER] Setting up web server..."));
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    DEBUG_PRINTLN(F("[WEBSERVER] Request: /"));
    request->send(LittleFS, "/index.html", "text/html"); });
  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    DEBUG_PRINTLN(F("[WEBSERVER] Request: /config.json"));
    File f = LittleFS.open("/config.json", "r");
    if (!f) {
      DEBUG_PRINTLN(F("[WEBSERVER] Error opening /config.json"));
      request->send(500, "application/json", "{\"error\":\"Failed to open config.json\"}");
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      DEBUG_PRINT(F("[WEBSERVER] Error parsing /config.json: "));
      DEBUG_PRINTLN(err.f_str());
      request->send(500, "application/json", "{\"error\":\"Failed to parse config.json\"}");
      return;
    }
    doc[F("mode")] = isAPMode ? "ap" : "sta";
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    DEBUG_PRINTLN(F("[WEBSERVER] Request: /save"));    
    JsonDocument doc;
    for (size_t i = 0; i < request->params(); i++) {
      const AsyncWebParameter* p = request->getParam(i);
      String n = p->name();
      String v = p->value();
      if (n == "brightness") doc[n] = v.toInt();
      else if (n == "flipDisplay") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "twelveHourToggle") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "showDayOfWeek") doc[n] = (v == "true" || v == "on" || v == "1"); 
      else if (n == "showHumidity") doc[n] = (v == "true" || v == "on" || v == "1");
      else doc[n] = v;
    }
    if (LittleFS.exists("/config.json")) {
      LittleFS.rename("/config.json", "/config.bak");
    }
    File f = LittleFS.open("/config.json", "w");
    if (!f) {
      DEBUG_PRINTLN(F("[WEBSERVER] Failed to open /config.json for writing"));
      JsonDocument errorDoc;
      errorDoc[F("error")] = "Failed to write config";
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }
    serializeJson(doc, f);
    f.close();
    File verify = LittleFS.open("/config.json", "r");
    JsonDocument test;
    DeserializationError err = deserializeJson(test, verify);
    verify.close();
    if (err) {
      DEBUG_PRINT(F("[WEBSERVER] Config corrupted after save: "));
      DEBUG_PRINTLN(err.f_str());
      JsonDocument errorDoc;
      errorDoc[F("error")] = "Config corrupted. Reboot cancelled.";
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }
    JsonDocument okDoc;
    okDoc[F("message")] = "Saved successfully. Rebooting...";
    String response;
    serializeJson(okDoc, response);
    request->send(200, "application/json", response);
    DEBUG_PRINTLN(F("[WEBSERVER] Rebooting..."));
    request->onDisconnect([]() {
      DEBUG_PRINTLN(F("[WEBSERVER] Rebooting..."));
      ESP.restart();
    }); });

  server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    DEBUG_PRINTLN(F("[WEBSERVER] Request: /restore"));
    if (LittleFS.exists("/config.bak")) {
      File src = LittleFS.open("/config.bak", "r");
      if (!src) {
        DEBUG_PRINTLN(F("[WEBSERVER] Failed to open /config.bak"));
        JsonDocument errorDoc;
        errorDoc[F("error")] = "Failed to open backup file.";
        String response;
        serializeJson(errorDoc, response);
        request->send(500, "application/json", response);
        return;
      }
      File dst = LittleFS.open("/config.json", "w");
      if (!dst) {
        src.close();
        DEBUG_PRINTLN(F("[WEBSERVER] Failed to open /config.json for writing"));
        JsonDocument errorDoc;
        errorDoc[F("error")] = "Failed to open config for writing.";
        String response;
        serializeJson(errorDoc, response);
        request->send(500, "application/json", response);
        return;
      }
      // Copy contents
      while (src.available()) {
        dst.write(src.read());
      }
      src.close();
      dst.close();

      JsonDocument okDoc;
      okDoc[F("message")] = "✅ Backup restored! Device will now reboot.";
      String response;
      serializeJson(okDoc, response);
      request->send(200, "application/json", response);
      request->onDisconnect([]() {
        DEBUG_PRINTLN(F("[WEBSERVER] Rebooting after restore..."));
        ESP.restart();
      });

    } else {
      DEBUG_PRINTLN(F("[WEBSERVER] No backup found"));
      JsonDocument errorDoc;
      errorDoc[F("error")] = "No backup found.";
      String response;
      serializeJson(errorDoc, response);
      request->send(404, "application/json", response);
    } });

  server.on("/ap_status", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    DEBUG_PRINT(F("[WEBSERVER] Request: /ap_status. isAPMode = "));
    DEBUG_PRINTLN(isAPMode);
    String json = "{\"isAP\": ";
    json += (isAPMode) ? "true" : "false";
    json += "}";
    request->send(200, "application/json", json); });

  /// Andre: add webhook for doorbell. The doorbell will send a POST request to this endpoint
  server.on("/doorbell", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    DEBUG_PRINTLN(F("[WEBSERVER] Doorbell POST request received"));
    // Here you can handle the doorbell event, e.g., play a sound or log it
    
    P.displayText("De bel!", PA_CENTER, 1000, 0, PA_PRINT, PA_NO_EFFECT);
    DOORBELL_EVENT = 1; // Call your doorbell animation function
    request->send(200, "text/plain", "Doorbell event received"); });

  server.begin();
  DEBUG_PRINTLN(F("[WEBSERVER] Web server started"));
}

void performDoorbellAnimation()
{
  DEBUG_PRINTLN(F("[ANIMATION] Performing doorbell animation..."));

  for (int i = 0; i < 20; i++)
  {
    P.displayClear();
    P.setInvert(true);
    P.print("DE BEL!");
    delay(500);
    P.displayClear();
    P.setInvert(false);
    P.print("DE BEL!");
    delay(500);
  }
  P.setFont(mFactory); // Custom font
  P.setInvert(false);
  P.displayClear();

  displayMode = 0; // Reset display mode after doorbell animation
  lastSwitch = millis();
}

uint8_t* getWeatherIcon(const char *iconId)
{
  for (unsigned int i = 0; i < sizeof(APIData) / sizeof(APIData[0]); i++)
  {
    if (strcmp(APIData[i].id, iconId) == 0)
    {
      memcpy(currentWeatherIcon, APIData[i].name, sizeof(currentWeatherIcon));
      DEBUG_PRINTF("[WEATHER] Icon found: %s\n", APIData[i].id);
      return currentWeatherIcon;
    }
  }
  DEBUG_PRINTLN(F("[WEATHER] Icon not found"));
  memset(currentWeatherIcon, 0, sizeof(currentWeatherIcon)); // Clear icon if not found
  return nullptr; // Return null if icon not found
}

void fetchWeather()
{
  DEBUG_PRINTLN(F("[WEATHER] Fetching weather data..."));
  if (WiFi.status() != WL_CONNECTED)
  {
    DEBUG_PRINTLN(F("[WEATHER] Skipped: WiFi not connected"));
    weatherAvailable = false;
    weatherFetched = false;
    return;
  }
  if (!openWeatherApiKey || strlen(openWeatherApiKey) != 32)
  {
    DEBUG_PRINTLN(F("[WEATHER] Skipped: Invalid API key (must be exactly 32 characters)"));
    weatherAvailable = false;
    weatherFetched = false;
    return;
  }
  if (!(strlen(openWeatherCity) > 0 && strlen(openWeatherCountry) > 0))
  {
    DEBUG_PRINTLN(F("[WEATHER] Skipped: City or Country is empty."));
    return;
  }

  DEBUG_PRINTLN(F("[WEATHER] Connecting to OpenWeatherMap..."));
  const char *host = "api.openweathermap.org";
  String url = "/data/2.5/weather?q=" + String(openWeatherCity) + "," + String(openWeatherCountry) + "&appid=" + openWeatherApiKey + "&units=" + String(weatherUnits);
  DEBUG_PRINTLN(F("[WEATHER] URL: ") + url);

  IPAddress ip;
  if (!WiFi.hostByName(host, ip))
  {
    DEBUG_PRINTLN(F("[WEATHER] DNS lookup failed!"));
    weatherAvailable = false;
    return;
  }

  if (!client.connect(host, 80))
  {
    DEBUG_PRINTLN(F("[WEATHER] Connection failed"));
    weatherAvailable = false;
    return;
  }

  DEBUG_PRINTLN(F("[WEATHER] Connected, sending request..."));
  String request = String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n";

  if (!client.print(request))
  {
    DEBUG_PRINTLN(F("[WEATHER] Failed to send request!"));
    client.stop();
    weatherAvailable = false;
    return;
  }

  unsigned long weatherStart = millis();
  const unsigned long weatherTimeout = 10000; // 10 seconds max

  bool isBody = false;
  String payload = "";
  String line = "";

  // Read headers and then the entire body at once
  while ((client.connected() || client.available()) &&
         millis() - weatherStart < weatherTimeout &&
         WiFi.status() == WL_CONNECTED)
  {
    line = client.readStringUntil('\n');
    if (line.length() == 0)
      continue;

    if (line.startsWith("HTTP/1.1"))
    {
      int statusCode = line.substring(9, 12).toInt();
      if (statusCode != 200)
      {
        DEBUG_PRINT(F("[WEATHER] HTTP error: "));
        DEBUG_PRINTLN(statusCode);
        client.stop();
        weatherAvailable = false;
        return;
      }
    }

    if (!isBody && line == "\r")
    {
      isBody = true;
      // Read the entire body at once
      while (client.available())
      {
        payload += (char)client.read();
      }
      break; // All done!
    }
    yield();
    delay(1);
  }
  client.stop();

  if (millis() - weatherStart >= weatherTimeout)
  {
    DEBUG_PRINTLN(F("[WEATHER] ERROR: Weather fetch timed out!"));
    weatherAvailable = false;
    return;
  }

  DEBUG_PRINTLN(F("[WEATHER] Response received."));
  DEBUG_PRINTLN(F("[WEATHER] Payload: ") + payload);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error)
  {
    DEBUG_PRINT(F("[WEATHER] JSON parse error: "));
    DEBUG_PRINTLN(error.f_str());
    weatherAvailable = false;
    return;
  }

  if (doc["main"].is<JsonObject>() && doc["main"]["temp"].is<float>())
  {
    float temp = doc["main"]["temp"];
    currentTemp = String((int)round(temp)) + "º";
    DEBUG_PRINTF("[WEATHER] Temp: %s\n", currentTemp.c_str());
    weatherAvailable = true;
  }
  else
  {
    DEBUG_PRINTLN(F("[WEATHER] Temperature not found in JSON payload"));
    weatherAvailable = false;
    return;
  }

  if (doc["main"].is<JsonObject>() && doc["main"]["humidity"].is<int>())
  {
    currentHumidity = doc["main"]["humidity"];
    DEBUG_PRINTF("[WEATHER] Humidity: %d%%\n", currentHumidity);
  }
  else
  {
    currentHumidity = -1;
  }

  if (doc["weather"].is<JsonArray>() && doc["weather"][0]["icon"].is<const char *>())
  {
    const char *icon = doc["weather"][0]["icon"];
    uint8_t* iconData = getWeatherIcon(icon);
    DEBUG_PRINTF("[WEATHER] Icon: %s\n", icon);
    DEBUG_PRINTLN(iconData ? "[WEATHER] Icon data found" : "[WEATHER] Icon data not found");
  }
  else
  {
    DEBUG_PRINTLN(F("[WEATHER] Weather icon not found in JSON payload"));
  }

  if (doc["main"].is<JsonObject>() && doc["main"]["pressure"].is<int>())
  {
    currentPressure = doc["main"]["pressure"];
    DEBUG_PRINTF("[WEATHER] Pressure: %d hPa\n", currentPressure);
  }
  else
  {
    currentPressure = -1;
  }

  weatherFetched = true;
}

void setup()
{
  Serial.begin(74880);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN(F("[SETUP] Starting setup..."));
  P.begin();
  P.setFont(mFactory); // Custom font
  loadConfig();        // Load config before setting intensity & flip
  P.setIntensity(brightness);
  P.setZoneEffect(0, flipDisplay, PA_FLIP_UD);
  P.setZoneEffect(0, flipDisplay, PA_FLIP_LR);
  DEBUG_PRINTLN(F("[SETUP] Parola (LED Matrix) initialized"));
  connectWiFi();
  DEBUG_PRINTLN(F("[SETUP] Wifi connected"));
  setupWebServer();
  DEBUG_PRINTLN(F("[SETUP] Webserver setup complete"));
  DEBUG_PRINTLN(F("[SETUP] Setup complete"));
  DEBUG_PRINTLN();
  printConfigToSerial();
  setupTime(); // Start NTP sync process
  displayMode = 0;
  lastSwitch = millis();
  lastColonBlink = millis();
}

void loop()
{
  if (DOORBELL_EVENT == 1)
  {
    performDoorbellAnimation();
    DOORBELL_EVENT = 0; // Reset the event
  }
  // --- AP Mode Animation ---
  static unsigned long apAnimTimer = 0;
  static int apAnimFrame = 0;
  if (isAPMode)
  {
    unsigned long now = millis();
    if (now - apAnimTimer > 750)
    {
      apAnimTimer = now;
      apAnimFrame++;
    }
    P.setTextAlignment(PA_CENTER);
    switch (apAnimFrame % 3)
    {
    case 0:
      P.print("AP©");
      break;
    case 1:
      P.print("APª");
      break;
    case 2:
      P.print("AP«");
      break;
    }
    yield(); // Let system do background work
    return;  // Don't run normal display logic
  }

  static bool colonVisible = true;
  const unsigned long colonBlinkInterval = 800;
  if (millis() - lastColonBlink > colonBlinkInterval)
  {
    colonVisible = !colonVisible;
    lastColonBlink = millis();
  }

  static unsigned long ntpAnimTimer = 0;
  static int ntpAnimFrame = 0;
  static bool tzSetAfterSync = false;

  // WEATHER FETCH
  static unsigned long lastFetch = 0;
  const unsigned long fetchInterval = 900000; // 15 minutes

  // NTP state machine
  switch (ntpState)
  {
  case NTP_IDLE:
    break;
  case NTP_SYNCING:
  {
    time_t now = time(nullptr);
    if (now > 1000)
    {
      DEBUG_PRINTLN(F("\n[TIME] NTP sync successful."));
      ntpSyncSuccessful = true;
      ntpState = NTP_SUCCESS;
    }
    else if (millis() - ntpStartTime > ntpTimeout || ntpRetryCount > maxNtpRetries)
    {
      DEBUG_PRINTLN(F("\n[TIME] NTP sync failed."));
      ntpSyncSuccessful = false;
      ntpState = NTP_FAILED;
    }
    else
    {
      if (millis() - ntpStartTime > (static_cast<unsigned long>(ntpRetryCount) * 1000))
      {
        DEBUG_PRINT(".");
        ntpRetryCount++;
      }
    }
    break;
  }
  case NTP_SUCCESS:
    if (!tzSetAfterSync)
    {
      const char *posixTz = ianaToPosix(timeZone);
      setenv("TZ", posixTz, 1);
      tzset();
      tzSetAfterSync = true;
    }
    ntpAnimTimer = 0;
    ntpAnimFrame = 0;
    break;
  case NTP_FAILED:
    ntpAnimTimer = 0;
    ntpAnimFrame = 0;
    break;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!weatherFetchInitiated)
    {
      weatherFetchInitiated = true;
      fetchWeather();
      lastFetch = millis();
    }
    if (millis() - lastFetch > fetchInterval)
    {
      DEBUG_PRINTLN(F("[LOOP] Fetching weather data..."));
      weatherFetched = false;
      fetchWeather();
      lastFetch = millis();
    }
  }
  else
  {
    weatherFetchInitiated = false;
  }

  // Time display logic
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  int dayOfWeek = timeinfo.tm_wday;
  char *daySymbol = daysOfTheWeek[dayOfWeek];

  char timeStr[9]; // enough for "12:34 AM"
  if (twelveHourToggle)
  {
    int hour12 = timeinfo.tm_hour % 12;
    if (hour12 == 0)
      hour12 = 12;
    sprintf(timeStr, "%d:%02d", hour12, timeinfo.tm_min);
  }
  else
  {
    sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  }

  // Only prepend day symbol if showDayOfWeek is true
  String formattedTime;
  if (showDayOfWeek)
  {
    formattedTime = String(daySymbol) + " " + String(timeStr);
  }
  else
  {
    formattedTime = String(timeStr);
  }

  unsigned long displayDuration = (displayMode == 0) ? clockDuration : weatherDuration;
  if (millis() - lastSwitch > displayDuration)
  {
    displayMode++;
    displayMode = displayMode % 3;
    lastSwitch = millis();
    DEBUG_PRINTF("[LOOP] Switching displaymode, Displaymode now: %d\n", displayMode);
  }

  P.setTextAlignment(PA_CENTER);
  static bool weatherWasAvailable = false;

  if (displayMode == 0)
  { // Clock
    if (ntpState == NTP_SYNCING)
    {
      if (millis() - ntpAnimTimer > 750)
      {
        ntpAnimTimer = millis();
        switch (ntpAnimFrame % 3)
        {
        case 0:
          P.print("sync®");
          break;
        case 1:
          P.print("sync¯");
          break;
        case 2:
          P.print("sync°");
          break;
        }
        ntpAnimFrame++;
      }
    }
    else if (!ntpSyncSuccessful)
    {
      P.print("no ntp");
    }
    else
    {
      String timeString = formattedTime;
      if (!colonVisible)
        timeString.replace(":", " ");
      P.print(timeString);
    }
  }
  else if (displayMode == 1)
  { // Weather screen 1mode
    if (weatherAvailable)
    {
      // --- Weather display string with humidity toggle and 99% cap ---
      String weatherDisplay;
      if (showHumidity && currentHumidity != -1)
      {
        int cappedHumidity = (currentHumidity > 99) ? 99 : currentHumidity;
        weatherDisplay = currentTemp + " " + String(cappedHumidity) + "%";
      }
      else
      {
        weatherDisplay = currentTemp + tempSymbol;
      }
      P.print(weatherDisplay.c_str());
      weatherWasAvailable = true;
    }
  }
  else if (displayMode == 2)
  { // Weather screen 2 mode
    if (weatherAvailable)
    {
      // --- Weather display string with pressure ---
      String weatherDisplay = "";
      if (currentPressure != -1)
      {
        weatherDisplay = String(currentPressure) + " hPa";
      }
      else
      {
        weatherDisplay = " ??? hPa";
      }
      P.print(weatherDisplay.c_str());
      weatherWasAvailable = true;
    }
    else
    {
      if (weatherWasAvailable)
      {
        DEBUG_PRINTLN(F("[DISPLAY] Weather not available, showing clock..."));
        weatherWasAvailable = false;
      }
      if (ntpSyncSuccessful)
      {
        String timeString = formattedTime;
        if (!colonVisible)
          timeString.replace(":", " ");
        P.print(timeString);
      }
      else
      {
        P.print("no temp");
      }
    }
  }

  static unsigned long lastDisplayUpdate = 0;
  const unsigned long displayUpdateInterval = 50;
  if (millis() - lastDisplayUpdate >= displayUpdateInterval)
  {
    lastDisplayUpdate = millis();
  }

  yield();
}