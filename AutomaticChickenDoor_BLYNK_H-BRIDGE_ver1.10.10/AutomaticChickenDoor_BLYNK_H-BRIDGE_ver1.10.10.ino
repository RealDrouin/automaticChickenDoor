/* Automatic Door sunshine sensor nodemcu esp8266, core 2.4.2, ide 1.8.5
   Made by Real Drouin 2020/jun/05

  IoT Internet of thing 'Blynk' and 'IFTTT' notification.

  ////////////////////////////////////////////////////////////////////////////////////
  H-Bridge tle5206-2s {Motor Speed Control} with (error flag output) and short protect.

  pin1 = OUT1               Connect to Motor
  pin2 = Error Flag         Connect to D7 with PullUp 2k to 3v NodeMcu
  pin3 = IN1                Connect to D6
  pin4 = GND                Connect to GND
  pin5 = IN2                Connect to D5
  pin6 = Vs (Motor Vcc)     Connect to 12v with Capacitor 100uf to gnd and *
  pin7 = OUT2               Connect to Motor

      install cap 100nf from pin6 and gnd as close as possible to the h-bridge.
  //////////////////////////////////////////////////////////////////////////

    install pullup 10k from 3v to D4 with PushButton to Gnd.
    Press 10sec to Setup Mode.

    install pullup 10k from 3v to D1 with Limit Reed Switch to Gnd.

                10k               LDR
    GND <----/\/\/\/\----A0----/\/\/\/\----3v

*/

const String ver = "Ver 1.10.10";

#include <NTPClient.h>
#include <Timezone.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>

#define SENSOR_PIN A0         // Light Sensor CDS

#include <ESP8266mDNS.h>

#include <BlynkSimpleEsp8266.h>

// Define NTP properties
#define NTP_OFFSET   0     // In seconds
#define NTP_INTERVAL 30 * 60000    // In miliseconds
#define NTP_ADDRESS  "ca.pool.ntp.org"  // change this to whatever pool is closest (see ntp.org)

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

String openTime;    // display value on webPage
String closeTime;   // display value on webPage

//open time
int shs;
int smins;
//close time
int ehs;
int emins;

String AuthToken = "";
String BlynkServer = "";
int doorTimer = 0;  // display on webpage
int BlynkPort;
volatile bool Connected2Blynk = false;
volatile bool Setup = false;
volatile bool notification = true;
bool isFirstConnect = true;

// IFTTT
String IftttKey = "";
String event = "";

unsigned int lux;             //Live Read LightSensor
unsigned int lightTrigger;

const int limitSwitch = 5; // D1 Limite Reed Switch at open possition
bool automatic = true;

// Door Logic State
bool openDoorBegin = false;   //enable ajax display count down timer
bool closeDoorBegin = false;  //enable ajax display count down timer
bool doorState;
bool openSunlight = true;
bool closeSunlight = true;

// tle5206 H-Bridge
const int doorDownPin = 12;   // D6 to H-Bridge
const int doorUpPin = 14;     // D5 to H-Bridge
const int errorFlagPin = 13;  // D7 Normal HIGH, Error Flag at LOW
bool errorFlag = false;
int pwm = 0;
byte pwmByte = 0;

///////// Push Button /////////
const int Button = 2; // D4 push 10sec for reset, push 1sec to open or close door.
byte PushButtonCount = 0;
volatile bool PushButton;

// System Uptime
byte hh = 0; byte mi = 0; byte ss = 0; // hours, minutes, seconds
unsigned int dddd = 0; // days (for if you want a calendar)
unsigned long lastTick = 0; // time that the clock last "ticked"
char timestring[25]; // for output

////////////
// Millis //
////////////////////////////////////////////////
const unsigned long check = 30000; // check wifi connected every 30sec
unsigned long previouscheck = 0;

int motorCloseMillis = 0;

int openTimerTriggerMillis; // millis sec delay before door open.
int openTimerTrigger; // minute delay before door open.
unsigned long previousOpenTimerTrigger = 0;

int closeTimerTriggerMillis; // millis sec delay before door close.
int closeTimerTrigger; // minute delay before door close.
unsigned long previousCloseTimerTrigger = 0;
/////////// End Millis //////////

String webSite, javaScript, XML, header, footer, ssid, password;

///////// Button CSS /////////
const String button = ".button {background-color: white;border: 2px solid #555555;color: black;padding: 16px 32px;text-align: center;text-decoration: none;display: inline-block;font-size: 16px;margin: 4px 2px;-webkit-transition-duration: 0.4s;transition-duration: 0.4s;cursor: pointer;}.button:hover {background-color: #555555;color: white;}.disabled {opacity: 0.6;pointer-events: none;}";
/////////////////////////////

byte percentQ = 0; // wifi signal strength %

ESP8266WebServer server(80);
WiFiClient client;
String readString;

// OTA UPDATE
ESP8266HTTPUpdateServer httpUpdater; //ota

///////////
// Blynk //
///////////////////////////////////////////
BlynkTimer timer;

BLYNK_WRITE(V0) {
  bool control = param.asInt();

  if (errorFlag == false) {
    Blynk.setProperty(V0, "onLabel", "...");
    Blynk.setProperty(V0, "offLabel", "...");

    if (control == true) {
      Open();
    } else if (control == false) {
      Close();
    }
  }
}

