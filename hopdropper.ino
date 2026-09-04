#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ========== KONFIGURATION ==========
// WiFi
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// MQTT
const char* mqtt_server = "192.168.1.100";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";
const char* mqtt_topic = "hopdropper/drop";

// Servo-Pins (GPIO-Nummern für ESP32-C6) - KONFIGURIERBAR
const int servo_pins[5] = {1, 2, 3, 4, 5};  // GPIO 1, 2, 3, 4, 5

// Servo-Positionen
const int servo_open = 90;    // Position offen (in Grad)
const int servo_closed = 0;   // Position geschlossen (in Grad)

// ========== GLOBALE VARIABLEN ==========
WiFiClient espClient;
PubSubClient client(espClient);
Servo servos[5];
unsigned long servo_close_time[5] = {0, 0, 0, 0, 0};  // Timer für automatisches Schließen
unsigned int servo_close_delay[5] = {0, 0, 0, 0, 0};  // Verzögerung in ms

// ========== FUNKTIONEN ==========

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Verbinde zu WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi verbunden!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Verbindung fehlgeschlagen!");
  }
}

void setup_servos() {
  Serial.println("Initialisiere Servos...");
  for (int i = 0; i < 5; i++) {
    servos[i].setPeriodHertz(50);
    servos[i].attach(servo_pins[i], 1000, 2000);
    servos[i].write(servo_closed);  // Alle Servos geschlossen
    Serial.print("Servo ");
    Serial.print(i + 1);
    Serial.print(" an GPIO ");
    Serial.println(servo_pins[i]);
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // JSON-Nachricht auslesen
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("JSON Parse Fehler: ");
    Serial.println(error.c_str());
    return;
  }

  // Slot und State auslesen
  if (!doc.containsKey("slot") || !doc.containsKey("state")) {
    Serial.println("Fehler: 'slot' oder 'state' fehlt in Payload");
    return;
  }

  int slot = doc["slot"];
  const char* state = doc["state"];
  
  // Drop Time optional (in Sekunden)
  unsigned int dropTime = doc["dropTime"] | 0;

  // Validierung
  if (slot < 1 || slot > 5) {
    Serial.println("Fehler: Slot muss zwischen 1 und 5 sein");
    return;
  }

  int servo_index = slot - 1;

  Serial.print("Empfangen: Slot ");
  Serial.print(slot);
  Serial.print(", State: ");
  Serial.print(state);
  if (dropTime > 0) {
    Serial.print(", DropTime: ");
    Serial.print(dropTime);
    Serial.println("s");
  } else {
    Serial.println();
  }

  // Servo steuern
  if (strcmp(state, "on") == 0) {
    servos[servo_index].write(servo_open);
    Serial.print("Servo ");
    Serial.print(slot);
    Serial.println(" geöffnet");

    // Timer setzen, falls dropTime angegeben
    if (dropTime > 0) {
      servo_close_time[servo_index] = millis() + (dropTime * 1000);
      servo_close_delay[servo_index] = dropTime * 1000;
    }
  } 
  else if (strcmp(state, "off") == 0) {
    servos[servo_index].write(servo_closed);
    Serial.print("Servo ");
    Serial.print(slot);
    Serial.println(" geschlossen");
    
    // Timer zurücksetzen
    servo_close_time[servo_index] = 0;
    servo_close_delay[servo_index] = 0;
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Verbinde zu MQTT... ");
    
    String clientId = "hopdropper_esp32_";
    clientId += String(random(0xffff), HEX);
    
    bool connected = false;
    if (mqtt_user[0] != '\0') {
      connected = client.connect(clientId.c_str(), mqtt_user, mqtt_password);
    } else {
      connected = client.connect(clientId.c_str());
    }

    if (connected) {
      Serial.println("verbunden!");
      client.subscribe(mqtt_topic);
      Serial.print("Abonniert Topic: ");
      Serial.println(mqtt_topic);
      
      // Status-Nachricht senden
      client.publish((String(mqtt_topic) + "/status").c_str(), "online");
    } else {
      Serial.print("Fehler, rc=");
      Serial.print(client.state());
      Serial.println(" - versuche in 5s erneut");
      delay(5000);
    }
  }
}

void handle_auto_close() {
  // Prüfe jeden Servo auf automatisches Schließen
  for (int i = 0; i < 5; i++) {
    if (servo_close_time[i] > 0 && millis() >= servo_close_time[i]) {
      servos[i].write(servo_closed);
      Serial.print("Auto-Close: Servo ");
      Serial.print(i + 1);
      Serial.println(" geschlossen");
      
      // MQTT-Nachricht senden
      StaticJsonDocument<128> doc;
      doc["slot"] = i + 1;
      doc["state"] = "off";
      char buffer[256];
      serializeJson(doc, buffer);
      client.publish(mqtt_topic, buffer);
      
      servo_close_time[i] = 0;
      servo_close_delay[i] = 0;
    }
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\nHopdropper ESP32-C6 Firmware startet...");
  
  setup_wifi();
  setup_servos();
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
}

// ========== LOOP ==========
void loop() {
  // WiFi verbunden?
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }

  // MQTT verbunden?
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Automatisches Schließen prüfen
  handle_auto_close();

  delay(10);
}
