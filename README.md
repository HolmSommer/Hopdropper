# cbpi4_hopdropper

CraftBeerPi 4 Plugin für einen über MQTT gesteuerten Hopdropper mit 5 Slots.

## Installation

```bash
pip install .
cbpi add cbpi4_hopdropper
```

Danach CraftBeerPi neu starten. MQTT muss in CBPi4 (Settings) aktiviert sein.

## Komponenten

### Hopdropper MQTT Actor

Ein Actor pro Slot anlegen (Hardware-Typ `Hopdropper MQTT Actor`).

| Parameter | Bedeutung |
|---|---|
| `Topic` | MQTT Topic, z. B. `hopdropper/drop` |
| `Slot` | Slot-/Servo-Nummer 1–5 |
| `DropTime` | Sekunden, die der Slot geöffnet bleibt (0 = manuell schließen) |
| `PayloadOn` / `PayloadOff` | Optionale eigene Payloads |

Standard-Payload:

```json
{"slot": 1, "state": "on"}
```

Nach `DropTime` Sekunden wird automatisch `{"slot": 1, "state": "off"}` gesendet
und der Actor in der UI auf „aus“ gesetzt.

Über die Actor-Aktion **Hopfen abwerfen** kann ein Slot manuell ausgelöst werden.

### Hopdropper Boil Step

Kochschritt mit Kettle, Sensor, Zieltemperatur und Kochzeit. Der Timer startet,
sobald die Zieltemperatur erreicht ist. Für jeden der 5 Slots werden ein Actor
und die Restzeit in Minuten hinterlegt, bei der abgeworfen wird.

## Firmware-Seite (ESP)

Der Controller abonniert das konfigurierte Topic und öffnet bei `state: "on"`
das angegebene Servo, bei `state: "off"` schließt er es wieder.