BLYNK_WRITE(V3) {
  bool control = param.asInt();

  if (control == true) {
    Blynk.setProperty(V0, "onLabel", "Rebooting!");
    Blynk.setProperty(V0, "offLabel", "Rebooting!");
    Blynk.notify("ChickenCoop, Rebooting!");

    delay(1000);
    WiFi.disconnect();
    delay(3000);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(limitSwitch, INPUT_PULLUP); // Opened door possition only.

  pinMode(Button, INPUT_PULLUP);

  pinMode(errorFlagPin, INPUT_PULLUP);

  pinMode(doorUpPin, OUTPUT);
  digitalWrite(doorUpPin, LOW);

  pinMode(doorDownPin, OUTPUT);
  digitalWrite(doorDownPin, LOW);

  //////////////////////////////////////
  // START EEPROM
  EEPROM.begin(512);
  delay(200);

  //////////////////////////////////////
  // Init EEPROM
  byte Init = EEPROM.read(451);

  if (Init != 111) {
    for (int i = 0; i < 512; ++i)
    {
      EEPROM.write(i, 0);
    }
    delay(100);
    EEPROM.commit();
    delay(200);

    EEPROM.write(451, 111);
    delay(100);
    EEPROM.commit();
    delay(200);
  }

  //////////////////////////////////////
  // READ EEPROM SSID & PASSWORD
  String esid;
  for (int i = 34; i < 67; ++i)
  {
    esid += char(EEPROM.read(i));
  }
  delay(200);
  ssid = esid;

  int IndexPASS = EEPROM.read(67) + 68;
  String epass = "";
  for (int i = 68; i < IndexPASS; ++i)
  {
    epass += char(EEPROM.read(i));
  }
  delay(200);
  password = epass;

  //////////////////////////////////////
  // READ EEPROM luxTrigger
  lightTrigger = EEPROM.read(101);

  //////////////////////////////////////
  // READ EEPROM OpenTimerTrigger
  openTimerTrigger = EEPROM.read(106);
  openTimerTriggerMillis = openTimerTrigger * 60000;

  //////////////////////////////////////
  // READ EEPROM CloseTimerTrigger
  closeTimerTrigger = EEPROM.read(109);
  closeTimerTriggerMillis = closeTimerTrigger * 60000;

  //////////////////////////////////////
  // Ifttt Key
  int KeyIndex = EEPROM.read(110) + 111;
  String Key = "";
  for (int i = 111; i < KeyIndex; ++i)
  {
    Key += char(EEPROM.read(i));
  }
  IftttKey = Key;

  //////////////////////////////////////
  // OpenTime
  int IndexOpenTime = EEPROM.read(150) + 151;
  String ReadOpenTime = "";
  for (int i = 151; i < IndexOpenTime; ++i)
  {
    ReadOpenTime += char(EEPROM.read(i));
  }
  ////// Open
  String openHeurs = getValue(ReadOpenTime, ':', 0);
  String openMinute = getValue(ReadOpenTime, ':', 1);
  shs = (openHeurs.toInt());
  smins = (openMinute.toInt());

  openTime = ReadOpenTime;

  //////////////////////////////////////
  // CloseTime
  int IndexCloseTime = EEPROM.read(160) + 161;
  String ReadCloseTime = "";
  for (int i = 161; i < IndexCloseTime; ++i)
  {
    ReadCloseTime += char(EEPROM.read(i));
  }
  ////// Close
  String closeHeurs = getValue(ReadCloseTime, ':', 0);
  String closeMinute = getValue(ReadCloseTime, ':', 1);
  ehs = (closeHeurs.toInt());
  emins = (closeMinute.toInt());

  closeTime = ReadCloseTime;

  //////////////////////////////////////
  // Blynk Ip Local Server
  int IndexBlynkServer = EEPROM.read(306) + 307;
  String ReadBlynkServer = "";
  for (int i = 307; i < IndexBlynkServer; ++i)
  {
    ReadBlynkServer += char(EEPROM.read(i));
  }
  BlynkServer = ReadBlynkServer;

  /////////////////////////////////////
  // Blynk Port Local Server
  int IndexBlynkPort = EEPROM.read(390) + 391;
  String ReadBlynkPort = "";
  for (int i = 391; i < IndexBlynkPort; ++i)
  {
    ReadBlynkPort += char(EEPROM.read(i));
  }
  BlynkPort = ReadBlynkPort.toInt();

  //////////////////////////////////////
  //BLYNK AUTH TOKEN
  int IndexToken = EEPROM.read(400) + 401;
  String ReadToken = "";
  for (int i = 401; i < IndexToken; ++i)
  {
    ReadToken += char(EEPROM.read(i));
  }
  AuthToken = ReadToken;

  //////////////////////////////////////
  // READ EEPROM Motor Close Delay
  int closemillisIndex = EEPROM.read(442) + 443;
  delay(200);
  String closemillisMotor = "";
  for (int i = 443; i < closemillisIndex; ++i)
  {
    closemillisMotor += char(EEPROM.read(i));
  }
  motorCloseMillis = closemillisMotor.toInt();

  //////////////////////////////////////
  // READ EEPROM Motor Speed
  pwmByte = EEPROM.read(450);
  pwm = map(pwmByte, 100, 0, 0, 1023);

  EEPROM.end();

  delay(100);

  ///////////////////
  // SSID PASSWORD //
  ///////////////////////////////////////////////////////////////
  server.on("/WiFi", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String WifiSsid = server.arg("ssid");
    String WifiPassword = server.arg("pass");

    if (WifiPassword.length() > 0 and WifiPassword.length() < 33) {
      // START EEPROM
      EEPROM.begin(512);
      delay(200);

      for (int i = 34; i < 100; ++i)
      {
        EEPROM.write(i, 0);
      }
      delay(100);
      EEPROM.commit();
      delay(200);

      for (int i = 0; i < WifiSsid.length(); ++i)
      {
        EEPROM.write(34 + i, WifiSsid[i]);
      }


      EEPROM.write(67, WifiPassword.length());
      for (int i = 0; i < WifiPassword.length(); ++i)
      {
        EEPROM.write(68 + i, WifiPassword[i]);
      }

      delay(100);
      EEPROM.commit();
      delay(200);
      EEPROM.end();

      handleREBOOT();
    }
    else if (WifiPassword.length() <= 9 or WifiPassword.length() > 32) {
      server.send(200, "text/html", "<header><h1>Error!, Please enter valid PASS! max32 character <a href=/wifisetting >Back!</a></h1></header>");
    }
    else {
      server.send(200, "text/html", "<header><h1>Error!, Please enter PASS! <a href=/wifisetting >Back!</a></h1></header>");
    }
  });

  ///////////////
  // Blynk Key //
  ////////////////////////////////////////////////////////////
  server.on("/Blynk", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String ReadKey = server.arg("key");
    String ReadBlynkIp = server.arg("server");
    String ReadBlynkPort = server.arg("port");

    if (ReadKey.length() < 33) {
      EEPROM.begin(512);
      delay(200);

      EEPROM.write(400, ReadKey.length());
      for (int i = 0; i < ReadKey.length(); ++i)
      {
        EEPROM.write(401 + i, ReadKey[i]);
      }
      delay(100);
      EEPROM.commit();
      delay(200);

      AuthToken = ReadKey;
      EEPROM.end();

      if (BlynkServer.length() > 5) {
        Blynk.config(AuthToken.c_str(), BlynkServer.c_str(), BlynkPort);
      }
      else {
        Blynk.config(AuthToken.c_str(), "blynk-cloud.com", 8442);
      }

      delay(1000);
      Blynk.connect(3333);  // timeout set to 10 seconds and then continue without Blynk
    }
    handleBLYNK();
  });

  //////////////////
  // Blynk Server //
  ////////////////////////////////////////////////////////////
  server.on("/BlynkServer", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String ReadBlynkServer = server.arg("server");
    String ReadBlynkPort = server.arg("port");

    EEPROM.begin(512);
    delay(200);

    EEPROM.write(306, ReadBlynkServer.length());
    for (int i = 0; i < ReadBlynkServer.length(); ++i)
    {
      EEPROM.write(307 + i, ReadBlynkServer[i]);
    }

    EEPROM.write(390, ReadBlynkPort.length());
    for (int i = 0; i < ReadBlynkPort.length(); ++i)
    {
      EEPROM.write(391 + i, ReadBlynkPort[i]);
    }

    delay(100);
    EEPROM.commit();
    delay(200);
    EEPROM.end();

    BlynkServer = ReadBlynkServer;
    BlynkPort = ReadBlynkPort.toInt();

    if (BlynkServer.length() > 5) {
      Blynk.config(AuthToken.c_str(), BlynkServer.c_str(), BlynkPort);
    }
    else {
      Blynk.config(AuthToken.c_str(), "blynk-cloud.com", 8442);
    }

    delay(1000);
    Blynk.connect(3333);  // timeout set to 10 seconds and then continue without Blynk

    handleBLYNK();
  });

  ///////////////
  // Ifttt Key //
  ////////////////////////////////////////////////////////////
  server.on("/Iftttkey", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String ReadKey = server.arg("key");

    if (ReadKey.length() < 33) {
      EEPROM.begin(512);
      delay(200);

      EEPROM.write(110, ReadKey.length());
      for (int i = 0; i < ReadKey.length(); ++i)
      {
        EEPROM.write(111 + i, ReadKey[i]);
      }
      delay(100);
      EEPROM.commit();
      delay(200);

      IftttKey = ReadKey;
      EEPROM.end();
    }
    handleIFTTT();
  });

  /////////////
  //TimeOpen //
  ///////////////////////////////////////////////////////////////
  server.on("/TimeOpen", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String ReadOpenTime = server.arg("OpenTime");
    //int luxint = (luxRead.toInt());

    EEPROM.begin(512);
    delay(200);

    EEPROM.write(150, ReadOpenTime.length());
    for (int i = 0; i < ReadOpenTime.length(); ++i)
    {
      EEPROM.write(151 + i, ReadOpenTime[i]);
    }
    delay(100);
    EEPROM.commit();// save on eeprom
    delay(200);
    EEPROM.end();

    ////// Open
    String openHeurs = getValue(ReadOpenTime, ':', 0);
    String openMinute = getValue(ReadOpenTime, ':', 1);
    shs = (openHeurs.toInt());
    smins = (openMinute.toInt());

    openTime = ReadOpenTime;

    handleDOOR();
  });

  //////////////
  //TimeClose //
  ///////////////////////////////////////////////////////////////
  server.on("/TimeClose", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String ReadCloseTime = server.arg("CloseTime");

    EEPROM.begin(512);
    delay(200);

    EEPROM.write(160, ReadCloseTime.length());
    for (int i = 0; i < ReadCloseTime.length(); ++i)
    {
      EEPROM.write(161 + i, ReadCloseTime[i]);
    }
    delay(100);
    EEPROM.commit();// save on eeprom
    delay(200);
    EEPROM.end();

    ////// Close
    String closeHeurs = getValue(ReadCloseTime, ':', 0);
    String closeMinute = getValue(ReadCloseTime, ':', 1);
    ehs = (closeHeurs.toInt());
    emins = (closeMinute.toInt());

    closeTime = ReadCloseTime;

    handleDOOR();
  });

  /////////////////
  // Lux Setting //
  ///////////////////////////////////////////////////////////////
  server.on("/LuxTrigger", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String luxRead = server.arg("LuxSetting");
    int luxint = (luxRead.toInt());

    String openTimerRead = server.arg("TimerSettingOpen");
    int openTimerint = (openTimerRead.toInt());

    String closeTimerRead = server.arg("TimerSettingClose");
    int closeTimerint = (closeTimerRead.toInt());

    EEPROM.begin(512);
    delay(200);

    if (luxRead.length() > 0 ) {
      EEPROM.write(101, luxint);
      delay(100);
      lightTrigger = luxint;
    }

    if (openTimerRead.length() > 0) {
      EEPROM.write(106, openTimerint);
      delay(100);
      openTimerTrigger = openTimerint;
      openTimerTriggerMillis = openTimerTrigger * 60000;
    }

    if (closeTimerRead.length() > 0) {
      EEPROM.write(109, closeTimerint);
      delay(100);
      closeTimerTrigger = closeTimerint;
      closeTimerTriggerMillis = closeTimerTrigger * 60000;
    }

    EEPROM.commit();// save on eeprom
    delay(200);
    EEPROM.end();
    handleDOOR();
  });

  //////////////////
  // MotorActived //
  ////////////////////////////////////////////////////////////
  server.on("/MotorDelay", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    String speed = server.arg("Speed");

    if (speed.length() > 0) {
      EEPROM.begin(512);
      delay(200);
      pwmByte = speed.toInt();
      EEPROM.write(450, pwmByte);
      delay(100);

      EEPROM.commit(); // save on eeprom
      delay(200);

      pwm = map(pwmByte, 100, 0, 0, 1023);

      motorCloseMillis = 0;

      EEPROM.end();
    }
    handleCAL();
  });

  ///////////////
  // Open Door //
  ///////////////////////////////////////////////////////////////
  server.on("/dooropen", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }
    if (errorFlag == false) {
      Open();
    }
    handleDOOR();
  });

  ////////////////
  // Close Door //
  ///////////////////////////////////////////////////////////////
  server.on("/doorclose", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }
    if (errorFlag == false) {
      Close();
    }
    handleDOOR();
  });

  ////////////////
  // Turn Right //
  ///////////////////////////////////////////////////////////////
  server.on("/right", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    errorFlag = false;
    doorState = 1;

    String For = server.arg("For");

    analogWrite(doorDownPin, pwm);
    digitalWrite(doorUpPin, HIGH);

    delay(For.toInt());

    digitalWrite(doorUpPin, LOW);
    digitalWrite(doorDownPin, LOW);

    handleCAL();
  });

  ////////////////
  // Turn Left //
  ///////////////////////////////////////////////////////////////
  server.on("/left", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    errorFlag = false;
    doorState = 0;

    String For = server.arg("For");

    analogWrite(doorUpPin, pwm);
    digitalWrite(doorDownPin, HIGH);

    delay(For.toInt());

    digitalWrite(doorDownPin, LOW);
    digitalWrite(doorUpPin, LOW);

    handleCAL();
  });

  //////////////////////////////
  // Door Calibration Process //
  ///////////////////////////////////////////////////////////////
  server.on("/calibration", []() {
    if (WiFi.status() == WL_CONNECTED) {
      if (!server.authenticate("admin", password.c_str()))
        return server.requestAuthentication();
    }

    if (errorFlag == true) {
      server.send(200, "text/html", "<header><h1>Error Flag, Please Check Motor! Press <a href=/doorcalibration >Back!</a></h1></header>");
    }
    else {

      //  server.send(200, "text/html", "<header><h1>Calibration in Progress ..., Press <a href=/door >Back!</a> when Finish!</h1></header>");

      delay(2000);

      // Opening Door
      if (errorFlag == false) {
        Blynk.notify("ChickenCoop, Door Calibration Started!");
        analogWrite(doorDownPin, pwm);
        digitalWrite(doorUpPin, HIGH);
        bool limit = digitalRead(limitSwitch);

        while (limit == HIGH) {
          limit = digitalRead(limitSwitch);

          bool ef = digitalRead(errorFlagPin);
          if (ef == LOW) {
            digitalWrite(doorDownPin, LOW);
            digitalWrite(doorUpPin, LOW);
            errorFlag = true;

            break;
          }
          delay(1);
        }
        digitalWrite(doorUpPin, LOW);
        digitalWrite(doorDownPin, LOW);
      }
      /////////////////////////////////////////////////
      // Closing Door

      delay(2000);

      unsigned long startMillis;
      unsigned long endMillis;
      bool pushButton = HIGH;

      startMillis = millis();

      analogWrite(doorUpPin, pwm);
      digitalWrite(doorDownPin, HIGH);

      while (pushButton == HIGH) {
        pushButton = digitalRead(Button);

        bool ef = digitalRead(errorFlagPin);
        if (ef == LOW) {
          digitalWrite(doorDownPin, LOW);
          digitalWrite(doorUpPin, LOW);
          errorFlag = true;

          break;
        }
        delay(1);
      }

      endMillis = millis();
      digitalWrite(doorUpPin, LOW);
      digitalWrite(doorDownPin, LOW);

      motorCloseMillis = endMillis - startMillis;
      String closeMillis = String(motorCloseMillis);

      /////////////////////
      // Save on EEprom //
      /////////////////////////////////////////////////
      if (errorFlag == false and closeMillis.length() < 6) {
        EEPROM.begin(512);
        delay(200);

        EEPROM.write(442, closeMillis.length());
        for (int i = 0; i < closeMillis.length(); ++i)
        {
          EEPROM.write(443 + i, closeMillis[i]);
        }
        delay(100);
        EEPROM.commit();
        delay(200);
        EEPROM.end();
        doorState = false;

        Blynk.notify("ChickenCoop, Door Calibration Done!");
      }
      else {
        motorCloseMillis = 0;
      }
      handle_OnConnect();
    }
  });

  //////////////////////
  // WiFi Connection //
  ////////////////////////////////////////////////////////////
  if ((ssid != "") and (password != "")) {
    WiFi.disconnect();
    WiFi.softAP("ChickenCoop-Door", password.c_str());
    Serial.println(F("Connecting... to Network!"));
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    WiFi.hostname("ChickenCoop-Door");
    delay(100);
    WiFi.begin(ssid.c_str(), password.c_str());
    Setup = false;
  } else {
    WiFi.disconnect();
    delay(200);
    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAP("Door-Setup", "");
    Serial.println(F("Setup Mode Actived!"));
    Setup = true;
  }
  delay(5000);

  httpUpdater.setup(&server, "/firmware", "admin", password.c_str());

  server.onNotFound(handleNotFound);
  server.on("/", handle_OnConnect);
  server.on("/door", handleDOOR);
  server.on("/wifisetting", handleWIFISETTING);
  server.on("/blynk", handleBLYNK);
  server.on("/ifttt", handleIFTTT);
  server.on("/doorcalibration", handleCAL);
  server.on("/reboot", handleREBOOT);

  ///////////
  // Blynk //
  ///////////////////////////////////////////////////
  if (BlynkServer.length() > 5) {
    Blynk.config(AuthToken.c_str(), BlynkServer.c_str(), BlynkPort);
  }
  else {
    Blynk.config(AuthToken.c_str(), "blynk-cloud.com", 8442);
  }

  Blynk.connect(3333);  // timeout set to 10 seconds and then continue without Blynk
  timer.setInterval(1000l, ReadSENSOR);
  ///////////////////////////////////////////////////

  server.begin();
  delay(200);

  Serial.println(WiFi.localIP());

  if (!MDNS.begin("chickencoop-door")) {             // Start the mDNS responder for chickencoop-door.local
    Serial.println(F("Error setting up MDNS responder!"));
  } else {
    Serial.println(F("mDNS responder started"));
  }

  timeClient.begin(); //ntp
}

