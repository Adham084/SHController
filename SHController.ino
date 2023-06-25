#include "ezButton.h"
#include "Arduino_JSON.h"
#include "Storage.h"
#include "Client.h"
#include "DHT.h"
#include "time.h"

// Notes ======================================================================================
// RAM 320 KB
// ROM 1.25 MB

// Definitions ================================================================================
// General
#define DEBUG          false
#define BAUD           115200          // Serial bit rate.
#define SECS_IN_DAY    86400           // 24 * 60 * 60

// Files
#define SCHEDULES_FILE "/schedules.json"
#define CONFIG_FILE    "/config.json"

// Pins
// Output
#define BUZZER_PIN     GPIO_NUM_17
#define LED_0_PIN      GPIO_NUM_23     // Room LED
#define LED_1_PIN      GPIO_NUM_22     // Hall LED
#define LED_2_PIN      GPIO_NUM_21     // Garage LED
#define LED_3_PIN      GPIO_NUM_19     // Fence LEDs
#define MOTOR_0_PIN    GPIO_NUM_27     // Fan Motor
#define MD_0_PIN       GPIO_NUM_33     // Garage Door Motor Right Direction
#define MD_1_PIN       GPIO_NUM_32     // Garage Door Motor Left Direction
// Input
#define ML_PB_1_PIN    GPIO_NUM_26     // Garage Door Motor Right Limit Button
#define ML_PB_0_PIN    GPIO_NUM_25     // Garage Door Motor Left Limit Button
#define LDR_PIN        GPIO_NUM_39
#define IR_PIN         GPIO_NUM_18
#define DHT11_PIN      GPIO_NUM_5
#define BUTTON_BUILTIN GPIO_NUM_0
// Note LED_BUILTIN    GPIO_NUM_2

// Variables ==================================================================================
// Components Objects
ezButton builtinBtn(BUTTON_BUILTIN);
ezButton ml0Btn(ML_PB_0_PIN);
ezButton ml1Btn(ML_PB_1_PIN);
DHT dht11(DHT11_PIN, DHT11);

// Private State
JSONVar config;
JSONVar controls;
JSONVar sensors;
JSONVar schedules;
long lastSendTime;
long irStartDelay;
long startMS;
long startEpoch;
int *nextCell;

// Hardware Handles
hw_timer_t *closeDoorTimer;
hw_timer_t *syncTimer;
hw_timer_t *syncTimer;
TaskHandle_t motorStopOnLimitTask;

// Constants
void (*actions[])() = {
  [](){ setGarageDoor(true); },
  [](){ setGarageDoor(false); },
  [](){ setGarageLight(true); },
  [](){ setGarageLight(false); },
};

// Core functions ======================================================================================
void setup()
{
  // Initialize serial.
  Serial.begin(BAUD);

  // Indicate setup started.
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("Setup started.");

  // Setup pins.
  pinMode(BUZZER_PIN,  OUTPUT);
  pinMode(LED_0_PIN,   OUTPUT);
  pinMode(LED_1_PIN,   OUTPUT);
  pinMode(LED_2_PIN,   OUTPUT);
  pinMode(LED_3_PIN,   OUTPUT);
  pinMode(MOTOR_0_PIN, OUTPUT);
  pinMode(MD_0_PIN,    OUTPUT);
  pinMode(MD_1_PIN,    OUTPUT);
  pinMode(LDR_PIN,     INPUT);
  pinMode(IR_PIN,      INPUT);

  // Setup ezButton.
  builtinBtn.setDebounceTime(50);
  ml0Btn.setDebounceTime(50);
  ml1Btn.setDebounceTime(50);

  // Setup storage.
  if (InitializeSD())
  {
    // Read config file.
    String configJson = readFile(CONFIG_FILE);

    if (configJson.isEmpty())
      defaultConfig();
    else
      config = JSON.parse(configJson);
      
    String schedulesJson = readFile(SCHEDULES_FILE);
    
    if (!schedulesJson.isEmpty())
      parseSchedules(schedulesJson);
  }
  else
  {
    defaultConfig();
  }

  // Initialize DHT11 sensor.
  dht11.begin();

  // Connect to WiFi.
  connectWiFi((const char *)config["ssid"], (const char *)config["password"]);

  // Get UTC Epoch from NTP server.
  configTime(0, 0, "pool.ntp.org");
  setUpEpoch();

  // Initialize hardware timer.
  // Assume clock rate of 80 MHz and divider of 80, results in 1000000 Hz,
  // divide it by 1000 to get pulses count in ms.
  closeDoorTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(closeDoorTimer, &closeDoorDelayed, true);
  timerAlarmWrite(closeDoorTimer, 1000 * (int)config["doorDelay"], false);
  timerAlarmEnable(closeDoorTimer);
  timerStop(closeDoorTimer);

  syncTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(syncTimer, &sync, true);
  timerAlarmWrite(syncTimer, 1000 * (int)config["updateInterval"], true);
  timerAlarmEnable(syncTimer);

  // Initialize multithreading task.
  //                     (function, name, stack_size, params, priority, handle, core)
  xTaskCreatePinnedToCore(motorStopOnLimit, "motorStopOnLimit", 1024, NULL, 0, &motorStopOnLimitTask, 0);

  // Indicate setup finished.
  Serial.print("Setup finished in ");
  Serial.print(millis());
  Serial.println("ms");
  Serial.println("-------------------------------------------------");
  digitalWrite(LED_BUILTIN, LOW);
}

