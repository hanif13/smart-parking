from flask import Flask, jsonify, render_template, request

from . import database
from . import serial_manager
from . import parking_analytics


app = Flask(__name__)


# =========================================================
# HOME
# =========================================================

@app.route("/")
def index():
    return render_template("dashboard.html")


# =========================================================
# STATUS
# =========================================================

@app.route("/api/status")
def api_status():
    return jsonify(
        serial_manager.get_state()
    )


# =========================================================
# PARKING
# =========================================================

@app.route("/api/parking")
def api_parking():
    return jsonify(
        serial_manager.get_parking()
    )


# =========================================================
# GATE
# =========================================================

@app.route("/api/gate")
def api_gate():
    return jsonify(
        serial_manager.get_gate()
    )


# =========================================================
# EVENTS
# =========================================================

@app.route("/api/events")
def api_events():
    limit = request.args.get(
        "limit",
        default=10,
        type=int
    )

    return jsonify(
        database.get_events(limit)
    )


# =========================================================
# DATABASE
# =========================================================

@app.route("/api/database")
def api_database():
    return jsonify(
        database.get_summary()
    )


# =========================================================
# RESET DATABASE
# =========================================================

@app.route("/api/database/reset", methods=["POST"])
def api_database_reset():
    database.reset_database()

    return jsonify({
        "success": True,
        "message": "Database reset successfully"
    })


# =========================================================
# ANALYTICS
# =========================================================
#
# สถิติการจอดรถ
#
# FREE -> FULL
#     = เริ่มต้นการจอด 1 ครั้ง
#
# FULL -> FREE
#     = สิ้นสุดการจอด
#     = คำนวณระยะเวลาที่จอด
#
# =========================================================

@app.route("/api/analytics")
def api_analytics():
    summary = database.get_summary()

    return jsonify({
        "enabled": True,
        "message": "Parking analytics enabled",
        "summary": summary
    })


# =========================================================
# PARKING TODAY STATS
# =========================================================

@app.route("/api/parking/stats")
def api_parking_stats():
    return jsonify(
        parking_analytics.get_today_stats()
    )


# =========================================================
# DAILY PARKING SUMMARY
# =========================================================

@app.route("/api/parking/daily")
def api_parking_daily():
    days = request.args.get(
        "days",
        default=7,
        type=int
    )

    # ป้องกันการส่งค่ามากเกินไป
    if days < 1:
        days = 1

    if days > 31:
        days = 31

    return jsonify(
        parking_analytics.get_daily_summary(days)
    )


# =========================================================
# RECENT PARKING SESSIONS
# =========================================================

@app.route("/api/parking/sessions")
def api_parking_sessions():
    limit = request.args.get(
        "limit",
        default=10,
        type=int
    )

    if limit < 1:
        limit = 1

    if limit > 100:
        limit = 100

    return jsonify(
        parking_analytics.get_recent_sessions(limit)
    )


# =========================================================
# CURRENT ACTIVE PARKING SESSIONS
# =========================================================

@app.route("/api/parking/active")
def api_parking_active():
    return jsonify(
        parking_analytics.get_active_sessions()
    )


# =========================================================
# HEALTH
# =========================================================

@app.route("/api/health")
def api_health():
    state = serial_manager.get_state()

    return jsonify({
        "status": "ok",
        "arduino1": state["arduino1"]["online"],
        "arduino2": state["arduino2"]["online"],
        "database": True
    })


# =========================================================
# MAIN
# =========================================================

def main():

    # -----------------------------------------------------
    # Start Arduino Serial Manager
    # -----------------------------------------------------

    serial_manager.start_serial_manager()

    # -----------------------------------------------------
    # Start Parking Analytics
    # -----------------------------------------------------

    parking_analytics.init_analytics_database()
    parking_analytics.start_analytics()

    print()
    print("========================================")
    print("       SMART PARKING SYSTEM")
    print("========================================")
    print("Dashboard : http://0.0.0.0:5000")
    print("Arduino 1 : /dev/ttyACM0")
    print("Arduino 2 : /dev/ttyACM1")
    print("Analytics : ENABLED")
    print("========================================")
    print()

    # -----------------------------------------------------
    # Start Flask
    # -----------------------------------------------------

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False,
        threaded=True
    )


# =========================================================
# ENTRY POINT
# =========================================================

if __name__ == "__main__":
    main()
