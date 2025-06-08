#include <WiFi.h>
#include <FirebaseESP32.h>
#include <PZEM004Tv30.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define WIFI_SSID "SouvikF23"
#define WIFI_PASSWORD "12345678"
#define FIREBASE_HOST "prepaid-smart-energy-meter-ee-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "your-database-secret"

#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define RELAY_PIN 13
#define BUTTON_PIN 4
#define RELAY_BUTTON_PIN 5
#define LED_ONLINE 18
#define LED_OFFLINE 19
#define LED_ALERT 25          // Red LED for balance/unit alert
#define FLAME_SENSOR_PIN 26   // Flame sensor input
#define BUZZER_PIN 27         // Buzzer pin

#define EEPROM_BALANCE_ADDR 0
#define EEPROM_UNITS_ADDR sizeof(float)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

HardwareSerial mySerial(2);
PZEM004Tv30 pzem(&mySerial, PZEM_RX_PIN, PZEM_TX_PIN);
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

float balance = 0.0, units = 0.0;
float deltaBalance = 0.0, deltaUnits = 0.0;
bool wifiConnected = false, firebaseAvailable = false;
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 5000;
int displayPage = 0;
unsigned long lastButtonPress = 0;
unsigned long lastRelayButtonPress = 0;
const unsigned long debounceDelay = 300;
unsigned long lastWiFiRetry = 0;
const unsigned long wifiRetryInterval = 10000;
unsigned long lastBlinkTime = 0;
bool ledState = false;

const float LOW_THRESHOLD = 5.0;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  mySerial.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_ONLINE, OUTPUT);
  pinMode(LED_OFFLINE, OUTPUT);
  pinMode(LED_ALERT, OUTPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay(); display.display();

  connectToWiFi();
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  getBulbState();

  if (wifiConnected && Firebase.getFloat(firebaseData, "/balance")) {
    firebaseAvailable = true;
    balance = firebaseData.floatData();
    Firebase.getFloat(firebaseData, "/units");
    units = firebaseData.floatData();
    EEPROM.put(EEPROM_BALANCE_ADDR, balance);
    EEPROM.put(EEPROM_UNITS_ADDR, units);
    EEPROM.commit();
  } else {
    firebaseAvailable = false;
    EEPROM.get(EEPROM_BALANCE_ADDR, balance);
    EEPROM.get(EEPROM_UNITS_ADDR, units);
    Serial.println("Running in OFFLINE mode.");
  }

  updateOLED();
}

void loop() {
  checkWiFiConnection();
  digitalWrite(LED_ONLINE, firebaseAvailable ? HIGH : LOW);
  digitalWrite(LED_OFFLINE, firebaseAvailable ? LOW : HIGH);

  handleAlertLED();
  handleFlameSensor();

  bool wasOffline = !firebaseAvailable;
  firebaseAvailable = (WiFi.status() == WL_CONNECTED && Firebase.getFloat(firebaseData, "/balance"));

  if (wasOffline && firebaseAvailable) {
    Serial.println("✅ Firebase reconnected. Syncing deltas.");
    float firebaseBal = firebaseData.floatData();
    Firebase.getFloat(firebaseData, "/units");
    float firebaseUnits = firebaseData.floatData();
    Firebase.setFloat(firebaseData, "/balance", firebaseBal - deltaBalance);
    Firebase.setFloat(firebaseData, "/units", firebaseUnits - deltaUnits);
    delay(500);
    Firebase.getFloat(firebaseData, "/balance"); balance = firebaseData.floatData();
    Firebase.getFloat(firebaseData, "/units"); units = firebaseData.floatData();
    EEPROM.put(EEPROM_BALANCE_ADDR, balance);
    EEPROM.put(EEPROM_UNITS_ADDR, units);
    EEPROM.commit();
    deltaBalance = 0; deltaUnits = 0;
    updateOLED();
  }

  if (firebaseAvailable && millis() - lastUpdateTime >= updateInterval) {
    Firebase.getFloat(firebaseData, "/balance"); balance = firebaseData.floatData();
    Firebase.getFloat(firebaseData, "/units"); units = firebaseData.floatData();
  }

  if (millis() - lastUpdateTime >= updateInterval) {
    float power = pzem.power();
    float energy = power * updateInterval / 3600000.0;
    float cost = energy * 9.0;

    deltaBalance += cost;
    deltaUnits += energy;
    balance -= cost;
    units -= energy;

    if (balance <= 0 || units <= 0) {
      balance = 0; units = 0;
      turnOffLoad();
    }

    EEPROM.put(EEPROM_BALANCE_ADDR, balance);
    EEPROM.put(EEPROM_UNITS_ADDR, units);
    EEPROM.commit();

    if (firebaseAvailable) {
      Firebase.setFloat(firebaseData, "/parameters/power", power);
      Firebase.setFloat(firebaseData, "/parameters/energy", energy);
      Firebase.setFloat(firebaseData, "/parameters/voltage", pzem.voltage());
      Firebase.setFloat(firebaseData, "/parameters/current", pzem.current());
      Firebase.setFloat(firebaseData, "/parameters/frequency", pzem.frequency());
      Firebase.setFloat(firebaseData, "/parameters/powerFactor", pzem.pf());

      float fbBal = 0.0, fbUnits = 0.0;
      Firebase.getFloat(firebaseData, "/balance"); fbBal = firebaseData.floatData();
      Firebase.getFloat(firebaseData, "/units"); fbUnits = firebaseData.floatData();
      Firebase.setFloat(firebaseData, "/balance", fbBal - deltaBalance);
      Firebase.setFloat(firebaseData, "/units", fbUnits - deltaUnits);
      deltaBalance = 0.0; deltaUnits = 0.0;
    }

    lastUpdateTime = millis();
    updateOLED();
  }

  if (firebaseAvailable) listenToFirebase();

  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > debounceDelay) {
    displayPage = (displayPage + 1) % 5;
    updateOLED();
    lastButtonPress = millis();
  }

  if (digitalRead(RELAY_BUTTON_PIN) == LOW && millis() - lastRelayButtonPress > debounceDelay) {
    bool currentState = digitalRead(RELAY_PIN);
    digitalWrite(RELAY_PIN, !currentState);
    Firebase.setBool(firebaseData, "/bulbs/bulb1", currentState == LOW ? false : true);
    lastRelayButtonPress = millis();
  }
}

