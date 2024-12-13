#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Servo.h>
#include <WiFi.h>
#include <HttpClient.h>
#include <nvs.h>
#include <nvs_flash.h>

#define MOTOR_PIN 33
#define BUTTON_PIN 37
#define LIGHT_SENSOR_PIN 15

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

Servo motor;

char ssid[50];
char pass[50];
const char kHostname[] = "worldtimeapi.org";
const char kPath[] = "/api/timezone/America/Los_Angeles.txt";
const int kNetworkTimeout = 30 * 1000;
const int kNetworkDelay = 1000;

bool allowOperation = false;

// Callback class for handling Bluetooth writes
class MotorControlCallbacks : public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		std::string value = pCharacteristic->getValue();
		if (value == "on") {
			motor.write(180); // Move motor to 180 degrees
			Serial.println("Motor moved to 180 degrees via Bluetooth");
		} else {
			Serial.println("Invalid Bluetooth command received");
		}
	}
};

void nvs_access() {
	// Initialize NVS
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	nvs_handle_t my_handle;
	err = nvs_open("storage", NVS_READWRITE, &my_handle);
	if (err == ESP_OK) {
		size_t ssid_len;
		size_t pass_len;
		err = nvs_get_str(my_handle, "ssid", ssid, &ssid_len);
		err |= nvs_get_str(my_handle, "pass", pass, &pass_len);
		nvs_close(my_handle);
		if (err != ESP_OK) {
			Serial.println("Failed to retrieve WiFi credentials");
		}
	} else {
		Serial.println("Error opening NVS");
	}
}

void setup() {
	Serial.begin(9600);
	
	motor.attach(MOTOR_PIN);
	motor.write(0); // Ensure motor starts at 0 degrees

	pinMode(BUTTON_PIN, INPUT_PULLUP);
	pinMode(LIGHT_SENSOR_PIN, INPUT);

	nvs_access();
	WiFi.begin(ssid, pass);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("\nWiFi connected");

	// Initialize BLE
	BLEDevice::init("ESP32 Motor Controller");
	BLEServer *pServer = BLEDevice::createServer();
	BLEService *pService = pServer->createService(SERVICE_UUID);
	BLECharacteristic *pCharacteristic = pService->createCharacteristic(
		CHARACTERISTIC_UUID,
		BLECharacteristic::PROPERTY_READ |
		BLECharacteristic::PROPERTY_WRITE
	);

	pCharacteristic->setCallbacks(new MotorControlCallbacks());
	pCharacteristic->setValue("Motor Control");
	pService->start();

	BLEAdvertising *pAdvertising = pServer->getAdvertising();
	pAdvertising->start();

	Serial.println("ESP32 Motor Controller ready.");
}

void fetchTime() {
	WiFiClient client;
	HttpClient http(client);
	int err = http.get(kHostname, kPath);
	if (err == 0) {
		err = http.responseStatusCode();
		if (err >= 0) {
			http.skipResponseHeaders();
			String response = "";
			while (http.available()) {
				response += (char)http.read();
			}
			if (response.indexOf("datetime:") != -1) {
				int timeIndex = response.indexOf("datetime:") + 9;
				String timeStr = response.substring(timeIndex, timeIndex + 19);
				int hour = timeStr.substring(11, 13).toInt();
				int minute = timeStr.substring(14, 16).toInt();
				allowOperation = (hour > 23 || (hour == 23 && minute >= 30));
				Serial.printf("Current time: %02d:%02d\n", hour, minute);
				Serial.printf("Operation allowed: %s\n", allowOperation ? "YES" : "NO");
			}
		}
	}
	http.stop();
}

void loop() {
	// Fetch time periodically
	static unsigned long lastTimeFetch = 0;
	if (millis() - lastTimeFetch > 60000) { // Every 60 seconds
		fetchTime();
		lastTimeFetch = millis();
	}

	if (allowOperation) {
		// Check if the button is pressed
		if (digitalRead(BUTTON_PIN) == HIGH) {
			motor.write(180); // Move motor to 180 degrees
			Serial.println("Motor moved to 180 degrees via button");
			delay(500);
		}

		// Check light sensor value
		int lightLevel = analogRead(LIGHT_SENSOR_PIN);
		if (lightLevel < 10) {
			motor.write(180); // Move motor to 180 degrees
			Serial.println("Motor moved to 180 degrees due to low light level");
		} else {
			motor.write(0); // Move motor back to 0 degrees
			Serial.println("Motor reset to 0 degrees due to sufficient light level");
		}
	}

	delay(100);
}