// Pinned to core 1.
void loop()
{
  if (getEpochNow() % SECS_IN_DAY < 3)
  {
    int l = schedules.length();
    
    for (int i = 0; i < l; i++)
      nextCell[i] = 1;
  }
  
  builtinBtn.loop();

  if (builtinBtn.isPressed())
  {
    // TODO: Enter config mode.
    return;
  }

  ml0Btn.loop();
  ml1Btn.loop();

  readLDR();
  readDHT11();
  readIR();

  checkSchedules();
}

// Read Sensors ===============================================================================
void readLDR()
{
  float light = (analogRead(LDR_PIN) / 4096.0) * 100.0;
  sensors["Light"] = light;

  setFenceLight(light < (double)config["lightThreshold"]);
}

void readDHT11()
{
  float temperature = dht11.readTemperature();
  float humidity = dht11.readHumidity();
  float heatIndex = dht11.computeHeatIndex(temperature, humidity, false);
  sensors["Temperature"] = temperature;
  sensors["Humidity"] = humidity;
  sensors["HeatIndex"] = heatIndex;

  setFan(temperature > (double)config["temperatureFan"]);
  setAlarm(temperature > (double)config["temperatureAlarm"]);
}

void readIR()
{
  int ir = digitalRead(IR_PIN);
  sensors["IR"] = ir;

  if (ir)
  {
    // Open the door and turn on the light.
    setGarageLight(true);
    setGarageDoor(true);

    // Start waiting for limit switch.
    vTaskResume(motorStopOnLimitTask);

    // Start waiting for "doorDelay" to close the door.
    timerRestart(closeDoorTimer);
    timerStart(closeDoorTimer);
  }
}

// Timer alarms ===============================================================================
void closeDoorDelayed()
{
  timerStop(closeDoorTimer);

  // Close the door and turn off the led.
  setGarageLight(false);
  setGarageDoor(false);

  // Start waiting for limit switch.
  vTaskResume(motorStopOnLimitTask);
}

void sync()
{
  Serial.println("Sending Data.");

  sensors["SDFree"] = sdFreeSpace();

  String sensorsPayload = JSON.stringify(sensors);
  String controlsPayload = JSON.stringify(controls);

  Serial.println(sensorsPayload);
  Serial.println(controlsPayload);

  sendPutRequest((const char *)config["sensorsApi"], sensorsPayload);
  sendPutRequest((const char *)config["controlsApi"], controlsPayload);
  
  Serial.println("Receiving Data.");

  applyChanges(JSON.parse(sendGetRequest((const char *)config["controlsApi"])));
  // TODO: Conditional GET.
  const char *json = sendGetRequest((const char *)config["schedulesApi"]);
  parseSchedules(json);
  saveSchedules(json);

  // TODO: Get config.
  
  Serial.print("Waiting for ");
  Serial.print((int)config["updateInterval"]);
  Serial.println(" ms.");
}

// Other thread ===============================================================================
// Pinned to core 0.
void motorStopOnLimit(void *parameter)
{
  while (true)
  {
    // High is released.
    while (ml0Btn.getState() && ml1Btn.getState())
      delay(10);
    
    // Stop the motor.
    digitalWrite(MD_0_PIN, LOW);
    digitalWrite(MD_1_PIN, LOW);

    vTaskSuspend(NULL);
  }
}