void loop() {
  timer.run();

  Connected2Blynk = Blynk.connected();
  if (Connected2Blynk) {
    Blynk.run();

    if (isFirstConnect == true && WiFi.status() == WL_CONNECTED) {
      Blynk.syncAll();
      isFirstConnect = false;
    }
  }

  server.handleClient();
  unsigned long currentMillis = millis();

  if ((currentMillis - previouscheck >= check)) {
    percentQ = 0;

    if (WiFi.RSSI() <= -100) {
      percentQ = 0;
    } else if (WiFi.RSSI() >= -50) {
      percentQ = 100;
    } else {
      percentQ = 2 * (WiFi.RSSI() + 100);
    }

    ///////////////////////////////////////////////////////////////////////////////////////

    if (Setup == false and WiFi.status() != WL_CONNECTED) {
      Serial.println(F("Reconnecting to WiFi..."));
      WiFi.disconnect();
      delay(100);
      WiFi.mode(WIFI_STA);
      delay(100);
      WiFi.hostname("ChickenCoop-Door");
      delay(100);
      WiFi.begin(ssid.c_str(), password.c_str());
    }
    else if (WiFi.status() == WL_CONNECTED) {
      String IP = (WiFi.localIP().toString());
      Serial.print(F("Connected! to "));
      Serial.print (WiFi.SSID());
      Serial.print (F(", Ip: "));
      Serial.println (IP);

      if (!Connected2Blynk && AuthToken.length() > 0) {
        if (BlynkServer.length() > 0) {
          Blynk.config(AuthToken.c_str(), BlynkServer.c_str(), BlynkPort);
        }
        else {
          Blynk.config(AuthToken.c_str(), "blynk-cloud.com", 8442);
        }
        Blynk.connect(3333);  // timeout set to 10 seconds and then continue without Blynk
      }
    }
    else if (Setup == true) {
      WiFi.disconnect();
      delay(200);
      WiFi.mode(WIFI_AP);
      delay(100);
      WiFi.softAP("Door-Setup", "");

      Setup = true;
      Serial.println(F("Setup Mode Actived, Please Connect to SSID: Door-Setup, IP: 192.168.4.1"));
    }
    BlynkBroadcast();
    notification = true;

    previouscheck = millis();
  }

  // System Uptime
  if ((micros() - lastTick) >= 1000000UL) {
    lastTick += 1000000UL;
    ss++;
    if (ss >= 60) {
      ss -= 60;
      mi++;
    }
    if (mi >= 60) {
      mi -= 60;
      hh++;
    }
    if (hh >= 24) {
      hh -= 24;
      dddd++;
    }
  }

  ///////////////////////////////////////////////////////////////
  //
  // Push Button (Setup Mode and Door Control).
  //
  ///////////////////////////////////////////////////////////////
  PushButtonCount = 0;
  PushButton = digitalRead(Button);

  if (PushButton == LOW && PushButtonCount == 0) {

    while (PushButton == LOW) {
      PushButton = digitalRead(Button);
      delay(1000);
      PushButtonCount++;

      if (PushButtonCount >= 10) {
        Serial.println(F("Controller reboot in Setup Mode, SSID = Door-Setup."));

        // START EEPROM
        EEPROM.begin(512);
        delay(200);

        // Erase SSID and PASSWORD
        for (int i = 34; i < 100; ++i)
        {
          EEPROM.write(i, 0);
        }
        delay(100);
        EEPROM.commit();
        delay(200);
        EEPROM.end();
        delay(200);
        WiFi.disconnect();
        delay(3000);
        ESP.restart();
      }
    }
    if (PushButtonCount > 0 and PushButtonCount < 10 and errorFlag == false) {
      if (doorState == true) {
        Close();
      }
      else {
        automatic = true;
        Open();
      }
    }
  }

  bool limit = digitalRead(limitSwitch);

  if (limit == HIGH && doorState == true) {
    doorState = false; // Door Close
    notification = true;

  } else if (limit == LOW && doorState == false) {
    doorState = true; // Door Open
    notification = true;

  }

  /////////////////////////////
  // Automatic by LightLevel //
  ////////////////////////////////////////////////////////////
  if (lux <= lightTrigger && automatic == false) {
    automatic = true;
  }

  ///////////////////////////////
  // Door Control by SunLight //
  ////////////////////////////////////////////////////////////
  if (lux >= lightTrigger and errorFlag == false and openSunlight == true and doorState == false && automatic == true) {
    previousCloseTimerTrigger = millis();

    closeDoorBegin = false;
    openDoorBegin = true; // enable ajax countdown timer display
    notification = true;

    if (millis() - previousOpenTimerTrigger >= openTimerTriggerMillis) {
      openDoorBegin = false; // disable ajax countdown timer display
      Open();
    }
  }
  else {
    previousOpenTimerTrigger = millis();
    openDoorBegin = false; // disable ajax countdown timer display
  }

  if (errorFlag == false and openSunlight == false and doorState == false) {
    if ((timeClient.getHours() == shs) and (timeClient.getMinutes() == smins))  {
      Open();
    }
  }

  //////////////////////////////////////////////////////////

  if (lux < lightTrigger and errorFlag == false and closeSunlight == true and doorState == true) {
    previousOpenTimerTrigger = millis();

    openDoorBegin = false;
    closeDoorBegin = true; // enable ajax countdown timer display
    notification = true;

    if (millis() - previousCloseTimerTrigger >= closeTimerTriggerMillis) {
      closeDoorBegin = false; // disable ajax countdown timer display
      Close();
    }
  }
  else {
    previousCloseTimerTrigger = millis();
    closeDoorBegin = false; // disable ajax countdown timer display
  }

  if (errorFlag == false and closeSunlight == false and doorState == true) {
    if ((timeClient.getHours() == ehs) and (timeClient.getMinutes() == emins))  {
      Close();
    }
  }

  ////////////////////////
  // Error Flag Check! //
  //////////////////////////////////////////////////////////
  bool ef = digitalRead(errorFlagPin);
  if (ef == LOW && errorFlag == false) {
    errorFlag = true;

    Blynk.notify("ChickenCoop, Error Flag! Check Door!");
    send_event("chickenDoor_fail");
  }
  yield();
}

