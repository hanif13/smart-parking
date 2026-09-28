# ============================================================
# SMART PARKING - PARKING ANALYTICS
#
# หน้าที่:
# 1. ตรวจการเปลี่ยนสถานะ FREE -> FULL
# 2. เริ่ม Parking Session
# 3. ตรวจ FULL -> FREE
# 4. จบ Parking Session
# 5. คำนวณเวลาจอด
# 6. เก็บสถิติรายวัน
#
# IMPORTANT:
# - ไม่ลบข้อมูลเดิม
# - ใช้ SQLite เดิม data/parking.db
# - ไม่บันทึกทุก polling
# - บันทึกเฉพาะตอนสถานะช่องเปลี่ยน
# ============================================================

import os
import sqlite3
import threading
import time
from datetime import datetime


# ============================================================
# PATH
# ============================================================

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DB_PATH = os.path.join(
    BASE_DIR,
    "data",
    "parking.db"
)


# ============================================================
# CONFIG
# ============================================================

POLL_INTERVAL = 1.0

SLOTS = ["P01", "P02", "P03", "P04"]


# ============================================================
# LOCK
# ============================================================

_db_lock = threading.Lock()

_analytics_thread = None
_stop_event = threading.Event()


# ============================================================
# DATABASE CONNECTION
# ============================================================

def get_connection():
    conn = sqlite3.connect(
        DB_PATH,
        timeout=10
    )

    conn.row_factory = sqlite3.Row

    return conn


# ============================================================
# INITIALIZE TABLES
# ============================================================

def init_analytics_database():

    os.makedirs(
        os.path.dirname(DB_PATH),
        exist_ok=True
    )

    with _db_lock:

        conn = get_connection()

        try:

            # ------------------------------------------------
            # Parking Sessions
            # ------------------------------------------------

            conn.execute("""
                CREATE TABLE IF NOT EXISTS parking_sessions (

                    id INTEGER PRIMARY KEY AUTOINCREMENT,

                    slot TEXT NOT NULL,

                    entry_time TEXT NOT NULL,

                    exit_time TEXT,

                    duration_minutes REAL,

                    status TEXT NOT NULL DEFAULT 'ACTIVE'

                )
            """)


            # ------------------------------------------------
            # Parking State
            #
            # เก็บสถานะล่าสุดของแต่ละช่อง
            # ------------------------------------------------

            conn.execute("""
                CREATE TABLE IF NOT EXISTS parking_slot_state (

                    slot TEXT PRIMARY KEY,

                    occupied INTEGER NOT NULL DEFAULT 0,

                    last_change TEXT NOT NULL,

                    active_session_id INTEGER

                )
            """)


            # ------------------------------------------------
            # Index
            # ------------------------------------------------

            conn.execute("""
                CREATE INDEX IF NOT EXISTS
                idx_parking_sessions_entry
                ON parking_sessions(entry_time)
            """)


            conn.execute("""
                CREATE INDEX IF NOT EXISTS
                idx_parking_sessions_exit
                ON parking_sessions(exit_time)
            """)


            conn.commit()

        finally:

            conn.close()


# ============================================================
# TIME
# ============================================================

def now_string():

    return datetime.now().strftime(
        "%Y-%m-%d %H:%M:%S"
    )


# ============================================================
# NORMALIZE SLOT
# ============================================================

def normalize_slot_name(slot):

    if isinstance(slot, int):

        return f"P{slot:02d}"

    text = str(slot).strip().upper()

    if text.startswith("S"):

        number = text[1:]

        if number.isdigit():

            return f"P{int(number):02d}"

    if text.startswith("P"):

        number = text[1:]

        if number.isdigit():

            return f"P{int(number):02d}"

    return text


# ============================================================
# START SESSION
# ============================================================