// Setters ====================================================================================
void applyChanges(JSONVar newControls)
{
  Serial.println("Applying Changes.");
  
  setGarageLight((bool)newControls["GarageLight"]);
  setGarageDoor((bool)newControls["GarageDoor"]);
}

void setGarageLight(bool isOn)
{
  if ((bool)controls["GarageLight"] != isOn)
  {
    controls["GarageLight"] = isOn;
    controls["GarageLightTS"] = getEpochNow();
    
    digitalWrite(LED_3_PIN, isOn);
  }
}

void setGarageDoor(bool isOpen)
{
  if ((bool)controls["GarageDoor"] != isOpen)
  {
    controls["GarageDoor"] = isOpen;
    controls["GarageDoorTS"] = getEpochNow();
    
    digitalWrite(MD_0_PIN, isOpen);
    digitalWrite(MD_1_PIN, !isOpen);
  }
}

void setFan(bool isOn)
{
  if ((bool)controls["Fan"] != isOn)
  {
    controls["Fan"] = isOn;
    controls["FanTS"] = getEpochNow();
  
    digitalWrite(MOTOR_0_PIN, isOn);
  }
}

void setAlarm(bool isOn)
{
  if ((bool)controls["Alarm"] != isOn)
  {
    controls["Alarm"] = isOn;
    controls["AlarmTS"] = getEpochNow();
    
    digitalWrite(BUZZER_PIN, isOn);
  }
}

void setFenceLight(bool isOn)
{
  if ((bool)controls["FenceLight"] != isOn)
  {
    controls["FenceLight"] = isOn;
    controls["FenceLightTS"] = getEpochNow();
  
    digitalWrite(LED_3_PIN, isOn);
  }
}

// Utils ======================================================================================
void saveSchedules(const char *newSchedulesString)
{
  JSONVar newSchedules = JSON.parse(newSchedulesString);

  if (!(schedules == newSchedules))
  {
    schedules = newSchedules;
    writeFile("/schedules.json", newSchedulesString);
  }
}

void parseSchedules(String json)
{
  schedules = JSON.parse(json);
  nextCell = new int[schedules.length()];
  
  // Initialize Next Cells
  int l = schedules.length();
  int secondOfDay = getEpochNow() % SECS_IN_DAY;
  
  for (int i = 0; i < l; i++)
  {
    int l2 = schedules[i].length();
    
    for (int j = 1; j < l2; j++)
    {
      if ((long)schedules[i][j]["time"] >= secondOfDay)
      {
        nextCell[i] = j;
        break;
      }
    }
  }
}

void checkSchedules()
{
  int l = schedules.length();
  int secondOfDay = getEpochNow() % SECS_IN_DAY;

  for (int i = 0; i < l; i++)
  {
    if (nextCell[i] >= schedules[i].length())
      continue;

    if ((long)schedules[i][nextCell[i]]["time"] <= secondOfDay)
    {
      actions[(int)schedules[i][nextCell[i]]["action"]]();
      nextCell[i]++;
    }
  }
}

void defaultConfig()
{
  config["temperatureFan"] = 28;
  config["temperatureAlarm"] = 45;
  config["lightThreshold"] = 50;
  config["doorDelay"] = 60000;

  config["updateInterval"] = 1000;

  // config["sensorsApi"] = "http://192.168.232.3:5257/api/sensors";
  // config["controlsApi"] = "http://192.168.232.3:5257/api/controls";
  // config["schedulesApi"] = "http://192.168.232.3:5257/api/schedules";
  // config["configApi"] = "http://192.168.232.3:5257/api/config";
  // config["ssid"] = "BOP";
  // config["password"] = "B0#B1313";

  config["sensorsApi"] = "https://shserver.onrender.com/api/sensors";
  config["controlsApi"] = "https://shserver.onrender.com/api/controls";
  config["schedulesApi"] = "https://shserver.onrender.com/api/schedules";
  config["configApi"] = "https://shserver.onrender.com/api/config";
  config["ssid"] = "TP-IoT";
  config["password"] = "1122334455";
}

void setUpEpoch()
{
  struct tm timeInfo;
  
  while (!getLocalTime(&timeInfo))
    delay(10);

  startMS = millis();
  startEpoch = mktime(&timeInfo);
}

long getEpochNow()
{
  return startEpoch + (millis() - startMS) / 1000;
}
