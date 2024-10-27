/*
    Title:  Letter-Box Alarm
    lastRelease:  2023-05-01

    Author:   Andreas Wallensteiner
    Date:     2023-01-02
    Descr:    Reed Sensor detects open Mail-Box, saves the Time, Status and recieved (Open-Events), sends them via Telegram-Message and goes to Sleep to save Battery.
    Hardware: ESP8266
    Pinout:                                     PIN-#:
      D1      Reed-Sensor 1:    newLetter:      5     "D1"
      D2      Reed-Sensor 2:    openMailbox:    4     "D2"
      D3      Alarm-LED:                        0      + LED_BUILTIN
      D4      Servo:            LockServo       2     "D4"
      D8      Buzzer:                           15    "D8"

      GND to Resistor {LED, REED1, REED2, Servo}

    Implemented Features:
    - BuiltinLED is on (for Light) while the Box is open
    - Sends Telegram Message after every new Mail
    - Sends Warning Message if there are more than 5 Mails in the Box
    - Sends a Message, when the Box is emptied
    - Commands:
        /open   : opens the Lock of the Box with an attached Servo-Motor an locks again after 30s
        /status : Sends Information: 1) how many Letters, 2) Postman´s-Time (of first Letter), 3) Last Mail delivered

    To-Do´s:
      X mailMessage_Sent Issue to tun true again
      X Add Servo-Motor & Test!
        x open Postbox-Lock via Telegram-command
        x autoclose Door after sertain time
      X Telegram-Messages
        x Bot-Startup
        x newMail
        x Fullstate
        x Postbox was emptied
        ? Reminder Message(s)
      o Prototype with Battery-Power
      o implement Server to open Postbox-Lock via Telegram
      o Telegram Actions

    Nice to have:
      o String-Handling:
        o use saved Strings for Messages
      o Deep-Sleep, Trigger: reed_1 or reed_2
      o OTA-Update

    Done:
      x Time:
        x get from Server
        x Save Statup Time
        x save Startup Time, firstMailTime, emptyBoxTime and count Time since those events
      x Buzzer-Sound, to verify that Box has been cleared!
      x complete main-funktions
      x getting rid of Delays
      x 2nd Reed
      x Wait (30 Seconds) to send Messages via Telegramm
======================================================================================================*/

// #include <NTPClient.h> //old NTP Client-libary
#include <ESP8266WiFi.h>
// #include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
// #include <WiFi101.h>
#include "arduino_secrets.h"
#include <Servo.h>
Servo servoLock;

// Time-Clinet
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient *timeClient;

// Wifi & Telegram-Credentials
char WIFI_SSID[] = SECRET_SSID; // fill out your Secrets in the "arduino_secrets.h" -Tab
char WIFI_PASSWORD[] = SECRET_PASS;
char BOT_TOKEN[] = SECRET_BOTTOKEN;
char CHAT_ID[] = SECRET_CHATID;

const unsigned long BOT_MTBS = 1000; // mean time between scan messages
unsigned long bot_lasttime;          // last time messages' scan has been done

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Pin-Settings
#define REED_SWITCH_1 5 // D1: Pin 5  Reed switch is connected to digital pin D1
#define REED_SWITCH_2 4 // D2: Pin 4  Reed switch is connected to digital pin D2
#define Alarm_LED 0     // D3: Pin 0  Alarm-Led (Outside)
#define SERVO_PIN 2     // D4: Pin 2  Servo to open the Lock
#define buzzerPin 15    // D8: Pin 15 Buzzer

// Delay-Variables:
unsigned long nowTime_millis = 0;           // Stores actual Time
unsigned long savedTime = 0;                // Stores the last time the LED was updated
unsigned long days_to_reminderMessage = 4;  // 4 Days to remind (in millis)
unsigned long days_since_firstMailTime = 0; // calculate the days since the first mail
const long onInterval = 200;                // The on time interval in milliseconds
const long offInterval = 3000;              // The off time interval in milliseconds
const int buzzerDelay = 2000;               // const buzzer delay
const long sendCounter = 30000;             // 30 Seconds delay to send "New-Mais" Info-Message

