#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

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

// Servo-Positionen (in Grad)
const int servo_open = 90;    // Position offen (in Grad)

// Servo-Bewegungsgeschwindigkeit
const unsigned long servo_move_time = 1000;  // 1 Sekunde für volle Bewegung

// EEPROM Konfiguration
#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0xAB        // Magic Byte zur Überprüfung
#define EEPROM_MAGIC_ADDR 0      // Adresse für Magic Byte
#define EEPROM_HOME_START 1       // Startadresse für Home-Positionen (5 Bytes)

// ========== ENUM für Servo-Zustand ==========
enum ServoState { CLOSED, OPEN, CLOSING, OPENING, INITIALIZING };

// ========== GLOBALE VARIABLEN ==========
WiFiClient espClient;
PubSubClient client(espClient);
Servo servos[5];

// Servo-Status tracking
ServoState servo_state[5] = {INITIALIZING, INITIALIZING, INITIALIZING, INITIALIZING, INITIALIZING};
int servo_current_pos[5] = {0, 0, 0, 0, 0};  // Aktuelle Position (0-90°)
int servo_target_pos[5] = {0, 0, 0, 0, 0};   // Zielposition
int servo_home[5] = {0, 0, 0, 0, 0};         // Nullpositionen (aus EEPROM)

// Timer für automatisches Schließen
unsigned long servo_close_time[5] = {0, 0, 0, 0, 0};

// Timer für sanfte Bewegung
unsigned long servo_move_start[5] = {0, 0, 0, 0, 0};

// Initialisierungs-Timer (1 Sekunde Abstand zwischen Servos)
unsigned long last_init_time = 0;
int init_servo_index = 0;
bool init_complete = false;

// MQTT Reconnect Timer (non-blocking)
unsigned long last_reconnect_attempt = 0;
const unsigned long reconnect_interval = 5000;  // 5 Sekunden zwischen Versuchen

// ========== EEPROM-FUNKTIONEN ==========

void eeprom_init() {
  EEPROM.begin(EEPROM_SIZE);
  Serial.println("EEPROM initialisiert");
}

void load_home_positions() {
  // Überprüfe Magic Byte
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  
  if (magic == EEPROM_MAGIC) {
    // Gültige Daten in EEPROM vorhanden
    for (int i = 0; i < 5; i++) {
      servo_home[i] = EEPROM.read(EEPROM_HOME_START + i);
      Serial.print("EEPROM: Servo ");
      Serial.print(i + 1);
      Serial.print(" Nullposition geladen: ");
      Serial.println(servo_home[i]);
    }
  } else {
    // Keine gültigen Daten - Standard verwenden
    Serial.println("Keine gespeicherten Nullpositionen - verwende Standard (0°)");
    for (int i = 0; i < 5; i++) {
      servo_home[i] = 0;
    }
  }
}

void save_home_position(int servo_index, int home_pos) {
  if (servo_index >= 0 && servo_index < 5) {
    // Speichere die Position
    EEPROM.write(EEPROM_HOME_START + servo_index, home_pos);
    servo_home[servo_index] = home_pos;
    
    // Speichere Magic Byte beim ersten Mal
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    
    // Commit zum Flash-Speicher
    EEPROM.commit();
    
    Serial.print("EEPROM: Servo ");
    Serial.print(servo_index + 1);
    Serial.print(" Nullposition gespeichert: ");
    Serial.println(home_pos);
  }
}

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

void setup_servos_sequential() {
  // Initiale Setup nur für erstes Servo
  if (init_servo_index == 0 && !init_complete) {
    Serial.println("\nStarte sequenzielle Servo-Initialisierung...");
    last_init_time = millis();
  }

  // Überprüfe ob 1 Sekunde seit letztem Init vergangen ist
  if (millis() - last_init_time >= 1000) {
    if (init_servo_index < 5) {
      int i = init_servo_index;
      
      // Servo initialisieren
      servos[i].setPeriodHertz(50);
      int pin = servos[i].attach(servo_pins[i], 1000, 2000);
      
      if (pin != UNKNOWN_PIN) {
        // Fahre zu Nullposition
        servo_current_pos[i] = servo_home[i];
        servo_target_pos[i] = servo_home[i];
        servos[i].write(servo_home[i]);
        servo_state[i] = CLOSED;
        
        Serial.print("✓ Servo ");
        Serial.print(i + 1);
        Serial.print(" initialisiert an GPIO ");
        Serial.print(servo_pins[i]);
        Serial.print(" - Nullposition: ");
        Serial.print(servo_home[i]);
        Serial.println("°");
      } else {
        Serial.print("✗ FEHLER: Servo ");
        Serial.print(i + 1);
        Serial.println(" konnte nicht initialisiert werden!");
      }
      
      init_servo_index++;
      last_init_time = millis();
    } else {
      // Alle Servos initialisiert
      init_complete = true;
      Serial.println("\n✓ Alle Servos initialisiert - System bereit!\n");
    }
  }
}

