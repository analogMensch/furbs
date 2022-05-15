/*
FURBS

version info (line30):
0.1 (2022-05-05)
0.2 (2022-05-07)
0.3 (2022-05-08)
0.4 (2022-05-10)
0.5 (2022-05-15)

servo 1 = closed/open, mouth, 0/40 max, 10/35 good
servo 2 = left eye, closed/open, 15/105
servo 3 = right eye, closed/open, 105/15
servo 4 = left ear, down/up, 30/150
servo 5 = right ear, down/up, 150/30
button mouth = GPIO0, unpressed/pressed, high/low
LDR = GPIO34, dark/bright, 4095/0
IRin = GPIO2, dark/bright, low/high
IRout = GPIO4, dark/bright, low/high
*/

// includes
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP32_Servo.h>
#include <DFRobotDFPlayerMini.h>
#include <IRCClient.h>

// hardware inputs/outputs
#define BUTTON 0
#define LDR 34
#define IRin 2
#define IRout 4
#define servoPin1 32
#define servoPin2 33
#define servoPin3 25
#define servoPin4 26
#define servoPin5 27
#define version "0.5"
#define ssid "yourSSID"
#define password "yourWifiPassword"
#define IRC_SERVER "irc.chat.twitch.tv"
#define IRC_PORT 6667
#define IRC_CHANNEL "#yourTwitchChannelName"

//ints/const ints/unsigned longs
int tongue;
int darkness;
int mode = 0;
int actionMode = 0;
int chatlogin = 0;
int basicanswer = 0;
const int nightshift = 2500; //light level for night
const int dayshift = 2000;  //light level for day
unsigned long dizzydelay = 5000;  //time between night and dizzy
unsigned long sleepdelay = 5000;  //time between dizzy and sleep
unsigned long IRCtimeout = 450000;  //timeout for rejoining IRC channel
unsigned long IRCpingpong;
unsigned long dizzytime;
unsigned long sleeptime;
unsigned long randomgenerator;

//bools
bool IRCdebug = false;  //send IRC debug messages to debug port/USB
bool nightmode = false;
bool daymode = false;
bool nightset = false;
bool dayset = false;
bool dizzymode = false;
bool sleepmode = false;

//char strings
const char* modeParameter = "mode";
const char* actionParameterA = "actionA";
const char* actionParameterB = "actionB";
const char index_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML>
    <html>
      <head>
      </head>
      <body bgcolor="808080">
        <p>
          FURBS
        </p>
        <p>
          <a href="/change?mode=0&actionA=earwiggleleft&actionB=0">wiggle left ear</a>
          <br>
          <a href="/change?mode=0&actionA=earwiggleright&actionB=0">wiggle right ear</a>
          <br>
          <a href="/change?mode=0&actionA=eyeblinkleft&actionB=0">blink left eye</a>
          <br>
          <a href="/change?mode=0&actionA=eyeblinkright&actionB=0">blink left eye</a>
        </p>
    </html>
    </html>
  )rawliteral";

//chat command arrays
const char* furbyNames[] = {"furby", "furbs", "furbo"};
const char* furbyNamesAnswers [] = {"Hab ich da meinen Namen gehört?", "Ja, das bin ich?", "Ähh...ja, hier! Was gibt es?", "Wer will was?"};

//message strings
String modeMessage;  //mode
String actionMessageA;  //message face/ears
String actionMessageB;  //message sound
String IRC_NICKNAME;  //IRC nickname
String IRC_OAUTH_TOKEN;  //IRC oAuth token
String chatmessage;  //IRC chat message

//servos
Servo servo1;  //servo mouth
Servo servo2;  //servo left eye
Servo servo3;  //servo right eye
Servo servo4;  //servo left ear
Servo servo5;  //servo right ear

//DF player
DFRobotDFPlayerMini DFplayer;

//creating a AsyncWebServer object 
AsyncWebServer server(80);
String processor(const String& var){
  return String();
}
String outputState(int output){
  if(digitalRead(output)){
    return "checked";
  }
  else {
    return "";
  }
}

//creating IRC client
WiFiClient wifiClient;
IRCClient client(IRC_SERVER, IRC_PORT, wifiClient);

//serials definition 
HardwareSerial externalSerial(0);  //serial 0 (debug port/USB) 
HardwareSerial internalSerial(1);  //serial 1 (DF player)

void printDetail(uint8_t type, int value);

