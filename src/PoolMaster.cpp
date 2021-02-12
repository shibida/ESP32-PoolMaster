/*
  PoolMaster Ph/ORP regulator for home pool system
  ESP32 version by Gixy31 based on original Arduino projet (c) Loic74 <loic74650@gmail.com> 2018-2020

  Differences from original project:
  - ESP32 MCU with WiFi (multiple access points)
  - ESP32-Arduino framework, PlatformIO IDE (.cpp, .h src files)
  - async MQTT client
  - JSON upgrade to version 6
  - lots of code modifications, but keeping the general behaviour
  - add a fourth pump (cleaning robot)
  - manage only 2 relays (+ the four pumps)
  - analog measurements done by external ADC I2C module (ADS1115), in async mode
  - and more...

***Compatibility***
  For this sketch to work on your setup you must change the following in the code and in config.h
  - possibly the pinout definitions
  - MAC address of DS18b20 water temperature sensor
  - MQTT broker IP address and login credentials
  - possibly the topic names on the MQTT broker to subscribe and publish to
  - the Kp,Ki,Kd parameters for both PID loops in case your peristaltic pumps have a different throughput than 1.5Liters/hour for the pH pump and 3.0Liters/hour for the Chlorine pump.
  Also the default Kp values were adjusted for a 50m3 pool volume. You might have to adjust the Kp values in case of a different pool volume and/or peristaltic pumps throughput
  (start by adjusting it proportionally). In any case these parameters are likely to require adjustments for every pool

***Brief description:***
  Four main metrics are measured and periodically reported over MQTT: water temperature and pressure, PH and ORP values
  Pumps states, tank-level estimates and other parameters are also periodically reported
  Two PID regulation loops are running in parallel: one for PH, one for ORP.
  PH is regulated by injecting Acid or PH+ from a tank into the pool water (a relay starts/stops the peristaltic pump)
  ORP is regulated by injecting Chlorine from a tank into the pool water (a relay starts/stops the Chlorine peristaltic pump)
  Defined time-slots and water temperature are used to start/stop the filtration pump for a daily given amount of time (a relay starts/stops the filtration pump)
  Communication with the system is performed using the MQTT protocol over a WiFi connection to the local network/MQTT broker.
  Every 30 seconds (by default), the system will publish on the "PoolTopicMeas1" and "PoolTopicMeas2"(see in code below) the following payloads in Json format:
  {"Tmp":818,"pH":321,"PSI":56,"Orp":583,"FilUpT":8995,"PhUpT":0,"ChlUpT":0}
  {"AcidF":100,"ChlF":100,"IO":11,"IO2":0}
  Tmp: measured Water temperature value in °C x100 (8.18°C in the above example payload)
  pH: measured pH value x100 (3.21 in the above example payload)
  Orp: measured Orp (aka Redox) value in mV (583mV in the above example payload)
  PSI: measured Water pressure value in bar x100 (0.56bar in the above example payload)
  FiltUpT: current running time of Filtration pump in seconds (reset every 24h. 8995secs in the above example payload)
  PhUpT: current running time of Ph pump in seconds (reset every 24h. 0secs in the above example payload)
  ChlUpT: current running time of Chl pump in seconds (reset every 24h. 0secs in the above example payload)
  AcidF: percentage fill estimate of acid tank ("pHTank" command must have been called when a new acid tank was set in place in order to have accurate value)
  ChlF: percentage fill estimate of Chlorine tank ("ChlTank" command must have been called when a new Chlorine tank was set in place in order to have accurate value)
  IO: a variable of type BYTE where each individual bit is the state of a digital input on the Arduino. These are :
    FiltPump: current state of Filtration Pump (0=on, 1=off)
    PhPump: current state of Ph Pump (0=on, 1=off)
    ChlPump: current state of Chl Pump (0=on, 1=off)
    PhlLevel: current state of Acid tank level (0=empty, 1=ok)
    ChlLevel: current state of Chl tank level (0=empty, 1=ok)
    PSIError: over-pressure error
    pHErr: pH pump overtime error flag
    ChlErr: Chl pump overtime error flag
  IO2: a variable of type BYTE where each individual bit is the state of a digital input on the Arduino. These are :
    pHPID: current state of pH PID regulation loop (1=on, 0=off)
    OrpPID: current state of Orp PID regulation loop (1=on, 0=off)
    Mode: state of pH and Orp regulation mode (0=manual, 1=auto)
    R0: state of Relay0
    R1: state of Relay1
    R2: state of Relay2

***MQTT API***
  Below are the Payloads/commands to publish on the "PoolTopicAPI" topic (see in code below) in Json format in order to launch actions on the Arduino:
  {"Mode":1} or {"Mode":0}         -> set "Mode" to manual (0) or Auto (1). In Auto, filtration starts/stops at set times of the day and PID's are enabled/disabled
  {"FiltPump":1} or {"FiltPump":0} -> manually start/stop the filtration pump.
  {"ChlPump":1} or {"ChlPump":0}   -> manually start/stop the Chl pump to add more Chlorine
  {"PhPump":1} or {"PhPump":0}     -> manually start/stop the Acid pump to lower the Ph
  {"PhPID":1} or {"PhPID":0}       -> start/stop the Ph PID regulation loop
  {"OrpPID":1} or {"OrpPID":0}     -> start/stop the Orp PID regulation loop
  {"PhCalib":[4.02,3.8,9.0,9.11]}  -> multi-point linear regression calibration (minimum 1 point-couple, 6 max.) in the form [ProbeReading_0, BufferRating_0, xx, xx, ProbeReading_n, BufferRating_n]
  {"OrpCalib":[450,465,750,784]}   -> multi-point linear regression calibration (minimum 1 point-couple, 6 max.) in the form [ProbeReading_0, BufferRating_0, xx, xx, ProbeReading_n, BufferRating_n]
  {"PSICalib":[0,0,0.71,0.6]}      -> multi-point linear regression calibration (minimum 2 point-couple, 6 max.) in the form [ElectronicPressureSensorReading_0, MechanicalPressureSensorReading_0, xx, xx, ElectronicPressureSensorReading_n, ElectronicPressureSensorReading_n]. Mechanical pressure sensor is typically located on the sand filter
  {"PhSetPoint":7.4}               -> set the Ph setpoint, 7.4 in this example
  {"OrpSetPoint":750.0}            -> set the Orp setpoint, 750mV in this example
  {"WSetPoint":27.0}               -> set the water temperature setpoint, 27.0deg in this example
  {"WTempLow":10.0}                -> set the water low-temperature threshold below which there is no need to regulate Orp and Ph (ie. in winter)
  {"OrpPIDParams":[4000,0,0]}      -> respectively set Kp,Ki,Kd parameters of the Orp PID loop. In this example they are set to 2857, 0 and 0
  {"PhPIDParams":[2000000,0,0.0]}  -> respectively set Kp,Ki,Kd parameters of the Ph PID loop. In this example they are set to 1330000, 0 and 0.0
  {"OrpPIDWSize":3600000}          -> set the window size of the Orp PID loop (in msec), 60mins in this example
  {"PhPIDWSize":3600000}           -> set the window size of the Ph PID loop (in msec), 60mins in this example
  {"Date":[1,1,1,18,13,32,0]}      -> set date/time of RTC module in the following format: (Day of the month, Day of the week, Month, Year, Hour, Minute, Seconds), in this example: Monday 1st January 2018 - 13h32mn00secs
  {"FiltT0":9}                     -> set the earliest hour (9:00 in this example) to run filtration pump. Filtration pump will not run beofre that hour
  {"FiltT1":20}                    -> set the latest hour (20:00 in this example) to run filtration pump. Filtration pump will not run after that hour
  {"PubPeriod":30}                 -> set the periodicity (in seconds) at which the system info (pumps states, tank levels states, measured values, etc) will be published to the MQTT broker
  {"PumpsMaxUp":1800}              -> set the Max Uptime (in secs) for the Ph and Chl pumps over a 24h period. If over, PID regulation is stopped and a warning flag is raised
  {"Clear":1}                      -> reset the pH and Orp pumps overtime error flags in order to let the regulation loops continue. "Mode", "PhPID" and "OrpPID" commands need to be switched back On (1) after an error flag was raised
  {"DelayPID":60}                  -> Delay (in mins) after FiltT0 before the PID regulation loops will start. This is to let the Orp and pH readings stabilize first. 30mins in this example. Should not be > 59mins
  {"TempExt":4.2}                  -> Provide the external temperature. Should be updated regularly and will be used to start filtration when outside air temperature is <-2.0deg. 4.2deg in this example
  {"PSIHigh":1.0}                  -> set the water high-pressure threshold (1.0bar in this example). When water pressure is over that threshold, an error flag is set.
  {"pHTank":[20,100]}              -> call this command when the Acid tank is replaced or refilled. First parameter is the tank volume in Liters, second parameter is its percentage fill (100% when full)
  {"ChlTank":[20,100]}             -> call this command when the Chlorine tank is replaced or refilled. First parameter is the tank volume in Liters, second parameter is its percentage fill (100% when full)
  {"Relay":[1,1]}                  -> call this generic command to actuate spare relays. Parameter 1 is the relay number (R1 in this example), parameter 2 is the relay state (ON in this example). This command is useful to use spare relays for additional features (lighting, etc). Available relay numbers are 1,2,6,7,8,9
  {"Reboot":1}                     -> call this command to reboot the controller (after 8 seconds from calling this command)
  {"pHPumpFR":1.5}                 -> call this command to set pH pump flow rate un L/h. In this example 1.5L/h
  {"ChlPumpFR":3}                  -> call this command to set Chl pump flow rate un L/h. In this example 3L/h
  {"RstpHCal":1}                   -> call this command to reset the calibration coefficients of the pH probe
  {"RstOrpCal":1}                   -> call this command to reset the calibration coefficients of the Orp probe
  {"RstPSICal":1}                   -> call this command to reset the calibration coefficients of the pressure sensor

*/
#undef __STRICT_ANSI__              // work-around for Time Zone definition
#include <stdint.h>                 // std lib (types definitions)
#include <Arduino.h>                // Arduino framework

