#define BLYNK_TEMPLATE_ID "TMPL3UagrVLXi"
#define BLYNK_TEMPLATE_NAME "eggIncubator"
#define BLYNK_AUTH_TOKEN "knXOhD6Pk5zFg2fgDig0x--OF1uaMMk7"

#define TINY_GSM_MODEM_SIM7600 
#define BLYNK_PRINT Serial

#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGsmClient.h>
#include <BlynkSimpleTinyGSM.h>

// Pins
#define SENSOR_PIN 4
#define FLOAT_SWITCH 13
#define RXD2 17         
#define TXD2 16         
#define RXD1 32         
#define TXD1 33         

// Relays (NC Logic: 0/LOW = ON, 1/HIGH = OFF)
const int sw2_LIGHT = 2, R_HEAT1 = 12, R_HEAT2 = 14, R_TILT_UP = 15, R_TILT_DN = 25, sw3_HUMIDITY = 26, R_FAN2_HIGH = 27;
int relayPins[] = {2, 12, 14, 15, 25, 26, 27};

// Globals
float tempThreshold = 35.0;
float humThreshold = 70.0;
const char apn[] = "airteliot.com"; 
bool tiltDirection = true; 

HardwareSerial simSerial(2);   
HardwareSerial nexSerial(1);   
TinyGsm modem(simSerial);
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
Adafruit_SHT4x sht4 = Adafruit_SHT4x();
BlynkTimer timer;
bool shtFound = false;

// --- THE MASTER 2-SECOND SYNC & DATA FUNCTION ---
void refreshAllData() {
  // 1. Read Sensors
  sensors.requestTemperatures(); 
  float dsTemp = sensors.getTempCByIndex(0);
  sensors_event_t humidity, s_temp;
  float shtHum = 0, shtTemp = 0;
  if (shtFound) { sht4.getEvent(&humidity, &s_temp); shtHum = humidity.relative_humidity; shtTemp = s_temp.temperature; }
  float avgTemp = (dsTemp + shtTemp) / 2.0;
  int tank = digitalRead(FLOAT_SWITCH);

  // 2. Automated Safety & Heat Logic
  if (avgTemp > tempThreshold || shtHum > humThreshold) {
    digitalWrite(R_FAN2_HIGH, 0); 
    digitalWrite(R_HEAT2, 1);     
  } else {
    digitalWrite(R_FAN2_HIGH, 1); 
    if (digitalRead(R_HEAT1) == 0 && avgTemp < (tempThreshold - 5.0)) digitalWrite(R_HEAT2, 0); 
    else digitalWrite(R_HEAT2, 1);
  }

  // 3. Process Modem Time & Signal
  int csq = modem.getSignalQuality();
  String dateTime = modem.getGSMDateTime(DATE_TIME); 
  String timeOnly = "--:--";
  int colonPos = dateTime.indexOf(':');
  if (colonPos != -1 && colonPos >= 2) {
      timeOnly = dateTime.substring(colonPos - 2, colonPos + 3);
  }

  // 4. INTERLOCKED TILT RELAY CONTROL
  if (tiltDirection == true) { 
    digitalWrite(R_TILT_DN, 1); // Ensure Down is OFF
    digitalWrite(R_TILT_UP, 0); // Turn Up ON
  } else {
    digitalWrite(R_TILT_UP, 1); // Ensure Up is OFF
    digitalWrite(R_TILT_DN, 0); // Turn Down ON
  }

  // 5. Read UI States
  int uiL = (digitalRead(sw2_LIGHT) == 0) ? 1 : 0;
  int ui1 = (digitalRead(R_HEAT1) == 0) ? 1 : 0;
  int ui2 = (digitalRead(R_HEAT2) == 0) ? 1 : 0;
  int uiH = (digitalRead(sw3_HUMIDITY) == 0) ? 1 : 0;

  // 6. UPDATE NEXTION
  nexSerial.print("x0.val=" + String((int)(avgTemp * 10)) + "\xFF\xFF\xFF");
  nexSerial.print("x2.val=" + String((int)(shtHum * 10)) + "\xFF\xFF\xFF");
  nexSerial.print("t17.txt=\"" + timeOnly + "\"\xFF\xFF\xFF");
  nexSerial.print("t19.txt=\"" + String(csq) + "\"\xFF\xFF\xFF");
  nexSerial.print("t16.txt=\"" + String(tiltDirection ? "DOWN" : "UP") + "\"\xFF\xFF\xFF");
  
  int lightPic = (uiL == 1) ? 12 : 11;
  nexSerial.print("sw2.val=" + String(uiL) + "\xFF\xFF\xFF");
  nexSerial.print("sw2.pic=" + String(lightPic) + "\xFF\xFF\xFF");
  nexSerial.print("sw3.val=" + String(uiH) + "\xFF\xFF\xFF");
  uint32_t sw3Color = (uiH == 1) ? 2016 : 63488;
  nexSerial.print("sw3.bco=" + String(sw3Color) + "\xFF\xFF\xFF");
  nexSerial.print("sw0.val=" + String(ui1) + "\xFF\xFF\xFF");
  nexSerial.print("sw1.val=" + String(ui2) + "\xFF\xFF\xFF");
  nexSerial.print("x1.val=" + String((int)(tempThreshold * 10)) + "\xFF\xFF\xFF");
  nexSerial.print("x3.val=" + String((int)(humThreshold * 10)) + "\xFF\xFF\xFF");
  
  if (tank == 0) { nexSerial.print("b6.pic=12\xFF\xFF\xFF"); nexSerial.print("b9.txt=\"EMPTY\"\xFF\xFF\xFF"); }
  else { nexSerial.print("b6.pic=10\xFF\xFF\xFF"); nexSerial.print("b9.txt=\"FULL\"\xFF\xFF\xFF"); }

  // 7. UPDATE BLYNK
  Blynk.virtualWrite(V0, dsTemp); 
  Blynk.virtualWrite(V1, shtTemp);
  Blynk.virtualWrite(V2, shtHum);
  Blynk.virtualWrite(V3, (tank == 0) ? "Empty" : "Full");
  Blynk.virtualWrite(V10, uiL);
  Blynk.virtualWrite(V11, ui1);
  Blynk.virtualWrite(V12, ui2);
  Blynk.virtualWrite(V4, uiH);
  Blynk.virtualWrite(V5, (tiltDirection ? 1 : 0));
  Blynk.virtualWrite(V20, tempThreshold);
  Blynk.virtualWrite(V21, humThreshold);
}

