"""
EcoFarm AI - Web + Mobile Live Dashboard
Arduino UNO Q - MPU (Linux) side

Reads soil zones, pump/LED/rain/motion/light/environment data, and LoRa
status from the MCU sketch via Bridge.call(), and streams it in real
time to any browser (desktop or mobile) connected to the WebUI over
WebSocket (Socket.IO).

Supports AUTO mode (sensor-based) and MANUAL override for both the
pump and the red LED, a manual buzzer test trigger, and a mobile
camera-based plant leaf health scan (rule-based edge analysis, inlined
below to avoid any module-import/bundling issues).
"""
import base64
import io
import threading
import time

from PIL import Image

from arduino.app_utils import App, Bridge
from arduino.app_bricks.web_ui import WebUI

POLL_INTERVAL_SECONDS = 2


# ---------------------------------------------------------------------------
# Plant Leaf Health Analyzer (rule-based edge analysis)
#
# Looks at the ratio of healthy-green vs yellow/brown pixels in a leaf photo
# to flag possible early stress or disease symptoms. Runs instantly
# on-device, no GPU/model needed.
# ---------------------------------------------------------------------------
def analyze_leaf_image(image_data_url):
    header_removed = image_data_url.split(",")[-1]
    image_bytes = base64.b64decode(header_removed)

    img = Image.open(io.BytesIO(image_bytes)).convert("RGB")
    img = img.resize((100, 100))

    pixels = list(img.getdata())
    total = len(pixels)
    green_count = 0
    yellow_brown_count = 0

    for r, g, b in pixels:
        if g > r and g > b and g > 60:
            green_count += 1
        elif r > 100 and g > 80 and b < 100 and r >= g:
            yellow_brown_count += 1

    green_ratio = green_count / total
    yellow_brown_ratio = yellow_brown_count / total

    if yellow_brown_ratio > 0.35:
        label = "Diseased"
        confidence = min(95, int(yellow_brown_ratio * 150))
        advice = (
            "Significant yellow/brown patches detected. Possible early "
            "blight or nutrient stress. Consider a neem oil spray and "
            "check irrigation in this zone."
        )
    elif yellow_brown_ratio > 0.15:
        label = "Stressed"
        confidence = min(85, int(yellow_brown_ratio * 200))
        advice = (
            "Some discoloration detected. Not critical yet — monitor "
            "closely over the next 2-3 days."
        )
    else:
        label = "Healthy"
        confidence = min(97, int(green_ratio * 120))
        advice = "Leaf looks healthy. Continue regular care and watering."

    return {
        "label": label,
        "confidence_percent": confidence,
        "advice": advice,
        "green_ratio": round(green_ratio * 100, 1),
        "yellow_brown_ratio": round(yellow_brown_ratio * 100, 1),
    }


# ---------------------------------------------------------------------------
# Sensor / Bridge data
# ---------------------------------------------------------------------------
def _safe_call(name, default=None):
    """
    Wraps Bridge.call() so a single flaky/slow MCU read never crashes the
    whole broadcast loop or blanks out the rest of the dashboard.
    """
    try:
        return Bridge.call(name)
    except Exception as e:
        print(f"Bridge.call('{name}') failed: {e}")
        return default


def read_all_data():
    """Pull the latest values from the MCU sketch via Bridge.
    Every key here maps 1:1 to a Bridge.provide() in sketch.ino."""
    return {
        "zone1": _safe_call("get_zone1", 0),
        "zone2": _safe_call("get_zone2", 0),
        "zone3": _safe_call("get_zone3", 0),
        "status": _safe_call("get_status", "Unknown"),
        "pump": _safe_call("get_pump", False),
        "manual_mode": _safe_call("get_manual_mode", False),
        "led": _safe_call("get_red_led", False),
        "led_manual_mode": _safe_call("get_led_manual_mode", False),
        "rain": _safe_call("get_rain", False),
        "motion": _safe_call("get_motion", False),
        "light_percent": _safe_call("get_light", 0),
        "air_temp": _safe_call("get_temperature", None),
        "pressure": _safe_call("get_pressure", None),
        "humidity": _safe_call("get_humidity", None),
        "soil_temp": _safe_call("get_soil_temperature", None),
        "lora_ready": _safe_call("get_lora_ready", False),
    }


def broadcast_loop():
    """Runs in the background, pushing fresh data to every connected client."""
    while True:
        try:
            data = read_all_data()
            ui.send_message("status", data)
        except Exception as e:
            print(f"Error reading Bridge data: {e}")
        time.sleep(POLL_INTERVAL_SECONDS)


def on_get_state(client, data):
    """Sent immediately when a browser (web or mobile) first connects."""
    try:
        ui.send_message("status", read_all_data(), client)
    except Exception as e:
        print(f"Error on get_state: {e}")


# --- Pump manual controls ---
def on_pump_on(client, data):
    Bridge.call("set_pump_manual", True)
    print("Pump forced ON (manual)")
    ui.send_message("status", read_all_data())


def on_pump_off(client, data):
    Bridge.call("set_pump_manual", False)
    print("Pump forced OFF (manual)")
    ui.send_message("status", read_all_data())


def on_auto_mode(client, data):
    Bridge.call("set_auto_mode")
    print("Pump back to AUTO")
    ui.send_message("status", read_all_data())


# --- Red LED manual controls ---
def on_led_on(client, data):
    Bridge.call("set_led_manual", True)
    print("Red LED forced ON (manual)")
    ui.send_message("status", read_all_data())


def on_led_off(client, data):
    Bridge.call("set_led_manual", False)
    print("Red LED forced OFF (manual)")
    ui.send_message("status", read_all_data())


def on_led_auto(client, data):
    Bridge.call("set_led_auto")
    print("Red LED back to AUTO")
    ui.send_message("status", read_all_data())


# --- Buzzer test ---
def on_test_buzzer(client, data):
    Bridge.call("test_buzzer")
    print("Buzzer test triggered")


# --- Mobile camera: plant leaf health scan ---
def on_analyze_photo(client, data):
    """
    data = { "image": "data:image/jpeg;base64,...." }
    Runs the leaf photo through analyze_leaf_image() and sends the result
    back. If the leaf looks diseased/stressed, also triggers the board's
    alert Bridge function (LED matrix alert animation).
    """
    try:
        image_data_url = data.get("image", "")
        result = analyze_leaf_image(image_data_url)
        print(f"Leaf scan result: {result['label']} ({result['confidence_percent']}%)")

        ui.send_message("photo_result", result, client)

        if result["label"] in ("Diseased", "Stressed"):
            Bridge.call("trigger_alert", True)
    except Exception as e:
        print(f"Photo analysis error: {e}")
        ui.send_message(
            "photo_result",
            {
                "label": "Error",
                "confidence_percent": 0,
                "advice": "Could not analyze that photo. Please try again with better lighting.",
                "green_ratio": 0,
                "yellow_brown_ratio": 0,
            },
            client,
        )


ui = WebUI()
ui.on_message("get_state", on_get_state)
ui.on_message("pump_on", on_pump_on)
ui.on_message("pump_off", on_pump_off)
ui.on_message("auto_mode", on_auto_mode)
ui.on_message("led_on", on_led_on)
ui.on_message("led_off", on_led_off)
ui.on_message("led_auto", on_led_auto)
ui.on_message("test_buzzer", on_test_buzzer)
ui.on_message("analyze_photo", on_analyze_photo)

# Background thread keeps pushing live updates without blocking the app
threading.Thread(target=broadcast_loop, daemon=True).start()

App.run()
