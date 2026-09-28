import threading
import time
import serial

from . import database


A1_PORT = "/dev/ttyACM0"
A2_PORT = "/dev/ttyACM1"
BAUD_RATE = 9600


class SerialManager:

    def __init__(self):
        self.running = False

        self.a1 = None
        self.a2 = None

        self.lock = threading.Lock()

        self.state = {
            "arduino1": {
                "online": False,
                "s1": 0,
                "s2": 0,
                "s3": 0,
                "s4": 0,
                "available": 4,
                "change_count": 0
            },

            "arduino2": {
                "online": False,
                "ir": 0,
                "gate": "CLOSED",
                "available": 4,
                "ir_count": 0,
                "open_count": 0,
                "close_count": 0
            }
        }

    # =====================================================
    # START
    # =====================================================

    def start(self):

        if self.running:
            return

        self.running = True

        print("[SERIAL] Connecting Arduino 1 ->", A1_PORT)
        print("[SERIAL] Connecting Arduino 2 ->", A2_PORT)
        print("[SERIAL] Serial manager started")
        print("[SERIAL] Arduino 1:", A1_PORT)
        print("[SERIAL] Arduino 2:", A2_PORT)

        threading.Thread(
            target=self.reader_a1,
            daemon=True
        ).start()

        threading.Thread(
            target=self.reader_a2,
            daemon=True
        ).start()

    # =====================================================
    # A1
    # =====================================================

    def reader_a1(self):

        while self.running:

            try:

                if self.a1 is None or not self.a1.is_open:

                    print(
                        "[SERIAL] Connecting Arduino 1 ->",
                        A1_PORT
                    )

                    self.a1 = serial.Serial(
                        A1_PORT,
                        BAUD_RATE,
                        timeout=1
                    )

                    print("[SERIAL] Arduino 1 CONNECTED")

                    with self.lock:
                        self.state["arduino1"]["online"] = True

                line = self.a1.readline().decode(
                    errors="ignore"
                ).strip()

                if line:
                    self.parse_a1(line)

            except Exception as e:

                with self.lock:
                    self.state["arduino1"]["online"] = False

                print("[SERIAL] Arduino 1 ERROR:", e)

                try:
                    if self.a1:
                        self.a1.close()
                except Exception:
                    pass

                self.a1 = None

                time.sleep(2)

    # =====================================================
    # A2
    # =====================================================

    def reader_a2(self):

        while self.running:

            try:

                if self.a2 is None or not self.a2.is_open:

                    print(
                        "[SERIAL] Connecting Arduino 2 ->",
                        A2_PORT
                    )

                    self.a2 = serial.Serial(
                        A2_PORT,
                        BAUD_RATE,
                        timeout=1
                    )

                    print("[SERIAL] Arduino 2 CONNECTED")

                    with self.lock:
                        self.state["arduino2"]["online"] = True

                line = self.a2.readline().decode(
                    errors="ignore"
                ).strip()

                if line:
                    self.parse_a2(line)

            except Exception as e:

                with self.lock:
                    self.state["arduino2"]["online"] = False

                print("[SERIAL] Arduino 2 ERROR:", e)

                try:
                    if self.a2:
                        self.a2.close()
                except Exception:
                    pass

                self.a2 = None

                time.sleep(2)

    # =====================================================
    # PARSE A1
    # =====================================================

    def parse_a1(self, line):

        if not line.startswith("A1,STATUS"):
            return

        try:

            parts = line.split(",")

            values = {}

            for item in parts[2:]:

                if "=" in item:

                    key, value = item.split("=", 1)
                    values[key.strip()] = value.strip()

            s1 = int(values.get("S1", 0))
            s2 = int(values.get("S2", 0))
            s3 = int(values.get("S3", 0))
            s4 = int(values.get("S4", 0))
            available = int(values.get("A", 4))
            change_count = int(values.get("CHG", 0))

            with self.lock:

                self.state["arduino1"] = {
                    "online": True,
                    "s1": s1,
                    "s2": s2,
                    "s3": s3,
                    "s4": s4,
                    "available": available,
                    "change_count": change_count
                }

            database.save_parking_status(
                s1,
                s2,
                s3,
                s4,
                available,
                change_count
            )

        except Exception as e:

            print("[SERIAL] A1 parse error:", e)

    # =====================================================
    # PARSE A2
    # =====================================================

    def parse_a2(self, line):

        if line.startswith("A2,STATUS"):

            try:

                parts = line.split(",")

                values = {}

                for item in parts[2:]:

                    if "=" in item:

                        key, value = item.split("=", 1)
                        values[key.strip()] = value.strip()

                ir = int(values.get("IR", 0))
                gate = values.get("GATE", "CLOSED")
                available = int(values.get("A", 4))
                ir_count = int(values.get("IRC", 0))
                open_count = int(values.get("OPEN", 0))
                close_count = int(values.get("CLOSE", 0))

                with self.lock:

                    self.state["arduino2"] = {
                        "online": True,
                        "ir": ir,
                        "gate": gate,
                        "available": available,
                        "ir_count": ir_count,
                        "open_count": open_count,
                        "close_count": close_count
                    }

                database.save_gate_status(
                    ir,
                    gate,
                    available,
                    ir_count,
                    open_count,
                    close_count
                )

            except Exception as e:

                print("[SERIAL] A2 parse error:", e)

            return

        # =================================================
        # GATE EVENTS
        # =================================================

        if line == "A2,EVENT,GATE_OPEN":

            database.save_event(
                "Arduino 2",
                "GATE_OPEN",
                "ไม้กั้นเปิด"
            )

            return

        if line == "A2,EVENT,GATE_CLOSE":

            database.save_event(
                "Arduino 2",
                "GATE_CLOSE",
                "ไม้กั้นปิด"
            )

            return

    # =====================================================
    # GET STATE
    # =====================================================

    def get_state(self):

        with self.lock:

            a1 = dict(self.state["arduino1"])
            a2 = dict(self.state["arduino2"])

        occupied = (
            a1["s1"]
            + a1["s2"]
            + a1["s3"]
            + a1["s4"]
        )

        return {
            "arduino1": a1,
            "arduino2": a2,

            "parking": {
                "s1": a1["s1"],
                "s2": a1["s2"],
                "s3": a1["s3"],
                "s4": a1["s4"],
                "available": a1["available"],
                "occupied": occupied
            },

            "gate": {
                "ir": a2["ir"],
                "gate": a2["gate"],
                "available": a2["available"],
                "ir_count": a2["ir_count"],
                "open_count": a2["open_count"],
                "close_count": a2["close_count"]
            }
        }

    # =====================================================
    # GET PARKING
    # =====================================================

    def get_parking(self):

        return self.get_state()["parking"]

    # =====================================================
    # GET GATE
    # =====================================================

    def get_gate(self):

        return self.get_state()["gate"]


# =========================================================
# SINGLETON
# =========================================================

serial_manager = SerialManager()


# =========================================================
# MODULE FUNCTIONS
# app.py เรียกใช้ตรงนี้
# =========================================================

def start_serial_manager():

    serial_manager.start()


def get_state():

    return serial_manager.get_state()


def get_parking():

    return serial_manager.get_parking()


def get_gate():

    return serial_manager.get_gate()