def start_session(slot):

    slot = normalize_slot_name(slot)

    timestamp = now_string()

    with _db_lock:

        conn = get_connection()

        try:

            cursor = conn.execute(
                """
                INSERT INTO parking_sessions
                (
                    slot,
                    entry_time,
                    status
                )
                VALUES (?, ?, 'ACTIVE')
                """,
                (
                    slot,
                    timestamp
                )
            )

            session_id = cursor.lastrowid

            conn.execute(
                """
                INSERT INTO parking_slot_state
                (
                    slot,
                    occupied,
                    last_change,
                    active_session_id
                )
                VALUES (?, 1, ?, ?)

                ON CONFLICT(slot)
                DO UPDATE SET
                    occupied = 1,
                    last_change = excluded.last_change,
                    active_session_id = excluded.active_session_id
                """,
                (
                    slot,
                    timestamp,
                    session_id
                )
            )

            conn.commit()

            print(
                f"[PARKING] {slot} -> FULL"
            )

            print(
                f"[PARKING] Session started #{session_id}"
            )

            return session_id

        finally:

            conn.close()


# ============================================================
# END SESSION
# ============================================================

def end_session(slot):

    slot = normalize_slot_name(slot)

    timestamp = now_string()

    with _db_lock:

        conn = get_connection()

        try:

            row = conn.execute(
                """
                SELECT
                    id,
                    entry_time
                FROM parking_sessions
                WHERE slot = ?
                  AND status = 'ACTIVE'
                ORDER BY id DESC
                LIMIT 1
                """,
                (slot,)
            ).fetchone()

            if row is None:

                print(
                    f"[PARKING] {slot} -> FREE "
                    f"(no active session)"
                )

                conn.execute(
                    """
                    INSERT INTO parking_slot_state
                    (
                        slot,
                        occupied,
                        last_change,
                        active_session_id
                    )
                    VALUES (?, 0, ?, NULL)

                    ON CONFLICT(slot)
                    DO UPDATE SET
                        occupied = 0,
                        last_change = excluded.last_change,
                        active_session_id = NULL
                    """,
                    (
                        slot,
                        timestamp
                    )
                )

                conn.commit()

                return None


            session_id = row["id"]

            entry_time = datetime.strptime(
                row["entry_time"],
                "%Y-%m-%d %H:%M:%S"
            )

            exit_time = datetime.strptime(
                timestamp,
                "%Y-%m-%d %H:%M:%S"
            )

            duration_seconds = (
                exit_time - entry_time
            ).total_seconds()

            duration_minutes = max(
                0,
                duration_seconds / 60
            )


            conn.execute(
                """
                UPDATE parking_sessions
                SET
                    exit_time = ?,
                    duration_minutes = ?,
                    status = 'COMPLETED'
                WHERE id = ?
                """,
                (
                    timestamp,
                    duration_minutes,
                    session_id
                )
            )


            conn.execute(
                """
                UPDATE parking_slot_state
                SET
                    occupied = 0,
                    last_change = ?,
                    active_session_id = NULL
                WHERE slot = ?
                """,
                (
                    timestamp,
                    slot
                )
            )


            conn.commit()


            print(
                f"[PARKING] {slot} -> FREE"
            )

            print(
                f"[PARKING] Session #{session_id}"
            )

            print(
                f"[PARKING] Duration: "
                f"{duration_minutes:.1f} minutes"
            )


            return {
                "session_id": session_id,
                "slot": slot,
                "entry_time": row["entry_time"],
                "exit_time": timestamp,
                "duration_minutes": duration_minutes
            }

        finally:

            conn.close()


# ============================================================
# PROCESS SLOT CHANGE
# ============================================================

