#include <Arduino.h>
#include <ArduinoBLE.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>

// ===== BLE =====
BLEService dataService("180A");  // Generic Access service
BLEStringCharacteristic rxCharacteristic("2A19", BLEWrite, 128);  // Rx (write from phone)
BLEStringCharacteristic txCharacteristic("2A1A", BLERead | BLENotify, 128);  // Tx (read from ESP)
const char* BLE_NAME = "ESP32_BLE";

// ===== WIFI CREDENTIALS =====
const char* WIFI_SSID = "iPhone - Adrian";
const char* WIFI_PASSWORD = "12456789";
const char* SERVER_URL = "https://pac-management.onrender.com/api/gate/authorize";

// ===== WEB SERVER =====
WebServer server(80);

// ===== SERVO MOTOR =====
#define SERVO_PIN 32  // D32 (GPIO32) - pin PWM disponibil
Servo servoMotor;
const int SERVO_CLOSED = 0;    // Poziția inchis
const int SERVO_OPEN = 90;     // Poziția deschis

// ===== IR SENSORS =====
#define IR_SENSOR1 5    // GPIO5 - Receptor IR 1 (intrare pe bariera)
#define IR_SENSOR2 27   // GPIO27 - Receptor IR 2 (ieșire din bariera)
#define IR_TRANSMITTER1 25  // GPIO25 - Control Transmitter IR 1 (ON/OFF)
#define IR_TRANSMITTER2 26  // GPIO26 - Control Transmitter IR 2 (ON/OFF)

// ===== STARI BARIERA =====
enum BarrierState {
  BARRIER_CLOSED,
  BARRIER_OPENING,
  BARRIER_WAITING_SENSOR1,
  BARRIER_WAITING_SENSOR2,
  BARRIER_CLOSING,
  BARRIER_OPEN
};

BarrierState barrierState = BARRIER_CLOSED;
bool sensor1_blocked = false;   // Detectează dacă fasciculul IR1 este blocat
bool sensor2_blocked = false;   // Detectează dacă fasciculul IR2 este blocat
bool sensor1_prev_state = false;
bool sensor2_prev_state = false;
unsigned long barrierOpenTime = 0;
const unsigned long BARRIER_TIMEOUT = 30000;  // 30 sec timeout

// ===== VARIABILE GLOBALE =====
String btReceivedString = "";      // Stringul primit de la BLE
String wifiReceivedString = "";    // Stringul primit de la WiFi (web)
unsigned long lastBTReceiveTime = 0;
unsigned long lastWifiReceiveTime = 0;
unsigned long lastPollTime = 0;
const unsigned long BT_TIMEOUT = 30000;   // 30 sec timeout pentru BLE
const unsigned long WIFI_TIMEOUT = 30000; // 30 sec timeout pentru WiFi
const unsigned long POLL_INTERVAL = 2000; // 2 sec poll catre server

// ===== FORWARD DECLARATIONS =====
void sendViaBLE(String message);
void controlBarrier(String command);
void pollServerForCommands();

// ===== SETUP WIFI CLIENT =====
void setupWiFi() {
  Serial.println("\n=== Conectare la WiFi ===");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ Conectat la WiFi!");
    Serial.print("  SSID: ");
    Serial.println(WIFI_SSID);
    Serial.print("  IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ Eroare la conexiune WiFi!");
  }
}

void sendToServer(String deviceData) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{\"device_id\":\"ESP32_PAC\",\"data\":\"" + deviceData + "\"}";
    Serial.print("→ Trimit catre server: ");
    Serial.println(payload);
    
    int httpCode = http.POST(payload);
    
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.print("✓ Raspuns server: ");
      Serial.println(response);
      
      // Parse raspunsul pentru OK/NOT OK
      if (response.indexOf("OK") != -1) {
        Serial.println("→ Server: ACCES ACCEPTAT");
        controlBarrier("OK");
      } else if (response.indexOf("NOT OK") != -1 || response.indexOf("REFUSED") != -1) {
        Serial.println("→ Server: ACCES REFUZAT");
        controlBarrier("NOT OK");
      }
    } else {
      Serial.print("✗ Eroare HTTP: ");
      Serial.println(httpCode);
    }
    
    http.end();
  } else {
    Serial.println("⚠ WiFi deconectat!");
  }
}

// ===== POLLING COMENZI DE LA SERVER =====
void pollServerForCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin("https://pac-management.onrender.com/api/gate/poll");
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    if (response.indexOf("OPEN") != -1) {
      Serial.println("→ Comanda OPEN primita de la server!");
      controlBarrier("OK");
    }
  }

  http.end();
}

// ===== HANDLER WEB PENTRU PAGINA PRINCIPALA =====
void handleRoot() {
  String json = "{\"status\":\"OK\",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",\"barrier\":" + String(barrierState) + "}";
  server.send(200, "application/json", json);
}

