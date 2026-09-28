"use strict";


/* ===================================================== */
/* HELPERS */
/* ===================================================== */

function $(id) {
    return document.getElementById(id);
}


function setText(id, value) {

    const element = $(id);

    if (element) {
        element.textContent = value;
    }

}


function setDeviceStatus(id, online) {

    const element = $(id);

    if (!element) {
        return;
    }

    element.classList.remove(
        "online",
        "offline"
    );

    if (online) {

        element.classList.add("online");
        element.textContent = "ONLINE";

    } else {

        element.classList.add("offline");
        element.textContent = "OFFLINE";

    }

}


/* ===================================================== */
/* PARKING SLOT */
/* ===================================================== */

function updateSlot(number, occupied) {

    const slot = $(`slot-${number}`);
    const status = $(`slot-${number}-status`);
    const car = $(`slot-${number}-car`);
    const empty = $(`slot-${number}-empty`);

    if (!slot || !status || !car || !empty) {
        return;
    }

    if (occupied) {

        slot.classList.remove("free");
        slot.classList.add("full");

        status.textContent = "FULL";

        car.classList.remove("hidden");
        empty.classList.add("hidden");

    } else {

        slot.classList.remove("full");
        slot.classList.add("free");

        status.textContent = "FREE";

        car.classList.add("hidden");
        empty.classList.remove("hidden");

    }

}


/* ===================================================== */
/* PARKING */
/* ===================================================== */

function updateParking(parking) {

    if (!parking) {
        return;
    }

    const s1 = Number(parking.s1 ?? 0);
    const s2 = Number(parking.s2 ?? 0);
    const s3 = Number(parking.s3 ?? 0);
    const s4 = Number(parking.s4 ?? 0);

    const available =
        Number(parking.available ?? 0);

    const occupied =
        Number(
            parking.occupied ??
            (s1 + s2 + s3 + s4)
        );

    updateSlot(1, s1 === 1);
    updateSlot(2, s2 === 1);
    updateSlot(3, s3 === 1);
    updateSlot(4, s4 === 1);

    setText(
        "available-count",
        available
    );

    setText(
        "occupied-count",
        occupied
    );

    setText(
        "header-available",
        available
    );

    setText(
        "header-occupied",
        occupied
    );

}


/* ===================================================== */
/* GATE */
/* ===================================================== */

function updateGate(gate) {

    if (!gate) {
        return;
    }

    const ir =
        Number(gate.ir ?? 0);

    const gateStatus =
        String(
            gate.gate ?? "CLOSED"
        ).toUpperCase();

    const isOpen =
        gateStatus === "OPEN";


    setText(
        "gate-status",
        isOpen ? "OPEN" : "CLOSED"
    );

    setText(
        "gate-detail",
        isOpen
            ? "ไม้กั้นกำลังเปิด"
            : "ไม้กั้นปิด"
    );


    setText(
        "ir-status",
        ir === 1
            ? "DETECTED"
            : "CLEAR"
    );

    setText(
        "ir-detail",
        ir === 1
            ? "พบรถบริเวณทางเข้า"
            : "ไม่พบรถ"
    );


    const badge =
        $("gate-badge");

    if (badge) {

        badge.classList.remove(
            "open",
            "closed"
        );

        badge.classList.add(
            isOpen
                ? "open"
                : "closed"
        );

        badge.textContent =
            isOpen
                ? "OPEN"
                : "CLOSED";

    }


    const arm =
        $("gate-arm");

    if (arm) {

        arm.classList.remove(
            "open",
            "closed"
        );

        arm.classList.add(
            isOpen
                ? "open"
                : "closed"
        );

    }


    setText(
        "gate-ir",
        ir === 1
            ? "CAR DETECTED"
            : "CLEAR"
    );


    setText(
        "gate-open-count",
        gate.open_count ?? 0
    );

    setText(
        "gate-close-count",
        gate.close_count ?? 0
    );


    const car =
        $("gate-car");

    if (car) {

        car.style.left =
            ir === 1
                ? "58%"
                : "20%";

    }

}


/* ===================================================== */
/* STATUS */
/* ===================================================== */