def process_slot(slot, occupied):

    slot = normalize_slot_name(slot)

    occupied = bool(occupied)

    with _db_lock:

        conn = get_connection()

        try:

            row = conn.execute(
                """
                SELECT
                    occupied
                FROM parking_slot_state
                WHERE slot = ?
                """,
                (slot,)
            ).fetchone()

        finally:

            conn.close()


    # --------------------------------------------------------
    # ยังไม่เคยรู้สถานะช่องนี้
    # --------------------------------------------------------

    if row is None:

        timestamp = now_string()

        with _db_lock:

            conn = get_connection()

            try:

                conn.execute(
                    """
                    INSERT INTO parking_slot_state
                    (
                        slot,
                        occupied,
                        last_change,
                        active_session_id
                    )
                    VALUES (?, ?, ?, NULL)
                    """,
                    (
                        slot,
                        int(occupied),
                        timestamp
                    )
                )

                conn.commit()

            finally:

                conn.close()


        # ถ้าเปิดระบบมาแล้วช่องมีรถ
        # เราจะเริ่ม session ใหม่ ณ เวลาที่ระบบเริ่มรู้จัก
        if occupied:

            start_session(slot)

        return


    previous = bool(row["occupied"])


    # --------------------------------------------------------
    # ไม่มีการเปลี่ยนแปลง
    # --------------------------------------------------------

    if previous == occupied:

        return


    # --------------------------------------------------------
    # FREE -> FULL
    # --------------------------------------------------------

    if not previous and occupied:

        start_session(slot)

        return


    # --------------------------------------------------------
    # FULL -> FREE
    # --------------------------------------------------------

    if previous and not occupied:

        end_session(slot)

        return


# ============================================================
# NORMALIZE STATE
# ============================================================

def extract_slots(state):

    if not isinstance(state, dict):

        return None


    parking = state.get(
        "parking",
        {}
    )

    if not isinstance(parking, dict):

        parking = {}


    slots = parking.get("slots")

    if isinstance(slots, list):

        result = {}

        for index, item in enumerate(slots, start=1):

            slot_name = f"P{index:02d}"

            occupied = False

            if isinstance(item, dict):

                value = item.get("occupied")

                if value is None:
                    value = item.get("full")

                if value is None:
                    value = item.get("status")

                if isinstance(value, str):

                    occupied = (
                        value.upper()
                        in ("FULL", "OCCUPIED", "1", "TRUE")
                    )

                else:

                    occupied = bool(value)

            else:

                occupied = bool(item)

            result[slot_name] = occupied

        return result


    # --------------------------------------------------------
    # Alternative structure:
    #
    # parking:
    # S1 = 0
    # S2 = 1
    # --------------------------------------------------------

    result = {}

    for index in range(1, 5):

        keys = [
            f"S{index}",
            f"s{index}",
            f"P{index:02d}",
            f"p{index:02d}",
        ]

        value = None

        for key in keys:

            if key in parking:

                value = parking[key]

                break

            if key in state:

                value = state[key]

                break


        if value is None:

            continue


        if isinstance(value, str):

            occupied = (
                value.upper()
                in ("FULL", "OCCUPIED", "1", "TRUE")
            )

        else:

            occupied = bool(value)


        result[f"P{index:02d}"] = occupied


    if result:

        return result


    return None


# ============================================================
# ANALYTICS LOOP
# ============================================================

def analytics_loop():

    print(
        "[ANALYTICS] Parking analytics started"
    )


    while not _stop_event.is_set():

        try:

            # Import inside loop เพื่อป้องกัน circular import
            from app import serial_manager

            state = serial_manager.get_state()

            slots = extract_slots(state)

            if slots:

                for slot, occupied in slots.items():

                    process_slot(
                        slot,
                        occupied
                    )

        except Exception as error:

            print(
                f"[ANALYTICS] Error: {error}"
            )

        _stop_event.wait(
            POLL_INTERVAL
        )


# ============================================================
# START
# ============================================================

def start_analytics():

    global _analytics_thread

    init_analytics_database()

    if (
        _analytics_thread is not None
        and _analytics_thread.is_alive()
    ):

        return


    _stop_event.clear()

    _analytics_thread = threading.Thread(
        target=analytics_loop,
        daemon=True
    )

    _analytics_thread.start()


# ============================================================
# STOP
# ============================================================

def stop_analytics():

    _stop_event.set()


# ============================================================
# TODAY STATISTICS
# ============================================================

