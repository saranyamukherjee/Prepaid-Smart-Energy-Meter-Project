#include <WiFi.h>
#include <FirebaseESP32.h>

// Replace with your network credentials
#define WIFI_SSID "your-SSID-here"          // Enter your WiFi name
#define WIFI_PASSWORD "your-password-here"  // Enter your WiFi password

// Firebase project credentials
#define FIREBASE_HOST "your-firebase-host-url-here"   // Enter your Firebase host URL
#define FIREBASE_AUTH "your-firebase-auth-token-here" // Enter your Firebase authentication token

// Pin configuration for the relay module
#define RELAY1_PIN 12  // GPIO12
#define RELAY2_PIN 13  // GPIO13
#define RELAY3_PIN 18  // GPIO18

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

String path = "/bulbs/";

void setup() {
  Serial.begin(115200);
  
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
  // Listen for state changes in Firebase
  listenToFirebase();
}

// Function to get the initial bulb state from Firebase
void getBulbState(String bulbId, int relayPin) {
  if (Firebase.getBool(firebaseData, path + bulbId)) {
    bool bulbState = firebaseData.boolData();
    digitalWrite(relayPin, bulbState ? LOW : HIGH);  // LOW activates the relay
    Serial.println(bulbId + " initial state: " + String(bulbState ? "ON" : "OFF"));
  }
}

// Function to listen to Firebase for changes
void listenToFirebase() {
  if (Firebase.getBool(firebaseData, path + "bulb1")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY1_PIN, state ? LOW : HIGH);
    Serial.println("Bulb1 state: " + String(state ? "ON" : "OFF"));
  } else {
    Serial.println("Error reading bulb1 state: " + firebaseData.errorReason());
  }

  if (Firebase.getBool(firebaseData, path + "bulb2")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY2_PIN, state ? LOW : HIGH);
    Serial.println("Bulb2 state: " + String(state ? "ON" : "OFF"));
  } else {
    Serial.println("Error reading bulb2 state: " + firebaseData.errorReason());
  }

  if (Firebase.getBool(firebaseData, path + "bulb3")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY3_PIN, state ? LOW : HIGH);
    Serial.println("Bulb3 state: " + String(state ? "ON" : "OFF"));
  } else {
    Serial.println("Error reading bulb3 state: " + firebaseData.errorReason());
  }
}
