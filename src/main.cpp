#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_wps.h>
#include <DHT.h>
#include <DHT_U.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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
WebServer server(80);
Preferences preferences;
HTTPClient http;
const unsigned long postInterval = 10000; //Period to wait until sending post request in millis
unsigned long lastPostTime = 0;

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

String wpspin2string(uint8_t a[]){
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

void sendPage() 
{
  if (WiFi.status() == WL_CONNECTED)
  {    
    Serial.println("\nWiFi connected");
    Serial.println("IP address: ");
    Serial.println("local IP: " + WiFi.localIP().toString() + "\n");

    server.send(200, "text/html", createFile("connected-successfully.html"));
  }
  else
  {
    Serial.println("");
    Serial.println("WiFi connection failed");

    server.send(404, "text/html", createFile("connected-failed.html"));
  }
}

void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info){   
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
      wpsStop();
      delay(10);
      WiFi.begin();              
      server.on("/connect-wps-result", []() 
                { server.send(200, "text/html", createFile("connected-successfully.html")); } );              
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

void onConnect()
{
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  preferences.begin("Credentials", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();     
  
  WiFi.begin(ssid.c_str(), password.c_str());

  int cycles = 0;
  while (WiFi.status() != WL_CONNECTED && cycles < 10)
  {
    delay(500);
    Serial.print(".");
    cycles++;
  }

  sendPage();

  // clear preferences
  preferences.begin("Credentials", false);
  preferences.clear();
  preferences.end();
}

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
  server.send(200, "text/html", createFile("waiting-connection-wps.html"));
}

void makePostRequestSendDhtData(float airHumidity, float temperature) 
{
  if (WiFi.status() == WL_CONNECTED)
  {      
    http.begin("http://192.168.1.7:8080/dht/addData");
    http.addHeader("Content-Type", "application/json");    

    StaticJsonDocument<200> doc;    
    doc["airHumidity"] = airHumidity;
    doc["temperature"] = temperature;    
    
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

void setup()
{
  Serial.begin(921600);  
  dht.begin();
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.softAP("MyESP32AP", "password");  
  Serial.println("\n\n" + WiFi.softAPIP().toString());

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  server.on("/", []()
            { server.send(200, "text/html", createFile("home.html")); });
  server.on("/styles.css", []()
            { server.send(200, "text/css", createFile("styles.css")); });     
  server.on("/connect", onConnect);   
  server.on("/connect-wps", waitingForConnectionFromWPS);
  server.onNotFound([]() { server.send(404, "text/html", createFile("connected-failed.html")); } ); 

  Serial.println("Web server started!"); 
  server.begin();    
}

void loop()
{
  server.handleClient();
  
  float temperature = dht.readTemperature();
  float airHumidity = dht.readHumidity();

  if (isnan(temperature) || isnan(airHumidity))
  {
    Serial.println("Failed to read data form sensor");  
    return;
  }   

  unsigned long currentTime = millis();
  if (currentTime - lastPostTime >= postInterval)
  {    
    makePostRequestSendDhtData(airHumidity, temperature);
    lastPostTime = currentTime;
  }
}