void setup(){
  //pin modes
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(IRin, INPUT);
  pinMode(IRout, OUTPUT);

  //serto pin attachments
  servo1.attach(servoPin1);  //servo mouth
  servo2.attach(servoPin2);  //servo left eye
  servo3.attach(servoPin3);  //servo right eye
  servo4.attach(servoPin4);  //servo left ear
  servo5.attach(servoPin5);  //servo right ear

  //serial communication start
  externalSerial.begin(9600, SERIAL_8N1, 3, 1);  //serial 0 (debug port/USB)
  internalSerial.begin(9600, SERIAL_8N1, 18, 21);  //serial 1 (DF player)

  externalSerial.println();
  externalSerial.println();
  externalSerial.println("------------------------------------");
  externalSerial.print("FURBS v");
  externalSerial.println(version);
  externalSerial.println("------------------------------------");

  wificonnect();

  // Route for root/web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/change", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (request->hasParam(modeParameter) && request->hasParam(actionParameterA) && request->hasParam(actionParameterB)) {
      modeMessage = request->getParam(modeParameter)->value();
      actionMessageA = request->getParam(actionParameterA)->value();
      actionMessageB = request->getParam(actionParameterB)->value();
      mode = modeMessage.toInt();
      actionMode++;
    }
    else {
      modeMessage = "No message sent";
      actionMessageA = "No message sent";
      actionMessageB = "No message sent";
    }
    externalSerial.println("WEB QUERY REQUESTS");
    externalSerial.print("--> selected mode: ");
    externalSerial.println(modeMessage);
    externalSerial.print("--> selected action A: ");
    externalSerial.println(actionMessageA);
    externalSerial.print("--> selected action B: ");
    externalSerial.println(actionMessageB);
    externalSerial.print("--> change requests = ");
    externalSerial.println(actionMode);
    request->send_P(200, "text/html", index_html, processor);
  });

  server.begin();

  //servo init
  externalSerial.println("servo init");
  servo1.write(40);  //mouth
  delay(500);
  servo1.write(0);
  delay(500);
  servo1.write(20);
  delay(500);
  servo2.write(105);  //left eye
  delay(500);
  servo2.write(60);
  delay(500);
  servo2.write(15);
  delay(500);
  servo3.write(15);  //right eye
  delay(500);
  servo3.write(60);
  delay(500);
  servo3.write(105);
  delay(500);
  servo4.write(60);  //left ear
  delay(500);
  servo4.write(120);
  delay(500);
  servo4.write(90);
  delay(500);
  servo5.write(120);  //right ear
  delay(500);
  servo5.write(60);
  delay(500);
  servo5.write(90);
  externalSerial.println("servo init done");

  //DF player init
  externalSerial.println(F("DF player init"));
  if (!DFplayer.begin(internalSerial)) { 
    externalSerial.println(DFplayer.readType(),HEX);
    externalSerial.println(F("Unable to begin:"));
    externalSerial.println(F("1.Please recheck the connection!"));
    externalSerial.println(F("2.Please insert the SD card!"));
    while(true);
  }
  externalSerial.println(F("DF player init done"));

  //IR init
  externalSerial.println("IR init");
  digitalWrite(IRout, HIGH);
  delay(1250);
  digitalWrite(IRout, LOW);
  externalSerial.println("IR init done");

  //DF player settings
  DFplayer.volume(10);
  DFplayer.EQ(DFPLAYER_EQ_BASS);
  DFplayer.outputDevice(DFPLAYER_DEVICE_SD);

  //ready sound/move
  DFplayer.play(1);
  delay(250);
  servo1.write(40);
  delay(1250);
  servo1.write(0);
  delay(50);
  servo1.write(20);
  delay(1250);
  for (int i=0; i<90; i++) {
    servo2.write(15+i);
    servo3.write(105-i);
    delay(5);
  }

  // generate string for twitch login
  externalSerial.println("twitch chat user init");
  IRC_NICKNAME = "yourBotNickname";
  IRC_OAUTH_TOKEN = "yourAuthToken";  
  client.setCallback(callback);
  client.setSentCallback(debugSentCallback);
  externalSerial.println("twitch chat user init done");
}
void loop() {
  while (WiFi.status() != WL_CONNECTED) {
    wifireconnect();
  }

  twitchchatlogin();

  if (millis() - IRCpingpong > IRCtimeout) {
    externalSerial.println("--> TWITCH TMI COMMAND: REJOIN CHANNEL");
    twitchchatrejoin();
  }
 
  client.loop();

  webcommands();
  
  lightcheck();

  //day/night shift
  if (nightmode && !dizzymode && millis() >= (dizzytime + dizzydelay)) {  //if dark for time defined in dizzytime
    dizzy();
    sleeptime = millis();
    dizzymode = true;
    dayset = false;
    externalSerial.println("getting dizzy");
  }
  if (nightmode && dizzymode && !sleepmode && millis() >= (sleeptime + sleepdelay)) {  //if dark for time defined in dizzytime plus sleeptime
    sleep();
    sleepmode = true;
    dayset = false;
    externalSerial.println("going to sleep");
  }
  if (tongue == LOW && dizzymode && !sleepmode) {  //wake up from being dizzy by pushing tongue
    delay (600);
    wakeupdizzy();
    dizzymode = false;
    nightset = false;
    dizzytime = millis();
    externalSerial.println("staying up");
  }
  if (tongue == LOW && dizzymode && sleepmode) {  //wake up from sleeping by pushing tongue
    wakeupsleep();
    dizzymode = false;
    sleepmode = false;
    nightset = false;
    dizzytime = millis();
    sleeptime = millis();
    externalSerial.println("waking up");
  }
  if (!nightmode && dizzymode && !sleepmode) {  //wake up from being dizzy by light
    wakeupdizzy();
    dizzymode = false;
    nightset = false;
    dizzytime = millis();
    externalSerial.println("keeping myself up");
  }
}

