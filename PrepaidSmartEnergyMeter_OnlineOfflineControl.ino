#include <WiFi.h>
#include <FirebaseESP32.h>
#include <PZEM004Tv30.h>
#include <EEPROM.h>  // Library for storing data in ESP32 memory

// Replace with your network credentials
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Firebase project credentials
#define FIREBASE_HOST "prepaid-smart-energy-meter-ee-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "your-database-secret"

// Define pins
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define RELAY1_PIN 12
#define RELAY2_PIN 13
#define RELAY3_PIN 18

// EEPROM addresses for balance and units
#define EEPROM_BALANCE_ADDR 0
#define EEPROM_UNITS_ADDR sizeof(float)

// Create a HardwareSerial object for PZEM
HardwareSerial mySerial(2);

// Initialize PZEM with HardwareSerial object
PZEM004Tv30 pzem(&mySerial, PZEM_RX_PIN, PZEM_TX_PIN);

// Create a FirebaseData object
FirebaseData firebaseData;

// Firebase authentication and configuration
FirebaseAuth auth;
FirebaseConfig config;

// Local variables for balance and units
float currentBalance = 0.0;
float currentUnits = 0.0;
bool loadsTurnedOff = false;
bool wifiConnected = false;
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 5000; // Update every 5 seconds

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);  // Initialize EEPROM with a 512-byte size

  // Start hardware serial for PZEM
  mySerial.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);

  // Connect to Wi-Fi
  connectToWiFi();

  // Configure Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Initialize relays as output and set them to OFF initially
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, HIGH);  // HIGH = OFF
  digitalWrite(RELAY2_PIN, HIGH);  // HIGH = OFF
  digitalWrite(RELAY3_PIN, HIGH);  // HIGH = OFF

  // Fetch initial state of bulbs from Firebase
  getBulbState("bulb1", RELAY1_PIN);
  getBulbState("bulb2", RELAY2_PIN);
  getBulbState("bulb3", RELAY3_PIN);

  // Load balance and units from Firebase if Wi-Fi is connected, otherwise load from EEPROM
  if (wifiConnected) {
    fetchInitialBalanceAndUnits();
  } else {
    loadBalanceAndUnitsFromEEPROM();
  }
}

void loop() {
  checkWiFiConnection();

  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= updateInterval) {
    if (wifiConnected) {
      sendRealTimeData();
    } else {
      deductBalanceAndUnitsOffline();
    }
    lastUpdateTime = currentTime;
  }

  // Listen for state changes in Firebase
  if (wifiConnected) {
    listenToFirebase();
  }
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");
  wifiConnected = true;
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    Serial.println("Wi-Fi disconnected.");
  } else if (!wifiConnected) {  // Reconnect event
    wifiConnected = true;
    Serial.println("Wi-Fi reconnected. Syncing data with Firebase.");
    syncLocalDataToFirebase();
  }
}

void fetchInitialBalanceAndUnits() {
  if (Firebase.getFloat(firebaseData, "/balance")) {
    currentBalance = firebaseData.floatData();
    EEPROM.put(EEPROM_BALANCE_ADDR, currentBalance);
    EEPROM.commit();
  }
  if (Firebase.getFloat(firebaseData, "/units")) {
    currentUnits = firebaseData.floatData();
    EEPROM.put(EEPROM_UNITS_ADDR, currentUnits);
    EEPROM.commit();
  }
}

void loadBalanceAndUnitsFromEEPROM() {
  EEPROM.get(EEPROM_BALANCE_ADDR, currentBalance);
  EEPROM.get(EEPROM_UNITS_ADDR, currentUnits);
}

void deductBalanceAndUnitsOffline() {
  float voltage = pzem.voltage();
  float current = pzem.current();
  float power = voltage * current;
  float energyConsumed = (power * (updateInterval / 3600000.0));

  if (!isnan(energyConsumed)) {
    float costPerUnit = 9.0; // Cost per unit (kWh)
    currentBalance -= energyConsumed * costPerUnit;
    currentUnits -= energyConsumed;

    if (currentBalance <= 0 || currentUnits <= 0) {
      currentBalance = 0;
      currentUnits = 0;
      turnOffLoads();
    }

    EEPROM.put(EEPROM_BALANCE_ADDR, currentBalance);
    EEPROM.put(EEPROM_UNITS_ADDR, currentUnits);
    EEPROM.commit();
  }
}

void syncLocalDataToFirebase() {
  if (Firebase.setFloat(firebaseData, "/balance", currentBalance)) {
    Serial.println("Balance synced to Firebase.");
  }
  if (Firebase.setFloat(firebaseData, "/units", currentUnits)) {
    Serial.println("Units synced to Firebase.");
  }
}

