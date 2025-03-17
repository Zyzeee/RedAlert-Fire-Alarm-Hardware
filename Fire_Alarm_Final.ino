#include <ESP8266WiFi.h>
#include <FirebaseArduino.h>
#include "DHTesp.h"
#include <WiFiManager.h> // Include the WiFiManager library

#define DHT_PIN 16
#define FireDetect_PIN D3
#define Smoke_Detector_PIN A0
#define BUZZER_PIN D1

#define FIREBASE_HOST "redalert-d4316-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "U7Cd6XsI83c0kLGEZw1R0ZTu7XFWqgcTRPqky99G"

#define OWNER_KEY "RA-FD-ESP8266-V1_0"
#define LOGS_CV_KEY_LENGTH 20

const float flameWeight = 6.0;
const float gasWeight = 2.0;
const float temperatureWeight = 6.0;

DHTesp dht;

WiFiManager wm;

String logsCVKey;
String UserID;

unsigned long previousSendTime = 0;
unsigned long previousCombinedSendTime = 0;
unsigned long previousHourSendTime = 0;
const unsigned long sendInterval = 60000; // 1 minute
const unsigned long combinedSendInterval = 60000; // 1 minute
const unsigned long hourSendInterval = 60000; //3600000; // 1 hour
const unsigned long resetInterval = 360000; // 3 minutes

unsigned long lastResetTime = 0;

// Default thresholds
int smokeThreshold = 20;
float temperatureThreshold = 20.0;

// Custom Parameters
char smokeThresholdStr[6] = "20";
char temperatureThresholdStr[6] = "20.0";

WiFiManagerParameter custom_smoke_threshold("smoke", "Smoke Threshold", smokeThresholdStr, 6);
WiFiManagerParameter custom_temp_threshold("temp", "Temperature Threshold", temperatureThresholdStr, 6);

int currentHour = 0;

void connectToWiFi() {
  Serial.println("WiFi is connecting!");
  
  // Add custom parameters to WiFiManager
  wm.addParameter(&custom_smoke_threshold);
  wm.addParameter(&custom_temp_threshold);

  // Uncomment if needed to reset config
  wm.resetSettings();
  bool res = wm.autoConnect("RedAlert");

  if (res) {
    Serial.println("Connected successfully!");
    
    // Save custom parameters after connection
    smokeThreshold = atoi(custom_smoke_threshold.getValue());
    temperatureThreshold = atof(custom_temp_threshold.getValue());
    Serial.print("Smoke Threshold: ");
    Serial.println(smokeThreshold);
    Serial.print("Temperature Threshold: ");
    Serial.println(temperatureThreshold);
  } else {
    Serial.println("Connection failed.");
  }
}

void initializeFirebase() {
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
}

