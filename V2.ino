/*
  My PV solar power station monitoring and control sketch based on Firebase platform
  credits to: Rui Santos
  
*/

#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include "INA219_WE.h"
#include <Wire.h>
#include "time.h"
#include <Adafruit_AHTX0.h>
// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Insert your network credentials
#define WIFI_SSID -"
#define WIFI_PASSWORD "-"

// Insert Firebase project API Key
#define API_KEY "-"
                

// Insert Authorized Email and Corresponding Password
#define USER_EMAIL "-"
#define USER_PASSWORD "-!"

// Insert RTDB URLefine the RTDB URL
#define DATABASE_URL "-"
                      

//INA219 IC2 address
#define I2C_ADDRESS 0x40





// Define Firebase objects
FirebaseData stream;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson json;

//
String uid;
// Database main path (to be updated in setup with the user UID)
String databasePath;
// Parent Node (to be updated in every loop)
String parentPath;
// Database child nodes
String tempPath = "/temperature";
String humPath = "/humidity";
String voltPath = "/voltage";
String timePath = "/timestamp";


int timestamp;

// Variables to save database paths
String listenerPath = "board1/outputs/digital/";
const char* ntpServer = "pool.ntp.org";
// Variables to save database paths


//DHT11 variables
float h ;
float t ;

// Declare outputs
const int output1 = 12;
const int output2 = 13;
const int output3 = 14;

//INA219 variables
float voltage;
float shuntVoltage_mV = 0.0;
float loadVoltage_V = 0.0;
float busVoltage_V = 0.0;
float current_mA = 0.0;

// Timer variables (send new readings every three minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 15000;
unsigned long previousMillis = 0;
//unsigned long interval = 30000;


Adafruit_AHTX0 aht;
INA219_WE ina219 = INA219_WE(I2C_ADDRESS);


// Initialize WiFi

void initWiFi() {
     
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
  Serial.println();
}



// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

// Callback function that runs on database changes
void streamCallback(FirebaseStream data){
  Serial.printf("stream path, %s\nevent path, %s\ndata type, %s\nevent type, %s\n\n",
                data.streamPath().c_str(),
                data.dataPath().c_str(),
                data.dataType().c_str(),
                data.eventType().c_str());
  printResult(data); //see addons/RTDBHelper.h
  Serial.println();

  // Get the path that triggered the function
  String streamPath = String(data.dataPath());

  // if the data returned is an integer, there was a change on the GPIO state on the following path /{gpio_number}
  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_integer){
    String gpio = streamPath.substring(1);
    int state = data.intData();
    Serial.print("GPIO: ");
    Serial.println(gpio);
    Serial.print("STATE: ");
    Serial.println(state);
    digitalWrite(gpio.toInt(), state);
  }

  /* When it first runs, it is triggered on the root (/) path and returns a JSON with all keys
  and values of that path. So, we can get all values from the database and updated the GPIO states*/
  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_json){
    FirebaseJson json = data.to<FirebaseJson>();

    // To iterate all values in Json object
    size_t count = json.iteratorBegin();
    Serial.println("\n---------");
    for (size_t i = 0; i < count; i++){
        FirebaseJson::IteratorValue value = json.valueAt(i);
        int gpio = value.key.toInt();
        int state = value.value.toInt();
        Serial.print("STATE: ");
        Serial.println(state);
        Serial.print("GPIO:");
        Serial.println(gpio);
        digitalWrite(gpio, state);
        Serial.printf("Name: %s, Value: %s, Type: %s\n", value.key.c_str(), value.value.c_str(), value.type == FirebaseJson::JSON_OBJECT ? "object" : "array");
    }
    Serial.println();
    json.iteratorEnd(); // required for free the used memory in iteration (node data collection)
  }
  
  //This is the size of stream payload received (current and max value)
  //Max payload size is the payload size under the stream path since the stream connected
  //and read once and will not update until stream reconnection takes place.
  //This max value will be zero as no payload received in case of ESP8266 which
  //BearSSL reserved Rx buffer size is less than the actual stream payload.
  Serial.printf("Received stream payload size: %d (Max. %d)\n\n", data.payloadLength(), data.maxPayloadLength());
}



void streamTimeoutCallback(bool timeout){

  if (timeout)
    Serial.println("stream timeout, resuming...\n");
  if (!stream.httpConnected())
    Serial.printf("error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
}



void setup(){
  Serial.begin(115200);
  initWiFi();
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
  configTime(0, 0, ntpServer);
  if (!aht.begin()) {
    Serial.println("Could not find AHT20 sensor!");
    while (1) delay(10);
  }
  Serial.println("AHT20 sensor initialized.");
  Wire.begin(); 
  // Initialize Outputs 
  pinMode(output1, OUTPUT);
  pinMode(output2, OUTPUT);
  pinMode(output3, OUTPUT);
  //update time function
  configTzTime("GMT0BST,M3.5.0/1,M10.5.0", "pool.ntp.org");
  //initialize INA219
  ina219.setBusRange(BRNG_16); 
  ina219.setShuntSizeInOhms(0.35); // Insert your shunt size in ohms  
  delay(10);  
  if (!ina219.init()){    
    Serial.println("FAILED TO FIND");      
    Serial.println("INA219 MODULE");
    while (1){}
  }  
  
  // Assign the api key (required)
  config.api_key = API_KEY;

  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the RTDB URL (required)
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);
  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;


  //Firebase.refreshToken(&config);
  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);

  // Update database path
  databasePath = "/UsersData/" + uid + "/readings";


  // Streaming (whenever data changes on a path)
  // Begin stream on a database path --> board1/outputs/digital
  if (!Firebase.RTDB.beginStream(&stream, listenerPath.c_str()))
    Serial.printf("stream begin error, %s\n\n", stream.errorReason().c_str());

  // Assign a calback function to run when it detects changes on the database
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);

  delay(2000);
}

void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Reconnecting WiFi...");
        WiFi.disconnect();
        WiFi.reconnect();
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
            Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nReconnected to WiFi!");
        } else {
            Serial.println("\nFailed to reconnect.");
        }
    }
}


void loop() {
  // Read INA219 values
  shuntVoltage_mV = ina219.getShuntVoltage_mV();
  busVoltage_V = ina219.getBusVoltage_V();
  current_mA = ina219.getCurrent_mA();
  loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000);
  
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp); // populate temp & humidity

  h = humidity.relative_humidity;
  t = temp.temperature - 5; // optional adjustment

  if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from AHT20 sensor!");
      return;
  }

  unsigned long currentMillis = millis();

  // Reconnect WiFi if needed
  checkWiFi();

  // Keep Firebase connection active
  if (Firebase.ready() && (currentMillis - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)) {
      sendDataPrevMillis = currentMillis;
      
      // Get current timestamp
      timestamp = getTime();
      Serial.print("time: ");
      Serial.println(timestamp);
      
      parentPath = databasePath + "/" + String(timestamp);

      json.set(tempPath.c_str(), String(t));
      json.set(humPath.c_str(), String(h));
      json.set(voltPath.c_str(), String(loadVoltage_V));
      json.set(timePath, String(timestamp));

      if (!Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json)) {
          Serial.printf("Firebase Error: %s\n", fbdo.errorReason().c_str());
          Firebase.reconnectWiFi(true);
          Firebase.begin(&config, &auth);
      } else {
          Serial.println("Set json... OK");
      }
  }

  // Refresh Firebase token if needed
  if (Firebase.isTokenExpired()) {
      Serial.println("Refreshing token...");
      Firebase.refreshToken(&config);
      delay(1000);
  }
}