void getBulbState(String bulbId, int relayPin) {
  if (Firebase.getBool(firebaseData, "/bulbs/" + bulbId)) {
    bool bulbState = firebaseData.boolData();
    digitalWrite(relayPin, bulbState ? LOW : HIGH);  // LOW activates the relay
    Serial.println(bulbId + " initial state: " + String(bulbState ? "ON" : "OFF"));
  } else {
    Serial.println("Error getting " + bulbId + " state: " + firebaseData.errorReason());
  }
}

void listenToFirebase() {
  if (Firebase.getBool(firebaseData, "/bulbs/bulb1")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY1_PIN, state ? LOW : HIGH);
    Serial.println("Bulb1 state: " + String(state ? "ON" : "OFF"));
  }

  if (Firebase.getBool(firebaseData, "/bulbs/bulb2")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY2_PIN, state ? LOW : HIGH);
    Serial.println("Bulb2 state: " + String(state ? "ON" : "OFF"));
  }

  if (Firebase.getBool(firebaseData, "/bulbs/bulb3")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY3_PIN, state ? LOW : HIGH);
    Serial.println("Bulb3 state: " + String(state ? "ON" : "OFF"));
  }
}

void sendRealTimeData() {
  // Check Wi-Fi connectivity before proceeding
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi is disconnected. Skipping data update.");
    return;
  }

  float voltage = pzem.voltage();
  float current = pzem.current();

  // Ensure voltage and current readings are valid
  if (isnan(voltage) || isnan(current)) {
    Serial.println("Invalid voltage or current readings.");
    return;
  }

  float power = voltage * current;
  float frequency = pzem.frequency();
  float powerFactor = pzem.pf();
  float energyConsumed = power * (updateInterval / 3600000.0); // Convert ms to hours

  // Retrieve balance and units from Firebase if possible
  if (Firebase.getFloat(firebaseData, "/balance")) {
    currentBalance = firebaseData.floatData();
  } else {
    Serial.println("Error fetching balance: " + firebaseData.errorReason());
    return;
  }
  
  if (Firebase.getFloat(firebaseData, "/units")) {
    currentUnits = firebaseData.floatData();
  } else {
    Serial.println("Error fetching units: " + firebaseData.errorReason());
    return;
  }

  // Check and update balance and units only if they are above zero
  if (currentBalance > 0 && currentUnits > 0) {
    float costPerUnit = 9.0; // Cost per unit (kWh)
    currentBalance -= energyConsumed * costPerUnit;
    currentUnits -= energyConsumed;

    // Ensure neither balance nor units drop below zero
    if (currentBalance < 0) currentBalance = 0;
    if (currentUnits < 0) currentUnits = 0;

    // Update balance in Firebase if modified
    if (Firebase.setFloat(firebaseData, "/balance", currentBalance)) {
      Serial.print("Updated Balance: ");
      Serial.println(currentBalance);
    } else {
      Serial.println("Error updating balance: " + firebaseData.errorReason());
    }

    // Update units in Firebase if modified
    if (Firebase.setFloat(firebaseData, "/units", currentUnits)) {
      Serial.print("Updated Units: ");
      Serial.println(currentUnits);
    } else {
      Serial.println("Error updating units: " + firebaseData.errorReason());
    }
  }

  // Turn off loads if balance or units reach zero
  if (currentBalance <= 0 || currentUnits <= 0) {
    turnOffLoads();
    Serial.println("Balance or units are zero. Loads turned off.");
  }

  // Send real-time energy data to Firebase
  if (!Firebase.setFloat(firebaseData, "/parameters/voltage", voltage))
    Serial.println("Error updating voltage: " + firebaseData.errorReason());
  if (!Firebase.setFloat(firebaseData, "/parameters/current", current))
    Serial.println("Error updating current: " + firebaseData.errorReason());
  if (!Firebase.setFloat(firebaseData, "/parameters/power", power))
    Serial.println("Error updating power: " + firebaseData.errorReason());
  if (!Firebase.setFloat(firebaseData, "/parameters/energy", energyConsumed))
    Serial.println("Error updating energy: " + firebaseData.errorReason());
  if (!Firebase.setFloat(firebaseData, "/parameters/frequency", frequency))
    Serial.println("Error updating frequency: " + firebaseData.errorReason());
  if (!Firebase.setFloat(firebaseData, "/parameters/powerFactor", powerFactor))
    Serial.println("Error updating power factor: " + firebaseData.errorReason());

  Serial.println("Real-time data sent to Firebase.");
}

void turnOffLoads() {
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(RELAY3_PIN, HIGH);

  if (Firebase.setBool(firebaseData, "/bulbs/bulb1", false)) Serial.println("Bulb1 OFF.");
  if (Firebase.setBool(firebaseData, "/bulbs/bulb2", false)) Serial.println("Bulb2 OFF.");
  if (Firebase.setBool(firebaseData, "/bulbs/bulb3", false)) Serial.println("Bulb3 OFF.");
}