/////////////////
// Read Sensor //
////////////////////////////////////////////////////////////
void ReadSENSOR() {

  if (openTime == "") {
    openSunlight = true;
  } else {
    openSunlight = false;
  }

  if (closeTime == "") {
    closeSunlight = true;
  } else {
    closeSunlight = false;
  }

  percentQ = 0;

  if (WiFi.RSSI() <= -100) {
    percentQ = 0;
  } else if (WiFi.RSSI() >= -50) {
    percentQ = 100;
  } else {
    percentQ = 2 * (WiFi.RSSI() + 100);
  }

  lux = analogRead(SENSOR_PIN);
  lux = map(lux, 0, 1024, 0, 100);

  if (lux > 100) lux = 100;
  if (lux < 0) lux = 0;

  Serial.print(F("Sunlight: "));
  Serial.println(lux);

  if (WiFi.status() == WL_CONNECTED) {

    if (Connected2Blynk) {
      BlynkBroadcast();
    }

    //////////////// NTP SYNC ///////////////

    time_t local, utc;
    timeClient.update();
    unsigned long epochTime =  timeClient.getEpochTime();
    utc = epochTime;

    // Then convert the UTC UNIX timestamp to local time
    // US Eastern Time Zone (New York, Detroit)
    TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  //UTC - 4 hours
    TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   //UTC - 5 hours
    Timezone usEastern(usEDT, usEST);
    local = usEastern.toLocal(utc);
    TimeChangeRule *tcr;        //
    Serial.println(timeClient.getFormattedTime());  //ntp

    if (usEastern.utcIsDST(utc)) {
      Serial.println(F("Daylight: -4"));
      timeClient.setTimeOffset(-14400); // UTC: -4 *60 *60 = -14400
    }
    else {
      Serial.println(F("Standard -5"));
      timeClient.setTimeOffset(-18000); // UTC: -5 *60 *60 = -18000
    }

    String IP = (WiFi.localIP().toString());
    Serial.print(F("Connected! "));
    Serial.println(IP);
    Serial.print(F("SSID: "));
    Serial.println(ssid);
  } else {
    Serial.println(F("Access Point Actived!"));
    Serial.println(F("192.168.4.1"));
    Serial.println(F("SSID: Door-Setup"));
  }
  Serial.println();
}