void initializeSensors() {
  dht.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(FireDetect_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(Smoke_Detector_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT); // Set the buzzer pin as output
  digitalWrite(BUZZER_PIN, LOW); // Initialize the buzzer as off
}

void readSmokeSensor(int &SmokeSensor) {
  SmokeSensor = analogRead(Smoke_Detector_PIN);
  Serial.print("Smoke Detector Value: ");
  Serial.println(SmokeSensor);
}

void checkFireDetection(bool &fireDetected) {
  if (digitalRead(FireDetect_PIN) == 0) {
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("** Fire detected!!! **");
    fireDetected = true;
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("No Fire detected");
    fireDetected = false;
  }
}

void getUserID() {
  String path = String("Owner/") + OWNER_KEY + "/userId";
  Serial.print("Getting UserID from path: ");
  Serial.println(path);
  UserID = Firebase.getString(path);
  if (Firebase.failed()) {
    Serial.print("Failed to get UserID from Firebase: ");
    Serial.println(Firebase.error());
  } else {
    Serial.print("UserID retrieved: ");
    Serial.println(UserID);
  }
}

void sendLogsToFirebase(float temperature, int SmokeSensor, bool fireDetected, int minuteCount) {
  String path = String("Logs/") + String(minuteCount) + "/";
  Firebase.setFloat(path + "Temperature", temperature);
  Firebase.setInt(path + "Smoke", SmokeSensor);
  Firebase.setBool(path + "Fire", fireDetected);
  Firebase.setString(path + "UserID", UserID);
  if (Firebase.failed()) {
    Serial.print("Failed to send data to Firebase: ");
    Serial.println(Firebase.error());
  }
}

void sendDataToFirebase(float temperature, int SmokeSensor, bool fireDetected) {
  String path = String("Owner/") + OWNER_KEY + "/";
  Firebase.setFloat(path + "Temperature", temperature);
  Firebase.setInt(path + "Smoke", SmokeSensor);
  Firebase.setBool(path + "Fire", fireDetected);
  if (Firebase.failed()) {
    Serial.print("Failed to send data to Firebase: ");
    Serial.println(Firebase.error());
  }
}

void sendCombinedValueToFirebase(float combinedValue, int minuteCount) {
  String path = String("LogsCV/") + String(minuteCount) + "/";
  Firebase.setFloat(path + "CombinedValue", combinedValue);
  Firebase.setString(path + "UserID", UserID);
  if (Firebase.failed()) {
    Serial.print("Failed to send combined value to Firebase: ");
    Serial.println(Firebase.error());
  }
}

void sendHourCombinedValueToFirebase(float combinedValue, int hourCount) {
  String path = String("HourCombinedCV/") + String(hourCount) + "/";
  Firebase.setFloat(path + "CombinedValue", combinedValue);
  Firebase.setString(path + "UserID", UserID);
  if (Firebase.failed()) {
    Serial.print("Failed to send hourly combined value to Firebase: ");
    Serial.println(Firebase.error());
  }
}

void resetFirebaseLogs() {
  Firebase.remove("Logs");
  Firebase.remove("LogsCV");
  if (Firebase.failed()) {
    Serial.print("Failed to reset Firebase logs: ");
    Serial.println(Firebase.error());
  } else {
    Serial.println("Firebase logs reset successfully.");
  }
}

void resetHourCombinedValues() {
  Firebase.remove("HourCombinedCV");
  if (Firebase.failed()) {
    Serial.print("Failed to reset hourly combined values: ");
    Serial.println(Firebase.error());
  } else {
    Serial.println("Hourly combined values reset successfully.");
  }
}

void defaultFirebaseValues() {
  String path = String("Owner/") + OWNER_KEY + "/";
  Firebase.setFloat(path + "Temperature", 0.0);
  Firebase.setInt(path + "Smoke", 0);
  Firebase.setBool(path + "Fire", false);
  Firebase.setBool(path + "arrived", true);
  if (Firebase.failed()) {
    Serial.print("Failed to reset Firebase values: ");
    Serial.println(Firebase.error());
  }
}

void sendArrivedFirebase() {
  String path = String("Owner/") + OWNER_KEY + "/";
  Firebase.setBool(path + "arrived", false);
  if (Firebase.failed()) {
    Serial.print("Failed to reset Firebase values: ");
    Serial.println(Firebase.error());
  }
}

void setup() {
  WiFi.mode(WIFI_STA);

  Serial.begin(115200);
  connectToWiFi();
  initializeFirebase();
  initializeSensors();
  getUserID();
  defaultFirebaseValues();
  lastResetTime = millis();
}

int SmokeSensor = 0;
bool fireDetected = false;

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Trying to reconnect...");
    connectToWiFi();
  } else {
    unsigned long currentTime = millis();
    Serial.println("Seconds");
    Serial.println((currentTime - previousSendTime) / 1000);

    readSmokeSensor(SmokeSensor);
    checkFireDetection(fireDetected);

    float temperature = dht.getTemperature();
    Serial.print("Temperature: ");
    Serial.println(temperature);

    float combinedValue = (fireDetected * flameWeight) + (SmokeSensor * gasWeight) + (temperature * temperatureWeight);
    sendDataToFirebase(temperature, SmokeSensor, fireDetected);

    if ((fireDetected && SmokeSensor >= smokeThreshold && temperature >= temperatureThreshold) ||
        (fireDetected || SmokeSensor >= smokeThreshold && temperature >= temperatureThreshold)) {
      sendArrivedFirebase();
      digitalWrite(BUZZER_PIN, HIGH); // Turn on the buzzer
    } else {
      digitalWrite(BUZZER_PIN, LOW); // Turn off the buzzer
    }

    if (currentTime - previousSendTime >= sendInterval) {
      previousSendTime = currentTime;
      int currentMinute = (currentTime / 60000) % 6; // Calculate the current minute count
      if (currentMinute == 0) {
        currentMinute = 6; // Adjust the minute count to start from 6 if the result is 0
      }
      sendLogsToFirebase(temperature, SmokeSensor, fireDetected, currentMinute);
      if (currentMinute == 6) {
        resetFirebaseLogs(); // Reset logs after reaching 6
      }
      if (Firebase.failed()) {
        Serial.print("Failed to send logs data to Firebase: ");
        Serial.println(Firebase.error());
      }
    }
    if (currentTime - previousCombinedSendTime >= combinedSendInterval) {
      previousCombinedSendTime = currentTime;
      int currentMinute = (currentTime / 60000) % 6; // Calculate the current minute count
      if (currentMinute == 0) {
        currentMinute = 6; // Adjust the minute count to start from 6 if the result is 0
      }
      sendCombinedValueToFirebase(combinedValue, currentMinute);
      if (currentMinute == 6) {
        resetFirebaseLogs(); // Reset logs after reaching 6
      }
      if (Firebase.failed()) {
        Serial.print("Failed to send combined value data to Firebase: ");
        Serial.println(Firebase.error());
      }
    }

    if (currentTime - previousHourSendTime >= hourSendInterval) {
      previousHourSendTime = currentTime;
      currentHour++;
      if (currentHour > 24) {
        currentHour = 1; // Reset hour count after 24
        resetHourCombinedValues();
      }
      sendHourCombinedValueToFirebase(combinedValue, currentHour);
      if (Firebase.failed()) {
        Serial.print("Failed to send hourly combined value data to Firebase: ");
        Serial.println(Firebase.error());
      }
    }

    delay(1000);
  }
}