async function loadStatus() {

    try {

        const response =
            await fetch(
                "/api/status",
                {
                    cache: "no-store"
                }
            );

        if (!response.ok) {
            throw new Error(
                `HTTP ${response.status}`
            );
        }

        const data =
            await response.json();


        updateParking(
            data.parking
        );

        updateGate(
            data.gate
        );


        const a1 =
            data.arduino1?.online === true;

        const a2 =
            data.arduino2?.online === true;


        setDeviceStatus(
            "arduino1-status",
            a1
        );

        setDeviceStatus(
            "arduino2-status",
            a2
        );


        const systemOnline =
            a1 && a2;

        const dot =
            $("system-dot");

        const text =
            $("system-text");


        if (systemOnline) {

            dot?.classList.remove(
                "offline"
            );

            dot?.classList.add(
                "online"
            );

            if (text) {
                text.textContent =
                    "SYSTEM ONLINE";
            }

        } else {

            dot?.classList.remove(
                "online"
            );

            dot?.classList.add(
                "offline"
            );

            if (text) {
                text.textContent =
                    "SYSTEM CHECK";
            }

        }


        setText(
            "last-update",
            "Last update: " +
            new Date().toLocaleTimeString(
                "th-TH"
            )
        );


    } catch (error) {

        console.error(
            "Status error:",
            error
        );

        const dot =
            $("system-dot");

        const text =
            $("system-text");


        dot?.classList.remove(
            "online"
        );

        dot?.classList.add(
            "offline"
        );


        if (text) {
            text.textContent =
                "CONNECTION ERROR";
        }

    }

}


/* ===================================================== */
/* EVENT HELPERS */
/* ===================================================== */

function eventIcon(eventType) {

    switch (eventType) {

        case "GATE_OPEN":
            return "🔓";

        case "GATE_CLOSE":
            return "🔒";

        case "PARKING_CHANGE":
            return "🚗";

        default:
            return "ℹ️";

    }

}


function formatEventType(eventType) {

    switch (eventType) {

        case "GATE_OPEN":
            return "เปิดไม้กั้น";

        case "GATE_CLOSE":
            return "ปิดไม้กั้น";

        case "PARKING_CHANGE":
            return "สถานะช่องจอดเปลี่ยน";

        default:
            return eventType || "กิจกรรมระบบ";

    }

}


function formatTime(timestamp) {

    if (!timestamp) {
        return "-";
    }

    const date =
        new Date(
            String(timestamp)
                .replace(" ", "T")
        );


    if (
        Number.isNaN(
            date.getTime()
        )
    ) {
        return timestamp;
    }


    return date.toLocaleTimeString(
        "th-TH",
        {
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit"
        }
    );

}


/* ===================================================== */
/* EVENTS */
/* ===================================================== */

async function loadEvents() {

    try {

        const response =
            await fetch(
                "/api/events?limit=10",
                {
                    cache: "no-store"
                }
            );


        if (!response.ok) {
            throw new Error(
                `HTTP ${response.status}`
            );
        }


        const events =
            await response.json();


        const container =
            $("activity-list");


        if (!container) {
            return;
        }


        if (
            !Array.isArray(events) ||
            events.length === 0
        ) {

            container.innerHTML = `
                <div class="empty-activity">
                    ยังไม่มีกิจกรรม
                </div>
            `;

            return;

        }


        container.innerHTML =
            events.map(event => {

                return `
                    <div class="activity-item">

                        <div class="activity-icon">
                            ${eventIcon(event.event_type)}
                        </div>

                        <div class="activity-main">

                            <strong>
                                ${formatEventType(
                                    event.event_type
                                )}
                            </strong>

                            <span>
                                ${event.message || ""}
                            </span>

                        </div>

                        <div class="activity-time">
                            ${formatTime(
                                event.timestamp
                            )}
                        </div>

                    </div>
                `;

            }).join("");


    } catch (error) {

        console.error(
            "Events error:",
            error
        );

    }

}


/* ===================================================== */
/* DATABASE */
/* ===================================================== */

async function loadDatabase() {

    try {

        const response =
            await fetch(
                "/api/database",
                {
                    cache: "no-store"
                }
            );


        if (!response.ok) {
            throw new Error(
                `HTTP ${response.status}`
            );
        }


        const data =
            await response.json();


        setText(
            "parking-records",
            data.parking_records ?? 0
        );

        setText(
            "gate-records",
            data.gate_records ?? 0
        );

        setText(
            "event-records",
            data.event_records ?? 0
        );


        setDeviceStatus(
            "database-status",
            true
        );


    } catch (error) {

        console.error(
            "Database error:",
            error
        );

        setDeviceStatus(
            "database-status",
            false
        );

    }

}


/* ===================================================== */
/* ANALYTICS */
/* ===================================================== */

function formatMinutes(value) {

    const minutes =
        Number(value ?? 0);

    if (!Number.isFinite(minutes)) {
        return "-";
    }

    if (minutes <= 0) {
        return "0";
    }

    if (minutes < 1) {
        return minutes.toFixed(1);
    }

    if (Number.isInteger(minutes)) {
        return String(minutes);
    }

    return minutes.toFixed(1);

}


function formatDateThai(dateString) {

    if (!dateString) {
        return "-";
    }

    const parts =
        String(dateString).split("-");

    if (parts.length !== 3) {
        return dateString;
    }

    return `${parts[2]}/${parts[1]}/${parts[0]}`;

}


function formatSessionTime(timestamp) {

    if (!timestamp) {
        return "-";
    }

    return formatTime(timestamp);

}