///////////////////
// HANDLE REBOOT //
////////////////////////////////////////////////////////////
void handleREBOOT() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!server.authenticate("admin", password.c_str()))
      return server.requestAuthentication();
  }

  String spinner = (F("<html>"));
  spinner += (F("<head><center><meta http-equiv=\"refresh\" content=\"30;URL='http://chickencoop-door.local'\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><style>"));

  spinner += (F(".loader {border: 16px solid #f3f3f3;border-radius: 50%;border-top: 16px solid #3498db;"));
  spinner += (F("width: 120px;height: 120px;-webkit-animation: spin 2s linear infinite;animation: spin 2s linear infinite;}"));

  spinner += (F("@-webkit-keyframes spin {0% { -webkit-transform: rotate(0deg); }100% { -webkit-transform: rotate(360deg); }}"));

  spinner += (F("@keyframes spin {0% { transform: rotate(0deg); }100% { transform: rotate(360deg); }}"));

  spinner += (F("</style></head>"));

  spinner += (F("<body style='background-color:#80bfff;'>"));
  spinner += (F("<p><h2>Rebooting Please Wait...</h2></p>"));
  spinner += (F("<div class=\"loader\"></div>"));
  spinner += (F("</body></center></html>"));

  server.send(200, "text/html",  spinner);

  Blynk.setProperty(V0, "onLabel", "Rebooting!");
  Blynk.setProperty(V0, "offLabel", "Rebooting!");

  delay(1000);
  WiFi.disconnect();
  delay(3000);
  ESP.restart();
}

//////////////////
// WIFI SETTING //
////////////////////////////////////////////////////////////
void handleWIFISETTING() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!server.authenticate("admin", password.c_str()))
      return server.requestAuthentication();
  }

  buildHeader();
  webSite = header;

  webSite += (F("<div class='topnav' id='myTopnav'>"));
  webSite += (F("<a href='/'>Home</a>"));
  webSite += (F("<a href='/door'>Door</a>"));
  webSite += (F("<a href='/wifisetting' class='active'>Wifi</a>"));
  webSite += (F("<a href=/blynk >Blynk</a>"));
  webSite += (F("<a href='/ifttt'>IFTTT</a>"));
  webSite += (F("<a href=/reboot onclick=\"return confirm('Are you sure?');\">Reboot</a>"));
  webSite += (F("<a href='/firmware'>Firmware Update</a>"));
  webSite += (F("<a href='javascript:void(0);' class='icon' onclick='myFunction()'>"));

  webSite += (F("<i class='fa fa-bars'></i></a>"));
  webSite += (F("</div>"));


  webSite += (F("<br><b><h1 style='font-family:verdana;color:blue; font-size:300%; text-align: center;'>Smart Chicken Door</font></h1></b><hr><hr>\n"));

  webSite += (F("<h1 style='font-family:verdana;color:blue;'><u>Wireless Network</u></h1>\n"));

  if (WiFi.status() == WL_CONNECTED) {
    String IP = (WiFi.localIP().toString());
    webSite += (F("<p>Network Connected! to <mark>"));
    webSite += WiFi.SSID();
    webSite += (F("</mark></p>"));
    webSite += (F("<p>Ip: "));
    webSite += IP;
    webSite += (F("</p>"));
    webSite += (F("<p>http://chickencoop-door.local</p>"));
  }

  webSite += (F("<hr><p><font color=blue><u>Wifi Scan</u></font></p>"));

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks(false, true);

  // sort by RSSI
  int indices[n];
  for (int i = 0; i < n; i++) {
    indices[i] = i;
  }
  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
        std::swap(indices[i], indices[j]);
      }
    }
  }

  String st = "";
  if (n > 5) n = 5;
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    st += "<small><li>";
    st += WiFi.RSSI(indices[i]);
    st += " dBm, ";
    st += WiFi.SSID(indices[i]);
    st += "</small></li>";
  }

  webSite += (F("<p>"));
  webSite += st;
  webSite += (F("</p>"));

  //// WiFi SSID & PASSWORD
  webSite += (F("<hr><h1 style='font-family:verdana;color:blue;'><u>Wifi Ssid & Pass</u></h1>\n"));
  webSite += (F("<form method='get' action='WiFi'><label>SSID: </label><input name='ssid' type='text' maxlength=32><br><br><label>PASS: </label><input name='pass' type='password' maxlength=32><br><br><input type='submit'></form>"));

  webSite += (F("<br><p><b>Reset:</b> Push on Button for 10sec to active Setup Mode,</p>"));
  webSite += (F("<p>Controller reboot in Setup Mode, SSID Door-Setup.</p>"));
  webSite += (F("<p>Ip: 192.168.4.1</p>"));
  webSite += (F("<hr><hr>"));

  buildFooter();
  webSite += footer;

  server.send(200, "text/html",  webSite);
}

//////////////////
// DOOR SETTING //
////////////////////////////////////////////////////////////
void handleDOOR() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!server.authenticate("admin", password.c_str()))
      return server.requestAuthentication();
  }

  buildHeader();
  webSite = header;

  webSite += (F("<div class='topnav' id='myTopnav'>"));
  webSite += (F("<a href='/'>Home</a>"));
  webSite += (F("<a href='/door' class='active'>Door</a>"));
  webSite += (F("<a href='/wifisetting'>Wifi</a>"));
  webSite += (F("<a href=/blynk >Blynk</a>"));
  webSite += (F("<a href='/ifttt'>IFTTT</a>"));
  webSite += (F("<a href=/reboot onclick=\"return confirm('Are you sure?');\">Reboot</a>"));
  webSite += (F("<a href='/firmware'>Firmware Update</a>"));
  webSite += (F("<a href='javascript:void(0);' class='icon' onclick='myFunction()'>"));
  webSite += (F("<i class='fa fa-bars'></i></a>"));
  webSite += (F("</div>"));

  webSite += (F("<br><b><h1 style='font-family:verdana;color:blue; font-size:300%; text-align: center;'>Smart Chicken Door</font></h1></b><hr><hr>\n"));

  // Door Control
  webSite += (F("<h1 style='font-family:verdana;color:blue;'><u>Door Control</u></h1>\n"));

  webSite += (F("<p><a href =/dooropen class=button>Open Door</a>"));
  webSite += (F("<a href =/doorclose class=button>Close Door</a></p>"));

  webSite += (F("<br><p><a href =/doorcalibration class=button>Chicken Door Calibration!</a></p>"));

  webSite += (F("<hr>"));

  //// Door Setting
  webSite += (F("<h1 style='font-family:verdana;color:blue;'><u>Timer Setting</u></h1>\n"));

  webSite += (F("<p><form method='get' action='TimeOpen'><label>Open: </label><input type=time name='OpenDoor'  value="));
  webSite += String(openTime);
  webSite += (F("><input type='submit'></form></p>\n"));
  webSite += (F("<p><form method='get' action='TimeClose'><label>Close: </label><input type=time name='CloseDoor'  value="));
  webSite += String(closeTime);
  webSite += (F("><input type='submit'></form></p>\n"));

  webSite += (F("<p><mark>Note: Leave blank if you want to control the door with sunlight.</mark></p><hr>\n"));

  webSite += (F("<h1 style='font-family:verdana;color:blue;'><u>Light Sensors Setting</u></h1>\n"));
  webSite += (F("<p><form method='get' action='LuxTrigger'><label>Trigger %: </label><input type=number min='5' max='95' name='LuxSetting' length=3 value="));
  webSite += String(lightTrigger);
  webSite += (F("><input type='submit'></form></p>\n"));

  webSite += (F("<p><form method='get' action='LuxTrigger'><label>Delay Before Open (min): </label><input type=number min='5' max='120' name='TimerSettingOpen' length=3 value="));
  webSite += String(openTimerTrigger);
  webSite += (F("><input type='submit'></form></p>\n"));

  webSite += (F("<p><form method='get' action='LuxTrigger'><label>Delay Before Close (min): </label><input type=number min='5' max='120' name='TimerSettingClose' length=3 value="));
  webSite += String(closeTimerTrigger);
  webSite += (F("><input type='submit'></form></p>\n"));

  webSite += (F("<hr><hr>"));

  buildFooter();
  webSite += footer;

  server.send(200, "text/html",  webSite);
}

