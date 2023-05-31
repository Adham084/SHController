#include "Client.h"
#include "Storage.h"
#include "DHT.h"
#include "Arduino_JSON.h"

// Notes
// RAM 320 KB
// ROM 1.25 MB

// General
#define DEBUG false
#define BAUD 115200 // Serial bit rate.

// Pins
#define LDR_PIN 36
#define BUZZER_PIN 17
#define DHT11_PIN 5
#define IR_PIN 18
#define LED_0_PIN 23   // Room 1 LED
#define LED_1_PIN 22   // Room 2 LED
#define LED_2_PIN 1    // Garage LED
#define LED_3_PIN 3    // Fence LEDs
#define MOTOR_0_PIN 39 // Fan Motor
#define MOTOR_1_PIN 34 // Garage Door Motor
#define ML_PB_0_PIN 35 // Garage Door Motor Left Limit Button
#define ML_PB_1_PIN 32 // Garage Door Motor Right Limit Button
#define BUTTON_BUILTIN 0
// LED_BUILTIN 2

// DHT11
DHT dht11(DHT11_PIN, DHT11);
// float humidity;
// float temperature;
// float heatIndex;

float temperatureFan = 28;
float temperatureAlarm = 45;

// LDR
// float light;
float lightThreshold = 50;

// IR
int doorDelay = 60000;

// WiFi
char *ssid = "Redmi 9";      // WiFi SSID.
char *password = "12345678"; // WiFi password.
#if DEBUG
#define HOST "http://192.168.232.3:5257/api/" // Local server domain for testing.
#else
#define HOST "https://shserver.onrender.com/api/" // Server domain.
#endif

#define SENSORS_API HOST + String("sensors")   // Sensors API URL.
#define CONTROLS_API HOST + String("controls") // Controls API URL.
#define UPDATE_INTERVAL 5000                   // How often to send status updates to server in ms.

// Private State
JSONVar controls;
JSONVar sensors;
long lastSendTime;
long irStartDelay;

hw_timer_t *Timer0_Cfg = NULL;

void setup()
{
  // Setup built-in functional pins.
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_BUILTIN, INPUT);

  // Indicate setup started.
  digitalWrite(LED_BUILTIN, HIGH);

  // Setup pins.
  pinMode(LDR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(DHT11_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(LED_0_PIN, OUTPUT);
  pinMode(LED_1_PIN, OUTPUT);
  pinMode(LED_2_PIN, OUTPUT);
  pinMode(LED_3_PIN, OUTPUT);
  pinMode(MOTOR_0_PIN, OUTPUT);
  pinMode(MOTOR_1_PIN, OUTPUT);
  pinMode(ML_PB_0_PIN, INPUT);
  pinMode(ML_PB_1_PIN, INPUT);

  // Initialize hardware timer.
  Timer0_Cfg = timerBegin(0, 80, true);
  timerAttachInterrupt(Timer0_Cfg, &irTimerElapsed, true);
  // Assume clock rate of 80 MHz and divider of 80, results in 1000000 Hz,
  // divide it by 1000 to get it in ms.
  timerAlarmWrite(Timer0_Cfg, 1000 * doorDelay, false);
  timerAlarmEnable(Timer0_Cfg);

  // Initialize DHT11 sensor.
  dht11.begin();

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

  readLDR();
  readDHT11();

  if (millis() - lastSendTime >= UPDATE_INTERVAL)
  {
    Serial.println("Sending Data.");

    sensors["SDFree"] = sdSpace();

    // controls["RoomLight"] =
    //     controls["GarageLight"] =
    //         controls["HallLight"] =
    //             controls["FenceLight"] =
    //                 controls["GarageDoor"] =
    //                     controls["Fan"] =
    //                         controls["Alarm"] =

    String sensorsPayload = JSON.stringify(sensors);
    String controlsPayload = JSON.stringify(controls);

    Serial.println(sensorsPayload);
    Serial.println(controlsPayload);

    sendPutRequest(SENSORS_API, sensorsPayload);
    sendPutRequest(CONTROLS_API, controlsPayload);

    Serial.print("Waiting for ");
    Serial.print(UPDATE_INTERVAL);
    Serial.println(" ms.");

    lastSendTime = millis();
  }
}

void readLDR()
{
  float light = (analogRead(LDR_PIN) / 4096.0) * 100.0;

  sensors["Light"] = light;

  if (light < lightThreshold)
  {
    digitalWrite(LED_3_PIN, HIGH);
  }
  else
  {
    digitalWrite(LED_3_PIN, LOW);
  }
}

void readDHT11()
{
  float temperature = dht11.readTemperature();
  float humidity = dht11.readHumidity();
  float heatIndex = dht11.computeHeatIndex(temperature, humidity, false);

  sensors["Temperature"] = temperature;
  sensors["Humidity"] = humidity;
  sensors["HeatIndex"] = heatIndex;

  if (temperature > temperatureFan)
  {
    digitalWrite(MOTOR_0_PIN, HIGH);
  }
  else
  {
    digitalWrite(MOTOR_0_PIN, LOW);
  }

  if (temperature > temperatureAlarm)
  {
    digitalWrite(BUZZER_PIN, HIGH);
  }
  else
  {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void readIR()
{
  int ir = digitalRead(IR_PIN);

  sensors["IR"] = ir;

  if (ir)
  {
    // Open the door and turn on the led.
    
    timerRestart(Timer0_Cfg);
    timerStart(Timer0_Cfg);
  }
}

void irTimerElapsed()
{
  timerStop(Timer0_Cfg);
  
  // Close the door and turn off the led.
}
