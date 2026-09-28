import sqlite3
from datetime import datetime, timedelta
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = BASE_DIR / "data"
DB_PATH = DATA_DIR / "parking.db"

DATA_DIR.mkdir(parents=True, exist_ok=True)


def get_connection():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_database():
    conn = get_connection()
    cur = conn.cursor()

    cur.execute("""
        CREATE TABLE IF NOT EXISTS parking_status (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            s1 INTEGER NOT NULL,
            s2 INTEGER NOT NULL,
            s3 INTEGER NOT NULL,
            s4 INTEGER NOT NULL,
            available INTEGER NOT NULL,
            occupied INTEGER NOT NULL,
            change_count INTEGER DEFAULT 0
        )
    """)

    cur.execute("""
        CREATE TABLE IF NOT EXISTS gate_status (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            ir INTEGER NOT NULL,
            gate TEXT NOT NULL,
            available INTEGER NOT NULL,
            ir_count INTEGER DEFAULT 0,
            open_count INTEGER DEFAULT 0,
            close_count INTEGER DEFAULT 0
        )
    """)

    cur.execute("""
        CREATE TABLE IF NOT EXISTS events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            source TEXT NOT NULL,
            event_type TEXT NOT NULL,
            message TEXT
        )
    """)

    conn.commit()
    conn.close()


def now():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


# =========================================================
# PARKING
# =========================================================

def save_parking_status(
    s1,
    s2,
    s3,
    s4,
    available,
    change_count=0
):
    occupied = 4 - available

    conn = get_connection()
    cur = conn.cursor()

    cur.execute("""
        SELECT s1, s2, s3, s4, available
        FROM parking_status
        ORDER BY id DESC
        LIMIT 1
    """)

    old = cur.fetchone()

    changed = (
        old is None
        or old["s1"] != s1
        or old["s2"] != s2
        or old["s3"] != s3
        or old["s4"] != s4
        or old["available"] != available
    )

    if changed:
        cur.execute("""
            INSERT INTO parking_status (
                timestamp,
                s1,
                s2,
                s3,
                s4,
                available,
                occupied,
                change_count
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            now(),
            s1,
            s2,
            s3,
            s4,
            available,
            occupied,
            change_count
        ))

        conn.commit()

    conn.close()


def get_parking_history(limit=100):
    conn = get_connection()

    rows = conn.execute("""
        SELECT *
        FROM parking_status
        ORDER BY id DESC
        LIMIT ?
    """, (limit,)).fetchall()

    conn.close()

    return [dict(row) for row in rows]


# =========================================================
# GATE
# =========================================================

def save_gate_status(
    ir,
    gate,
    available,
    ir_count=0,
    open_count=0,
    close_count=0
):
    conn = get_connection()

    conn.execute("""
        INSERT INTO gate_status (
            timestamp,
            ir,
            gate,
            available,
            ir_count,
            open_count,
            close_count
        )
        VALUES (?, ?, ?, ?, ?, ?, ?)
    """, (
        now(),
        ir,
        gate,
        available,
        ir_count,
        open_count,
        close_count
    ))

    conn.commit()
    conn.close()


def get_gate_history(limit=100):
    conn = get_connection()

    rows = conn.execute("""
        SELECT *
        FROM gate_status
        ORDER BY id DESC
        LIMIT ?
    """, (limit,)).fetchall()

    conn.close()

    return [dict(row) for row in rows]


# =========================================================
# EVENTS
# =========================================================

def save_event(source, event_type, message=""):
    conn = get_connection()

    conn.execute("""
        INSERT INTO events (
            timestamp,
            source,
            event_type,
            message
        )
        VALUES (?, ?, ?, ?)
    """, (
        now(),
        source,
        event_type,
        message
    ))

    conn.commit()
    conn.close()


def get_events(limit=20):
    conn = get_connection()

    rows = conn.execute("""
        SELECT *
        FROM events
        ORDER BY id DESC
        LIMIT ?
    """, (limit,)).fetchall()

    conn.close()

    return [dict(row) for row in rows]


# =========================================================
# SUMMARY
# =========================================================

def get_summary():
    conn = get_connection()

    parking = conn.execute("""
        SELECT *
        FROM parking_status
        ORDER BY id DESC
        LIMIT 1
    """).fetchone()

    gate = conn.execute("""
        SELECT *
        FROM gate_status
        ORDER BY id DESC
        LIMIT 1
    """).fetchone()

    parking_count = conn.execute("""
        SELECT COUNT(*) AS count
        FROM parking_status
    """).fetchone()["count"]

    gate_count = conn.execute("""
        SELECT COUNT(*) AS count
        FROM gate_status
    """).fetchone()["count"]

    event_count = conn.execute("""
        SELECT COUNT(*) AS count
        FROM events
    """).fetchone()["count"]

    conn.close()

    return {
        "parking_records": parking_count,
        "gate_records": gate_count,
        "event_records": event_count,
        "latest_parking": dict(parking) if parking else None,
        "latest_gate": dict(gate) if gate else None
    }


# =========================================================
# RESET
# =========================================================

def reset_database():
    conn = get_connection()

    conn.execute("DELETE FROM parking_status")
    conn.execute("DELETE FROM gate_status")
    conn.execute("DELETE FROM events")

    conn.commit()
    conn.close()


# สร้าง Database ตอนเปิดโปรแกรม
init_database()
