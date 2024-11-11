import firebase_admin
from firebase_admin import credentials, db, auth
import time
import requests
import Adafruit_DHT
from ina219 import INA219
from ina219 import DeviceRangeError
import RPi.GPIO as GPIO
import os

# Insert your network and Firebase credentials here
WIFI_SSID = "REPLACE_WITH_YOUR_SSID"
WIFI_PASSWORD = "REPLACE_WITH_YOUR_PASSWORD"
API_KEY = "REPLACE_WITH_YOUR_PROJECT_API_KEY"
USER_EMAIL = "REPLACE_WITH_THE_USER_EMAIL"
USER_PASSWORD = "REPLACE_WITH_THE_USER_PASSWORD"
DATABASE_URL = "REPLACE_WITH_YOUR_DATABASE_URL"

# Set Firebase credentials
cred_path = "/path/to/your-firebase-adminsdk.json"
cred = credentials.Certificate(cred_path)
firebase_admin.initialize_app(cred, {'databaseURL': DATABASE_URL})

# Sensor and GPIO configuration
DHT_SENSOR = Adafruit_DHT.DHT11
DHT_PIN = 33
I2C_ADDRESS = 0x40
OUTPUT_PINS = [12, 13, 14]
timer_delay = 180  # delay in seconds

# INA219 setup
ina = INA219(shunt_ohms=0.35, max_expected_amps=2.0, address=I2C_ADDRESS)
ina.configure(voltage_range=ina.RANGE_16V)

# Setup GPIO
GPIO.setmode(GPIO.BCM)
GPIO.setup(DHT_PIN, GPIO.IN)
for pin in OUTPUT_PINS:
    GPIO.setup(pin, GPIO.OUT)

# Function to get NTP time
def get_ntp_time():
    try:
        response = requests.get("http://worldtimeapi.org/api/timezone/Etc/UTC")
        if response.ok:
            datetime_str = response.json()["utc_datetime"]
            return int(time.mktime(time.strptime(datetime_str, "%Y-%m-%dT%H:%M:%S.%fZ")))
        return int(time.time())
    except Exception as e:
        print(f"Error getting NTP time: {e}")
        return int(time.time())

# Function to read DHT11 sensor
def read_dht_sensor():
    humidity, temperature = Adafruit_DHT.read(DHT_SENSOR, DHT_PIN)
    if humidity is None or temperature is None:
        print("DHT11 sensor read failed")
        return None, None
    return humidity, temperature - 5  # Adjust for calibration if needed

# Function to read INA219 sensor
def read_ina_sensor():
    try:
        shunt_voltage = ina.shunt_voltage()  # mV
        bus_voltage = ina.voltage()  # V
        current = ina.current()  # mA
        load_voltage = bus_voltage + (shunt_voltage / 1000)
        return load_voltage
    except DeviceRangeError as e:
        print(f"INA219 read error: {e}")
        return None

# Callback function for Firebase stream
def stream_callback(event):
    print("Received data change:", event.data)
    if isinstance(event.data, dict):
        for pin, state in event.data.items():
            GPIO.output(int(pin), int(state))
    elif event.data:
        GPIO.output(int(event.path.strip("/")), int(event.data))

# Main setup for Firebase stream
def init_firebase_stream():
    db.reference("board1/outputs/digital").listen(stream_callback)

# Main monitoring and data upload function
def main_loop():
    last_send_time = time.time()
    
    while True:
        # Read sensors
        humidity, temperature = read_dht_sensor()
        load_voltage = read_ina_sensor()
        
        # Check WiFi connection (omitted here, as WiFi reconnection is device-specific)

        # Send data to Firebase if it's time
        if time.time() - last_send_time > timer_delay:
            last_send_time = time.time()
            timestamp = get_ntp_time()

            if humidity is not None and temperature is not None and load_voltage is not None:
                # Create JSON payload
                data = {
                    "temperature": temperature,
                    "humidity": humidity,
                    "voltage": load_voltage,
                    "timestamp": timestamp,
                }

                # Update Firebase
                ref_path = f"/UsersData/{auth.get_user_by_email(USER_EMAIL).uid}/readings/{timestamp}"
                db.reference(ref_path).set(data)
                print("Data uploaded to Firebase:", data)
                
            else:
                print("Failed to read sensors. Retrying...")

        # Refresh Firebase token if needed (handled automatically by Firebase Admin SDK)
        time.sleep(1)

# Initialization
if __name__ == "__main__":
    init_firebase_stream()
    main_loop()
