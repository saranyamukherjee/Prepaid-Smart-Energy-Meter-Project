#include <WiFi.h>
#include <FirebaseESP32.h>
#include <PZEM004Tv30.h>

// Replace with your network credentials
#define WIFI_SSID "your-SSID-here"
#define WIFI_PASSWORD "your-password-here"

// Firebase project credentials
#define FIREBASE_HOST "your-firebase-host-url-here"
#define FIREBASE_AUTH "your-database-secret"

// Define pins
#define PZEM_RX_PIN 16   // RX2 on ESP32
#define PZEM_TX_PIN 17   // TX2 on ESP32
#define RELAY1_PIN 12    // D12 on ESP32
#define RELAY2_PIN 13    // D13 on ESP32
#define RELAY3_PIN 18    // D18 on ESP32

// Create a HardwareSerial object for PZEM
HardwareSerial mySerial(2);  // Use UART2

// Initialize PZEM with HardwareSerial object
PZEM004Tv30 pzem(&mySerial, PZEM_RX_PIN, PZEM_TX_PIN);

// Create a FirebaseData object
FirebaseData firebaseData;

// Firebase authentication and configuration
FirebaseAuth auth;
FirebaseConfig config;

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
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(RELAY3_PIN, HIGH);

  // Fetch the initial state from Firebase
  getBulbState("bulb1", RELAY1_PIN);
  getBulbState("bulb2", RELAY2_PIN);
  getBulbState("bulb3", RELAY3_PIN);
}

void loop() {
  // Listen for state changes in Firebase
  listenToFirebase();

  // Fetch and send real-time data to Firebase
  sendRealTimeData();
  delay(10000);
}

void getBulbState(String bulbId, int relayPin) {
  if (Firebase.getBool(firebaseData, "/bulbs/" + bulbId)) {
    bool bulbState = firebaseData.boolData();
    digitalWrite(relayPin, bulbState ? LOW : HIGH);
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
  float power = pzem.power();
  float energy = pzem.energy();
  float frequency = pzem.frequency();
  float powerFactor = pzem.pf();

  Serial.print("Voltage: ");
  Serial.print(voltage);
  Serial.println(" V");

  Serial.print("Current: ");
  Serial.print(current);
  Serial.println(" A");

  Serial.print("Power: ");
  Serial.print(power);
  Serial.println(" W");

  Serial.print("Energy: ");
  Serial.print(energy);
  Serial.println(" kWh");

  Serial.print("Frequency: ");
  Serial.print(frequency);
  Serial.println(" Hz");

  Serial.print("Power Factor: ");
  Serial.print(powerFactor);
  Serial.println();

  // Send data to Firebase
  Firebase.setFloat(firebaseData, "/parameters/voltage", voltage);
  Firebase.setFloat(firebaseData, "/parameters/current", current);
  Firebase.setFloat(firebaseData, "/parameters/power", power);
  Firebase.setFloat(firebaseData, "/parameters/energy", energy);
  Firebase.setFloat(firebaseData, "/parameters/frequency", frequency);
  Firebase.setFloat(firebaseData, "/parameters/powerFactor", powerFactor);

  if (firebaseData.errorReason() != "") {
    Serial.println("Error updating real-time data: " + firebaseData.errorReason());
  } else {
    Serial.println("Data sent to Firebase");
  }
}
