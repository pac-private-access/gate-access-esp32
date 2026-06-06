#include <Arduino.h>
#include <BluetoothSerial.h>
#include <WiFi.h>
#include <WebServer.h>

// ===== BLUETOOTH =====
BluetoothSerial SerialBT;
const char* BT_NAME = "ESP32_BT";

// ===== SOFTAP (WIFI) =====
const char* AP_SSID = "ESP32_Network_PAC";
const char* AP_PASSWORD = "12345678";

// ===== WEB SERVER =====
WebServer server(80);

// ===== VARIABILE GLOBALE =====
String btReceivedString = "";      // Stringul primit de la Bluetooth
String wifiReceivedString = "";    // Stringul primit de la WiFi (web)
String btBuffer = "";               // Buffer pentru colectare Bluetooth
unsigned long lastBTReceiveTime = 0;
unsigned long lastWifiReceiveTime = 0;
const unsigned long BT_TIMEOUT = 30000; // 30 sec timeout pentru BT
const unsigned long WIFI_TIMEOUT = 30000; // 30 sec timeout pentru WiFi

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
      <h2>📥 Date Primite via Bluetooth</h2>
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

// ===== HANDLER PENTRU TRIMITERE PRIN BLUETOOTH =====
void handleSendBT() {
  if (server.hasArg("text")) {
    String text = server.arg("text");
    
    Serial.print("→ Web trimite pe BT: ");
    // Salvez ce s-a trimis
    wifiReceivedString = text;
    lastWifiReceiveTime = millis();
    
    Serial.println(text);
    
    // ===== CONDIȚIE - VERIFICA ACCES =====
    if (text == "OK") {
      Serial.println("✓ Acces ACCEPTAT - Pornesc bariera...");
      // TODO: digitalWrite(SERVO_PIN, HIGH);  // Pornesc servomotor
      SerialBT.println("✓ Acces permis - Bariera se deschide");
    } 
    else if (text == "NOT OK") {
      Serial.println("✗ Acces REFUZAT!");
      SerialBT.println("✗ Acces REFUZAT - Nu ai permisiune!");
    }
    // =====================================
    
    SerialBT.println(text);
    
    server.send(200, "application/json", "{\"status\":\"OK\",\"message\":\"Trimis!\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"ERROR\",\"message\":\"Text lipsit\"}");
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(9600);
  delay(2000);
  
  Serial.println("\n\n╔════════════════════════════════════╗");
  Serial.println("║   ESP32 Bluetooth ↔ WiFi Gateway  ║");
  Serial.println("╚════════════════════════════════════╝\n");
  
  Serial.println("→ Inițializare Bluetooth...");
  if (SerialBT.begin(BT_NAME)) {
    Serial.print("✓ Bluetooth pornit: ");
    Serial.println(BT_NAME);
  } else {
    Serial.println("✗ Eroare Bluetooth!");
  }
  
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
  server.handleClient();
  
  if (SerialBT.available()) {
    char c = SerialBT.read();
    Serial.write(c);
    
    if (c == '\n' || c == '\r') {
      if (btBuffer.length() > 0) {
        btReceivedString = btBuffer;
        Serial.print("✓ BT primit: ");
        Serial.println(btReceivedString);
        
        btBuffer = "";
        lastBTReceiveTime = millis();
      }
    } else {
      btBuffer += c;
    }
  }
  
  if (btReceivedString.length() > 0 && millis() - lastBTReceiveTime > BT_TIMEOUT) {
    btReceivedString = "";
  }
  
  if (wifiReceivedString.length() > 0 && millis() - lastWifiReceiveTime > WIFI_TIMEOUT) {
    wifiReceivedString = "";
  }
  
  delay(10);
}