def get_today_stats():

    today = datetime.now().strftime(
        "%Y-%m-%d"
    )

    with _db_lock:

        conn = get_connection()

        try:

            completed = conn.execute(
                """
                SELECT
                    COUNT(*) AS count,
                    COALESCE(
                        AVG(duration_minutes),
                        0
                    ) AS avg_minutes,
                    COALESCE(
                        MAX(duration_minutes),
                        0
                    ) AS max_minutes
                FROM parking_sessions
                WHERE status = 'COMPLETED'
                  AND substr(exit_time, 1, 10) = ?
                """,
                (today,)
            ).fetchone()


            entries = conn.execute(
                """
                SELECT COUNT(*) AS count
                FROM parking_sessions
                WHERE substr(entry_time, 1, 10) = ?
                """,
                (today,)
            ).fetchone()


            active = conn.execute(
                """
                SELECT COUNT(*) AS count
                FROM parking_sessions
                WHERE status = 'ACTIVE'
                """,
            ).fetchone()


            return {
                "date": today,
                "cars_entered": entries["count"],
                "cars_exited": completed["count"],
                "currently_parked": active["count"],
                "average_minutes": round(
                    completed["avg_minutes"],
                    1
                ),
                "max_minutes": round(
                    completed["max_minutes"],
                    1
                )
            }

        finally:

            conn.close()


# ============================================================
# DAILY SUMMARY
# ============================================================

def get_daily_summary(days=7):

    days = max(
        1,
        min(int(days), 31)
    )

    with _db_lock:

        conn = get_connection()

        try:

            rows = conn.execute(
                """
                SELECT
                    substr(entry_time, 1, 10) AS date,
                    COUNT(*) AS cars_entered
                FROM parking_sessions
                WHERE entry_time >= datetime(
                    'now',
                    ?
                )
                GROUP BY substr(entry_time, 1, 10)
                ORDER BY date DESC
                """,
                (
                    f"-{days - 1} days",
                )
            ).fetchall()


            exited_rows = conn.execute(
                """
                SELECT
                    substr(exit_time, 1, 10) AS date,
                    COUNT(*) AS cars_exited,
                    COALESCE(
                        AVG(duration_minutes),
                        0
                    ) AS average_minutes,
                    COALESCE(
                        MAX(duration_minutes),
                        0
                    ) AS max_minutes
                FROM parking_sessions
                WHERE status = 'COMPLETED'
                  AND exit_time >= datetime(
                      'now',
                      ?
                  )
                GROUP BY substr(exit_time, 1, 10)
                """,
                (
                    f"-{days - 1} days",
                )
            ).fetchall()


            entered_map = {
                row["date"]: row["cars_entered"]
                for row in rows
            }

            exited_map = {
                row["date"]: row
                for row in exited_rows
            }


            all_dates = sorted(
                set(
                    entered_map.keys()
                ) |
                set(
                    exited_map.keys()
                ),
                reverse=True
            )


            result = []

            for date in all_dates:

                exited = exited_map.get(date)

                result.append({
                    "date": date,
                    "cars_entered": entered_map.get(
                        date,
                        0
                    ),
                    "cars_exited": (
                        exited["cars_exited"]
                        if exited else 0
                    ),
                    "average_minutes": round(
                        exited["average_minutes"],
                        1
                    ) if exited else 0,
                    "max_minutes": round(
                        exited["max_minutes"],
                        1
                    ) if exited else 0
                })


            return result

        finally:

            conn.close()


# ============================================================
# RECENT SESSIONS
# ============================================================

def get_recent_sessions(limit=10):

    limit = max(
        1,
        min(int(limit), 100)
    )

    with _db_lock:

        conn = get_connection()

        try:

            rows = conn.execute(
                """
                SELECT
                    id,
                    slot,
                    entry_time,
                    exit_time,
                    duration_minutes,
                    status
                FROM parking_sessions
                ORDER BY id DESC
                LIMIT ?
                """,
                (limit,)
            ).fetchall()


            return [
                dict(row)
                for row in rows
            ]

        finally:

            conn.close()


# ============================================================
# ACTIVE SESSIONS
# ============================================================

def get_active_sessions():

    with _db_lock:

        conn = get_connection()

        try:

            rows = conn.execute(
                """
                SELECT
                    id,
                    slot,
                    entry_time,
                    status
                FROM parking_sessions
                WHERE status = 'ACTIVE'
                ORDER BY entry_time ASC
                """
            ).fetchall()


            return [
                dict(row)
                for row in rows
            ]

        finally:

            conn.close()