// Servo-Codition
const int start = 0;
const int closed = 0;
const int open = 180;

// Counters:
int mails = 0; // Mail-Counter

unsigned long firstMailTime_millis = 0;
unsigned long lastMailTime_millis = 0;
unsigned long emptyBoxTime_millis = 0;
String firstMailTime_string = ""; // stores UTC-Time of your first Mail the day
String lastMailTime_string = "";
String emptyBoxTime_string = "";

// States-Flags:
bool newMail_State = false;
bool fullMail_State = false;
bool empty_State = true;
bool led_State = false;
bool reed1_wasOpen = false;
bool reed1_wasClosed = true;
bool reed2_wasOpen = false;

// Servo: Flag & Time:
bool servo_isOpen = false;
unsigned long servo_isOpen_millis = 0;

// Messages:
bool mailMessage_Sent = false;
unsigned long mailMessage_Sent_millis = 0;

bool emptyMessage_Sent = false;
bool openMessage_Sent = false;
bool reminderMessage_Sent = false;

// Debug-Mode:
bool debug_State = true; // true: for Debug-Messages & Tones, false = off

// Startup-Time:
int startup = 0;                      // Statup Flag
unsigned long StartupTime_millis = 0; // saves Startuptime in millis
String startupTime_string = "";       // saves the formatted StartupTime

//================== Unterprogramme: ================================================================
/* Buzzer-Alarms:
    - NoticeTone();           // NoticeTone:      C1 short, C1 long
    - ConfirmTone();          // Confirmation:    C1 short, C2 long
    - NoticeTone_short();     // short Notice:    C1 beeb, short Notice
*/

void NoticeTone()
{ // C1 short, C1 long = NoticeTone
  tone(buzzerPin, 100);
  delay(150);
  noTone(buzzerPin);
  delay(50);
  tone(buzzerPin, 100);
  delay(1000);
  noTone(buzzerPin);
  delay(buzzerDelay);
}

void ConfirmTone()
{ // C1 short, C2 long = Confirmation
  tone(buzzerPin, 100);
  delay(150);
  noTone(buzzerPin);
  delay(50);
  tone(buzzerPin, 200);
  delay(1000);
  noTone(buzzerPin);
  delay(buzzerDelay);
}

void NoticeTone_short()
{ // C1 beeb, short Notice
  tone(buzzerPin, 100);
  delay(150);
  noTone(buzzerPin);
  // delay(buzzerDelay);
}

void DebugTone()
{ // C1 beeb, short Notice
  if (debug_State = true)
  { // debugTone
    NoticeTone_short();
  }
}

// Servo-Unterprogramme

void LockOpen()
{ // Function to Open the Lock

  Serial.println("Servo opens . . .");
  bot.sendMessage(CHAT_ID, "wird entsperrt ...");
  delay(2000);

  servoLock.write(open); // Position 0° wird angesteuert

  Serial.println(" is open!");
  Serial.println("isOpen_millis: " + String(millis())); // Debug Message
  bot.sendMessage(CHAT_ID, "--> schließe zum Versperren die Türe.");

  servo_isOpen = true;            // Flag for openLock
  servo_isOpen_millis = millis(); // saves Time
}

void LockClose()
{

  while (digitalRead(REED_SWITCH_2) != LOW) // wait until the Door is closed
    delay(3000);

  servoLock.write(closed); // Function to CLOSE the Lock

  Serial.println("Servo closed");
  // bot.sendMessage(CHAT_ID, "Abgeschlossen!");
  ConfirmTone();

  servo_isOpen = false;
}

// TELEGRAM-Message Handler:

