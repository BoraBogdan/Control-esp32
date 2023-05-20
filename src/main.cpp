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
#define soilHumidityPin 34
#define resetButtonPin 23
#define redPin 22
#define greenPin 21

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
TickTwo timerStatusConn(waitForStableConn, connectionInterval, 1);
StaticJsonDocument<200> networkStatusDoc;

bool timeExpired = false;
bool credentialsSaved = false;
bool resetButtonPressed = false;
unsigned long lastPostTime = 0;
bool wpsStarted = false;


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

//* setup server requests
void setupServerRequests()
{    
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    checkPreferencesForCredentials();
    request->send(SPIFFS, "/check-saved-network.html", "text/html");
  });

  server.on("/jquery-3.6.4.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/jquery-3.6.4.js", "text/javascript");
  });

  server.on("/home", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.print("home");
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

  server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request){
    String ssidFromClient = "";
    String passwordFromClient = "";

    if (request->hasParam("ssid") && request->hasParam("password")) 
    {    
      Serial.println("handle connect");  
      ssidFromClient = request->getParam("ssid")->value();
      passwordFromClient = request->getParam("password")->value();
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
  WiFi.softAP("MyESP32AP", "password");  
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
void postRequestSendDhtData() 
{
  float temperature = dht.readTemperature();
  float airHumidity = dht.readHumidity(); 

  if (isnan(temperature) || isnan(airHumidity))
  {
    Serial.println("Failed to read data form sensor");  
    return;
  } 

  if (WiFi.status() == WL_CONNECTED)
  {      
    http.begin("http://192.168.2.101:8080/dht/addData");
    http.addHeader("Content-Type", "application/json");    

    StaticJsonDocument<200> doc;    
    doc["airHumidity"] = airHumidity;
    doc["temperature"] = temperature;    
    doc["microcontrollerID"] = 1;
    String jsonStr;
    serializeJson(doc, jsonStr);    
    
    int httpResponseCode = http.POST(jsonStr);   

    if (httpResponseCode > 0)
    {      
      String response = http.getString();
      Serial.println("\nStatus code: " + String(httpResponseCode));
      Serial.println(response);        
      Serial.println();
    } else
    {
      Serial.println("Error on HTTP request");
      Serial.println("\nStatus code: " + String(httpResponseCode) + "\n");
    }
    http.end();     
  } 
}

void postRequestSendSoiltData() 
{
  ushort soilHumidity = analogRead(soilHumidityPin);

  if (WiFi.status() == WL_CONNECTED)
  {      
    http.begin("http://192.168.2.101:8080/soil/addSoilData");
    http.addHeader("Content-Type", "application/json");    

    StaticJsonDocument<200> doc;    
    doc["soilHumidity"] = soilHumidity; 
    doc["microcontrollerID"] = 0;
    String jsonStr;
    serializeJson(doc, jsonStr);    
    
    int httpResponseCode = http.POST(jsonStr);   

    if (httpResponseCode > 0)
    {      
      String response = http.getString();
      Serial.println("\nStatus code: " + String(httpResponseCode));
      Serial.println(response);        
      Serial.println();
    } else
    {
      Serial.println("Error on HTTP request");
      Serial.println("\nStatus code: " + String(httpResponseCode) + "\n");
    }
    http.end();     
  } 
}
//*end post request methods

//* reset preferences with button and light led
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
  if (credentialsSaved)
  {
    digitalWrite(greenPin, HIGH);
    digitalWrite(redPin, LOW);
  } else 
  {
    digitalWrite(greenPin,  LOW);
    digitalWrite(redPin, HIGH);
  }
}

//* cheking if the button is pressed and sets flag
void IRAM_ATTR checkButtonISR()
{
  resetButtonPressed = true;
}

//* perodic post requesti in loop
void sendRequests(ulong currentTime)
{
  if (currentTime - lastPostTime >= postInterval)
  {   
    postRequestSendDhtData();    
    postRequestSendSoiltData();
    lastPostTime = currentTime;
  } 
}

void setup()
{
  Serial.begin(921600); 
  dht.begin();

  pinMode(resetButtonPin, INPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);   

  setupWiFi();
  checkPreferencesForCredentials();
  mountSPIFFS();
  setupServerRequests();

  attachInterrupt(resetButtonPin, checkButtonISR, FALLING);

  Serial.println("Web server started!");     
  server.addHandler(&ws);  
  server.begin();   
  ticker.attach(2, sendConnectedStatus);   
  checkForResetButtonPressed.attach_ms(200, resetNetworkCredentials);
}

void loop()
{  
  lightLeds();  
  
  unsigned long currentTime = millis();
  sendRequests(currentTime);

  timerStatusConn.update();
}