// ===== HANDLER PENTRU STATUS (JSON) =====
void handleStatus() {
  String json = "{\"status\":\"OK\",\"btData\":\"";
  json += btReceivedString;
  json += "\",\"wifiData\":\"";
  json += wifiReceivedString;
  json += "\"}";
  server.send(200, "application/json", json);
}

// ===== HANDLER PENTRU TRIMITERE PRIN BLE =====
void handleSendBT() {
  if (server.hasArg("text")) {
    String text = server.arg("text");
    
    Serial.print("→ Web trimite: ");
    wifiReceivedString = text;
    lastWifiReceiveTime = millis();
    
    Serial.println(text);
    
    // Trimite data catre server
    sendToServer(text);
    
    server.send(200, "application/json", "{\"status\":\"OK\",\"message\":\"Trimis!\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"ERROR\",\"message\":\"Text lipsit\"}");
  }
}

// ===== SETUP BLE =====
void setupBLE() {
  Serial.println("→ Inițializare BLE...");
  
  if (!BLE.begin()) {
    Serial.println("✗ Eroare BLE!");
    while (1);
  }
  
  // Setează proprietățile BLE
  BLE.setLocalName(BLE_NAME);
  BLE.setAdvertisedService(dataService);
  
  // Adaugă caracteristicile
  dataService.addCharacteristic(rxCharacteristic);
  dataService.addCharacteristic(txCharacteristic);
  
  // Adaugă servicul
  BLE.addService(dataService);
  
  // Setează valorile inițiale
  rxCharacteristic.writeValue("");
  txCharacteristic.writeValue("");
  
  // Pornește advertsingul
  BLE.advertise();
  Serial.print("✓ BLE pornit: ");
  Serial.println(BLE_NAME);
  Serial.println("  Aștept conectare...");
}

// ===== SETUP SERVO =====
void setupServo() {
  Serial.println("→ Inițializare Servo Motor...");
  servoMotor.attach(SERVO_PIN, 1000, 2000);
  servoMotor.write(SERVO_CLOSED);
  Serial.println("✓ Servo Motor inițializat pe pinul D32");
  Serial.println("  Poziție: ÎNCHIS (0°)");
}

// ===== SETUP IR SENSORS =====
void setupIRSensors() {
  Serial.println("→ Inițializare Senzori IR...");
  pinMode(IR_SENSOR1, INPUT);
  pinMode(IR_SENSOR2, INPUT);
  pinMode(IR_TRANSMITTER1, OUTPUT);
  pinMode(IR_TRANSMITTER2, OUTPUT);
  digitalWrite(IR_TRANSMITTER1, LOW);  // Transmitter 1 oprit inițial
  digitalWrite(IR_TRANSMITTER2, LOW);  // Transmitter 2 oprit inițial
  Serial.println("✓ Senzor IR1 (GPIO5) - INTRARE");
  Serial.println("✓ Senzor IR2 (GPIO27) - IEȘIRE");
  Serial.println("✓ Transmitter IR1 (GPIO25) - CONTROL ON/OFF");
  Serial.println("✓ Transmitter IR2 (GPIO26) - CONTROL ON/OFF");
}

// ===== FUNCTIE PENTRU CONTROL BARIERA =====
void controlBarrier(String command) {
  if (command == "OK") {
    Serial.println("✓ Acces ACCEPTAT - Deschid bariera...");
    digitalWrite(IR_TRANSMITTER1, HIGH);  // Activez transmitter IR 1
    digitalWrite(IR_TRANSMITTER2, HIGH);  // Activez transmitter IR 2
    delay(50);
    servoMotor.write(SERVO_OPEN);
    barrierState = BARRIER_OPENING;
    barrierOpenTime = millis();
    sensor1_blocked = false;
    sensor2_blocked = false;
    Serial.println("  Transmitter IR: PORNIT (ambii) - Aștept mașini...");
    sendViaBLE("✓ Acces permis - Bariera se deschide");
  }
  else if (command == "NOT OK") {
    Serial.println("✗ Acces REFUZAT - Bariera rămâne ÎNCHIS");
    digitalWrite(IR_TRANSMITTER1, LOW);
    digitalWrite(IR_TRANSMITTER2, LOW);
    servoMotor.write(SERVO_CLOSED);
    barrierState = BARRIER_CLOSED;
    sensor1_blocked = false;
    sensor2_blocked = false;
    sendViaBLE("✗ Acces REFUZAT - Nu ai permisiune!");
  }
}