//////////////////
// HANDLE BLYNK //
////////////////////////////////////////////////////////////
void handleBLYNK() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!server.authenticate("admin", password.c_str()))
      return server.requestAuthentication();
  }

  buildHeader();
  webSite = header;

  webSite += (F("<div class='topnav' id='myTopnav'>"));
  webSite += (F("<a href=/ >Home</a>"));
  webSite += (F("<a href='/door'>Door</a>"));
  webSite += (F("<a href=/wifisetting>Wifi</a>"));
  webSite += (F("<a href=/blynk class='active'>Blynk</a>"));
  webSite += (F("<a href='/ifttt'>IFTTT</a>"));
  webSite += (F("<a href=/reboot onclick=\"return confirm('Are you sure?');\">Reboot</a>"));
  webSite += (F("<a href=/firmware >Firmware Update</a>"));
  webSite += (F("<a href='javascript:void(0);' class='icon' onclick='myFunction()'>"));
  webSite += (F("<i class='fa fa-bars'></i></a>"));
  webSite += (F("</div>"));

  webSite += (F("<br><b><h1 style='font-family:verdana; color:blue; font-size:300%; text-align:center;'>Smart Chicken Door</font></h1></b><hr><hr>"));

  // BLYNK CONFIG

  webSite += (F("<p><h1 style='font-family:verdana; color:blue; font-size:200%;'>Please Download Blynk APP.</h1></p>"));
  webSite += (F("<p><form method='get' action='Blynk'><label>AuthToken: </label><input type='password' name='key' maxlength='32' value="));
  webSite += String(AuthToken);
  webSite += (F("><input type='submit'></form></p>\n"));

  webSite += (F("<hr><p><h1 style='font-family:verdana; color:blue; font-size:200%;'>Blynk Server</h1></p>"));
  webSite += (F("<p><form method='get' action='BlynkServer'><label>Server: </label><input type='text' name='server' maxlength='80' value="));

  if (BlynkServer.length() > 5) {
    webSite += String(BlynkServer);
  }
  else {
    webSite += (F("blynk-cloud.com"));
  }

  webSite += (F("><label> Port: </label><input type='number' name='port' maxlength='4' value="));

  if (BlynkServer.length() > 5) {
    webSite += String(BlynkPort);
  }
  else {
    webSite += (F("8442"));
  }

  webSite += (F("><p><input type='submit'></form></p>\n"));

  webSite += (F("<p>Note: Enter your Blynk server address or leave empty to use default Blynk server.</p>"));
  webSite += (F("<br><p>V0:Styled Button(Door Control), V2:Gauge(Sunshine %), V3:Styled Button(Reset),</p>"));
  webSite += (F("<p>Notification.</p>"));
  webSite += (F("<hr><hr>"));

  buildFooter();
  webSite += footer;

  server.send(200, "text/html",  webSite);
}

/////////////////////////////
// HANDLE Door Calibration //
////////////////////////////////////////////////////////////
void handleCAL() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!server.authenticate("admin", password.c_str()))
      return server.requestAuthentication();
  }

  buildHeader();
  webSite = header;

  webSite += (F("<div class='topnav' id='myTopnav'>"));
  webSite += (F("<a href='/door'>Back!</a>"));
  webSite += (F("<a href='javascript:void(0);' class='icon' onclick='myFunction()'>"));
  webSite += (F("<i class='fa fa-bars'></i></a>"));
  webSite += (F("</div>"));

  webSite += (F("<br><b><h1 style='font-family:verdana;color:blue; font-size:300%; text-align: center;'>Smart Chicken Door</font></h1></b>\n"));

  if (motorCloseMillis == 0) {
    webSite += (F("<p><h1><font color='red'>Please perform door calibration!</font></h1></p>\n"));
  }

  webSite += (F("<hr><hr><p><h1 style='font-family:verdana;color:blue; font-size:200%;'><u>Door Control</u>"));
  webSite += (F("</h1></p>"));

  webSite += (F("<p><a href =/right?For=1000 class=button>(Open) Actived for 1sec</a>"));
  webSite += (F("<a href =/right?For=250 class=button>250msec</a></p>"));
  webSite += (F("<p><a href =/left?For=1000 class=button>(Close) Actived for 1sec</a>"));
  webSite += (F("<a href =/left?For=250 class=button>250msec</a></p>"));

  webSite += (F("<hr>"));

  webSite += (F("<p><h1 style='font-family:verdana;color:blue; font-size:200%;'><u>Door Calibration</u>"));
  webSite += (F("</h1></p>"));

  webSite += (F("<p><form method='get' action='MotorDelay'><label>Motor Speed %: </label><input type=number min='50' max='100' name='Speed' length=3 value="));
  webSite += String(pwmByte);
  webSite += (F("><input type='submit'></form></p><br>"));

  webSite += (F("<p><li><mark>When calibration started!, The door start to opening... and closing...</mark></li></p>"));
  webSite += (F("<p><li><mark>Press push button when the door is closed at desired position.<mark></li></p>"));
  webSite += (F("<p><li><mark>Calibration finish!<mark></p>"));

  webSite += (F("<br><p><h1 style='font-family:verdana;color:blue; font-size:200%;'><u>Calibration Result</u></h1></p>"));

  if (motorCloseMillis == 0) {
    webSite += (F("<p><font color='red'>Please perform door calibration!</font></p>\n"));
  } else {
    webSite += (F("<p>Close Delay's (millisec): "));
    webSite += String(motorCloseMillis);
    webSite += (F("</p>"));
  }

  webSite += (F("<br><p><a href =/calibration class=button>Start Calibration</a></p>"));

  webSite += (F("<hr><hr>"));

  buildFooter();
  webSite += footer;

  server.send(200, "text/html",  webSite);
}

//////////////////
// HANDLE IFTTT //
////////////////////////////////////////////////////////////
void handleIFTTT() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!server.authenticate("admin", password.c_str()))
      return server.requestAuthentication();
  }

  buildHeader();
  webSite = header;
  webSite += (F("<div class='topnav' id='myTopnav'>"));
  webSite += (F("<a href=/ >Home</a>"));
  webSite += (F("<a href='/door'>Door</a>"));
  webSite += (F("<a href='/wifisetting'>Wifi</a>"));
  webSite += (F("<a href='/blynk'>Blynk</a>"));
  webSite += (F("<a href='/ifttt' class='active'>IFTTT</a>"));
  webSite += (F("<a href=/reboot onclick=\"return confirm('Are you sure?');\">Reboot</a>"));
  webSite += (F("<a href=/firmware >Firmware Update</a>"));
  webSite += (F("<a href='javascript:void(0);' class='icon' onclick='myFunction()'>"));
  webSite += (F("<i class='fa fa-bars'></i></a>"));
  webSite += (F("</div>"));

  webSite += (F("<br><b><h1 style='font-family:verdana;color:blue; font-size:300%; text-align: center;'>Smart Chicken Door</font></h1></b><hr><hr>\n"));

  webSite += (F("<p><h1 style='font-family:verdana;color:blue; font-size:200%;'><a href='https://ifttt.com/' target='_blank'>https://ifttt.com</a></h1></p>"));
  webSite += (F("<p><form method='get' action='Iftttkey'><label>Webhooks Key: </label><input type='password' name='key' maxlength='32' value="));
  webSite += String(IftttKey);
  webSite += (F("><input type='submit'></form></p>\n"));

  webSite += (F("<br><p><b><h3>IFTTT Internet Of Thing Service IoT.</h3></b></p>"));
  webSite += (F("<p>Ex: Door State control Bulb light in House.</p>"));

  webSite += (F("<br><p><b>Event Trigger</b></p>"));
  webSite += (F("<p><mark>chickenDoor_open</mark></p>"));
  webSite += (F("<p><mark>chickenDoor_close</mark></p>"));
  webSite += (F("<p><mark>chickenDoor_fail</mark></p>"));

  webSite += (F("<hr><hr>"));

  buildFooter();
  webSite += footer;

  server.send(200, "text/html",  webSite);
}