#include "Config.h"
#include "PoolMaster.h"

// Used to simulate pH/ORP sensors. Very simple simulation: the sensor value is computed from the 
// output of the PID loop to reach linearly the theorical value produced by this output after one hour
//#define SIMU

#ifdef SIMU
bool init_simu = true;
double pHLastValue = 7.;
unsigned long pHLastTime = 0;
double OrpLastValue = 730.;
unsigned long OrpLastTime = 0;
double pHTab [3] {0.,0.,0.};
double ChlTab [3] {0.,0.,0.};
uint8_t iw = 0;
uint8_t jw = 0;
bool newpHOutput = false;
bool newChlOutput = false;
double pHCumul = 0.;
double ChlCumul = 0.;
#endif

// Firmware revision
String Firmw = "ESP-1.0";

//Settings structure and its default values
//To be modified for operational version : 
// delayPIDs = 10 (minutes)
// PSI parameters to determine
// calibration coefficients
StoreStruct storage =
{ 
  CONFIG_VERSION,
  0, 0, 1,
  12, 8, 22, 8, 22, 10,
  1800, 1800, 30000,
  1200000, 1200000, 0, 0,
  7.3, 750.0, 1.8, 0.75, 10.0, 18.0, 3.0, 3.507951, -1.923527, -972.741849, 2477.392837, 1.0, 0.0,
  2250000.0, 0.0, 0.0, 18000.0, 0.0, 0.0, 0.0, 0.0, 28.0, 7.3, 750., 1.3,
  100.0, 100.0, 20.0, 20.0, 1.5, 1.5
};

tm timeinfo;

// Queue object to store incoming JSON commands (up to 10)
ArduinoQueue<String> queueIn(QUEUE_SIZE_ITEMS, QUEUE_SIZE_BYTES);

// Setup oneWire instances to communicate with temperature sensors (one bus per sensor)
OneWire oneWire_W(ONE_WIRE_BUS_W);
OneWire oneWire_A(ONE_WIRE_BUS_A);
// Pass our oneWire reference to Dallas Temperature library instance
DallasTemperature sensors_W(&oneWire_W);
DallasTemperature sensors_A(&oneWire_A);
// MAC Address of DS18b20 water temperature sensor
DeviceAddress DS18B20_W = { 0x28, 0x9F, 0x24, 0x24, 0x0C, 0x00, 0x00, 0xA9 };
DeviceAddress DS18B20_A = { 0x28, 0xB0, 0x70, 0x75, 0xD0, 0x01, 0x3C, 0x9D };

// Setup an ADS1115 instance for pressure measurements
ADS1115Scanner adc(0x48);  // Address 0x48 is the default
float ph_sensor_value;     // pH sensor current value
float orp_sensor_value;    // ORP sensor current value
float psi_sensor_value;    // PSI sensor current value

// The four pumps of the system (instanciate the Pump class)
// In this case, all pumps start/Stop are managed by relays. pH, ORP and Robot pumps are interlocked with 
// filtration pump
Pump FiltrationPump(FILTRATION_PUMP, FILTRATION_PUMP, NO_TANK, NO_INTERLOCK, 0.0, 0.0);
Pump PhPump(PH_PUMP, PH_PUMP, PH_LEVEL, FILTRATION_PUMP, storage.pHPumpFR, storage.pHTankVol);
Pump ChlPump(CHL_PUMP, CHL_PUMP, CHL_LEVEL, FILTRATION_PUMP, storage.ChlPumpFR, storage.ChlTankVol);
Pump RobotPump(ROBOT_PUMP, ROBOT_PUMP, NO_TANK, FILTRATION_PUMP, 0., 0.);

// PIDs instances
//Specify the direction and initial tuning parameters
PID PhPID(&storage.PhValue, &storage.PhPIDOutput, &storage.Ph_SetPoint, storage.Ph_Kp, storage.Ph_Ki, storage.Ph_Kd, DIRECT);
PID OrpPID(&storage.OrpValue, &storage.OrpPIDOutput, &storage.Orp_SetPoint, storage.Orp_Kp, storage.Orp_Ki, storage.Orp_Kd, DIRECT);

