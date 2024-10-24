#include <WiFi.h>
#include <FirebaseESP32.h>
#include <PZEM004Tv30.h>

// Replace with your network credentials
#define WIFI_SSID "your-SSID"
#define WIFI_PASSWORD "your-password"

// Firebase project credentials
#define FIREBASE_HOST "your-firebase-database-url"
#define FIREBASE_AUTH "your-database-secret"

// Define pins
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define RELAY1_PIN 12
#define RELAY2_PIN 13
#define RELAY3_PIN 18

// Create a HardwareSerial object for PZEM
HardwareSerial mySerial(2);

// Initialize PZEM with HardwareSerial object
PZEM004Tv30 pzem(&mySerial, PZEM_RX_PIN, PZEM_TX_PIN);

// Create a FirebaseData object
FirebaseData firebaseData;

// Firebase authentication and configuration
FirebaseAuth auth;
FirebaseConfig config;

// Store previous energy value for calculation
float previousEnergy = 0.0;
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 5000; // Update every 5 seconds

// Create a flag to track load state
bool loadsTurnedOff = false;

void setup() {
  Serial.begin(115200);

  // Start hardware serial for PZEM
  mySerial.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");

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

  // Fetch the initial state from Firebase
  getBulbState("bulb1", RELAY1_PIN);
  getBulbState("bulb2", RELAY2_PIN);
  getBulbState("bulb3", RELAY3_PIN);
}

void loop() {
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= updateInterval) {
    // Fetch and send real-time data to Firebase
    sendRealTimeData();
    lastUpdateTime = currentTime;
  }

  // Listen for state changes in Firebase
  listenToFirebase();
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
  float voltage = pzem.voltage();
  float current = pzem.current();

  // Ensure voltage and current are valid, but allow zero readings
  if (isnan(voltage) || isnan(current)) {
    Serial.println("Invalid voltage or current readings.");
    return; // Exit only if readings are NaN
  }

  float power = voltage * current; // Calculate power in watts
  float frequency = pzem.frequency();
  float powerFactor = pzem.pf();

  // Calculate energy consumed based on power
  float energyConsumed = (power * (updateInterval / 3600000.0)); // Convert milliseconds to hours

  Serial.print("Calculated Energy Consumed: ");
  Serial.println(energyConsumed);

  // Proceed only if energy consumed is a valid number
  if (isnan(energyConsumed)) {
    Serial.println("Energy consumed calculation resulted in NaN");
    return; // Exit the function if energyConsumed is NaN
  }

  // Fetch the current balance from Firebase
  if (Firebase.getFloat(firebaseData, "/balance")) {
    float currentBalance = firebaseData.floatData();
    float costPerUnit = 9.0; // Cost per unit (kWh)

    // Fetch the current units
    float currentUnits = 0;
    if (Firebase.getFloat(firebaseData, "/units")) {
      currentUnits = firebaseData.floatData();
    }

    // Check if both currentBalance and currentUnits are fetched correctly
    if (firebaseData.errorReason() != "") {
      Serial.println("Error fetching balance or units: " + firebaseData.errorReason());
      return; // Exit if there is an error in fetching data
    }

    // Only deduct if balance and units are above 0
    if (currentBalance > 0 && currentUnits > 0) {
      currentBalance -= energyConsumed * costPerUnit;
      currentUnits -= energyConsumed;

      // Make sure units don't go below zero
      if (currentUnits < 0) {
        currentUnits = 0;
      }

      // Update units in Firebase
      if (Firebase.setFloat(firebaseData, "/units", currentUnits)) {
        Serial.print("Updated Units: ");
        Serial.println(currentUnits);
      } else {
        Serial.println("Error updating units: " + firebaseData.errorReason());
      }

      // Reset loadsTurnedOff flag when balance is recharged and loads are on
      if (loadsTurnedOff && currentBalance > 0) {
        loadsTurnedOff = false; // Allow loads to turn off again if balance reaches zero later
        Serial.println("Loads turned back on, resetting loadsTurnedOff flag.");
      }

      // If balance or units drop to zero or below, turn off all loads
      if (currentBalance <= 0 || currentUnits <= 0) {
        currentBalance = 0;
        currentUnits = 0;

        // Ensure all loads are turned off
        if (!loadsTurnedOff) {
          turnOffLoads();  // Turn off all relays and update Firebase states
          loadsTurnedOff = true;
          Serial.println("Balance is zero or negative. Loads turned off and bulb states updated in Firebase.");
        }
      }

      // Update the balance in Firebase
      if (Firebase.setFloat(firebaseData, "/balance", currentBalance)) {
        Serial.print("Updated Balance: ");
        Serial.println(currentBalance);
      } else {
        Serial.println("Error updating balance: " + firebaseData.errorReason());
      }

    } else if ((currentBalance <= 0 || currentUnits <= 0) && !loadsTurnedOff) {
      // If balance is already zero and loads are not turned off, turn them off
      turnOffLoads();  // Turn off all relays and update Firebase states
      loadsTurnedOff = true;
      Serial.println("Balance is zero or negative. Loads turned off and bulb states updated in Firebase.");
    }
  } else {
    Serial.println("Error fetching balance: " + firebaseData.errorReason());
  }

  // Send real-time data to Firebase
  Firebase.setFloat(firebaseData, "/parameters/voltage", voltage);
  Firebase.setFloat(firebaseData, "/parameters/current", current);
  Firebase.setFloat(firebaseData, "/parameters/power", power);
  Firebase.setFloat(firebaseData, "/parameters/energy", energyConsumed); // Send calculated energy
  Firebase.setFloat(firebaseData, "/parameters/frequency", frequency);
  Firebase.setFloat(firebaseData, "/parameters/powerFactor", powerFactor);

  if (firebaseData.errorReason() != "") {
    Serial.println("Error updating real-time data: " + firebaseData.errorReason());
  } else {
    Serial.println("Data sent to Firebase");
  }
}

void turnOffLoads() {
  // Turn off all relays
  digitalWrite(RELAY1_PIN, HIGH);  // Turn off relay 1
  digitalWrite(RELAY2_PIN, HIGH);  // Turn off relay 2
  digitalWrite(RELAY3_PIN, HIGH);  // Turn off relay 3

  // Update Firebase to set all bulbs to false
  if (Firebase.setBool(firebaseData, "/bulbs/bulb1", false)) {
    Serial.println("Bulb1 state set to OFF in Firebase.");
  } else {
    Serial.println("Error setting Bulb1 state: " + firebaseData.errorReason());
  }

  if (Firebase.setBool(firebaseData, "/bulbs/bulb2", false)) {
    Serial.println("Bulb2 state set to OFF in Firebase.");
  } else {
    Serial.println("Error setting Bulb2 state: " + firebaseData.errorReason());
  }

  if (Firebase.setBool(firebaseData, "/bulbs/bulb3", false)) {
    Serial.println("Bulb3 state set to OFF in Firebase.");
  } else {
    Serial.println("Error setting Bulb3 state: " + firebaseData.errorReason());
  }
}
