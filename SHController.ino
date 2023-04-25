#include "Client.h"
#include "Storage.h"

// General
#define BAUD 115200 // Serial bit rate.

// Pins
#define BUTTON_BUILTIN 0
#define LM35_PIN 4
#define LDR_PIN 4

// Temperature
#define INTERVAL 3000

float temperature;
float tempSum;
float tempCount;
int deltaTime;

long lastTime;

// Light
float light;

// WiFi
char *ssid = "Redmi 9";                              // WiFi SSID.
char *password = "12345678";                         // WiFi password.
// #define HOST "http://192.168.232.3:5257/api/sensors" // Local server domain for testing.
#define HOST "https://shserver.onrender.com/api/"    // Server domain.
#define SENSORS_API "sensors"                        // Server domain.
#define CONTROLS_API "controls"                      // Server domain.
#define UPDATE_INTERVAL 10000                        // How often to send status updates to server.

void setup()
{
    // Setup pins.
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    pinMode(LM35_PIN, INPUT);
    pinMode(LDR_PIN, INPUT);

    // Indicate setup started.
    digitalWrite(LED_BUILTIN, HIGH);

    // Initialize serial.
    Serial.begin(BAUD);

    while (!Serial)
        delay(100);

    Serial.println("Setup started.");

    // Setup storage.
    InitializeSD();

    // TODO: Read config file, if no file found, enter BLE mode for first time config.
    // TODO: If SD mount failed config via BLE.

    // Connect to WiFi.
    connectWiFi(ssid, password);

    // Initialize PLX-DAQ.
    Serial.println("CLEARDATA");
    Serial.println("LABEL,Time,C");

    Serial.println("Setup finished.");
    Serial.println("--------------------------------------------------------");

    // Indicate setup finished.
    digitalWrite(LED_BUILTIN, LOW);
}

void loop()
{
    if (!digitalRead(BUTTON_BUILTIN))
    {
        // TODO: Enter config mode.
        return;
    }

    readLight();
    readTemp();

    String payload = "{\"temp\":" + String(temperature, 1) + ",\"light\":" + String(light, 1) + "}";
    Serial.println("Sending Data.");
    Serial.println(payload);

    // TODO: Save state as struct and sync it with one call, i.e. sendState.
    sendPutRequest(HOST + String(SENSORS_API), payload);

    Serial.print("Waiting for ");
    Serial.print(UPDATE_INTERVAL);
    Serial.println(" ms.");

    delay(UPDATE_INTERVAL);
}

// Not confirmed.

void readLight()
{
    light = (analogRead(LDR_PIN) / 4096.0) * 100.0;
    Serial.println("DATA,TIME," + String(light));
}

void readTemp()
{
    temperature = (analogRead(LM35_PIN) / 4096.0) * 330.0;
    Serial.println("DATA,TIME," + String(temperature));
}

void readTempAvg()
{
    deltaTime += (int)(millis() - lastTime);
    tempSum += (analogRead(LM35_PIN) / 4096.0) * 330.0;
    tempCount++;

    if (deltaTime >= INTERVAL)
    {
        temperature = tempSum / tempCount;

        deltaTime = 0;
        tempCount = 0;
        tempSum = 0;

        Serial.println("DATA,TIME," + (String)temperature);
    }

    lastTime = millis();
    delay(20);
}

int analogAvg(int pin, int samples)
{
    unsigned int total = 0;

    for (int n = 0; n < samples; n++)
        total += analogRead(pin);

    return total / samples;
}
