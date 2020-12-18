#include <Arduino.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "DHTesp.h"

#define WIFI_SSID "TheMarma's2.4"
#define WIFI_PASSWORD "56746747"
#define INFLUXDB_URL "http://192.168.0.134:8085/"
#define INFLUXDB_TOKEN "WNgkAWBopvLgKtiCOGT2Vnka2gSZLIeKgK12KMz1hy2cJuU3AjF6XHu_8jcqUcSaaFJLjAYjaT_LTAcUnlFbdw=="
#define INFLUXDB_ORG "FinWa"
#define INFLUXDB_BUCKET "Arduino"
#define TZ_INFO "America/Argentina/Buenos_Aires"
#define DEVICE "ESP32"

#define LED_PINT 2

#define DHTTYPE DHT11
#define DHT_PIN_DATA  4

DHTesp dht;
ComfortState cf;

WiFiMulti wifiMulti;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point sensor("wifi_status");
Point sensor2("weather");

TaskHandle_t TaskSendData, TaskReadSensors, TaskWebRequests, TaskStatusSensors;
SemaphoreHandle_t semaforo;

WebServer server(80);

String str_sensor2 = "";
String str_sensor1 = "";
TickType_t delay10seg = 10000 / portTICK_PERIOD_MS;
TickType_t delay5seg = 5000 / portTICK_PERIOD_MS;
TickType_t delay1seg = 1000 / portTICK_PERIOD_MS;
TickType_t delay500ms = 500 / portTICK_PERIOD_MS;
TickType_t delay100ms = 100 / portTICK_PERIOD_MS;

void WatchdogError();
void SendMetricsWifi();
void SendMetricsDHT11();
void blink();
void ReadSensors();

void funcTaskSendData(void *parameter){ 
  for(;;){
    vTaskDelay(delay10seg);
    xSemaphoreTake(semaforo,portMAX_DELAY);
    if ((WiFi.RSSI() == 0) && (wifiMulti.run() != WL_CONNECTED))
    { 
      Serial.print("Connecting to wifi");
      int count = 0;
      while (wifiMulti.run() != WL_CONNECTED) {
        Serial.print(".");
        digitalWrite(LED_PINT, HIGH);
        vTaskDelay(delay1seg);
        digitalWrite(LED_PINT, LOW);
        vTaskDelay(delay1seg);   
      }
    }
    else{
      SendMetricsWifi();
      SendMetricsDHT11();
    }
    xSemaphoreGive(semaforo);  
  }
  vTaskDelay(delay100ms);
}

void funcTaskReadSensors(void *parameter){  
  for(;;){
    vTaskDelay(delay10seg);
    xSemaphoreTake(semaforo,portMAX_DELAY);
    ReadSensors();
    xSemaphoreGive(semaforo);    
  }
  vTaskDelay(delay100ms);
}

void funcTaskStatusSensors(void *parameter){ 
  for(;;){
    vTaskDelay(delay5seg);
    xSemaphoreTake(semaforo,portMAX_DELAY);    
    WatchdogError();
    xSemaphoreGive(semaforo);
  }
  vTaskDelay(delay100ms);
}

void funcTaskWebRequests(void *parameter){    
  for(;;){
    vTaskDelay(delay100ms);
    if ((WiFi.RSSI() == 0) && (wifiMulti.run() != WL_CONNECTED))
    { 
        vTaskDelay(delay10seg);
    }
    else{
      server.handleClient();       
    }
  }
  vTaskDelay(delay100ms);  
}

void InitPins(){
  pinMode(LED_PINT, OUTPUT); 
}

void InitWifi(){
   Serial.print("Setup to wifi");
  // Setup wifi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.println();

  digitalWrite(LED_PINT, HIGH);
  delay(100);
  digitalWrite(LED_PINT, LOW);
  delay(100);
  digitalWrite(LED_PINT, HIGH);
  delay(500);
  digitalWrite(LED_PINT, LOW);
  
  Serial.print("Connecting to wifi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(LED_PINT, HIGH);
    delay(500);
    digitalWrite(LED_PINT, LOW);
    delay(500);
  }
  
  Serial.println();
  Serial.print("Connected to ");Serial.print(WIFI_SSID);Serial.print(" IP address: ");Serial.println(WiFi.localIP());  
  Serial.println();  
  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }
}