void lightcheck() {
  //read LDR and tongue button
  darkness = analogRead(LDR);
  tongue = digitalRead(BUTTON);

  //set day/ night bools
  if (darkness >= nightshift) {
    nightmode = true;
    dayset = false;
  }
  else {
    nightmode = false;
  }
  if (darkness <= dayshift) {
    daymode = true;
    nightset = false;
  }
  else {
    daymode = false;
  }
  if (nightmode && !nightset) {
    dizzytime = millis();
    externalSerial.print ("night shift with light level ");
    externalSerial.print(darkness);
    externalSerial.print(" at ");
    externalSerial.print(sleeptime);
    externalSerial.println("ms");
    if (!dizzymode) {
      externalSerial.print("waiting for ");
      externalSerial.print(dizzydelay + sleepdelay);
      externalSerial.println("ms before going to sleep");
    }
    nightset = true;
  }
  if (daymode && !dayset) {
    externalSerial.print ("day shift with light level ");
    externalSerial.print(darkness);
    externalSerial.print(" at ");
    externalSerial.print(millis());
    externalSerial.println("ms");
    dayset = true;
  }
}

void dizzy() {
  for (int i=40; i>0; i--) {
    servo2.write(60+i);
    servo3.write(60-i);
    delay(5);
  }
  servo1.write(0);
  for (int i=0; i<40; i++) {
    servo1.write(0+i);
    delay(5);
  }
  delay(50);
  servo1.write(30);
}

void sleep() {
  for (int i=45; i>0; i--) {
    servo2.write(15+i);
    servo3.write(105-i);
    delay(5);
  }
  for (int i=60; i>0; i--) {
    servo4.write(30+i);
    servo5.write(150-i);
    delay(10);
  }
}

void wakeupdizzy() {
  for (int i=0; i<45; i++) {
    servo2.write(60+i);
    servo3.write(60-i);
    delay(5);
  }
  servo1.write(40);
  for (int i=40; i>0; i--) {
    servo1.write(0+i);
    delay(10);
  }
  delay(50);
  servo1.write(20);
}

void wakeupsleep() {
  for (int i=0; i<60; i++) {
    servo4.write(30+i);
    delay(10);
  }
  for (int i=0; i<45; i++) {
    servo2.write(15+i);
    delay(5);
  }
  delay(2500);
  for (int i=45; i>0; i--) {
    servo2.write(15+i);
    delay(5);
  }
  for (int i=0; i<60; i++) {
    servo5.write(150-i);
    delay(10);
  }
  for (int i=0; i<90; i++) {
    servo2.write(15+i);
    servo3.write(105-i);
    delay(5);
  }
  servo1.write(40);
  for (int i=40; i>0; i--) {
    servo1.write(0+i);
    delay(10);
  }
  delay(50);
  servo1.write(20);
}

void earwiggleleft() {
  for (int i=0; i<60; i++) {
    servo4.write(90-i);
    delay(5);
  }
  for (int i=60; i>0; i--) {
    servo4.write(90-i);
    delay(5);
  }
}

void earwiggleright() {
  for (int i=0; i<60; i++) {
    servo5.write(90+i);
    delay(5);
  }
  for (int i=60; i>0; i--) {
    servo5.write(90+i);
    delay(5);
  }
}