// --- BLYNK INPUTS ---
BLYNK_WRITE(V10) { digitalWrite(sw2_LIGHT, (param.asInt() == 1) ? 0 : 1); refreshAllData(); }
BLYNK_WRITE(V11) { 
  int val = param.asInt();
  digitalWrite(R_HEAT1, (val == 1) ? 0 : 1); 
  if (val == 0) digitalWrite(R_HEAT2, 1); 
  refreshAllData(); 
}
BLYNK_WRITE(V12) { 
  if (digitalRead(R_HEAT1) == 0) digitalWrite(R_HEAT2, (param.asInt() == 1) ? 0 : 1);
  refreshAllData(); 
}
BLYNK_WRITE(V4) { digitalWrite(sw3_HUMIDITY, (param.asInt() == 1) ? 0 : 1); refreshAllData(); } 
BLYNK_WRITE(V5)  { tiltDirection = (param.asInt() == 1); refreshAllData(); }
BLYNK_WRITE(V20) { tempThreshold = param.asFloat(); refreshAllData(); }
BLYNK_WRITE(V21) { humThreshold = param.asFloat(); refreshAllData(); }

void setup() {
  Serial.begin(115200);
  for (int i=0; i<7; i++) { pinMode(relayPins[i], OUTPUT); digitalWrite(relayPins[i], 1); }
  pinMode(FLOAT_SWITCH, INPUT_PULLUP);
  Wire.begin(); 
  if (sht4.begin()) shtFound = true;
  sensors.begin(); 
  
  nexSerial.begin(9600, SERIAL_8N1, RXD1, TXD1); 
  nexSerial.setTimeout(50);
  simSerial.begin(115200, SERIAL_8N1, RXD2, TXD2);
  delay(5000); 
  
  Blynk.begin(BLYNK_AUTH_TOKEN, modem, apn, "", "", "blynk.cloud", 80);
  
  modem.sendAT("+CTZU=1"); modem.waitResponse();
  modem.sendAT("+CLTS=1"); modem.waitResponse();

  Blynk.syncAll();
  timer.setInterval(2000L, refreshAllData); 
}

void loop() {
  Blynk.run();
  timer.run();
  
  if (nexSerial.available() > 0) {
    String nexData = nexSerial.readStringUntil('\n'); 
    nexData.trim();
    
    if (nexData.startsWith("L")) {
      digitalWrite(sw2_LIGHT, (nexData.charAt(1) == '1') ? 0 : 1);
      refreshAllData();
    }
    else if (nexData.startsWith("H")) {
      digitalWrite(sw3_HUMIDITY, (nexData.charAt(1) == '1') ? 0 : 1);
      refreshAllData();
    }
    else if (nexData.indexOf("T4") != -1) { tiltDirection = true; refreshAllData(); }
    else if (nexData.indexOf("T5") != -1) { tiltDirection = false; refreshAllData(); }
    else if (nexData.indexOf("TUP") != -1) { tempThreshold += 0.5; refreshAllData(); }
    else if (nexData.indexOf("TDN") != -1) { tempThreshold -= 0.5; refreshAllData(); }
    else if (nexData.indexOf("HUP") != -1) { humThreshold += 1; refreshAllData(); }
    else if (nexData.indexOf("HDN") != -1) { humThreshold -= 1; refreshAllData(); }
    
    else if (nexData.indexOf("R1") != -1) { 
      int pos = nexData.indexOf("R1");
      int s = (nexData.charAt(pos + 2) == '1') ? 1 : 0; 
      digitalWrite(R_HEAT1, (s == 1) ? 0 : 1); 
      if (s == 0) digitalWrite(R_HEAT2, 1); 
      refreshAllData(); 
    } 
    else if (nexData.indexOf("R2") != -1) { 
      int pos = nexData.indexOf("R2");
      int s = (nexData.charAt(pos + 2) == '1') ? 1 : 0; 
      if (digitalRead(R_HEAT1) == 0) digitalWrite(R_HEAT2, (s == 1) ? 0 : 1);
      refreshAllData();
    }
  }
}