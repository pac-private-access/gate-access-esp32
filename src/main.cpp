#include <Arduino.h>
#include <ArduinoBLE.h>
#include <WiFi.h>
#include <WebServer.h>

// ===== BLE =====
BLEService dataService("180A");  // Generic Access service
BLEStringCharacteristic rxCharacteristic("2A19", BLEWrite, 128);  // Rx (write from phone)
BLEStringCharacteristic txCharacteristic("2A1A", BLERead | BLENotify, 128);  // Tx (read from ESP)
const char* BLE_NAME = "ESP32_BLE";

// ===== SOFTAP (WIFI) =====
const char* AP_SSID = "ESP32_Network_PAC";
const char* AP_PASSWORD = "12345678";

// ===== WEB SERVER =====
WebServer server(80);

// ===== VARIABILE GLOBALE =====
String btReceivedString = "";      // Stringul primit de la BLE
String wifiReceivedString = "";    // Stringul primit de la WiFi (web)
unsigned long lastBTReceiveTime = 0;
unsigned long lastWifiReceiveTime = 0;
const unsigned long BT_TIMEOUT = 30000; // 30 sec timeout pentru BLE
const unsigned long WIFI_TIMEOUT = 30000; // 30 sec timeout pentru WiFi

// ===== FORWARD DECLARATIONS =====
void sendViaBLE(String message);

// ===== SETUP SOFTAP =====
void setupSoftAP() {
  Serial.println("\n=== Inițializare SoftAP ===");
  WiFi.mode(WIFI_AP);
  
  bool result = WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  if (result) {
    Serial.println("✓ SoftAP inițializat cu succes!");
    Serial.print("  Rețea: ");
    Serial.println(AP_SSID);
    Serial.print("  Parola: ");
    Serial.println(AP_PASSWORD);
    Serial.print("  IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("✗ Eroare la inițializare SoftAP!");
  }
}

// ===== HANDLER WEB PENTRU PAGINA PRINCIPALA =====
void handleRoot() {
  String html = R"(<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>ESP32 Bluetooth ↔ WiFi</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Arial, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 10px;
      box-shadow: 0 10px 40px rgba(0, 0, 0, 0.2);
      padding: 30px;
      max-width: 500px;
      width: 100%;
    }
    h1 {
      color: #333;
      margin-bottom: 10px;
      text-align: center;
    }
    .status {
      text-align: center;
      color: #666;
      font-size: 14px;
      margin-bottom: 25px;
    }
    .section {
      margin-bottom: 25px;
      padding: 20px;
      background: #f8f9fa;
      border-radius: 8px;
      border-left: 4px solid #667eea;
    }
    .section h2 {
      color: #667eea;
      font-size: 16px;
      margin-bottom: 12px;
    }
    .data-box {
      background: white;
      padding: 12px;
      border-radius: 5px;
      border: 1px solid #ddd;
      word-break: break-all;
      font-family: monospace;
      color: #333;
      min-height: 40px;
      display: flex;
      align-items: center;
    }
    .empty {
      color: #999;
      font-style: italic;
    }
    input[type='text'] {
      width: 100%;
      padding: 10px;
      border: 1px solid #ddd;
      border-radius: 5px;
      font-size: 14px;
      margin-bottom: 10px;
    }
    button {
      width: 100%;
      padding: 12px;
      background: #667eea;
      color: white;
      border: none;
      border-radius: 5px;
      font-size: 14px;
      font-weight: bold;
      cursor: pointer;
      transition: background 0.3s;
    }
    button:hover {
      background: #764ba2;
    }
    button:active {
      transform: scale(0.98);
    }
    .info {
      font-size: 12px;
      color: #999;
      margin-top: 15px;
      text-align: center;
    }
  </style>
</head>
<body>
  <div class='container'>
    <h1>📱 ESP32 Gateway</h1>
    <div class='status'>Conectare WiFi pentru pagina de control</div>
    
    <div class='section'>
      <h2> Date Primite via Bluetooth</h2>
      <div class='data-box' id='btDataBox'>
        <span class='empty'>Aștept date...</span>
      </div>
    </div>
    
    <div class='section'>
      <h2>📤 Trimite via Bluetooth</h2>
      <input type='text' id='btInput' placeholder='Introdu textul...' maxlength='100'>
      <button onclick='sendViaBT()'>Trimite prin Bluetooth</button>
    </div>
    
    <div class='info'>
      ℹ️ Auto-refresh: 1 sec | IP: 192.168.4.1
    </div>
  </div>
  
  <script>
    setInterval(function() {
      fetch('/status')
        .then(r => r.json())
        .then(data => {
          let box = document.getElementById('btDataBox');
          if (data.btData && data.btData.trim().length > 0) {
            box.textContent = data.btData;
          } else {
            box.innerHTML = '<span class="empty">Aștept date...</span>';
          }
        })
        .catch(e => console.log('Eroare:', e));
    }, 1000);
    
    function sendViaBT() {
      let input = document.getElementById('btInput');
      let text = input.value.trim();
      
      if (text.length === 0) {
        alert('⚠️ Introdu un text!');
        return;
      }
      
      fetch('/send_bt?text=' + encodeURIComponent(text))
        .then(r => r.json())
        .then(data => {
          if (data.status === 'OK') {
            alert('✓ Trimis!');
            input.value = '';
          } else {
            alert('✗ Eroare!');
          }
        })
        .catch(e => alert('Eroare: ' + e));
    }
  </script>
</body>
</html>)";
  
  server.send(200, "text/html; charset=utf-8", html);
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
    
    Serial.print("→ Web trimite pe BLE: ");
    // Salvez ce s-a trimis
    wifiReceivedString = text;
    lastWifiReceiveTime = millis();
    
    Serial.println(text);
    
    // ===== CONDIȚIE - VERIFICA ACCES =====
    if (text == "OK") {
      Serial.println("✓ Acces ACCEPTAT - Pornesc bariera...");
      // TODO: digitalWrite(SERVO_PIN, HIGH);  // Pornesc servomotor
      sendViaBLE("✓ Acces permis - Bariera se deschide");
    } 
    else if (text == "NOT OK") {
      Serial.println("✗ Acces REFUZAT!");
      sendViaBLE("✗ Acces REFUZAT - Nu ai permisiune!");
    }
    // =====================================
    
    sendViaBLE(text);
    
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

// ===== SETUP =====
void setup() {
  Serial.begin(9600);
  delay(2000);
  
  Serial.println("\n\n╔════════════════════════════════════╗");
  Serial.println("║   ESP32 BLE ↔ WiFi Gateway        ║");
  Serial.println("╚════════════════════════════════════╝\n");
  
  setupBLE();
  
  setupSoftAP();
  Serial.println("\n→ Inițializare Server Web...");
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/send_bt", handleSendBT);
  server.begin();
  Serial.println("✓ Server web pornit pe 192.168.4.1");
  
  Serial.println("\n✓ Sistem ready!\n");
}

// ===== LOOP =====
void loop() {
  BLEDevice central = BLE.central();
  
  if (central) {
    Serial.print("✓ Client BLE conectat: ");
    Serial.println(central.address());
    
    while (central.connected()) {
      server.handleClient();
      
      // Verifica dacă am primit date pe RX characteristic
      if (rxCharacteristic.written()) {
        String receivedData = rxCharacteristic.value();
        Serial.print("✓ BLE primit: ");
        Serial.println(receivedData);
        
        btReceivedString = receivedData;
        lastBTReceiveTime = millis();
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