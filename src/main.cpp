/*
 *  LEDs working
 *  Reset button working
 *  Credentials connect working
 *  WPS working when wps started on router
 ?  if wps is not started on router you need to reconnect to the mcu to get the fail page
*/
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_wps.h>
#include <DHT.h>
#include <DHT_U.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <TickTwo.h>

#define ESP_WPS_MODE      WPS_TYPE_PBC
#define ESP_MANUFACTURER  "ESPRESSIF"
#define ESP_MODEL_NUMBER  "ESP32"
#define ESP_MODEL_NAME    "ESPRESSIF IOT"
#define ESP_DEVICE_NAME   "ESP STATION"
#define DHTTYPE DHT11
#define DHTPIN 33
#define postInterval 10000             //Period waited until sending post request in millis
#define connectionInterval 10000       //Period waited for a stable connection until sending a response (in ms)
#define soilHumidityPin   34
#define resetButtonPin    23
#define redPin            22
#define greenPin          21
#define yellowPin         19   // yellow LED  — MCU config saved indicator
#define configResetPin    16

// --- AWS backend config (stored in NVS — not hardcoded) ---
const char*         SERVER_HOST       = "34.205.177.195";  // update after each ECS redeploy
const int           SERVER_PORT       = 8080;
const unsigned long HEARTBEAT_INTERVAL = 300000;        // 5 minutes in ms

// Set to 1 to send synthetic values for the 5 sensors not physically wired yet
// (air pressure, CO2, soil temp, soil pH, light). Flip to 0 once real sensors land.
#define MOCK_EXTRA_SENSORS  1

//? --- Prototypes ---
void waitForStableConn();
void saveCredentials();
void resetNetworkCredentials();
void checkPreferencesForCredentials();

DHT dht(DHTPIN, DHTTYPE);
static esp_wps_config_t config;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;
HTTPClient http;
Ticker ticker;
Ticker checkForResetButtonPressed;
Ticker checkForConfigReset;
TickTwo timerStatusConn(waitForStableConn, connectionInterval, 1);
StaticJsonDocument<200> networkStatusDoc;

bool timeExpired = false;
bool credentialsSaved = false;
bool resetButtonPressed = false;
unsigned long lastPostTime  = 0;
unsigned long lastHeartbeat = 0;
bool wpsStarted = false;

// MCU config (API key + device index stored in NVS namespace "Config")
bool   configSaved              = false;
bool   configResetButtonPressed = false;
String g_apiKey      = "";
String g_deviceIndex = "";


//*start wps methods
void wpsInitConfig()
{
  config.wps_type = ESP_WPS_MODE;
  strcpy(config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(config.factory_info.device_name, ESP_DEVICE_NAME);
}

void wpsStart()
{
    if(esp_wifi_wps_enable(&config))
    {
    	Serial.println("WPS Enable Failed");
    } else if(esp_wifi_wps_start(0))
    {
    	Serial.println("WPS Start Failed");
    }
}

void wpsStop()
{
    if(esp_wifi_wps_disable())
    {
    	Serial.println("WPS Disable Failed");
    }
}

String wpspin2string(uint8_t a[])
{
  char wps_pin[9];
  for(int i=0;i<8;i++)
  {
    wps_pin[i] = a[i];
  }
  wps_pin[8] = '\0';
  return (String)wps_pin;
}

void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info)
{   
  switch(event)
  {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("Station Mode Started");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("Connected to: " + String(WiFi.SSID()));
      Serial.print("Got IP: ");
      Serial.println(WiFi.localIP().toString());            
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:      
      Serial.println("Disconnected from station, attempting reconnection");
      WiFi.reconnect();
      break;

    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      Serial.println("WPS Successfull, stopping WPS and connecting to: " + String(WiFi.SSID()));      
      saveCredentials();
      wpsStop();
      delay(10);                              
      break;

    case ARDUINO_EVENT_WPS_ER_FAILED:
      Serial.println("WPS Failed, retrying");         
      wpsStop();
      wpsStart();                
      break;

    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      Serial.println("WPS Timedout, retrying");
      wpsStop();
      wpsStart();
      break;

    case ARDUINO_EVENT_WPS_ER_PIN:
      Serial.println("WPS_PIN = " + wpspin2string(info.wps_er_pin.pin_code));
      break;

    default:      
      break;
  }
}
//*end wps methods

