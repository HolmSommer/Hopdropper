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

Ein Actor pro Slot anlegen (Hardware-Typ `HopDropperActor`).

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

Vollwertiger Ersatz für den CBPi Boil Step: Kettle, Sensor, Zieltemperatur und
Kochzeit. Der Timer startet automatisch, sobald die Zieltemperatur erreicht ist.

| Parameter | Bedeutung |
|---|---|
| `AutoMode` | Kettlelogic beim Start automatisch ein- und am Ende ausschalten |
| `LidAlert` | Hinweis „Deckel abnehmen", sobald 88 °C / 190 °F erreicht sind |
| `First_Wort` / `First_Wort_text` | Hinweis auf Vorderwürzehopfung beim Start |
| `Hop_1` … `Hop_6` | Restzeit in Minuten, bei der abgeworfen wird |
| `Hop_1_text` … `Hop_6_text` | Name der Hopfengabe (aus dem Rezept) |
| `Hop_1_Actor` … `Hop_6_Actor` | Optionale manuelle Zuordnung des Dropper-Actors |

Aktionen im laufenden Schritt: **Timer starten** und **5 Minuten hinzufügen**.

## Rezeptimport (MMuM, Kleiner Brauhelfer, BeerXML, Brewfather)

Die Parameternamen entsprechen exakt denen des CBPi-Boil-Steps, daher füllt der
Rezeptimport `Timer`, `Temp`, `First_Wort` und `Hop_1` … `Hop_6` automatisch.

Damit der Import diesen Step statt des Standard-Boil-Steps anlegt, in den CBPi
Einstellungen den Parameter `steps_boil` auf `HopDropperStep` setzen.

Die Zuordnung Hopfengabe → Dropper-Slot passiert automatisch: Der Step sucht alle
Actoren vom Typ `HopDropperActor` und ordnet Hopfengabe 1 dem Slot 1 zu, usw.
Deshalb bleibt die Zuordnung auch nach einem Rezeptimport erhalten. `Hop_x_Actor`
wird nur benötigt, wenn du davon abweichen willst. Gibt es für eine Hopfengabe
keinen Slot (z. B. die 6. bei fünf Slots), kommt nur eine Benachrichtigung.

## Firmware-Seite (ESP)

Der Controller abonniert das konfigurierte Topic und öffnet bei `state: "on"`
das angegebene Servo, bei `state: "off"` schließt er es wieder.