void InitSensors(){
  Serial.print("Setup DTH_11");
  dht.setup(DHT_PIN_DATA, DHTesp::DHT11);
  Serial.println();

  Serial.print("Setup sensor's tag");

  sensor.addTag("device", DEVICE);
  sensor.addTag("SSID", WiFi.SSID());

  sensor2.addTag("device", "DHT_11");
  Serial.println();
}

void InitInfluxDB(){
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
  
  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}

void InitWebServer(){
  server.on("/", []() {
    server.send(200, "application/json", "{'message':'hello from esp32!'}");
  });

  server.on("/temp/", []() {  
   String temp = String("<html>\
                          <head>\
                            <meta http-equiv='refresh' content='10'/>\
                            <title>Weather Dashboard</title>\
                            <style>\
                            </style>\
                            </head>\
                          <body>\
                            <div id='dashboard'></div>\
                            <script type='text/javascript'>\
                              var json = " + str_sensor2 + ";\
                              var elm = document.getElementById('dashboard');\
                              elm.innerHTML += '<h1>Dashboard</h1><hr /><h2>Temperatura: '+json.temperatura+'C</h2><h2>Humedad: '+json.humedad+'%</h2><h2>Confort: '+json.comfort+'</h2><h2>Percepcion: '+json.percepcion+'</h2>';\
                            </script>\
                            </body>\
                        </html>");              
    server.send(200, "text/html", temp);    
  });

  server.on("/stat/", []() {  
    server.send(200, "application/json",str_sensor2);    
  });

  server.on("/wifi/", []() {  
    server.send(200, "application/json",str_sensor1);    
  });
  
  server.begin();
  Serial.println("HTTP server started");
}

void InitTask(){
  semaforo = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(funcTaskReadSensors,"TaskReadSensors",2048,NULL,1,&TaskReadSensors,1 );
  xTaskCreatePinnedToCore(funcTaskStatusSensors,"TaskStatusSensors",2048,NULL,2,&TaskStatusSensors,1);
  xTaskCreatePinnedToCore(funcTaskSendData,"TaskSendData",2048,NULL,1,&TaskSendData,0);  
  xTaskCreatePinnedToCore(funcTaskWebRequests,"TaskWebRequests",2048,NULL,2,&TaskWebRequests,0);
  Serial.println("Tasks started");
}

void setup() {     
  Serial.println("Init Setup");  
  Serial.begin(115200);
  InitPins();
  InitWifi();
  InitSensors();
  InitInfluxDB();  
  Serial.println("End Setup"); 

  InitWebServer();  
  InitTask();
}

void loop() {
}

void WatchdogError()
{    
  digitalWrite(LED_PINT, LOW);
  if ((WiFi.RSSI() == 0) && (wifiMulti.run() != WL_CONNECTED))
  {      
    Serial.println("Wifi connection lost");          
  } 
  else {  
    if (!client.writePoint(sensor) || !client.writePoint(sensor2)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());      
      blink();
      blink();        
    }
    else {
      blink();  
    }  
  }
}

void SendMetricsWifi()
{
  // Store measured value into point
  sensor.clearFields();
  // Report RSSI of currently connected network
  sensor.addField("rssi", WiFi.RSSI());
  str_sensor1 = String("{'wifi_power_db':"+String(WiFi.RSSI())+"}");
  // Print what are we exactly writing
  Serial.print("Writing: ");
  Serial.println(client.pointToLineProtocol(sensor));
}

void SendMetricsDHT11()
{
  Serial.println(client.pointToLineProtocol(sensor2));  
}

