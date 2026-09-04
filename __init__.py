import asyncio
import json
import logging

from cbpi.api import (
    CBPiActor,
    CBPiStep,
    Property,
    StepResult,
    action,
    parameters,
)
from cbpi.api.dataclasses import NotificationType
from cbpi.api.timer import Timer

logger = logging.getLogger(__name__)


@parameters([
    Property.Text(label="Topic", configurable=True, default_value="hopdropper/drop",
                  description="MQTT Topic, auf das der Hopdropper hört"),
    Property.Select(label="Slot", options=[1, 2, 3, 4, 5],
                    description="Hopfen-Slot / Servo-Nummer des Hopdroppers (1-5)"),
    Property.Number(label="DropTime", configurable=True, default_value=3,
                    description="Sekunden, die der Slot geöffnet bleibt. 0 = manuell schließen"),
    Property.Text(label="PayloadOn", configurable=True, default_value="",
                  description="Optionales eigenes ON-Payload. Leer = JSON {\"slot\": x, \"state\": \"on\"}"),
    Property.Text(label="PayloadOff", configurable=True, default_value="",
                  description="Optionales eigenes OFF-Payload. Leer = JSON {\"slot\": x, \"state\": \"off\"}"),
])
class HopDropperActor(CBPiActor):
    """Ein Slot eines MQTT Hopdroppers. Ein Actor pro Slot anlegen."""

    def __init__(self, cbpi, id, props):
        CBPiActor.__init__(self, cbpi, id, props)
        self.power = 100
        self.auto_off_task = None

    async def on_start(self):
        self.topic = self.props.get("Topic", "hopdropper/drop")
        self.slot = int(self.props.get("Slot", 1))
        self.drop_time = float(self.props.get("DropTime", 3))
        self.payload_on = self.props.get("PayloadOn", "")
        self.payload_off = self.props.get("PayloadOff", "")
        self.state = False

    async def on_stop(self):
        self.cancel_auto_off()
        await self.off()

    def cancel_auto_off(self):
        if self.auto_off_task is not None and not self.auto_off_task.done():
            self.auto_off_task.cancel()
        self.auto_off_task = None

    async def publish(self, state):
        if self.cbpi.satellite is None:
            logger.warning("Hopdropper %s: MQTT ist in CraftBeerPi nicht aktiviert", self.id)
            return
        custom = self.payload_on if state else self.payload_off
        if custom:
            payload = custom
        else:
            payload = json.dumps({"slot": self.slot, "state": "on" if state else "off"})
        await self.cbpi.satellite.publish(self.topic, payload, False)

    async def on(self, power=None):
        if power is not None:
            self.power = power
        self.cancel_auto_off()
        await self.publish(True)
        self.state = True
        if self.drop_time > 0:
            self.auto_off_task = asyncio.create_task(self.auto_off())

    async def auto_off(self):
        try:
            await asyncio.sleep(self.drop_time)
            await self.cbpi.actor.off(self.id)
        except asyncio.CancelledError:
            pass

    async def off(self):
        self.cancel_auto_off()
        await self.publish(False)
        self.state = False

    async def set_power(self, power):
        self.power = power
        await self.cbpi.actor.actor_update(self.id, power)

    def get_state(self):
        return self.state

    @action(key="Hopfen abwerfen", parameters=[])
    async def drop(self):
        await self.cbpi.actor.on(self.id)

    async def run(self):
        while self.running:
            await asyncio.sleep(1)


@parameters([
    Property.Number(label="Kettle", configurable=True, description="Kettle ID"),
    Property.Sensor(label="Sensor", description="Temperatursensor zum Starten des Timers"),
    Property.Number(label="Temp", configurable=True,
                    description="Ab dieser Temperatur startet der Timer"),
    Property.Number(label="Timer", configurable=True, description="Kochzeit in Minuten"),
    Property.Actor(label="Hop_1"),
    Property.Number(label="Hop_1_Timer", configurable=True,
                    description="Restzeit in Minuten, bei der Slot 1 abwirft"),
    Property.Actor(label="Hop_2"),
    Property.Number(label="Hop_2_Timer", configurable=True,
                    description="Restzeit in Minuten, bei der Slot 2 abwirft"),
    Property.Actor(label="Hop_3"),
    Property.Number(label="Hop_3_Timer", configurable=True,
                    description="Restzeit in Minuten, bei der Slot 3 abwirft"),
    Property.Actor(label="Hop_4"),
    Property.Number(label="Hop_4_Timer", configurable=True,
                    description="Restzeit in Minuten, bei der Slot 4 abwirft"),
    Property.Actor(label="Hop_5"),
    Property.Number(label="Hop_5_Timer", configurable=True,
                    description="Restzeit in Minuten, bei der Slot 5 abwirft"),
])
class HopDropperStep(CBPiStep):
    """Kochschritt, der die Hopfengaben über den Hopdropper automatisch abwirft."""

    HOP_COUNT = 5

    async def NextStep(self, **kwargs):
        await self.next()

    async def on_timer_done(self, timer):
        self.summary = ""
        self.kettle.target_temp = 0
        await self.push_update()
        await self.next()

    async def on_timer_update(self, timer, seconds):
        self.summary = Timer.format_time(seconds)
        self.remaining_seconds = seconds
        for i in range(1, self.HOP_COUNT + 1):
            await self.check_hop_timer(i)
        await self.push_update()

    async def on_start(self):
        self.summary = "Warte auf Zieltemperatur"
        self.kettle = self.get_kettle(self.props.get("Kettle", None))
        self.remaining_seconds = None
        self.hops_added = [False] * self.HOP_COUNT
        if self.kettle is not None:
            self.kettle.target_temp = int(self.props.get("Temp", 0))
        if self.timer is None:
            self.timer = Timer(int(self.props.get("Timer", 0)) * 60,
                               on_update=self.on_timer_update,
                               on_done=self.on_timer_done)
        self.timer.is_running = False
        await self.push_update()

    async def on_stop(self):
        await self.timer.stop()
        self.summary = ""
        await self.push_update()

    async def reset(self):
        self.timer = Timer(int(self.props.get("Timer", 0)) * 60,
                           on_update=self.on_timer_update,
                           on_done=self.on_timer_done)
        self.hops_added = [False] * self.HOP_COUNT

    async def check_hop_timer(self, number):
        if self.hops_added[number - 1]:
            return
        value = self.props.get("Hop_%s_Timer" % number, None)
        actor = self.props.get("Hop_%s" % number, None)
        if value is None or value == "" or actor is None:
            return
        if self.remaining_seconds is not None and self.remaining_seconds <= (int(value) * 60 + 1):
            self.hops_added[number - 1] = True
            await self.cbpi.actor.on(actor)
            self.cbpi.notify("Hopdropper", "Hopfengabe %s abgeworfen" % number, NotificationType.INFO)

    async def run(self):
        while self.running:
            await asyncio.sleep(1)
            sensor_value = self.get_sensor_value(self.props.get("Sensor", None))
            if sensor_value is not None and sensor_value.get("value") is not None:
                if sensor_value.get("value") >= int(self.props.get("Temp", 0)) and self.timer.is_running is not True:
                    self.timer.start()
                    self.timer.is_running = True
        return StepResult.DONE


def setup(cbpi):
    """Registrierung der Plugin-Komponenten in CraftBeerPi 4."""
    cbpi.plugin.register("Hopdropper MQTT Actor", HopDropperActor)
    cbpi.plugin.register("Hopdropper Boil Step", HopDropperStep)