void handleAlertLED() {
  if (balance <= 0 || units <= 0) {
    digitalWrite(LED_ALERT, HIGH);
  } else if (balance < LOW_THRESHOLD || units < LOW_THRESHOLD) {
    if (millis() - lastBlinkTime > 500) {
      ledState = !ledState;
      digitalWrite(LED_ALERT, ledState);
      lastBlinkTime = millis();
    }
  } else {
    digitalWrite(LED_ALERT, LOW);
  }
}

void handleFlameSensor() {
  if (digitalRead(FLAME_SENSOR_PIN) == LOW) {
    Serial.println("🔥 Flame detected! Tripping relay and buzzing...");
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(5000);
    digitalWrite(BUZZER_PIN, LOW);
    Firebase.setBool(firebaseData, "/bulbs/bulb1", false);
  }
}

void updateOLED() {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  switch (displayPage) {
    case 0:
      display.setCursor(0,0); display.println("Mode:");
      display.println(firebaseAvailable ? "ONLINE" : "OFFLINE"); break;
    case 1:
      display.setCursor(0,0); display.println("Balance & Units:");
      display.print("Bal: "); display.println(balance);
      display.print("Unit: "); display.println(units); break;
    case 2:
      display.setCursor(0,0); display.println("Voltage & Current:");
      display.print("V: "); display.println(pzem.voltage());
      display.print("I: "); display.println(pzem.current()); break;
    case 3:
      display.setCursor(0,0); display.println("Power & Energy:");
      display.print("P: "); display.println(pzem.power());
      display.print("E: "); display.println(pzem.energy()); break;
    case 4:
      display.setCursor(0,0); display.println("Freq & PF:");
      display.print("Hz: "); display.println(pzem.frequency());
      display.print("PF: "); display.println(pzem.pf()); break;
  }
  display.display();
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
    firebaseAvailable = false;
    if (millis() - lastWiFiRetry > wifiRetryInterval) {
      Serial.println("Wi-Fi lost. Reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      lastWiFiRetry = millis();
    }
  } else {
    if (!wifiConnected) {
      Serial.println("Wi-Fi Reconnected");
    }
    wifiConnected = true;
  }
}

void getBulbState() {
  if (Firebase.getBool(firebaseData, "/bulbs/bulb1")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY_PIN, state ? LOW : HIGH);
    Serial.println("Bulb initial state: " + String(state ? "ON" : "OFF"));
  }
}

void listenToFirebase() {
  if (Firebase.getBool(firebaseData, "/bulbs/bulb1")) {
    bool state = firebaseData.boolData();
    digitalWrite(RELAY_PIN, state ? LOW : HIGH);
    Serial.println("Bulb state: " + String(state ? "ON" : "OFF"));
  }
}

void turnOffLoad() {
  digitalWrite(RELAY_PIN, HIGH);
  Firebase.setBool(firebaseData, "/bulbs/bulb1", false);
}