// Sanfte Servo-Bewegung (1 Sekunde von aktueller zu Zielposition)
void move_servo_smooth(int servo_index) {
  if (servo_index < 0 || servo_index >= 5) {
    return;
  }

  unsigned long elapsed = millis() - servo_move_start[servo_index];
  
  if (elapsed >= servo_move_time) {
    // Bewegung abgeschlossen
    servo_current_pos[servo_index] = servo_target_pos[servo_index];
    servos[servo_index].write(servo_target_pos[servo_index]);
    
    if (servo_state[servo_index] == OPENING) {
      servo_state[servo_index] = OPEN;
      Serial.print("Servo ");
      Serial.print(servo_index + 1);
      Serial.println(" vollständig geöffnet");
    } else if (servo_state[servo_index] == CLOSING) {
      servo_state[servo_index] = CLOSED;
      Serial.print("Servo ");
      Serial.print(servo_index + 1);
      Serial.println(" vollständig geschlossen");
    }
  } else {
    // Interpolation: lineare Bewegung
    int start_pos = (servo_state[servo_index] == OPENING) ? servo_home[servo_index] : servo_open;
    int pos_diff = servo_target_pos[servo_index] - start_pos;
    int current_pos = start_pos + (pos_diff * elapsed) / servo_move_time;
    
    servo_current_pos[servo_index] = current_pos;
    servos[servo_index].write(current_pos);
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

  // Slot und State auslesen mit Typ-Überprüfung
  if (!doc["slot"].is<int>() || !doc["state"].is<const char*>()) {
    Serial.println("Fehler: 'slot' oder 'state' hat ungültigen Typ");
    return;
  }

  int slot = doc["slot"];
  const char* state = doc["state"];
  
  // Drop Time optional (in Sekunden)
  unsigned int dropTime = doc["dropTime"] | 0;

  // Validierung Slot
  if (slot < 1 || slot > 5) {
    Serial.print("Fehler: Slot ");
    Serial.print(slot);
    Serial.println(" muss zwischen 1 und 5 sein");
    return;
  }

  // Keine Befehle während Initialisierung
  if (!init_complete) {
    Serial.println("Fehler: System wird noch initialisiert. Bitte warten...");
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

  // Servo steuern - mit Fehlerbehandlung für unbekannte States
  if (strcmp(state, "on") == 0) {
    servo_target_pos[servo_index] = servo_open;
    servo_state[servo_index] = OPENING;
    servo_move_start[servo_index] = millis();
    
    Serial.print("Servo ");
    Serial.print(slot);
    Serial.println(" wird geöffnet...");

    // Timer setzen, falls dropTime angegeben
    if (dropTime > 0) {
      servo_close_time[servo_index] = millis() + (dropTime * 1000);
    }
  } 
  else if (strcmp(state, "off") == 0) {
    servo_target_pos[servo_index] = servo_home[servo_index];
    servo_state[servo_index] = CLOSING;
    servo_move_start[servo_index] = millis();
    
    Serial.print("Servo ");
    Serial.print(slot);
    Serial.println(" wird geschlossen...");
    
    // Timer zurücksetzen
    servo_close_time[servo_index] = 0;
  } 
  else if (strcmp(state, "config") == 0) {
    // CONFIG-Command: Nullposition einstellen
    int home_pos = doc["homePos"] | -1;
    if (home_pos >= 0 && home_pos <= 45) {
      servo_home[servo_index] = home_pos;
      servo_current_pos[servo_index] = home_pos;
      servo_target_pos[servo_index] = home_pos;
      servos[servo_index].write(home_pos);
      servo_state[servo_index] = CLOSED;
      
      // Speichere in EEPROM (nonvolatil)
      save_home_position(servo_index, home_pos);
      
      Serial.print("Servo ");
      Serial.print(slot);
      Serial.print(" Nullposition gespeichert: ");
      Serial.println(home_pos);
      
      // Bestätigung senden
      StaticJsonDocument<128> response;
      response["slot"] = slot;
      response["command"] = "config";
      response["homePos"] = home_pos;
      response["status"] = "ok";
      response["stored"] = "eeprom";
      char buffer[256];
      serializeJson(response, buffer);
      client.publish((String(mqtt_topic) + "/config/response").c_str(), buffer);
    } else {
      Serial.print("Fehler: homePos muss zwischen 0 und 45 liegen, erhalten: ");
      Serial.println(home_pos);
    }
  }
  else if (strcmp(state, "status") == 0) {
    // Status-Anfrage: gebe aktuelle Positionen aus
    StaticJsonDocument<256> status_response;
    JsonArray positions = status_response.createNestedArray("positions");
    
    for (int i = 0; i < 5; i++) {
      JsonObject servo_status = positions.createNestedObject();
      servo_status["slot"] = i + 1;
      servo_status["home"] = servo_home[i];
      servo_status["current"] = servo_current_pos[i];
      servo_status["state"] = (servo_state[i] == CLOSED) ? "closed" :
                              (servo_state[i] == OPEN) ? "open" :
                              (servo_state[i] == CLOSING) ? "closing" :
                              (servo_state[i] == OPENING) ? "opening" : "init";
    }
    
    char buffer[512];
    serializeJson(status_response, buffer);
    client.publish((String(mqtt_topic) + "/status/response").c_str(), buffer);
  }
  else {
    Serial.print("Fehler: Unbekannter State '");
    Serial.print(state);
    Serial.println("'. Erwartet: 'on', 'off', 'config' oder 'status'");
  }
}

void reconnect() {
  // Nur versuchen, wenn genug Zeit seit letztem Versuch vergangen ist
  if (millis() - last_reconnect_attempt < reconnect_interval) {
    return;
  }

  last_reconnect_attempt = millis();

  if (!client.connected()) {
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
    }
  }
}

void handle_auto_close() {
  // Prüfe jeden Servo auf automatisches Schließen
  for (int i = 0; i < 5; i++) {
    if (servo_close_time[i] > 0) {
      // Sichere Zeitvergleich (Overflow-sicher)
      if ((long)(millis() - servo_close_time[i]) >= 0) {
        servo_target_pos[i] = servo_home[i];
        servo_state[i] = CLOSING;
        servo_move_start[i] = millis();
        
        Serial.print("Auto-Close: Servo ");
        Serial.print(i + 1);
        Serial.println(" wird geschlossen");
        
        // MQTT-Nachricht senden
        StaticJsonDocument<128> doc;
        doc["slot"] = i + 1;
        doc["state"] = "off";
        doc["reason"] = "auto-close";
        char buffer[256];
        serializeJson(doc, buffer);
        client.publish(mqtt_topic, buffer);
        
        servo_close_time[i] = 0;
      }
    }
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\n===========================================");
  Serial.println("Hopdropper ESP32-C6 Firmware v2.1");
  Serial.println("Mit EEPROM-Speicherung & sequenzieller Init");
  Serial.println("===========================================\n");
  
  // EEPROM initialisieren und Nullpositionen laden
  eeprom_init();
  load_home_positions();
  
  setup_wifi();
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
  
  Serial.println("\nBeginn Servo-Initialisierung in 1 Sekunden-Abstand...\n");
}

// ========== LOOP ==========
void loop() {
  // WiFi verbunden?
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }

  // Sequenzielle Servo-Initialisierung
  if (!init_complete) {
    setup_servos_sequential();
  }

  // MQTT verbunden? (Non-blocking) - nur wenn Init komplett
  if (init_complete) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }

  // Sanfte Servo-Bewegungen verarbeiten (nur wenn Init komplett)
  if (init_complete) {
    for (int i = 0; i < 5; i++) {
      if (servo_state[i] == OPENING || servo_state[i] == CLOSING) {
        move_servo_smooth(i);
      }
    }

    // Automatisches Schließen prüfen
    handle_auto_close();
  }

  delay(10);
}