void handle_OnConnect() {

  server.send(200, "text/html", SendHTML());
}

/////////////////////
// HANDLE NOTFOUND //
////////////////////////////////////////////////////////////
void handleNotFound() {
  server.sendHeader("Location", "/", true);  //Redirect to our html web page
  server.send(302, "text/plane", "");
}

String SendHTML() {

  String ptr = (F("<!DOCTYPE html>"));
  // ptr += (F("<html>"));
  ptr += (F("<html lang='en'>"));

  ptr += (F("<head>"));
  //  <!-- Required meta tags -->
  ptr += (F("<title>Smart Chicken Door</title>"));
  ptr += (F("<meta charset='utf-8'>"));

  ptr += (F("<meta name='viewport' content='width=device-width, initial-scale=1.0' shrink-to-fit=no'>"));
  ptr += (F("<meta name='description' content='Smart Chicken Door'>"));
  ptr += (F("<meta name='author' content='Made by Real Drouin'>"));

  ptr += (F("<link href='https://fonts.googleapis.com/css?family=Open+Sans:300,400,600' rel='stylesheet'>"));
  ptr += (F("<style>"));
  ptr += (F("html { font-family: 'Open Sans', sans-serif; display: block; margin: 0px auto; text-align: center;color: #444444;}"));
  ptr += (button);
  ptr += (F("</style>"));

  // AJAX script
  ptr += (F("<script>\n"));
  ptr += (F("setInterval(loadDoc,1000);\n")); // Update WebPage Every 1sec
  ptr += (F("function loadDoc() {\n"));
  ptr += (F("var xhttp = new XMLHttpRequest();\n"));
  ptr += (F("xhttp.onreadystatechange = function() {\n"));
  ptr += (F("if (this.readyState == 4 && this.status == 200) {\n"));
  ptr += (F("document.body.innerHTML =this.responseText}\n"));
  ptr += (F("};\n"));
  ptr += (F("xhttp.open(\"GET\", \"/\", true);\n"));
  ptr += (F("xhttp.send();\n"));
  ptr += (F("}\n"));
  ptr += (F("</script>\n"));
  ///////////////////////////////////////

  ptr += (F("</head>"));
  ptr += (F("<body style='background-color:#80bfff;'>"));
  ptr += (F("<b><h1 style='font-family:verdana;color:blue; font-size:300%; text-align: center;'>Smart Chicken Door</font></h1></b>\n"));

  char buf[21];
  sprintf(buf, "<h2>NTP Clock: %02dh:%02dm:%02ds</h2>", timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds());

  ptr += (F("<p>"));
  ptr += (buf);
  ptr += (F("</p>"));

  if (Connected2Blynk) {
    ptr += (F("<p><mark>Blynk Server Connected!</mark></p>"));
  }
  else {
    ptr += (F("<p><mark>Blynk Server Disconnected!</mark></p>"));
  }

  ptr += (F("<hr><hr><h2>"));

  ///////////////////////////////////////
  ptr += (F("<p>Light Level: "));
  ptr += String(lux);
  ptr += (F("%</p>"));
  ///////////////////////////////////////

  ///////////////////////////////////////
  ptr += (F("<hr><font color='blue'>Door Status</font><br>"));

  if (doorState == true) {
    ptr += (F("Opened"));
  }
  else {
    ptr += (F("Closed"));
  }
  ptr += (F("<hr>"));

  ptr += (F("<font color='blue'>OPEN</font><br>"));
  if (openSunlight == true) {
    ptr += (F("Light Level!"));
  }
  else {
    char buf[21];
    sprintf(buf, "%02dh:%02dm", shs, smins);

    ptr += (buf);
  }
  ///////////////////////////////////////
  ptr += (F("<hr><font color='blue'>CLOSE</font><br>"));
  if (closeSunlight == true) {
    ptr += (F("Light Level!"));
  }
  else {
    char buf[21];
    sprintf(buf, "%02dh:%02dm", ehs, emins);

    ptr += (buf);
  }

  ptr += (F("<hr>"));

  if (errorFlag == true) {
    ptr += (F("<p><font color='red'>Error Flag from H-Bridge - Please Check Door!</font></p>"));
  }
  else {
    //////////////////////////////////////////////////////////////////////
    ////// Close Door Delay automatic mode
    if (closeDoorBegin == true) {
      unsigned long allSec = (closeTimerTriggerMillis - (millis() - previousCloseTimerTrigger)) / 1000;

      int secsRemaining = allSec % 3600;
      int Min = secsRemaining / 60;
      int Sec = secsRemaining % 60;

      ptr += (F("<p><font color='blue'>Delay Before Closing Door</font></p>"));

      char buf[21];
      sprintf(buf, "%02dm:%02ds", Min, Sec);
      ptr += (buf);
    }

    ////// Open Door Delay automatic mode
    else if (openDoorBegin == true) {
      unsigned long allSec = (openTimerTriggerMillis - (millis() - previousOpenTimerTrigger)) / 1000;

      int secsRemaining = allSec % 3600;
      int Min = secsRemaining / 60;
      int Sec = secsRemaining % 60;

      ptr += (F("<p><font color='blue'>Delay Before Opening Door</font></p>"));

      char buf[21];
      sprintf(buf, "%02dm:%02ds", Min, Sec);
      ptr += (buf);
    }
    else if (motorCloseMillis == 0) {
      ptr += (F("<p><h1><font color='red'>Please perform door calibration!</font></h1></p>\n"));
      ptr += (F("<br><p><a href =/doorcalibration class=button>Chicken Door Calibration!</a></p>"));
    }
    else {
      ptr += (F("<p><h1><font color='blue'>Normal Operation</font></h1></p>"));
    }
  }

  ptr += (F("</h2></body><hr><hr>"));
  ptr += (F("<p><a href ='/door' class='button'>Admin</a></p>"));
  ptr += (F("<p><font color = 'blue'><i>Signal Strength: </i></font> "));
  ptr += String(percentQ);
  ptr += (F(" %</p>"));

  // System Uptime
  sprintf(timestring, "%d days %02d:%02d:%02d", dddd , hh, mi, ss);
  ptr += (F("<p>System Uptime: "));
  ptr += String(timestring);
  ptr += (F("</p>"));

  //////////////////////////////////////

  ptr += (F("<p><small>Smart Chicken Door "));
  ptr += (ver);
  ptr += (F("</p><p><small>Made by Real Drouin ve2droid@gmail.com</small></p>"));

  ptr += (F("</html>\n"));
  return ptr;
}