//* Starts WPS connection
void startConnectionWPS()
{  
  Serial.println();
  WiFi.onEvent(WiFiEvent);
  Serial.println("Starting WPS");
  wpsInitConfig();
  wpsStart(); 
  wpsStarted = true;
}

//* if connection to WiFi is successfulf prints the ip
void printIP() 
{  
  Serial.println("\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println("local IP: " + WiFi.localIP().toString() + "\n");
  
}

//* updates the json after starting the connection
void waitForStableConn()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Conectat");
    printIP();
    wpsStarted = false;
    saveCredentials();    
  } else if (WiFi.status() != WL_CONNECTED)
  {    
    Serial.println("Deconectat");
    timeExpired = true;    
    if (wpsStarted) 
    {
      ESP.restart();
    }
    
  }
}

//* load API key + device index from NVS "Config" namespace
void loadConfig()
{
  preferences.begin("Config", true);          // read-only
  g_apiKey      = preferences.getString("ApiKey",      "");
  g_deviceIndex = preferences.getString("DeviceIndex", "");
  preferences.end();
  if (g_apiKey.length() > 0 && g_deviceIndex.length() > 0) {    
    configSaved = true;
  }
}

//* ISR — sets flag only, same pattern as the WiFi reset button
void IRAM_ATTR configResetISR()
{
  configResetButtonPressed = true;
}

//* wipe "Config" NVS — called by Ticker every 200 ms
void checkConfigResetButton()
{
  if (configResetButtonPressed)
  {    
    preferences.begin("Config", false);
    preferences.clear();
    preferences.end();
    g_apiKey      = "";
    g_deviceIndex = "";
    configSaved   = false;
    configResetButtonPressed = false;
    Serial.println("MCU config cleared from NVS");
  }
}

//* setup server requests
void setupServerRequests()
{    
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!configSaved) {
      request->send(SPIFFS, "/api-key.html", "text/html");
    } else {
      checkPreferencesForCredentials();
      request->send(SPIFFS, "/check-saved-network.html", "text/html");
    }
  });

  server.on("/api-key", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/api-key.html", "text/html");
  });

  server.on("/save-config", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("apiKey") && request->hasParam("deviceIndex")) {
      String key = request->getParam("apiKey")->value();
      String idx = request->getParam("deviceIndex")->value();
      if (key.length() > 0 && idx.length() > 0) {
        preferences.begin("Config", false);
        preferences.putString("ApiKey",      key);
        preferences.putString("DeviceIndex", idx);
        preferences.end();
        g_apiKey      = key;
        g_deviceIndex = idx;
        configSaved   = true;
        Serial.println("MCU config saved — device: " + idx);
      }
    }
    request->redirect("/home");
  });

  server.on("/jquery-3.6.4.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/jquery-3.6.4.js", "text/javascript");
  });

  server.on("/home", HTTP_GET, [](AsyncWebServerRequest *request){    
    timeExpired = false;
    request->send(SPIFFS, "/home.html", "text/html");
  });

  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/styles.css", "text/css");
  });  

  server.on("/success", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/connected-successfully.html", "text/html");
  }); 

  server.on("/fail", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/connected-failed.html", "text/html");
  }); 

  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request){
    String ssidFromClient = "";
    String passwordFromClient = "";

    if (request->hasParam("ssid", true) && request->hasParam("password", true))
    {
      Serial.println("handle connect");
      ssidFromClient     = request->getParam("ssid",     true)->value();
      passwordFromClient = request->getParam("password", true)->value();
      WiFi.begin(ssidFromClient.c_str(), passwordFromClient.c_str());
      request->send(SPIFFS, "/waiting-connection.html", "text/html");
      timerStatusConn.start();
    }
  });

  server.on("/connect-wps", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/waiting-connection.html", "text/html");
    startConnectionWPS();
    timerStatusConn.start();    
  });
}

//* setup WiFi config
void setupWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.softAP("MyESP32AP", "passwordSecure");  
  Serial.println("\n\n" + WiFi.softAPIP().toString());  
}

//* mounting and checking SPIFFS
void mountSPIFFS()
{
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  } 
}

//*start post request methods