// Various Flags
bool PhLevelError = 0;                          // PH tank level alarm
bool ChlLevelError = 0;                         // Cl tank level alarm
bool EmergencyStopFiltPump = 0;                 // flag will be (re)set by double-tapp button
bool PSIError = false;                          // Water pressure OK
bool AntiFreezeFiltering = false;               //Filtration anti freeze mod
bool DoneForTheDay = false;                     // Reset actions done once per day
bool d_calc = false;                            // Filtration duration computed
bool cleaning_done = false;                     // daily cleaning done 

// Signal filtering library. 
RunningMedian samples_WTemp = RunningMedian(11);
RunningMedian samples_ATemp = RunningMedian(11);
RunningMedian samples_Ph    = RunningMedian(11);
RunningMedian samples_Orp   = RunningMedian(11);
RunningMedian samples_PSI   = RunningMedian(5);

//Date-Time variables
char TimeBuffer[25];

//State Machine
//Getting a 12 bits temperature reading on a DS18b20 sensor takes >750ms
//Here we use the sensor in asynchronous mode, request a temp reading and use
//the nice "YASM" state-machine library to do other things while it is being obtained
YASM gettemp;

//Callbacks
//Here we use the SoftTimer library which handles multiple timers (Tasks)
//It is more elegant and readable than a single loop() functtion, especially
//when tasks with various frequencies are to be used

void ADS1115Callback(Task* me);
void OrpRegulationCallback(Task* me);
void PHRegulationCallback(Task* me);
void GenericCallback(Task* me);
void PublishDataCallback(Task* me);
void StatusLightsCallback(Task* me);

Task t1(125, ADS1115Callback);                  //ORP, PH and PSI measurement readiness
Task t2(1100, OrpRegulationCallback);           //ORP regulation loop
Task t3(1300, PHRegulationCallback);            //PH regulation loop
Task t4(PUBLISHINTERVAL, PublishDataCallback);  //Publish data to MQTT broker every 30 secs
Task t5(700, GenericCallback);                  //Various things handled/updated in this loop every 0.6 secs
Task t6(3100, StatusLightsCallback);            //Status LED display

// NVS Non Volatile SRAM (eqv. EEPROM)
Preferences nvs;      

// Functions prototypes
void keepWiFiAlive(void *);
void StartTime(void);
void readLocalTime(void);
bool loadConfig(void);
bool saveConfig(void);
void WiFiEvent(WiFiEvent_t);
void mqttInit(void);                     
void mqttErrorPublish(const char*);
void InitTFT(void);
void ResetTFT(void);
void UpdateTFT();
void gettemp_start(void);
void gettemp_request(void);
void gettemp_wait(void);
void gettemp_read(void);
void PublishSettings(void);
void SetPhPID(bool);
void SetOrpPID(bool);
int  freeRam (void);
void ProcessCommand(String);
void getMeasures(void);
bool saveParam(const char*,uint8_t );