// ===== FUNCTIE PENTRU CITIRE SENZORI IR =====
void checkIRSensors() {
  bool ir1_current = (digitalRead(IR_SENSOR1) == LOW);  // LOW = fascicul blocat
  bool ir2_current = (digitalRead(IR_SENSOR2) == LOW);
  
  // Detecție tranziție senzor 1 (HIGH -> LOW = mașina INTRĂ)
  if (ir1_current && !sensor1_prev_state) {
    Serial.println("✓ Mașina DETECTATĂ pe Senzor 1!");
    sensor1_blocked = true;
  }
  sensor1_prev_state = ir1_current;
  
  // Detecție tranziție senzor 2 (HIGH -> LOW = mașina IESE)
  if (ir2_current && !sensor2_prev_state) {
    Serial.println("✓ Mașina DETECTATĂ pe Senzor 2!");
    sensor2_blocked = true;
  }
  sensor2_prev_state = ir2_current;
  
  // Mașină de stări pentru control bariera
  switch (barrierState) {
    case BARRIER_OPENING:
      barrierState = BARRIER_WAITING_SENSOR1;
      break;
    
    case BARRIER_WAITING_SENSOR1:
      if (sensor1_blocked) {
        Serial.println("✓✓ Senzor 1 OK - Aștept Senzor 2...");
        barrierState = BARRIER_WAITING_SENSOR2;
      }
      break;
    
    case BARRIER_WAITING_SENSOR2:
      if (sensor2_blocked) {
        Serial.println("✓✓✓ INCHID BARIERA!");
        servoMotor.write(SERVO_CLOSED);
        digitalWrite(IR_TRANSMITTER1, LOW);  // Opresc transmitter 1
        digitalWrite(IR_TRANSMITTER2, LOW);  // Opresc transmitter 2
        barrierState = BARRIER_CLOSED;
        sensor1_blocked = false;
        sensor2_blocked = false;
        sendViaBLE("✓ Mașina a trecut - Bariera se închide");
      }
      break;
    
    case BARRIER_CLOSED:
    case BARRIER_OPEN:
    default:
      break;
  }
  
  // Timeout de siguranță
  if (barrierState != BARRIER_CLOSED && millis() - barrierOpenTime > BARRIER_TIMEOUT) {
    Serial.println("⚠ TIMEOUT - Inchid bariera forțat!");
    servoMotor.write(SERVO_CLOSED);
    digitalWrite(IR_TRANSMITTER1, LOW);
    digitalWrite(IR_TRANSMITTER2, LOW);
    barrierState = BARRIER_CLOSED;
    sensor1_blocked = false;
    sensor2_blocked = false;
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(9600);
  delay(2000);
  
  Serial.println("\n\n╔════════════════════════════════════╗");
  Serial.println("║   ESP32 BLE ↔ WiFi Gateway        ║");
  Serial.println("╚════════════════════════════════════╝\n");
  
  setupBLE();
  setupWiFi();
  
  Serial.println("\n→ Inițializare Server Web local...");
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/send_bt", handleSendBT);
  server.begin();
  Serial.println("✓ Server web pornit (local control enabled)");
  
  setupServo();
  setupIRSensors();
  
  Serial.println("\n✓ Sistem ready!\n");
}

// ===== LOOP =====
void loop() {
  server.handleClient();
  checkIRSensors();

  if (millis() - lastPollTime > POLL_INTERVAL) {
    lastPollTime = millis();
    pollServerForCommands();
  }

  BLEDevice central = BLE.central();
  
  if (central) {
    Serial.print("✓ Client BLE conectat: ");
    Serial.println(central.address());
    
    while (central.connected()) {
      server.handleClient();
      checkIRSensors();

      if (millis() - lastPollTime > POLL_INTERVAL) {
        lastPollTime = millis();
        pollServerForCommands();
      }

      // Verifica dacă am primit date pe RX characteristic
      if (rxCharacteristic.written()) {
        String receivedData = rxCharacteristic.value();
        Serial.print("✓ BLE primit: ");
        Serial.println(receivedData);
        
        btReceivedString = receivedData;
        lastBTReceiveTime = millis();
        
        // Trimite data catre server
        sendToServer(receivedData);
      }
      
      if (btReceivedString.length() > 0 && millis() - lastBTReceiveTime > BT_TIMEOUT) {
        btReceivedString = "";
      }
      
      if (wifiReceivedString.length() > 0 && millis() - lastWifiReceiveTime > WIFI_TIMEOUT) {
        wifiReceivedString = "";
      }
      
      delay(10);
    }
    
    Serial.println("✗ Client BLE deconectat");
  }
  
  delay(50);
}

// ===== FUNCTIE PENTRU TRIMITERE PRIN BLE =====
void sendViaBLE(String message) {
  if (BLE.central()) {
    txCharacteristic.writeValue(message);
    Serial.print("→ BLE trimis: ");
    Serial.println(message);
  } else {
    Serial.println("⚠ Nici un client BLE conectat");
  }
}