void ReadSensors(){
  //DTH_11  
  //float humidity = dht.readHumidity();
  //float temp = dht.readTemperature();
  TempAndHumidity newValues = dht.getTempAndHumidity();

  float humidity = newValues.humidity;
  float temp = newValues.temperature;
  
  if (dht.getStatus() != 0) {
    Serial.println("DHT11 error status: " + String(dht.getStatusString()));
  }
  else if (isnan(humidity) || isnan(temp)) {
    Serial.println(F("Failed to read from DHT sensor!"));
  }
  else 
  {
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(temp, humidity);
    float dewPoint = dht.computeDewPoint(temp, humidity);
    float cr = dht.getComfortRatio(cf, temp, humidity);

    String comfortStatus;
    switch(cf) {
      case Comfort_OK:
        comfortStatus = "Comfort_OK";
        break;
      case Comfort_TooHot:
        comfortStatus = "Comfort_TooHot";
        break;
      case Comfort_TooCold:
        comfortStatus = "Comfort_TooCold";
        break;
      case Comfort_TooDry:
        comfortStatus = "Comfort_TooDry";
        break;
      case Comfort_TooHumid:
        comfortStatus = "Comfort_TooHumid";
        break;
      case Comfort_HotAndHumid:
        comfortStatus = "Comfort_HotAndHumid";
        break;
      case Comfort_HotAndDry:
        comfortStatus = "Comfort_HotAndDry";
        break;
      case Comfort_ColdAndHumid:
        comfortStatus = "Comfort_ColdAndHumid";
        break;
      case Comfort_ColdAndDry:
        comfortStatus = "Comfort_ColdAndDry";
        break;
      default:
        comfortStatus = "Unknown:";
        break;
    };

    float ah = dht.computeAbsoluteHumidity(temp, humidity);
    byte cp = dht.computePerception(temp, humidity);

    String perceptionStatus;
    switch(cp) {
      case 0:
        perceptionStatus = "Dry";
        break;
      case 1:
        perceptionStatus = "Very comfortable";
        break;
      case 2:
        perceptionStatus = "Comfortable";
        break;
      case 3:
        perceptionStatus = "Ok";
        break;
      case 4:
        perceptionStatus = "Uncomfortable";
        break;
      case 5:
        perceptionStatus = "Quite uncomfortable";
        break;
      case 6:
        perceptionStatus = "Very uncomfortable";
        break;
      case 7:
        perceptionStatus = "Severe uncomfortable";
        break;  
      default:
        perceptionStatus = "Unknown:";
        break;
    };
    
    // Store measured value into point
    sensor2.clearFields();
    sensor2.addField("humidity", humidity);
    sensor2.addField("temperature", temp);
    sensor2.addField("heat_index", hic);    
    
    sensor2.addField("dew_point", dewPoint);    
    sensor2.addField("comfort_ratio", cr);    
    sensor2.addField("comfort_status", comfortStatus);   

    sensor2.addField("humidity_absolute", ah);   
    sensor2.addField("perception_index", cp);   
    sensor2.addField("perception_status", perceptionStatus);      
    sensor2.addField("comfort_status", comfortStatus);   
      
    // Print what are we exactly writing
    Serial.print("Writing_DHT: ");  
    Serial.println();
    Serial.print(F("Temp: ")); Serial.print(temp); Serial.print(F("[C]"));Serial.print(", ");Serial.print(humidity); Serial.print(F("[%]("));Serial.print(ah);Serial.print(")");    
    str_sensor2 = String("{'temperatura':"+String(temp)+",'humedad':"+String(humidity) + ",'comfort':'"+comfortStatus+"','percepcion':'"+perceptionStatus+"'}");
    Serial.println();        
    Serial.print(F("Confort: "));Serial.print(comfortStatus);Serial.print(", ");Serial.print(F("Percepcion: "));Serial.print(perceptionStatus);          
    Serial.println();
  }   
}

void blink()
{ 
  digitalWrite(LED_PINT, HIGH);
  delay(100); 
  digitalWrite(LED_PINT, LOW); 
  delay(100); 
}