// Setup
void setup()
{
  //Serial port for debug info
  Serial.begin(115200);

  // Set appropriate debug level. The level is defined in PoolMaster.h
  Debug.setDebugLevel(DEBUG_LEVEL);
  Debug.timestampOn();

  // Initialize Nextion TFT
  InitTFT();
  ResetTFT();

  //Read ConfigVersion. If does not match expected value, restore default values
  if(nvs.begin("PoolMaster",true))
  {
    uint8_t vers = nvs.getUChar("ConfigVersion",0);
    Debug.print(DBG_INFO,"Stored version: %d",vers);
    nvs.end();

    if (vers == CONFIG_VERSION)
    {
      Debug.print(DBG_INFO,"Same version: %d / %d. Loading settings from NVS",vers,CONFIG_VERSION);
      if(loadConfig()) Debug.print(DBG_INFO,"Config loaded"); //Restore stored values from NVS
    }
    else
    {
      Debug.print(DBG_INFO,"New version: %d / %d. Loading new default settings",vers,CONFIG_VERSION);      
      if(saveConfig()) Debug.print(DBG_INFO,"Config saved");  //First time use. Save new default values to NVS
    }

  } else {
    Debug.print(DBG_ERROR,"NVS Error");
    nvs.end();
  }  

  //Initialize pump objects with stored config data
  PhPump.SetFlowRate(storage.pHPumpFR);
  PhPump.SetTankVolume(storage.pHTankVol);
  ChlPump.SetFlowRate(storage.ChlPumpFR);
  ChlPump.SetTankVolume(storage.ChlTankVol);

  //Define pins directions
  pinMode(FILTRATION_PUMP, OUTPUT);
  pinMode(PH_PUMP, OUTPUT);
  pinMode(CHL_PUMP, OUTPUT);
  pinMode(ROBOT_PUMP,OUTPUT);

  pinMode(RELAY_R0, OUTPUT);
  pinMode(RELAY_R1, OUTPUT);
  pinMode(RELAY_R2, OUTPUT);

  pinMode(BUZZER, OUTPUT);

  // As the relays on the board are activated by a LOW level, set all levels HIGH at startup
  digitalWrite(FILTRATION_PUMP,HIGH);
  digitalWrite(PH_PUMP,HIGH); 
  digitalWrite(CHL_PUMP,HIGH);
  digitalWrite(ROBOT_PUMP,HIGH);
  digitalWrite(RELAY_R0,HIGH);
  digitalWrite(RELAY_R1,HIGH);
  digitalWrite(RELAY_R2,HIGH);

// Warning: pins used here have no pull-ups, provide external ones
  pinMode(CHL_LEVEL, INPUT);
  pinMode(PH_LEVEL, INPUT);

  // Initialize watch-dog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  //Initialize MQTT
  mqttInit();

  // Initialize WiFi events management (on connect/disconnect)
  WiFi.onEvent(WiFiEvent);

  // Create a task on core 1 to monitor WiFi connection and reconnect if necessary
  xTaskCreatePinnedToCore(
    keepWiFiAlive,
    "keepWiFiAlive", // Task name
    5000,            // stack size 
    NULL,            // Parameter
    2,               // Priority           
    NULL,            // Task handle
    1                // Core 
  );

  delay(500);    // let task start-up and wait for connection
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Config NTP, get time and set system time. This is done here in setup then every day at midnight
  // note: in timeinfo struct, months are from 0 to 11 and years are from 1900. Thus the corrections
  // to pass arguments to setTime which needs months from 1 to 12 and years from 2000...
  // DST (Daylight Saving Time) is managed automatically
  StartTime();
  readLocalTime();
  setTime(timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year-100);
  Debug.print(DBG_INFO,"%d/%02d/%02d %02d:%02d:%02d",year(),month(),day(),hour(),minute(),second());

  // Initialize the mDNS library.
  while (!MDNS.begin("PoolMaster")) {
    Debug.print(DBG_ERROR,"Error setting up MDNS responder!");
    delay(1000);
  }
  MDNS.addService("http", "tcp", SERVER_PORT);

  //Start temperatures measurement state machine
  gettemp.next(gettemp_start);

  // Start I2C and ADS1115 for analog measurements in asynchronous mode
  Wire.begin(I2C_SDA,I2C_SCL);

  adc.setSpeed(ADS1115_SPEED_8SPS);
  adc.addChannel(ADS1115_CHANNEL0, ADS1115_RANGE_6144);
  adc.addChannel(ADS1115_CHANNEL1, ADS1115_RANGE_6144);
  adc.addChannel(ADS1115_CHANNEL2, ADS1115_RANGE_6144);
  adc.setSamples(1);
  adc.start();

  // Clear status LEDs

  Wire.beginTransmission(0x38);
  Wire.write((uint8_t)0xFF);
  Wire.endTransmission();

  // Start filtration pump at power-on if within scheduled time slots -- You can choose not to do this and start pump manually
  if (storage.AutoMode && (hour() >= storage.FiltrationStart) && (hour() < storage.FiltrationStop))
    FiltrationPump.Start();
  else FiltrationPump.Stop();

  // Robot pump off at start
  RobotPump.Stop();

  // PIDs off at start
  SetPhPID (false);
  SetOrpPID(false);

  //Publish settings
  PublishSettings();

  // Initialize PIDs
  storage.PhPIDwindowStartTime  = millis();
  storage.OrpPIDwindowStartTime = millis();

  // Limit the PIDs output range in order to limit max. pumps runtime (safety first...)
  // Set also a lower limit at 30s (a lower pump duration does'nt mean anything)
  PhPID.SetSampleTime((int)storage.PhPIDWindowSize);
  PhPID.SetOutputLimits(0, storage.PhPIDWindowSize);    //Whatever happens, don't allow continuous injection of Acid for more than a PID Window

  OrpPID.SetSampleTime((int)storage.OrpPIDWindowSize);
  OrpPID.SetOutputLimits(0, storage.OrpPIDWindowSize);  //Whatever happens, don't allow continuous injection of Chl for more than a PID Window

  // Initialize pumps
  FiltrationPump.SetMaxUpTime(0);     //no runtime limit for the filtration pump
  PhPump.SetMaxUpTime(storage.PhPumpUpTimeLimit * 1000);
  ChlPump.SetMaxUpTime(storage.ChlPumpUpTimeLimit * 1000);
  RobotPump.SetMaxUpTime(0);          //no runtime limit for the robot pump

  // ADS1115 update loop
  SoftTimer.add(&t1);

  // Orp regulation loop
  SoftTimer.add(&t2);

  // PH regulation loop
  SoftTimer.add(&t3);

  // Publish loop
  SoftTimer.add(&t4);

  // General loop
  SoftTimer.add(&t5);

  // Status LED loop
  SoftTimer.add(&t6);

  //display remaining RAM space. For debug
  Debug.print(DBG_DEBUG,"memCheck: %db",freeRam());

  // Initialize OTA (On The Air update)
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname("PoolMaster");
  ArduinoOTA.setPasswordHash("510179c0211489b9625a5f2e41da8469"); // hash de Fgixy001
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    Debug.print(DBG_INFO,"Start updating %s",type);
  });
  ArduinoOTA.onEnd([]() {
  Debug.print(DBG_INFO,"End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    esp_task_wdt_reset();           // reset Watchdog as upload may last some time...
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Debug.print(DBG_ERROR,"Error[%u]: ", error);
    if      (error == OTA_AUTH_ERROR)    Debug.print(DBG_ERROR,"Auth Failed");
    else if (error == OTA_BEGIN_ERROR)   Debug.print(DBG_ERROR,"Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Debug.print(DBG_ERROR,"Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Debug.print(DBG_ERROR,"Receive Failed");
    else if (error == OTA_END_ERROR)     Debug.print(DBG_ERROR,"End Failed");
  });

  ArduinoOTA.begin();

}

//Update loop for ADS1115 measurements
void ADS1115Callback(Task* me)
{
  adc.update();
  if(adc.ready()){                                         // all conversions done ?
    orp_sensor_value = adc.readAverage(0)*0.1875/1000.;    // ORP sensor current value
    ph_sensor_value  = adc.readAverage(1)*0.1875/1000.;    // pH sensor current value
    psi_sensor_value = adc.readAverage(2)*0.1875/1000.;    // psi sensor current value
    adc.start();                                           // restart conversion
  }
}

void StatusLightsCallback(Task* me)
{
  static uint8_t line = 0;
  uint8_t status;

  status = 0;
  status |= (line & 1) << 1;
  if(line == 0)
  {
    line = 1;
    status |= (storage.AutoMode & 1) << 2;
    status |= (AntiFreezeFiltering & 1) << 3;
    status |= (PSIError & 1) << 7;
  } else
  {
    line = 0;
    status |= (PhPID.GetMode() & 1) << 2;
    status |= (OrpPID.GetMode() & 1) << 3;
    status |= (!PhPump.TankLevel() & 1) << 4;
    status |= (!ChlPump.TankLevel() & 1) << 5;
    status |= (PhPump.UpTimeError & 1) << 6;
    status |= (ChlPump.UpTimeError & 1) << 7;  
  }
  (status & 0xF0) ? digitalWrite(BUZZER,HIGH) : digitalWrite(BUZZER,LOW) ;
  if(WiFi.status() == WL_CONNECTED) status |= 0x01;
    else status |= 0x00;
  Debug.print(DBG_VERBOSE,"Status LED : 0x%02x",status);  
  Wire.beginTransmission(0x38);
  Wire.write(~status);
  Wire.endTransmission();
}

//Loop where various tasks are updated/handled
void GenericCallback(Task* me)
{
  // reset watchdog
  esp_task_wdt_reset();

  // Handle OTA update
  ArduinoOTA.handle();

  //request temp reading
  gettemp.run();
  //get Ph, ORP and PSI probes measures
  getMeasures();

  //update pumps
  FiltrationPump.loop();
  PhPump.loop();
  ChlPump.loop();
  RobotPump.loop();

  //Process queued incoming JSON commands if any
  if (queueIn.itemCount() > 0)
    ProcessCommand(queueIn.dequeue());

  //reset time counters at midnight and send sync request to time server
  if (hour() == 0 && !DoneForTheDay)
  {
    //First store current Chl and Acid consumptions of the day in Eeprom
    storage.AcidFill = storage.AcidFill - PhPump.GetTankUsage();
    storage.ChlFill = storage.ChlFill - ChlPump.GetTankUsage();
    saveConfig();

    FiltrationPump.ResetUpTime();
    PhPump.ResetUpTime();
    ChlPump.ResetUpTime();
    RobotPump.ResetUpTime();

    EmergencyStopFiltPump = false;
    d_calc = false;
    DoneForTheDay = true;
    cleaning_done = false;

    readLocalTime();
    setTime(timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year-100);

  }
  else if(hour() == 1)
  {
    DoneForTheDay = false;
  }

  // Compute next Filtering duration and start/stop hours at 15:00 (to filter during the hotest period of the day)
  // Depending on water temperature, the filtration duration is either 2 hours, temp/3 or temp/2 hours.
  #ifdef DEBUG
  if (second() == 0 && !d_calc)
  #else
  if (hour() == 15 && !d_calc)
  #endif
  {
    if (storage.TempValue < storage.WaterTempLowThreshold){
        storage.FiltrationDuration = 2;}
    else if (storage.TempValue >= storage.WaterTempLowThreshold && storage.TempValue < storage.WaterTemp_SetPoint){
        storage.FiltrationDuration = round(storage.TempValue / 3.);}
    else if (storage.TempValue >= storage.WaterTemp_SetPoint){
        storage.FiltrationDuration = round(storage.TempValue / 2.);}
  
    storage.FiltrationStart = 15 - (int)round(storage.FiltrationDuration / 2.);
    if (storage.FiltrationStart < storage.FiltrationStartMin)
      storage.FiltrationStart = storage.FiltrationStartMin;    
    storage.FiltrationStop = storage.FiltrationStart + storage.FiltrationDuration;
    if (storage.FiltrationStop > storage.FiltrationStopMax)
      storage.FiltrationStop = storage.FiltrationStopMax;

    saveParam("FiltrStart",storage.FiltrationStart);  
    saveParam("FiltrStop",storage.FiltrationStop);  

    Debug.print(DBG_INFO,"Filtration duration: %dh",storage.FiltrationDuration);
    Debug.print(DBG_INFO,"Start: %dh - Stop: %dh",storage.FiltrationStart,storage.FiltrationStop);

    d_calc = true;
  }
  #ifdef DEBUG
  if(second() == 30 && d_calc) d_calc = false;
  #endif

  //start filtration pump as scheduled
  if (!EmergencyStopFiltPump && !FiltrationPump.IsRunning() && storage.AutoMode && !PSIError && hour() >= storage.FiltrationStart && hour() < storage.FiltrationStop )
    FiltrationPump.Start();

  //start cleaning robot for 2 hours 30mn after filtration start
  if (FiltrationPump.IsRunning() && storage.AutoMode && !RobotPump.IsRunning() && ((millis() - FiltrationPump.LastStartTime) / 1000 / 60) >= 30 && !cleaning_done)
  {
    RobotPump.Start();
    Debug.print(DBG_INFO,"Robot Start 30mn after Filtration");    
  }
  if(RobotPump.IsRunning() && storage.AutoMode && ((millis() - RobotPump.LastStartTime) / 1000 / 60) >= 120)
  {
    RobotPump.Stop();
    cleaning_done = true;
    Debug.print(DBG_INFO,"Robot Stop after: %d mn",(int)(millis()-RobotPump.LastStartTime)/1000/60);
  }

  // start PIDs with delay after FiltrationStart in order to let the readings stabilize
  // start inhibited if water temperature below threshold (winter)
  if (FiltrationPump.IsRunning() && storage.AutoMode && !PhPID.GetMode() &&
     ((millis() - FiltrationPump.LastStartTime) / 1000 / 60 >= storage.DelayPIDs) &&
     (hour() >= storage.FiltrationStart) && (hour() < storage.FiltrationStop) &&
     storage.TempValue >= storage.WaterTempLowThreshold)
  {
    //Start PIDs
    SetPhPID(true);
    SetOrpPID(true);
  }

  //stop filtration pump and PIDs as scheduled unless we are in AntiFreeze mode
  if (storage.AutoMode && FiltrationPump.IsRunning() && !AntiFreezeFiltering && (hour() >= storage.FiltrationStop || hour() < storage.FiltrationStart))
  {
    SetPhPID(false);
    SetOrpPID(false);
    FiltrationPump.Stop();
  }

  //Outside regular filtration hours, start filtration in case of cold Air temperatures (<-2.0deg)
  if (!EmergencyStopFiltPump && storage.AutoMode && !PSIError && !FiltrationPump.IsRunning() && ((hour() < storage.FiltrationStart) || (hour() > storage.FiltrationStop)) && (storage.TempExternal < -2.0))
  {
    FiltrationPump.Start();
    AntiFreezeFiltering = true;
  }

  //Outside regular filtration hours and if in AntiFreezeFiltering mode but Air temperature rose back above 2.0deg, stop filtration
  if (storage.AutoMode && FiltrationPump.IsRunning() && ((hour() < storage.FiltrationStart) || (hour() > storage.FiltrationStop)) && AntiFreezeFiltering && (storage.TempExternal > 2.0))
  {
    FiltrationPump.Stop();
    AntiFreezeFiltering = false;
  }

  //If filtration pump has been running for over 30secs but pressure is still low, stop the filtration pump, something is wrong, set error flag
  if (FiltrationPump.IsRunning() && ((millis() - FiltrationPump.LastStartTime) > 30000) && (storage.PSIValue < storage.PSI_MedThreshold))
  {
    FiltrationPump.Stop();
    PSIError = true;
    mqttErrorPublish("PSI Error");
  }  

  // Over-pressure error
  if (storage.PSIValue > storage.PSI_HighThreshold)
  {
    FiltrationPump.Stop();
    PSIError = true;
    mqttErrorPublish("PSI Error");
  } else if(storage.PSIValue >= storage.PSI_MedThreshold)
    PSIError = false;

  //UPdate Nextion TFT
  UpdateTFT();

}

void PHRegulationCallback(Task * me)
{
  //do not compute PID if filtration pump is not running
  //because if Ki was non-zero that would let the OutputError increase
  if (FiltrationPump.IsRunning()  && (PhPID.GetMode() == AUTOMATIC))
  {  
    if(PhPID.Compute()){
      Debug.print(DBG_VERBOSE,"Ph  regulation: %10.2f, %13.9f, %13.9f, %17.9f",storage.PhPIDOutput,storage.PhValue,storage.Ph_SetPoint,storage.Ph_Kp);
      if(storage.PhPIDOutput < (double)30000.) storage.PhPIDOutput = 0.;
      Debug.print(DBG_INFO,"Ph  regulation: %10.2f",storage.PhPIDOutput);
#ifdef SIMU
      newpHOutput = true;
#endif            
    }
#ifdef SIMU
    else newpHOutput = false;
#endif    
    /************************************************
      turn the Acid pump on/off based on pid output
    ************************************************/
    if (millis() - storage.PhPIDwindowStartTime > storage.PhPIDWindowSize)
    {
      //time to shift the Relay Window
      storage.PhPIDwindowStartTime += storage.PhPIDWindowSize;
    }
    if (storage.PhPIDOutput < millis() - storage.PhPIDwindowStartTime)
      PhPump.Stop();
    else
      PhPump.Start();   
  }
}

//Orp regulation loop
void OrpRegulationCallback(Task * me)
{
  //do not compute PID if filtration pump is not running
  //because if Ki was non-zero that would let the OutputError increase
  if (FiltrationPump.IsRunning() && (OrpPID.GetMode() == AUTOMATIC))
  {
    if(OrpPID.Compute()){
      Debug.print(DBG_VERBOSE,"ORP regulation: %10.2f, %13.9f, %12.9f, %17.9f",storage.OrpPIDOutput,storage.OrpValue,storage.Orp_SetPoint,storage.Orp_Kp);
      if(storage.OrpPIDOutput < (double)30000.) storage.OrpPIDOutput = 0.;    
      Debug.print(DBG_INFO,"Orp regulation: %10.2f",storage.OrpPIDOutput);
#ifdef SIMU
      newChlOutput = true;
#endif      
    }
#ifdef SIMU
    else newChlOutput = false;
#endif    
    /************************************************
      turn the Chl pump on/off based on pid output
    ************************************************/
    if (millis() - storage.OrpPIDwindowStartTime > storage.OrpPIDWindowSize)
    {
      //time to shift the Relay Window
      storage.OrpPIDwindowStartTime += storage.OrpPIDWindowSize;
    }
    if (storage.OrpPIDOutput < millis() - storage.OrpPIDwindowStartTime)
      ChlPump.Stop();
    else
      ChlPump.Start();
  }
}

//Enable/Disable Chl PID
void SetPhPID(bool Enable)
{
  if (Enable)
  {
    //Start PhPID
    PhPump.ClearErrors();
    storage.PhPIDOutput = 0.0;
    storage.PhPIDwindowStartTime = millis();
    PhPID.SetMode(AUTOMATIC);
    storage.Ph_RegulationOnOff = 1;
  }
  else
  {
    //Stop PhPID
    PhPID.SetMode(MANUAL);
    storage.Ph_RegulationOnOff = 0;
    storage.PhPIDOutput = 0.0;
    PhPump.Stop();
  }
}

//Enable/Disable Orp PID
void SetOrpPID(bool Enable)
{
  if (Enable)
  {
    //Start OrpPID
    ChlPump.ClearErrors();
    storage.OrpPIDOutput = 0.0;
    storage.OrpPIDwindowStartTime = millis();
    OrpPID.SetMode(AUTOMATIC);
    storage.Orp_RegulationOnOff = 1;

  }
  else
  {
    //Stop OrpPID
    OrpPID.SetMode(MANUAL);
    storage.Orp_RegulationOnOff = 0;
    storage.OrpPIDOutput = 0.0;
    ChlPump.Stop();
  }
}

//Update Ph, Orp and PSI values
void getMeasures()
{
  //Ph
  //storage.PhValue = 7.0 - ((2.5 - ph_sensor_value)/(0.257179 + 0.000941468 * storage.TempValue));     // formula to compute pH which takes water temperature into account
  //storage.PhValue = (0.0178 * ph_sensor_value * 200.0) - 1.889;                                       // formula to compute pH without taking temperature into account (assumes 27deg water temp)
  storage.PhValue = (storage.pHCalibCoeffs0 * ph_sensor_value) + storage.pHCalibCoeffs1;                //Calibrated sensor response based on multi-point linear regression
  samples_Ph.add(storage.PhValue);                                                                      // compute average of pH from last 5 measurements
  storage.PhValue = samples_Ph.getAverage(5);

#ifdef SIMU
  if(!init_simu){
    if(newpHOutput) {
      pHTab[iw] = storage.PhPIDOutput;
      pHCumul = pHTab[0]+pHTab[1]+pHTab[2];
      iw++;
      iw %= 3;
    }
    storage.PhValue = pHLastValue + pHCumul/4500000.*(double)((millis()-pHLastTime)/3600000.);
    pHLastValue = storage.PhValue;
    pHLastTime = millis();
  } else {
    init_simu = false;
    pHLastTime = millis();
    pHLastValue = 7.0;
    storage.PhValue = pHLastValue;
    storage.OrpValue = OrpLastValue;
    OrpLastTime = millis();
    OrpLastValue = 730.0;
    for(uint8_t i=0;i<3;i++) {
      pHTab[i] = 0.;
      ChlTab[i] = 0.;
    }  
  }  
#endif

  //ORP
  //float orp_sensor_value = analogRead(ORP_MEASURE) * 5.0 / 1023.0;                                      // from 0.0 to 5.0 V
  //storage.OrpValue = ((2.5 - orp_sensor_value) / 1.037) * 1000.0;                                     // from -2000 to 2000 mV where the positive values are for oxidizers and the negative values are for reducers
  storage.OrpValue = (storage.OrpCalibCoeffs0 * orp_sensor_value) + storage.OrpCalibCoeffs1;            //Calibrated sensor response based on multi-point linear regression
  samples_Orp.add(storage.OrpValue);                                                                    // compute average of ORP from last 5 measurements
  storage.OrpValue = samples_Orp.getAverage(5);

#ifdef SIMU
  if(!init_simu){
    if(newChlOutput) {
      ChlTab[jw] = storage.OrpPIDOutput;
      ChlCumul = ChlTab[0]+ChlTab[1]+ChlTab[2];
      jw++;
      jw %= 3;
    }    
    storage.OrpValue = OrpLastValue + ChlCumul/36000.*(double)((millis()-OrpLastTime)/3600000.);
    OrpLastValue = storage.OrpValue;
    OrpLastTime = millis();    
  } 
#endif

  //PSI (water pressure)
  //float psi_sensor_value = ((analogRead(PSI_MEASURE) * 0.03) - 0.5) * 5.0 / 4.0;                        // from 0.5 to 4.5V -> 0.0 to 5.0 Bar (depends on sensor ref!)                                                                           // Remove this line when sensor is integrated!!!
  storage.PSIValue = (storage.PSICalibCoeffs0 * psi_sensor_value) + storage.PSICalibCoeffs1;            //Calibrated sensor response based on multi-point linear regression
  samples_PSI.add(storage.PSIValue);                                                                    // compute average of PSI from last 5 measurements
  storage.PSIValue = samples_PSI.getAverage(3);


  Debug.print(DBG_DEBUG,"pH: %5.3fV - %4.2f - ORP: %5.3fV - %3.0fmV - PSI: %5.3fV - %4.2fBar\r",
    ph_sensor_value,storage.PhValue,orp_sensor_value,storage.OrpValue,psi_sensor_value,storage.PSIValue);

}

bool loadConfig()
{
  nvs.begin("PoolMaster",true);

  storage.ConfigVersion         = nvs.getUChar("ConfigVersion",0);
  storage.Ph_RegulationOnOff    = nvs.getBool("Ph_RegOnOff",false);
  storage.Orp_RegulationOnOff   = nvs.getBool("Orp_RegOnOff",false);  
  storage.AutoMode              = nvs.getBool("AutoMode",true);
  storage.FiltrationDuration    = nvs.getUChar("FiltrDuration",12);
  storage.FiltrationStart       = nvs.getUChar("FiltrStart",8);
  storage.FiltrationStop        = nvs.getUChar("FiltrStop",20);
  storage.FiltrationStartMin    = nvs.getUChar("FiltrStartMin",8);
  storage.FiltrationStopMax     = nvs.getUChar("FiltrStopMax",22);
  storage.DelayPIDs             = nvs.getUChar("DelayPIDs",0);
  storage.PhPumpUpTimeLimit     = nvs.getULong("PhPumpUTL",900);
  storage.ChlPumpUpTimeLimit    = nvs.getULong("ChlPumpUTL",2500);
  storage.PublishPeriod         = nvs.getULong("PublishPeriod",30000);
  storage.PhPIDWindowSize       = nvs.getULong("PhPIDWSize",60000);
  storage.OrpPIDWindowSize      = nvs.getULong("OrpPIDWSize",60000);
  storage.PhPIDwindowStartTime  = nvs.getULong("PhPIDwStart",0);
  storage.OrpPIDwindowStartTime = nvs.getULong("OrpPIDwStart",0);
  storage.Ph_SetPoint           = nvs.getDouble("Ph_SetPoint",7.3);
  storage.Orp_SetPoint          = nvs.getDouble("Orp_SetPoint",750);
  storage.PSI_HighThreshold     = nvs.getDouble("PSI_High",0.5);
  storage.PSI_MedThreshold      = nvs.getDouble("PSI_Med",0.25);
  storage.WaterTempLowThreshold = nvs.getDouble("WaterTempLow",10.);
  storage.WaterTemp_SetPoint    = nvs.getDouble("WaterTempSet",27.);
  storage.TempExternal          = nvs.getDouble("TempExternal",3.);
  storage.pHCalibCoeffs0        = nvs.getDouble("pHCalibCoeffs0",4.3);
  storage.pHCalibCoeffs1        = nvs.getDouble("pHCalibCoeffs1",-2.63);
  storage.OrpCalibCoeffs0       = nvs.getDouble("OrpCalibCoeffs0",-1189.);
  storage.OrpCalibCoeffs1       = nvs.getDouble("OrpCalibCoeffs1",2564.);
  storage.PSICalibCoeffs0       = nvs.getDouble("PSICalibCoeffs0",1.11);
  storage.PSICalibCoeffs1       = nvs.getDouble("PSICalibCoeffs1",0.);
  storage.Ph_Kp                 = nvs.getDouble("Ph_Kp",2000000.);
  storage.Ph_Ki                 = nvs.getDouble("Ph_Ki",0.);
  storage.Ph_Kd                 = nvs.getDouble("Ph_Kd",0.);
  storage.Orp_Kp                = nvs.getDouble("Orp_Kp",2500.);
  storage.Orp_Ki                = nvs.getDouble("Orp_Ki",0.);
  storage.Orp_Kd                = nvs.getDouble("Orp_Kd",0.);
  storage.PhPIDOutput           = nvs.getDouble("PhPIDOutput",0.);
  storage.OrpPIDOutput          = nvs.getDouble("OrpPIDOutput",0.);
  storage.TempValue             = nvs.getDouble("TempValue",18.);
  storage.PhValue               = nvs.getDouble("PhValue",0.);
  storage.OrpValue              = nvs.getDouble("OrpValue",0.);
  storage.PSIValue              = nvs.getDouble("PSIValue",0.4);
  storage.AcidFill              = nvs.getDouble("AcidFill",100.);
  storage.ChlFill               = nvs.getDouble("ChlFill",100.);
  storage.pHTankVol             = nvs.getDouble("pHTankVol",20.);
  storage.ChlTankVol            = nvs.getDouble("ChlTankVol",20.);
  storage.pHPumpFR              = nvs.getDouble("pHPumpFR",1.5);
  storage.ChlPumpFR             = nvs.getDouble("ChlPumpFR",1.5);

  nvs.end();

  Serial << storage.ConfigVersion << '\n';
  Serial << storage.Ph_RegulationOnOff << ", " << storage.Orp_RegulationOnOff << ", " << storage.AutoMode << '\n';
  Serial << storage.FiltrationDuration << ", " << storage.FiltrationStart << ", " << storage.FiltrationStop << ", " << storage.FiltrationStartMin << ", " << storage.FiltrationStopMax << ", " << storage.DelayPIDs << '\n';
  Serial << storage.PhPumpUpTimeLimit << ", " << storage.ChlPumpUpTimeLimit << ", " << storage.PublishPeriod << '\n';
  Serial << storage.PhPIDWindowSize << ", " << storage.OrpPIDWindowSize << ", " << storage.PhPIDwindowStartTime << ", " << storage.OrpPIDwindowStartTime << '\n';
  Serial << storage.Ph_SetPoint << ", " << storage.Orp_SetPoint << ", " << storage.PSI_HighThreshold << ", " << storage.PSI_MedThreshold << ", " << storage.WaterTempLowThreshold << ", " << storage.WaterTemp_SetPoint << ", " << storage.TempExternal << ", " << storage.pHCalibCoeffs0 << ", " << storage.pHCalibCoeffs1 << ", " << storage.OrpCalibCoeffs0 << ", " << storage.OrpCalibCoeffs1 << ", " << storage.PSICalibCoeffs0 << ", " << storage.PSICalibCoeffs1 << '\n';
  Serial << storage.Ph_Kp << ", " << storage.Ph_Ki << ", " << storage.Ph_Kd << ", " << storage.Orp_Kp << ", " << storage.Orp_Ki << ", " << storage.Orp_Kd << ", " << storage.PhPIDOutput << ", " << storage.OrpPIDOutput << ", " << storage.TempValue << ", " << storage.PhValue << ", " << storage.OrpValue << ", " << storage.PSIValue << '\n';
  Serial << storage.AcidFill << ", " << storage.ChlFill << ", " << storage.pHTankVol << ", " << storage.ChlTankVol << ", " << storage.pHPumpFR << ", " << storage.ChlPumpFR << '\n';
  
  return (storage.ConfigVersion == CONFIG_VERSION);
}

bool saveConfig()
{
  nvs.begin("PoolMaster",false);

  size_t i = nvs.putUChar("ConfigVersion",storage.ConfigVersion);
  i += nvs.putBool("Ph_RegOnOff",storage.Ph_RegulationOnOff);
  i += nvs.putBool("Orp_RegOnOff",storage.Orp_RegulationOnOff);  
  i += nvs.putBool("AutoMode",storage.AutoMode);
  i += nvs.putUChar("FiltrDuration",storage.FiltrationDuration);
  i += nvs.putUChar("FiltrStart",storage.FiltrationStart);
  i += nvs.putUChar("FiltrStop",storage.FiltrationStop);
  i += nvs.putUChar("FiltrStartMin",storage.FiltrationStartMin);
  i += nvs.putUChar("FiltrStopMax",storage.FiltrationStopMax);
  i += nvs.putUChar("DelayPIDs",storage.DelayPIDs);
  i += nvs.putULong("PhPumpUTL",storage.PhPumpUpTimeLimit);
  i += nvs.putULong("ChlPumpUTL",storage.ChlPumpUpTimeLimit);
  i += nvs.putULong("PublishPeriod",storage.PublishPeriod);
  i += nvs.putULong("PhPIDWSize",storage.PhPIDWindowSize);
  i += nvs.putULong("OrpPIDWSize",storage.OrpPIDWindowSize);
  i += nvs.putULong("PhPIDwStart",storage.PhPIDwindowStartTime);
  i += nvs.putULong("OrpPIDwStart",storage.OrpPIDwindowStartTime);
  i += nvs.putDouble("Ph_SetPoint",storage.Ph_SetPoint);
  i += nvs.putDouble("Orp_SetPoint",storage.Orp_SetPoint);
  i += nvs.putDouble("PSI_High",storage.PSI_HighThreshold);
  i += nvs.putDouble("PSI_Med",storage.PSI_MedThreshold);
  i += nvs.putDouble("WaterTempLow",storage.WaterTempLowThreshold);
  i += nvs.putDouble("WaterTempSet",storage.WaterTemp_SetPoint);
  i += nvs.putDouble("TempExternal",storage.TempExternal);
  i += nvs.putDouble("pHCalibCoeffs0",storage.pHCalibCoeffs0);
  i += nvs.putDouble("pHCalibCoeffs1",storage.pHCalibCoeffs1);
  i += nvs.putDouble("OrpCalibCoeffs0",storage.OrpCalibCoeffs0);
  i += nvs.putDouble("OrpCalibCoeffs1",storage.OrpCalibCoeffs1);
  i += nvs.putDouble("PSICalibCoeffs0",storage.PSICalibCoeffs0);
  i += nvs.putDouble("PSICalibCoeffs1",storage.PSICalibCoeffs1);
  i += nvs.putDouble("Ph_Kp",storage.Ph_Kp);
  i += nvs.putDouble("Ph_Ki",storage.Ph_Ki);
  i += nvs.putDouble("Ph_Kd",storage.Ph_Kd);
  i += nvs.putDouble("Orp_Kp",storage.Orp_Kp);
  i += nvs.putDouble("Orp_Ki",storage.Orp_Ki);
  i += nvs.putDouble("Orp_Kd",storage.Orp_Kd);
  i += nvs.putDouble("PhPIDOutput",storage.PhPIDOutput);
  i += nvs.putDouble("OrpPIDOutput",storage.OrpPIDOutput);
  i += nvs.putDouble("TempValue",storage.TempValue);
  i += nvs.putDouble("PhValue",storage.PhValue);
  i += nvs.putDouble("OrpValue",storage.OrpValue);
  i += nvs.putDouble("PSIValue",storage.PSIValue);
  i += nvs.putDouble("AcidFill",storage.AcidFill);
  i += nvs.putDouble("ChlFill",storage.ChlFill);
  i += nvs.putDouble("pHTankVol",storage.pHTankVol);
  i += nvs.putDouble("ChlTankVol",storage.ChlTankVol);
  i += nvs.putDouble("pHPumpFR",storage.pHPumpFR);
  i += nvs.putDouble("ChlPumpFR",storage.ChlPumpFR);

  nvs.end();

  Debug.print(DBG_INFO,"Bytes saved: %d / %d\n",i,sizeof(storage));
  return (i == sizeof(storage)) ;

}

// functions to save any type of parameter (4 overloads with same name but different arguments)

bool saveParam(const char* key, uint8_t val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putUChar(key,val);
  return(i == sizeof(val));
}

bool saveParam(const char* key, bool val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putBool(key,val);
  return(i == sizeof(val));
}

bool saveParam(const char* key, unsigned long val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putULong(key,val);
  return(i == sizeof(val));
}

bool saveParam(const char* key, double val)
{
  nvs.begin("PoolMaster",false);
  size_t i = nvs.putDouble(key,val);
  return(i == sizeof(val));
}

//Compute free RAM
//useful to check if it does not shrink over time
int freeRam () {
  int v = xPortGetFreeHeapSize();
  return v;
}

////////////////////////gettemp state machine///////////////////////////////////
//Init DS18B20 one-wire library
void gettemp_start()
{
  // Start up the library
  sensors_W.begin();
  sensors_W.begin(); //Work-around of a OneWire library bug for enumeration
  sensors_A.begin();

  Debug.print(DBG_INFO,"1wire W devices: %d device(s) found",sensors_W.getDeviceCount());
  Debug.print(DBG_INFO,"1wire A devices: %d device(s) found",sensors_A.getDeviceCount());

  if (!sensors_W.getAddress(DS18B20_W, 0)) Debug.print(DBG_ERROR,"Unable to find address for bus W");
    else {
      Serial.printf("DS18B20_W: ");
      for(uint8_t i=0;i<8;i++){
        Serial.printf("%02x",DS18B20_W[i]);
        if(i<7) Serial.print(":");
          else Serial.printf("\r\n");
      }
    }  
  if (!sensors_A.getAddress(DS18B20_A, 0)) Debug.print(DBG_ERROR,"Unable to find address for bus A");  
    else {
      Serial.printf("DS18B20_A: ");
      for(uint8_t i=0;i<8;i++){
        Serial.printf("%02x",DS18B20_A[i]);
        if(i<7) Serial.print(":");
          else Serial.printf("\r\n");
      }
    } 

  // set the resolution
  sensors_W.setResolution(DS18B20_W, TEMPERATURE_RESOLUTION);
  sensors_A.setResolution(DS18B20_A, TEMPERATURE_RESOLUTION);

  //don't wait ! Asynchronous mode
  sensors_W.setWaitForConversion(false);
  sensors_A.setWaitForConversion(false);

  gettemp.next(gettemp_request);
}

//Request temperature asynchronously
void gettemp_request()
{
  sensors_W.requestTemperatures();
  sensors_A.requestTemperatures();
  gettemp.next(gettemp_wait);
}

//Wait asynchronously for requested temperature measurement
void gettemp_wait()
{ //we need to wait that time for conversion to finish
  if (gettemp.elapsed(1000 / (1 << (12 - TEMPERATURE_RESOLUTION))))
    gettemp.next(gettemp_read);
}

//read and print temperature measurement
//in case of reading error, the buffer is not updated and the last value is kept
void gettemp_read()
{
  double temp = sensors_W.getTempC(DS18B20_W);
  if (temp == NAN || temp == -127) {
    Debug.print(DBG_WARNING,"Error getting Water temperature");
  }  else storage.TempValue = temp;
  samples_WTemp.add(storage.TempValue);
  storage.TempValue = samples_WTemp.getAverage(5);
  Debug.print(DBG_DEBUG,"DS18B20_W: %6.2f°C",storage.TempValue);

  temp = sensors_A.getTempC(DS18B20_A);
  if (temp == NAN || temp == -127) {
    Debug.print(DBG_WARNING,"Error getting Air temperature");
  }  else storage.TempExternal = temp;
  samples_ATemp.add(storage.TempExternal);
  storage.TempExternal = samples_ATemp.getAverage(5);
  Debug.print(DBG_DEBUG,"DS18B20_A: %6.2f°C",storage.TempExternal);

  gettemp.next(gettemp_request);
}

void setPublishPeriod(unsigned long period){
  t4.setPeriodMs(period); //in msecs
}

void StartTime(){
  configTime(0, 0,"0.pool.ntp.org","1.pool.ntp.org","2.pool.ntp.org"); // 3 possible NTP servers
  setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",3);                       // configure local time with automatic DST  
  tzset();
  delay(200);
  Debug.print(DBG_INFO,"NTP configured");
}

void readLocalTime(){
  if(!getLocalTime(&timeinfo)){
    Debug.print(DBG_WARNING,"Failed to obtain time");
    StartTime();
  }
  Serial.println(&timeinfo,"%A, %B %d %Y %H:%M:%S");
}