void handleNewMessages(int numNewMessages)
{
  Serial.print("handleNewMessages ");
  Serial.println(numNewMessages);

  String answer;
  for (int i = 0; i < numNewMessages; i++)
  {
    telegramMessage &msg = bot.messages[i];
    Serial.println("Received " + msg.text);

    if (msg.text == "/help")
    {
      answer = "Du brauchst _help_,? Versuchs mit den Befehlen: /start /status oder /open";
    }
    else if (msg.text == "/start")
    { // send Description
      answer = "Hi *" + msg.from_name + "*, willkommen zum Postkastl-Bot! \n";
      answer += "Dieser Bot sendet automatisch eine Nachricht: \n";
      answer += "- ein neuer Brief eingeworfen wurde, \n";
      answer += "- der Postkasten geleert wurde. \n";
    }
    else if (msg.text == "/status")
    {
      if (mails == 0)
      {
        answer = "Aktuell sind *keine* Briefe im Postkasten. \n";
      }
      else
      {
        answer = "Aktuell sind [" + String(mails) + "] Briefe/Sendungen im Postkasten! \n";
        answer += "Postlerzeit: " + firstMailTime_string + "\n";
        answer += "letzer Einwurf:  " + lastMailTime_string;
      }
    }
    else if (msg.text == "/open")
    {
      // answer = " ";
      LockOpen(); // opens ServoLock
    }
    else
    {
      answer = "Bitte gebe einen gültigen Befehl ein wie: \n";
      answer += "/status oder /help";
    }
    bot.sendMessage(msg.chat_id, answer, "Markdown");
  }
}

void bot_setup()
{
  const String commands = F("["
                            "{\"command\":\"help\",  \"description\":\"Get bot usage help\"},"
                            "{\"command\":\"start\", \"description\":\"Message sent when you open a chat with a bot\"},"
                            "{\"command\":\"open\", \"description\":\"Öffnet den Postkasten (ohne Schlüssel)\"},"
                            "{\"command\":\"status\",\"description\":\"Infos zu wartenden Nachrichten\"}" // no comma on last command
                            "]");
  bot.setMyCommands(commands);
  bot.sendMessage("25235518", "Postkasten-Bot ist online!", "Markdown");
}

// ===================================================================================================================