/* ===================================================== */
/* TODAY ANALYTICS */
/* ===================================================== */

async function loadParkingStats() {

    try {

        const response =
            await fetch(
                "/api/parking/stats",
                {
                    cache: "no-store"
                }
            );


        if (!response.ok) {
            throw new Error(
                `HTTP ${response.status}`
            );
        }


        const data =
            await response.json();


        setText(
            "analytics-entered",
            data.cars_entered ?? 0
        );


        setText(
            "analytics-exited",
            data.cars_exited ?? 0
        );


        setText(
            "analytics-current",
            data.currently_parked ?? 0
        );


        setText(
            "analytics-average",
            formatMinutes(
                data.average_minutes
            )
        );


        setText(
            "analytics-max",
            formatMinutes(
                data.max_minutes
            )
        );


    } catch (error) {

        console.error(
            "Parking stats error:",
            error
        );

    }

}


/* ===================================================== */
/* DAILY SUMMARY */
/* ===================================================== */

async function loadDailySummary() {

    try {

        const response =
            await fetch(
                "/api/parking/daily?days=7",
                {
                    cache: "no-store"
                }
            );


        if (!response.ok) {
            throw new Error(
                `HTTP ${response.status}`
            );
        }


        const data =
            await response.json();


        const body =
            $("daily-summary-body");


        if (!body) {
            return;
        }


        if (
            !Array.isArray(data) ||
            data.length === 0
        ) {

            body.innerHTML = `
                <tr>
                    <td colspan="5">
                        ยังไม่มีข้อมูลการจอด
                    </td>
                </tr>
            `;

            return;

        }


        body.innerHTML =
            data.map(row => {

                return `
                    <tr>

                        <td>
                            ${formatDateThai(row.date)}
                        </td>

                        <td>
                            ${row.cars_entered ?? 0}
                        </td>

                        <td>
                            ${row.cars_exited ?? 0}
                        </td>

                        <td>
                            ${formatMinutes(
                                row.average_minutes
                            )}
                            นาที
                        </td>

                        <td>
                            ${formatMinutes(
                                row.max_minutes
                            )}
                            นาที
                        </td>

                    </tr>
                `;

            }).join("");


    } catch (error) {

        console.error(
            "Daily analytics error:",
            error
        );

    }

}


/* ===================================================== */
/* PARKING SESSIONS */
/* ===================================================== */

async function loadParkingSessions() {

    try {

        const response =
            await fetch(
                "/api/parking/sessions?limit=10",
                {
                    cache: "no-store"
                }
            );


        if (!response.ok) {
            throw new Error(
                `HTTP ${response.status}`
            );
        }


        const data =
            await response.json();


        const body =
            $("parking-sessions-body");


        if (!body) {
            return;
        }


        if (
            !Array.isArray(data) ||
            data.length === 0
        ) {

            body.innerHTML = `
                <tr>
                    <td colspan="5">
                        ยังไม่มีประวัติการจอด
                    </td>
                </tr>
            `;

            return;

        }


        body.innerHTML =
            data.map(session => {

                const active =
                    session.status === "ACTIVE";


                const statusText =
                    active
                        ? "กำลังจอด"
                        : "ออกแล้ว";


                const statusClass =
                    active
                        ? "session-active"
                        : "session-complete";


                const duration =
                    active
                        ? "กำลังนับ..."
                        : `${formatMinutes(
                            session.duration_minutes
                        )} นาที`;


                return `
                    <tr>

                        <td>
                            <strong>
                                P${String(
                                    session.slot
                                ).padStart(2, "0")}
                            </strong>
                        </td>

                        <td>
                            ${formatSessionTime(
                                session.entry_time
                            )}
                        </td>

                        <td>
                            ${active
                                ? "-"
                                : formatSessionTime(
                                    session.exit_time
                                )}
                        </td>

                        <td>
                            ${duration}
                        </td>

                        <td>
                            <span class="${statusClass}">
                                ${statusText}
                            </span>
                        </td>

                    </tr>
                `;

            }).join("");


    } catch (error) {

        console.error(
            "Parking sessions error:",
            error
        );

    }

}


/* ===================================================== */
/* REFRESH ANALYTICS */
/* ===================================================== */

async function loadAnalytics() {

    await loadParkingStats();
    await loadDailySummary();
    await loadParkingSessions();

}


/* ===================================================== */
/* START */
/* ===================================================== */

async function refreshAll() {

    await loadStatus();
    await loadEvents();
    await loadDatabase();
    await loadAnalytics();

}


/* ===================================================== */
/* FIRST LOAD */
/* ===================================================== */

refreshAll();


/* ===================================================== */
/* REAL-TIME */
/* ===================================================== */

setInterval(
    loadStatus,
    1000
);


setInterval(
    loadEvents,
    3000
);


setInterval(
    loadDatabase,
    5000
);


setInterval(
    loadAnalytics,
    3000
);
