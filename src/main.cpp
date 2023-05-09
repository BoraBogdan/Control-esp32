#include <WiFi.h>
#include <WebServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_wps.h>
#include <DHT.h>
#include <DHT_U.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>

#define ESP_WPS_MODE      WPS_TYPE_PBC
#define ESP_MANUFACTURER  "ESPRESSIF"
#define ESP_MODEL_NUMBER  "ESP32"
#define ESP_MODEL_NAME    "ESPRESSIF IOT"
#define ESP_DEVICE_NAME   "ESP STATION"
#define DHTTYPE DHT11
#define DHTPIN 33

/*
    IMPLEMENTARE WPS + POST REQUEST PERIODIC- completa
*/

DHT dht(DHTPIN, DHTTYPE);
static esp_wps_config_t config;
// WebServer server(80);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;
HTTPClient http;
const unsigned long postInterval = 10000; //Period to wait until sending post request in millis
unsigned long lastPostTime = 0;
const uint8_t soilHumidityPin = 34;
const uint8_t resetButtonPin = 23;
const uint8_t buttonPin = 22;
Ticker ticker;
StaticJsonDocument<200> networkStatusDoc;
bool timeExpired = false;


void wpsInitConfig(){
  config.wps_type = ESP_WPS_MODE;
  strcpy(config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(config.factory_info.device_name, ESP_DEVICE_NAME);
}

void wpsStart(){
    if(esp_wifi_wps_enable(&config)){
    	Serial.println("WPS Enable Failed");
    } else if(esp_wifi_wps_start(0)){
    	Serial.println("WPS Start Failed");
    }
}

void wpsStop(){
    if(esp_wifi_wps_disable()){
    	Serial.println("WPS Disable Failed");
    }
}

String wpspin2string(uint8_t a[])
{
  char wps_pin[9];
  for(int i=0;i<8;i++){
    wps_pin[i] = a[i];
  }
  wps_pin[8] = '\0';
  return (String)wps_pin;
}

String createFile(String nameAndExtensionOfFile) 
{
  String searchFor = "/" + nameAndExtensionOfFile;
  File file = SPIFFS.open(searchFor, "r");
  if (!file) 
    {
      Serial.println("Failed to open file");
      return "";
    }
  String html = file.readString();
  file.close();
  return html;
}

// void sendPage() 
// {
//   if (WiFi.status() == WL_CONNECTED)
//   {    
//     Serial.println("\nWiFi connected");
//     Serial.println("IP address: ");
//     Serial.println("local IP: " + WiFi.localIP().toString() + "\n");

//     server.send(200, "text/html", createFile("connected-successfully.html"));
//   }
//   else
//   {
//     Serial.println("");
//     Serial.println("WiFi connection failed");

//     server.send(404, "text/html", createFile("connected-failed.html"));
//   }
// }

void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info)
{   
  switch(event){
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
      preferences.begin("Credentials",false);
      preferences.putString("SSID", WiFi.SSID());
      preferences.putString("Password", WiFi.psk());
      preferences.end();
      wpsStop();
      delay(10);
      WiFi.begin();              
      // server.on("/connect-wps-result", []() 
      //           { server.send(200, "text/html", createFile("connected-successfully.html")); } );              
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

void printDotsWhileWaitingToConnect ()
{
  int cycles = 0;
  while (WiFi.status() != WL_CONNECTED && cycles < 10)
  {    
    Serial.print(".");
    cycles++;    
  }
  timeExpired = true;
}

// void onConnect()
// {
//   // String ssid = server.arg("ssid");
//   // String password = server.arg("password");

//   preferences.begin("Credentials", false);
//   preferences.putString("ssid", ssid);
//   preferences.putString("password", password);
//   preferences.end();     
  
//   WiFi.begin(ssid.c_str(), password.c_str());

//   printDotsWhileWaitingToConnect();

//   // int cycles = 0;
//   // while (WiFi.status() != WL_CONNECTED && cycles < 10)
//   // {
//   //   delay(500);
//   //   Serial.print(".");
//   //   cycles++;
//   // }

//   sendPage();

//   // clear preferences
//   // preferences.begin("Credentials", false);
//   // preferences.clear();
//   // preferences.end();
// }

void onConnectWPS()
{
  delay(10);
  Serial.println();
  WiFi.onEvent(WiFiEvent);
  Serial.println("Starting WPS");
  wpsInitConfig();
  wpsStart();       
}

void waitingForConnectionFromWPS() 
{
  onConnectWPS();
  // server.send(200, "text/html", createFile("waiting-connection-wps.html"));
}

void makePostRequestSendDhtData() 
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
    http.begin("http://192.168.0.101:8080/dht/addData");
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

void makePostRequestSendSoiltData() 
{
  ushort soilHumidity = analogRead(soilHumidityPin);

  if (WiFi.status() == WL_CONNECTED)
  {      
    http.begin("http://192.168.0.101:8080/soil/addSoilData");
    http.addHeader("Content-Type", "application/json");    

    StaticJsonDocument<200> doc;    
    doc["soilHumidity"] = soilHumidity; 
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

void sendConnectedStatus() 
{   
  if (ws.count() > 0)
  {
    String json;
    networkStatusDoc["status"] = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
    networkStatusDoc["expired"] = timeExpired ? "expired" : "valid";
    serializeJson(networkStatusDoc, json);      
    ws.textAll(json);  
  }  
}

void resetNetworkCredentials() 
{
  if (digitalRead(resetButtonPin) == LOW)
  {
    digitalWrite(buttonPin, HIGH);
    preferences.begin("Credentials", false);
    preferences.clear();
    preferences.end();
  } else 
  {
    digitalWrite(buttonPin, LOW);
  }
}

void setup()
{
  Serial.begin(921600); 

  dht.begin();

  pinMode(resetButtonPin, INPUT);
  pinMode(buttonPin, OUTPUT);

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.softAP("MyESP32AP", "password");  

  Serial.println("\n\n" + WiFi.softAPIP().toString());  

  preferences.begin("Credentials", false);
  String ssid = preferences.getString("SSID", "");
  String password = preferences.getString("Password", "");
  if ( ssid == "" || password == "") 
  {
    Serial.println("No WiFi credentials");
    timeExpired = true;
  } else 
  {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid.c_str(), password.c_str());    
    printDotsWhileWaitingToConnect();
  }
  preferences.end();

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  // server.on("/", []()
  //           { server.send(200, "text/html", createFile("check-saved-network.html")); });
  // server.on("/jquery-3.6.4.js", []()
  //           { server.send(200, "text/javascript", createFile("jquery-3.6.4.js")); });
  // server.on("/network-status", sendConnectedStatus);
  // server.on("/", []()
  //           { server.send(200, "text/html", createFile("home.html")); });
  // server.on("/styles.css", []()
  //           { server.send(200, "text/css", createFile("styles.css")); });     
  // server.on("/connect", onConnect);   
  // server.on("/connect-wps", waitingForConnectionFromWPS);
  // server.onNotFound([]() { server.send(404, "text/html", createFile("connected-failed.html")); } ); 

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/check-saved-network.html", "text/html");
  });

  server.on("/jquery-3.6.4.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/jquery-3.6.4.js", "text/javascript");
  });

  server.on("/home", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/home.html", "text/html");
  });

  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/styles.css", "text/css");
  });  

  server.on("/connected-succesfully", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/connected-succesfully.html", "text/html");
  });    

  Serial.println("Web server started!");   
  server.addHandler(&ws);
  server.begin();      
  ticker.attach(5, sendConnectedStatus);    
}

void loop()
{
  // server.handleClient();  
  unsigned long currentTime = millis();
  if (currentTime - lastPostTime >= postInterval)
  {   
    makePostRequestSendDhtData();
    delay(10);
    makePostRequestSendSoiltData();

    lastPostTime = currentTime;
  } 

  resetNetworkCredentials();
}