//////////////////
// BUILD HEADER //
////////////////////////////////////////////////////////////
void buildHeader() {

  header = "";
  header += (F("<!doctype html>\n"));

  header += (F("<html lang='en'>"));

  header += (F("<head>"));
  //  <!-- Required meta tags -->
  header += (F("<title>ChickenCoop Admin</title>"));
  header += (F("<meta charset='utf-8'>"));
  header += (F("<meta name='viewport' content='width=device-width, initial-scale=1.0' shrink-to-fit=no'>"));
  header += (F("<meta name='description' content='Smart Chicken Door'>"));
  header += (F("<meta name='author' content='Made by Real Drouin'>"));
  header += (F("<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css'>"));

  header += (F("<style>body {margin: 0;text-align: center;font-family: Arial, Helvetica, sans-serif;}"));
  header += (F(".topnav {overflow: hidden;background-color: #333;}"));
  header += (F(".topnav a {float: left;display: block;color: #f2f2f2;text-align: center;padding: 14px 16px;text-decoration: none;font-size: 17px;}"));
  header += (F(".topnav a:hover {background-color: #ddd;color: black;}"));
  header += (F(".topnav a.active {background-color: blue;color: white;}"));
  header += (F(".topnav .icon {display: none;}"));

  header += (F("@media screen and (max-width: 600px) {.topnav a:not(:first-child) {display: none;}.topnav a.icon {float: right;display: block;}}"));
  header += (F("@media screen and (max-width: 600px) {.topnav.responsive {position: relative;}.topnav.responsive .icon {position: absolute;right: 0;top: 0;}"));
  header += (F(".topnav.responsive a {float: none;display: block;text-align: left;}}"));

  header += String(button);
  header += (F("</style>\n"));

  header += (F("</head>\n"));
  header += (F("<body style='background-color:#80bfff;'>"));
}

//////////////////
// BUILD FOOTER //
////////////////////////////////////////////////////////////
void buildFooter() {

  footer = (F("<br>"));
  footer += (F("<address> Contact: <a href='mailto:ve2droid@gmail.com'>Real Drouin</a>"));
  footer += (F("</address>"));
  footer += (F("<p><small>Made by Real Drouin, Smart Chicken Door "));
  footer += String(ver);
  footer += (F("</small>"));
  footer += (F("</footer>"));

  footer += (F("<script>function myFunction() {var x = document.getElementById('myTopnav');if (x.className === 'topnav') {x.className += ' responsive';} else {x.className = 'topnav';}}</script>"));

  footer += (F("</body>\n"));
  footer += (F("</html>\n"));
}

///////////////
// Open Door //
////////////////////////////////////////////////////////////
void Open()
{
  notification = true;
  if (doorState == false && errorFlag == false) {

    unsigned long StartMillis = millis();
    bool limit = digitalRead(limitSwitch);

    analogWrite(doorDownPin, pwm);
    digitalWrite(doorUpPin, HIGH);

    while (limit == HIGH) {
      limit = digitalRead(limitSwitch);

      if (millis() - StartMillis >= motorCloseMillis + (100 - pwm * 75)) {
        digitalWrite(doorUpPin, LOW);
        digitalWrite(doorDownPin, LOW);
        errorFlag = true;
        break;
      }

      bool ef = digitalRead(errorFlagPin);
      if (ef == LOW) {
        digitalWrite(doorDownPin, LOW);
        digitalWrite(doorUpPin, LOW);
        errorFlag = true;
        Blynk.notify("ChickenCoop, Error Flag! Check Door!");
        break;
      }
      delay(1);
    }

    digitalWrite(doorUpPin, LOW);
    digitalWrite(doorDownPin, LOW);

    if (errorFlag == false) {
      doorState = true;
      Blynk.notify("ChickenCoop, Door Open!");
      send_event("chickenDoor_open");
    } else {
      Blynk.notify("ChickenCoop, Error Flag! Check Door!");
      send_event("chickenDoor_fail");
    }
  }
}

////////////////
// Close Door //
////////////////////////////////////////////////////////////
void Close()
{
  notification = true;
  doorTimer = 0;
  if (doorState == true && errorFlag == false) {
    automatic = false;

    unsigned long StartMillis = millis();

    analogWrite(doorUpPin, pwm);
    digitalWrite(doorDownPin, HIGH);

    while (1) {
      if (millis() - StartMillis >= motorCloseMillis) {
        digitalWrite(doorDownPin, LOW);
        digitalWrite(doorUpPin, LOW);
        break;
      }

      bool ef = digitalRead(errorFlagPin);
      if (ef == LOW) {
        digitalWrite(doorDownPin, LOW);
        digitalWrite(doorUpPin, LOW);
        errorFlag = true;
        Blynk.notify("ChickenCoop, Error Flag! Check Door!");
        break;
      }
      delay(1);
    }

    digitalWrite(doorDownPin, LOW);
    digitalWrite(doorUpPin, LOW);

    if (errorFlag == false) {
      doorState = false;
      Blynk.notify("ChickenCoop, Door Close!");
      send_event("chickenDoor_close");
    } else {
      Blynk.notify("ChickenCoop, Error Flag! Check Door!");
      send_event("chickenDoor_fail");
    }
  }
}

/////////////////////
// BROADCAST IFTTT //
////////////////////////////////////////////////////////////
void send_event(String event)
{
  if (WiFi.status() == WL_CONNECTED and IftttKey.length() > 2) {
    Serial.println(F("IFTTT Broadcast!"));

    String url = "/trigger/" + String(event) + "/with/key/" + String(IftttKey);

    String value_1 = "";
    String value_2 = "";
    String value_3 = "";

    if (errorFlag == true) {
      value_1 += "Fail! , Check Chicken Door!";
    }
    else {
      if (doorState == true) {
        value_1 += (F("Door Open, "));
      } else {
        value_1 += (F("Door Close, "));
      }
      value_2 += (WiFi.localIP().toString());
      value_3 += String(percentQ);
    }

    String data = "";
    data = data + "\n" + "{\"value1\":\"" + value_1 + "\",\"value2\":\"" + value_2 + "\",\"value3\":\"" + value_3 + "\"}";

    if (client.connect("maker.ifttt.com", 80)) {

      client.println("POST " + url + " HTTP/1.1");
      client.println("Host: maker.ifttt.com");
      client.println("User-Agent: Arduino/1.0");
      client.print("Accept: *");
      client.print("/");
      client.println("*");
      client.print("Content-Length: ");
      client.println(data.length());
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      client.println(data);

      int timeout = millis() + 5000;
      while (client.available() == 0) {
        if (timeout - millis() < 0) {
          client.stop();
          delay(200);
          return;
        }
      }
    }
    delay(1);
    readString = "";
    client.flush();
    client.stop();
  }
}

void BlynkBroadcast() {
  Blynk.virtualWrite(V2, lux);
  delay(100);
  ///////////////////////////////////////////////////////////
  if (errorFlag == true) {
    Blynk.setProperty(V0, "onLabel", "DOOR FAIL!");
    Blynk.setProperty(V0, "offLabel", "DOOR FAIL!");
    Blynk.setProperty(V0, "onBackColor", "#ff0000"); Blynk.setProperty(V0, "offBackColor", "#ff0000");
  }
  else {
    if (notification == true) {
      Blynk.virtualWrite(V0, doorState);
      Blynk.setProperty(V0, "onBackColor", "#0066ff"); Blynk.setProperty(V0, "offBackColor", "#0066ff");

      if (doorState == true) {
        ////// Close Door Delay automatic mode
        if (closeDoorBegin == true) {
          unsigned long allSec = (closeTimerTriggerMillis - (millis() - previousCloseTimerTrigger)) / 1000;

          int secsRemaining = allSec % 3600;
          int Min = secsRemaining / 60;
          int Sec = secsRemaining % 60;

          char buf[21];
          sprintf(buf, "Close in %02dm:%02ds", Min, Sec);
          Blynk.setProperty(V0, "onLabel", buf);
        }
        else {
          Blynk.setProperty(V0, "onLabel", "Door Opened!");
        }
      }
      else if (doorState == false) {
        ////// Open Door Delay automatic mode
        if (openDoorBegin == true) {
          unsigned long allSec = (openTimerTriggerMillis - (millis() - previousOpenTimerTrigger)) / 1000;

          int secsRemaining = allSec % 3600;
          int Min = secsRemaining / 60;
          int Sec = secsRemaining % 60;

          char buf[21];
          sprintf(buf, "Open in %02dm:%02ds", Min, Sec);
          Blynk.setProperty(V0, "offLabel", buf);
        } else {
          Blynk.setProperty(V0, "offLabel", "Door Closed!");
        }
      }
    }
  }
  notification = false;
}

String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