void setup()
{

  Serial.begin(9600);
  // Serial.begin(115200);
  Serial.println();

  pinMode(LED_BUILTIN, OUTPUT);         // Set onboard LED pin as output
  pinMode(Alarm_LED, OUTPUT);           // Set Alarm-LED as output
  pinMode(REED_SWITCH_1, INPUT_PULLUP); // Set reed switch pin as input with pull-up resistor
  pinMode(REED_SWITCH_2, INPUT_PULLUP); // Set reed switch pin as input with pull-up resistor

  digitalWrite(LED_BUILTIN, HIGH); // Turn off onboard LED on startup

  servoLock.attach(SERVO_PIN, 500, 2500); // attach Servo (PIN 2/D4, min, max)
  servoLock.write(start);

  configTime(0, 0, "pool.ntp.org");      // get UTC time via NTP
  secured_client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org

  // Connecting to Wifi
  Serial.println();
  Serial.print("Connecting to Wifi SSID ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.println("IP address: ");
  Serial.print(WiFi.localIP());
  // Serial.println("IP address: " + WiFi.localIP().toString());
  Serial.println();
  DebugTone();

  // Check NTP/Time, usually it is instantaneous and you can delete the code below.
  Serial.print("Retrieving time: ");
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(now);

  bot_setup(); // Telegram-Bot Start

  // Time-Client
  timeClient = new NTPClient(ntpUDP, "pool.ntp.org", 7200);
  timeClient->begin();
  timeClient->update();

  // LED indicates it is starting up, flashed 3 times:
  for (int i = 0; i < 4; i++)
  {
    digitalWrite(Alarm_LED, HIGH);
    delay(200);
    digitalWrite(Alarm_LED, LOW);
    delay(200);
  }
  digitalWrite(Alarm_LED, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(3000);
  digitalWrite(Alarm_LED, LOW);
  digitalWrite(LED_BUILTIN, HIGH);

  startup = 1;
}

void loop()
{

  static int mails_alt = mails; // checks if mails-Variable has changed since

  // Time
  timeClient->update();
  unsigned long nowTime_millis = millis();
  String nowTime_string = String(timeClient->getFormattedTime()); // get formatted Time
  delay(100);

  // Telegram-Message-Handling
  if (millis() - bot_lasttime > BOT_MTBS)
  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages)
    {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    bot_lasttime = millis();
  }

  // Checks if Startup, runs only once
  if (startup == 1)
  {

    StartupTime_millis = millis();
    startupTime_string = String(timeClient->getFormattedTime()); // saves current StartuptTime
    Serial.println("Started up!");
    Serial.println("Startupt Time: " + startupTime_string);
    Serial.println();

    // Telegram: Startup-Message
    bot.sendMessage(CHAT_ID, "Bot ist online!");

    NoticeTone_short(); // Startup-"Beep"
    startup += 1;
  }

  // If the REED-SWITCH 1 is open (LOW) = NEW MAIL(S)
  if (digitalRead(REED_SWITCH_1) == HIGH)
  {

    reed1_wasOpen = true;
    reed1_wasClosed = false;
    newMail_State = true; // LED can start to flash  o should do later

    Serial.println("REED_1 is: OPEN"); // #debug message
  }

  // New Mail was inserted an the slit snapped back again
  if ((digitalRead(REED_SWITCH_1) == LOW) && (reed1_wasOpen == true))
  {

    mails += 1;
    reed1_wasOpen = false;

    if (mails == 1)
    {

      firstMailTime_millis = millis(); // saves time firstMailTime
      firstMailTime_string = timeClient->getFormattedTime();

      Serial.println("[!]: New MAIL in you Postbox!");
      Serial.println("collected at: " + firstMailTime_string);
      Serial.println();
    }
    else if (mails == 5)
    {

      Serial.println("[" + String(mails) + "]: New MAIL(s) in you Postbox!");
      Serial.println("[!]: Your mailbox is pretty full!");
      Serial.println("--> Please send someone to empty it.");
      Serial.println();

      // Telegram-Message: "Mailbox-Full"
      Serial.println("TELEGRAM-MESSAGE: Full-Box");

      bot.sendMessage(CHAT_ID, "Dein Briefkasten ist ziemlich voll!");
      bot.sendMessage(CHAT_ID, "[" + String(mails) + "]: Nachrichten im Briefkasten!");
      bot.sendMessage(CHAT_ID, "--> Sende jemanden zum Entleeren!");

      fullMail_State = true;
    }
    else
    {

      lastMailTime_millis = millis(); // saves Time of first Mail
      lastMailTime_string = String(timeClient->getFormattedTime());

      Serial.println();
      Serial.println("[" + String(mails) + "]: New MAIL(s) in you Postbox!");
      Serial.println("first collected at: " + firstMailTime_string);
      Serial.println("last collected at:  " + lastMailTime_string);
      Serial.println();
    }

    Serial.println("TELEGRAM MESSAGE: Neue Nachrichten"); // Debug Message
    Serial.println();
    bot.sendMessage(CHAT_ID, "[" + String(mails) + "]: Neue Nachrichten im Briefkasten!");
    // bot.sendMessage(CHAT_ID, "Postler-Zeit: " + firstMailTime_string);
    // bot.sendMessage(CHAT_ID, "letzer Einwurf um:  " + lastMailTime_string);

    if (millis() - lastMailTime_millis > 30000)
    {
      if (firstMailTime_millis != 0)
      { //&& (mails == 1))
        bot.sendMessage(CHAT_ID, "Postler-Zeit:   " + firstMailTime_string);
      }
      if (lastMailTime_millis != 0)
      {
        bot.sendMessage(CHAT_ID, "letzer Einwurf: " + lastMailTime_string);
      }
    }

    mailMessage_Sent_millis = millis();
    mailMessage_Sent = true; // reset the Flag??

    mails_alt = mails;
  }

  /*
  //LED Lights while Box is Open
  if (digitalRead(REED_SWITCH_2) == HIGH){
    digitalWrite(LED_BUILTIN, LOW); // turn Led ON = LOW -> ONBOARD-LED is reversed
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  */

  // If the REED-SWITCH 2 is open (LOW): MAILBOX CLEARED
  if (digitalRead(REED_SWITCH_2) == HIGH)
  {

    digitalWrite(LED_BUILTIN, LOW); // turn Led ON = LOW -> ONBOARD-LED is reversed

    if (openMessage_Sent == false)
    {

      Serial.println("[!]: Your Mailbox is open");
      Serial.println("opened at: " + nowTime_string);
      Serial.println();
      bot.sendMessage(CHAT_ID, "Dein Postkastl wurde geöffnet!");

      openMessage_Sent = true;
    }

    reed2_wasOpen = true;      // opened
    emptyMessage_Sent = false; // re-sets emptyMessage
    fullMail_State = false;
    delay(2000);
  }
  else
  {
    digitalWrite(LED_BUILTIN, HIGH); // tur off
  }

  // Mailbox was opened & closed again --> assumes it is empty:
  if ((digitalRead(REED_SWITCH_2) == LOW) && (reed2_wasOpen == true))
  { // REED_2 was closed

    digitalWrite(LED_BUILTIN, HIGH); // Turn off LED

    mails = 0;             // delete MailCounter
    reed2_wasOpen = false; // = was closed
    newMail_State = false; // Mailbox should be cleared now

    firstMailTime_millis = 0;
    lastMailTime_millis = 0;

    emptyBoxTime_string = nowTime_string; // saves current formatted-Time
    emptyBoxTime_millis = millis();

    if (servo_isOpen == true)
    {
      LockClose(); // Lock the Door after closed
    }

    Serial.println("Your Mailbox was CLOSED again."); // Debug-Message
    Serial.println("closed at: " + nowTime_string);
    Serial.println();

    bot.sendMessage(CHAT_ID, "Postkastl wieder geschlossen!"); // Debug Message

    // Messages:
    if (emptyMessage_Sent == false)
    {

      Serial.println();
      Serial.println("[!]: Your Mailbox was just cleared!");
      Serial.println("[" + String(mails) + "] Mails ramaining!");
      Serial.println("at: " + emptyBoxTime_string);
      Serial.println();

      // Telegram Message:
      // bot.sendMessage(CHAT_ID, "Dein Postkasten ist wieder leer!");
      delay(1000);
      bot.sendMessage(CHAT_ID, "[" + String(mails) + "] Nachrichten.");

      emptyMessage_Sent = true;
      openMessage_Sent = false;
      reminderMessage_Sent = false;
      days_since_firstMailTime = 0;
    }
  }

  // Reminder-Message(s):
  // 1. Warning: if Mailbox is full (see above)
  // 2. Warning: if Mailbox has not been emptied since 4 days
  days_since_firstMailTime = ((nowTime_millis - firstMailTime_millis) / 86400000); // 1 day in millis

  if ((days_since_firstMailTime >= days_to_reminderMessage) && (reminderMessage_Sent = false))
  { // Reminder-Message after [4] days with new Mail

    Serial.println("[!]: This is a Reminder to empty your mailbox!");
    Serial.println("[" + String(mails) + "] Mails are waiting since [" + String(days_since_firstMailTime) + "] Days.");

    bot.sendMessage(CHAT_ID, "Vergiss nicht, deinen Postenkasten zu Entleeren!");
    bot.sendMessage(CHAT_ID, "[" + String(mails) + "] Nachrichten warten \n seit [" + String(days_since_firstMailTime) + "] Tagen.");

    reminderMessage_Sent = true;
  }

  // LED-Indicates for newMails
  if (newMail_State)
  { // LED-Alarm indicates new Mail(s) in the Mailbox
    if (led_State)
    { // If the LED is on
      if (nowTime_millis - savedTime >= onInterval)
      {                               // Check if the on interval has passed
        savedTime = nowTime_millis;   // Update the previous time
        led_State = false;            // Change the LED state to off
        digitalWrite(Alarm_LED, LOW); // Turn the LED off
      }
    }
    else
    { // If the LED is off
      if (nowTime_millis - savedTime >= offInterval)
      {                                // Check if the off interval has passed
        savedTime = nowTime_millis;    // Update the previous time
        led_State = true;              // Change the LED state to on
        digitalWrite(Alarm_LED, HIGH); // Turn the LED on
      }
    }
  }

  // close Lock after 30 Seconds for safety if open
  if (servo_isOpen == true && ((millis() - servo_isOpen_millis) >= sendCounter))
  {                                                                        // after 30 Seconds
    Serial.println("Servo wird geschlossen durch 30 Sekunden Zeitablauf"); // Debug-Messages
    LockClose();
  }
}