#if MOCK_EXTRA_SENSORS
//* Mock-sensor helpers — synthetic 24-hour cycle from millis(), random walks
//* Each function maintains internal state with a `static` so successive calls
//* drift smoothly instead of jumping. Day phase wraps every 24h since boot.
static float dayPhase()
{
  unsigned long secOfDay = (millis() / 1000UL) % 86400UL;
  return secOfDay / 86400.0f;                            // 0..1
}

static float daylight()                                  // 0 at "night", peak 1 at "noon"
{
  float p = dayPhase();
  float v = sinf(p * 2.0f * PI - PI / 2.0f);
  return v < 0.0f ? 0.0f : v;
}

static float mockAirPressure()                           // hPa, drift ±0.3 per call, clamp 990–1035
{
  static float v = 1013.25f;
  v += random(-3, 4) / 10.0f;
  if (v < 990.0f)  v = 990.0f;
  if (v > 1035.0f) v = 1035.0f;
  return v;
}

static float mockCo2()                                   // ppm, high at night, low at day, clamp 380–1200
{
  static float drift = 0.0f;
  drift += random(-10, 11) / 10.0f;
  if (drift < -100.0f) drift = -100.0f;
  if (drift >  100.0f) drift =  100.0f;
  float v = 800.0f - daylight() * 350.0f + drift;
  if (v < 380.0f)  v = 380.0f;
  if (v > 1200.0f) v = 1200.0f;
  return v;
}

static float mockSoilTemp()                              // °C, slow drift 17–22
{
  static float v = 18.5f;
  v += random(-5, 6) / 100.0f;
  if (v < 17.0f) v = 17.0f;
  if (v > 22.0f) v = 22.0f;
  return v;
}

static float mockSoilPh()                                // pH, slow drift 5.5–7.5
{
  static float v = 6.5f;
  v += random(-1, 2) / 100.0f;
  if (v < 5.5f) v = 5.5f;
  if (v > 7.5f) v = 7.5f;
  return v;
}

static float mockLight()                                 // lux, follows daylight curve, clamp 0–35000
{
  static float drift = 0.0f;
  drift += random(-100, 101) / 100.0f;
  if (drift < -2000.0f) drift = -2000.0f;
  if (drift >  2000.0f) drift =  2000.0f;
  float v = daylight() * 30000.0f + drift;
  if (v < 0.0f)     v = 0.0f;
  if (v > 35000.0f) v = 35000.0f;
  return v;
}
#endif  // MOCK_EXTRA_SENSORS

//* sends all sensor readings in one flat JSON POST to the single-reading endpoint
void postSensorData()
{
  float temperature = dht.readTemperature();
  float airHumidity = dht.readHumidity();

  if (isnan(temperature) || isnan(airHumidity))
  {
    Serial.println("Failed to read data from DHT sensor");
    return;
  }

  // Map raw ADC (0-4095) to soil moisture % (dry=0%, wet=100%)
  int   rawSoil  = analogRead(soilHumidityPin);
  float soilPct  = (4095 - rawSoil) * 100.0f / 4095.0f;

  if (WiFi.status() == WL_CONNECTED)
  {
    String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT
                 + "/api/v1/sensor-data/device/" + g_deviceIndex;

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", g_apiKey.c_str());

    // Build flat JSON object — all sensor values as direct fields
    StaticJsonDocument<384> doc;
    doc["temperature"]  = temperature;
    doc["humidity"]     = airHumidity;
    doc["soilMoisture"] = soilPct;

#if MOCK_EXTRA_SENSORS
    doc["airPressure"]     = mockAirPressure();
    doc["co2Level"]        = mockCo2();
    doc["soilTemperature"] = mockSoilTemp();
    doc["soilPh"]          = mockSoilPh();
    doc["lightIntensity"]  = mockLight();
#endif

    String jsonStr;
    serializeJson(doc, jsonStr);

    int httpResponseCode = http.POST(jsonStr);

    if (httpResponseCode > 0)
    {
      String response = http.getString();
      Serial.println("\nSensor POST — status: " + String(httpResponseCode));
      Serial.println(response);
      Serial.println();
    } else
    {
      Serial.println("Error on sensor POST");
      Serial.println("Status code: " + String(httpResponseCode) + "\n");
    }
    http.end();
  }
}