void eyeblinkleft() {
  for (int i=90; i>0; i--) {
    servo2.write(15+i);
    delay(5);
  }
  for (int i=0; i<90; i++) {
    servo2.write(15+i);
    delay(5);
  }
}

void eyeblinkright() {
  for (int i=0; i<90; i++) {
    servo3.write(15+i);
    delay(5);
  }
  for (int i=90; i>0; i--) {
    servo3.write(15+i);
    delay(5);
  }
}

void wificonnect() {
// Connect to Wi-Fi
  WiFi.begin(ssid, password);
  externalSerial.println("wifi init");
  externalSerial.print("--> connecting to ");
  externalSerial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    externalSerial.print(".");
  }
  externalSerial.println();

  // Print ESP Local IP Address
  externalSerial.print("--> connected with IP ");
  externalSerial.println(WiFi.localIP());
  externalSerial.println("wifi init done");  
}

void wifireconnect() {
  externalSerial.println("reset wifi connection");
  WiFi.disconnect();
  delay(5000);
  wificonnect();
}

void twitchchatlogin(){
  if (!client.connected()) {
    if (client.connect(IRC_NICKNAME, "", IRC_OAUTH_TOKEN)) {
      externalSerial.println("twitch chat login");
      client.sendRaw("JOIN " + String(IRC_CHANNEL));
      externalSerial.println("twitch chat login done");
      externalSerial.println("--> TWITCH TMI COMMAND: JOIN CHANNEL");
      sendTwitchMessage("Here I stand...look around, around, around, around, around...");
      chatlogin = 0;
    }
    else {
      if (chatlogin < 5) {
        externalSerial.println("twitch chat login retry");
        chatlogin++;
        delay(5000);
      }
      else {
        wifireconnect();
        chatlogin = 0;
      }
    }
    return;
  }
}

void twitchchatrejoin(){
  client.sendRaw("JOIN " + String(IRC_CHANNEL));
}

void callback(IRCMessage ircMessage) {
  // PRIVMSG ignoring CTCP messages
  if (ircMessage.command == "PRIVMSG" && ircMessage.text[0] != '\001') {
    String message("<" + ircMessage.nick + "> " + ircMessage.text);
    externalSerial.print("--> TWITCH CHAT MESSAGE: ");
    externalSerial.println(message);
    chatmessage = ircMessage.text;
    chatmessage.toLowerCase();
    externalSerial.print("Message incoming (lower case): ");
    externalSerial.println(chatmessage);

    //chat message commands reading
    chatcommands();
    return;
  }  
  
  if (IRCdebug) {
    externalSerial.print("--> TWITCH TMI MESSAGE: ");
    externalSerial.println(ircMessage.original);
  }
}

void debugSentCallback(String data) {
  if (IRCdebug) {
    externalSerial.print("--> TWITCH CHANNEL MESSAGE: ");
    externalSerial.println(data);
  }
  if (data.indexOf("PONG")) {
    externalSerial.println("--> TWITCH TMI COMMAND: PING PONG");
    IRCpingpong = millis();
  }
}

void sendTwitchMessage(String message) {
  client.sendMessage(IRC_CHANNEL, message);
}

void chatcommands() {
  if (!sleepmode) {  //chat commands in awake mode
    //someone chats one of Furbys names
    for (int i = 0; i < sizeof(furbyNames) / sizeof(char*); i++) {
      if (chatmessage.indexOf(furbyNames[i]) >= 0) {
        randomgenerator = millis();
        if ((randomgenerator & 0x01) == 1) {
          earwiggleleft();
        } 
        else {
          earwiggleright();
        }
        basicanswer = random(sizeof(furbyNamesAnswers) / sizeof(char*));
        sendTwitchMessage(furbyNamesAnswers[basicanswer]);
      } 
    }
  }
  if (sleepmode) {  //chat commands in sleep mode
    //someone chats one of Furbys names
    for (int i = 0; i < sizeof(furbyNames) / sizeof(char*); i++) {
      if (chatmessage.indexOf(furbyNames[i]) >= 0) {
        sendTwitchMessage("...zZz...zZz...zZz...");
      } 
    }
  }
}

void webcommands() {
  if (mode == 0 && actionMode > 0){
    if (actionMessageA == "earwiggleleft") {
      earwiggleleft();
    }
    if (actionMessageA == "earwiggleright") {
      earwiggleright();
    }
    if (actionMessageA == "eyeblinkleft") {
      eyeblinkleft();
    }
    if (actionMessageA == "eyeblinkright") {
      eyeblinkright();
    }
    actionMode--;
  }
}