//* sends a heartbeat to keep last_seen fresh (called every 5 minutes)
void sendHeartbeat()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT
                 + "/api/v1/microcontrollers/" + g_deviceIndex + "/heartbeat";

    http.begin(url);
    http.addHeader("X-API-Key", g_apiKey.c_str());

    int httpResponseCode = http.POST("");

    if (httpResponseCode > 0)
    {
      Serial.println("Heartbeat — status: " + String(httpResponseCode));
    } else
    {
      Serial.println("Heartbeat failed — status: " + String(httpResponseCode));
    }
    http.end();
  }
}

//*end post request methods
//* reset preferences with button
void resetNetworkCredentials() 
{
  if (resetButtonPressed)
  {
    preferences.begin("Credentials", false);
    Serial.println("button apasat");
    preferences.clear();
    preferences.end();    
    credentialsSaved = false;
    resetButtonPressed = false;
  }
}

//* update json sent to the WebSocket client + sending it to all clients
void sendConnectedStatus() 
{   
  Serial.print(ws.count());
  if (ws.count() > 0)
  {
    String json;
    networkStatusDoc["status"] = (WiFi.status() == WL_CONNECTED) ? "conectat" : "deconectat";
    networkStatusDoc["expired"] = timeExpired ? "expired" : "valid";
    serializeJson(networkStatusDoc, json);          
    ws.textAll(json);  
  }  
}

//* check for saved network credentials in preferences
void checkPreferencesForCredentials()
{
  preferences.begin("Credentials", false);
  String ssid = preferences.getString("SSID", "");  
  String password = preferences.getString("Password", "");
  preferences.end();
  if ( ssid == "" || password == "") 
  {
    Serial.println("No WiFi credentials");
    timeExpired = true;
  } else 
  {
    if (WiFi.status() != WL_CONNECTED)
    {    
    Serial.println("Connecting to WiFi...");
    credentialsSaved = true;
    WiFi.begin(ssid.c_str(), password.c_str());    
    timerStatusConn.start();   
    }
  }
  
}


//* puts WiFi credentials in preferences and sets credentialsSaved = true
void saveCredentials()
{
  preferences.begin("Credentials",false);
  preferences.putString("SSID", WiFi.SSID());
  preferences.putString("Password", WiFi.psk());
  preferences.end();
  credentialsSaved = true;
}

//* light leds accordingly
void lightLeds()
{
  // Green / Red: WiFi credentials state
  digitalWrite(greenPin, credentialsSaved ? HIGH : LOW);
  digitalWrite(redPin,   credentialsSaved ? LOW  : HIGH);
  // Yellow: MCU config state (API key + device index saved)
  digitalWrite(yellowPin, configSaved ? HIGH : LOW);
}

//* cheking if the button is pressed and sets flag
void IRAM_ATTR checkButtonISR()
{
  resetButtonPressed = true;
}

//* periodic sensor post + heartbeat in loop
void sendRequests(ulong currentTime)
{
  if (currentTime - lastPostTime >= postInterval)
  {
    postSensorData();
    lastPostTime = currentTime;
  }

  if (currentTime - lastHeartbeat >= HEARTBEAT_INTERVAL)
  {
    sendHeartbeat();
    lastHeartbeat = currentTime;
  }
}

void setup()
{
  Serial.begin(921600);
  dht.begin();

  pinMode(resetButtonPin, INPUT);
  pinMode(redPin,         OUTPUT);
  pinMode(greenPin,       OUTPUT);
  pinMode(yellowPin,      OUTPUT);
  pinMode(configResetPin, INPUT);

  loadConfig();
  setupWiFi();
  if (configSaved) checkPreferencesForCredentials();
  mountSPIFFS();
  setupServerRequests();

  attachInterrupt(resetButtonPin,  checkButtonISR,   FALLING);
  attachInterrupt(configResetPin,  configResetISR,   FALLING);

  Serial.println("Web server started!");
  server.addHandler(&ws);
  server.begin();
  ticker.attach(2, sendConnectedStatus);
  checkForResetButtonPressed.attach_ms(200, resetNetworkCredentials);
  checkForConfigReset.attach_ms(200, checkConfigResetButton);
}

void loop()
{  
  lightLeds();  
  
  unsigned long currentTime = millis();
  sendRequests(currentTime);

  timerStatusConn.update();
}