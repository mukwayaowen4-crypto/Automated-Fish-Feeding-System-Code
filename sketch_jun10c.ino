#include "uRTCLib.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// RTC and LCD Configuration
uRTCLib rtc(0x68);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Servo Configuration
Servo feedServo;
const int servoPin = 9;
const int openAngle = 90;
const int closedAngle = 0;
const unsigned long feedDuration = 2000;  // 2 seconds

// LED Configuration
const int LED_POWER = 2;
const int LED_FEEDING = 3;
const int LED_MANUAL = 4;
const int BUZZER = 6;

// Button and Switch Configuration
const int MODE_SWITCH_PIN = 7;
const int MANUAL_BTN_PIN = 8;

// Ultrasonic Sensor Configuration
const int TRIG_PIN = 10;
const int ECHO_PIN = 11;
unsigned long lastUltrasonicRead = 0;
const unsigned long ultrasonicInterval = 5000; // read every 5 seconds

// Feed level states
enum FeedLevelState { NORMAL, CRITICAL, FULL, EMPTY };
FeedLevelState currentFeedState = NORMAL;
FeedLevelState lastReportedState = NORMAL;

// Feeding schedule for different fish types
const int feedingTimesGrower[3] = {0, 8, 16};
// Fixed array boundary to match 4 elements declared
const int feedingTimesFingerling[4] = {0, 6, 12, 18};

const int feedMinute= 0;

// Fish type selection
enum FishType { GROWER, FINGERLING };
FishType currentFishType = GROWER;
bool lastSwitchState = LOW;

// Feeding control variables
bool isFeeding = false;
bool isManualFeeding = false;
unsigned long feedingStartTime = 0;
int lastFedHour = -1;
int lastFedMinute = -1;

// Display mode
enum DisplayMode { MODE_DATETIME, MODE_NEXT_FEED };
DisplayMode currentMode = MODE_DATETIME;
unsigned long lastModeSwitch = 0;
unsigned long feedingScreenStartTime = 0;
const unsigned long datetimeDuration = 600000;  // 10 minutes
const unsigned long nextFeedDuration = 30000;   // 30 seconds

// Buzzer alert timing
bool preFeedAlertActive = false;
bool postFeedAlertActive = false;
unsigned long preFeedAlertStart = 0;
unsigned long postFeedAlertStart = 0;
int beepCount = 0;
const unsigned long alertDuration = 10000;  // 10 seconds

// Manual feeding
bool manualFeedTriggered = false;
unsigned long manualFeedStartTime = 0;
const unsigned long manualFeedDuration = 2000;  // 2 seconds manual feed

int lastMinute = -1;

// GSM Variables
int _timeout;
String _buffer;
String incomingSMS = "";
String authorizedNumber = "+256788566088"; // Authorized phone number

void checkFeedLevel();
void sendFeedLevelAlert(FeedLevelState state, float distance);
void checkIncomingSMS();
void startManualFeedingFromSMS(String senderNumber);
void sendStatusUpdate(String senderNumber);
void sendSMS(String phoneNumber, String message);
void handleModeSwitch();
void handleManualButton();
void startManualFeeding();
void stopManualFeeding();
void displayManualFeedingStatus();
void startFeeding(bool isManual);
void stopFeeding();
void displayFeedingStatus();
void updateFeedingCountdownWhileAlert();
void updateDateTimeDisplay(const char* fishType);
void displayNextFeeding(const int* feedingTimes, const char* fishType);
void handleAlerts(unsigned long currentMillis);
void testAlertPattern();

void setup() {
  // Initialize RTC
  //rtc.set(0, 45, 16, 3, 10, 6, 26 );
  URTCLIB_WIRE.begin();
  Wire.begin();
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  
  // Initialize Servo
  feedServo.attach(servoPin);
  feedServo.write(closedAngle);
  delay(500);
  
  // Initialize LEDs
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_FEEDING, OUTPUT);
  pinMode(LED_MANUAL, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  
  // Initialize button and switch
  pinMode(MODE_SWITCH_PIN, INPUT);
  pinMode(MANUAL_BTN_PIN, INPUT);
  
  // Initialize Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // Initialize GSM module via native Hardware Serial
  Serial.begin(9600); 
  delay(1000);
  Serial.println("AT+CMGF=1"); // Text mode
  delay(200);
  Serial.println("AT+CNMI=1,2,0,0,0"); // Set to receive SMS directly
  delay(200);
  
  // Power LED always ON
  digitalWrite(LED_POWER, HIGH);
  digitalWrite(LED_FEEDING, LOW);
  digitalWrite(LED_MANUAL, LOW);
  digitalWrite(BUZZER, LOW);
  
  // Display startup sequence
  lcd.print("Fish Feeder UICT");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);
  
  //Test buzzer and LEDs
  testAlertPattern();
  delay(500);
  digitalWrite(LED_FEEDING, HIGH);
  delay(500);
  digitalWrite(LED_FEEDING, LOW);
  
  lastModeSwitch = millis();
  updateDateTimeDisplay("GW");
  lastMinute = rtc.minute();

  // Read initial switch state
  lastSwitchState = digitalRead(MODE_SWITCH_PIN);
  currentFishType = (lastSwitchState == HIGH) ? FINGERLING : GROWER;
  
  // Send startup SMS
  sendSMS(authorizedNumber, "Fish Feeder System Started");
}

void loop() {
  rtc.refresh();
  unsigned long currentMillis = millis();
  
  int currentHour = rtc.hour();
  int currentMinute = rtc.minute();
  
  // Check for incoming SMS commands
  checkIncomingSMS();
  
  // Handle manual button
  handleManualButton();
  
  // Handle mode switch
  handleModeSwitch();
  
  // Handle non-blocking buzzer alerts
  handleAlerts(currentMillis);
  
  // Ultrasonic feed level monitoring (non-blocking)
  if (currentMillis - lastUltrasonicRead >= ultrasonicInterval) {
    lastUltrasonicRead = currentMillis;
    checkFeedLevel();
  }
  
  // Get current feeding schedule based on fish type
  const int* feedingTimes = (currentFishType == GROWER) ? feedingTimesGrower : feedingTimesFingerling;
  const char* fishTypeStr = (currentFishType == GROWER) ? "GW" : "FNG";
  
  // Check if feeding time is approaching (1 minute before)
  bool feedingApproaching = false;
  // Dynamic iteration bound checking based on schedule choice
  int limit = (currentFishType == GROWER) ? 3 : 4; 
  for (int i = 0; i < limit; i++) {
    if (currentHour == feedingTimes[i] && currentMinute == feedMinute -1) {
      feedingApproaching = true;
      break;
    }
  }
  
  // Pre-feeding alert (1 minute before feeding starts)
  if (feedingApproaching && !preFeedAlertActive && !isFeeding && !isManualFeeding) {
    preFeedAlertActive = true;
    preFeedAlertStart = currentMillis;
    beepCount = 0;
    lcd.clear();
    lcd.print("FEEDING SOON!");
    lcd.setCursor(0, 1);
    lcd.print("In 1 minute...");
  }
  
  // End pre-feed alert after 10 seconds
  if (preFeedAlertActive && !isFeeding && (currentMillis - preFeedAlertStart >= alertDuration)) {
    preFeedAlertActive = false;
    digitalWrite(BUZZER, LOW);
    digitalWrite(LED_FEEDING, LOW);
  }
  
  // Check if it's time to feed (automatic)
  bool shouldFeed = false;
  for (int i = 0; i < limit; i++) {
    if (currentHour == feedingTimes[i] && currentMinute == feedMinute) {
      shouldFeed = true;
      break;
    }
  }
  
  // Handle automatic feeding logic
  if (shouldFeed && !isFeeding && !isManualFeeding) {
    if (lastFedHour != currentHour || lastFedMinute != currentMinute) {
      startFeeding(false);  // false = automatic feeding
    }
  }
  
  // Handle manual feeding
  if (isManualFeeding) {
    unsigned long currentTime = millis();
    if (currentTime - manualFeedStartTime >= manualFeedDuration) {
      stopManualFeeding();
    } else {
      displayManualFeedingStatus();
    }
  }
  
  // Manage ongoing automatic feeding
  if (isFeeding && !isManualFeeding) {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - feedingStartTime;
    
    // Check if feeding is about to end (10 seconds before)
    if (!postFeedAlertActive && (feedDuration - elapsed <= alertDuration) && (feedDuration - elapsed) > 0) {
      postFeedAlertActive = true;
      postFeedAlertStart = currentTime; 
      beepCount = 0;
      lcd.clear();
      lcd.print("FEEDING ENDING");
      lcd.setCursor(0, 1);
      lcd.print("In 10 secs ...");
    }
    
    // End post-feed alert after 10 seconds
    if (postFeedAlertActive && (currentTime - postFeedAlertStart >= alertDuration)) {
      postFeedAlertActive = false;
      digitalWrite(BUZZER, LOW);
    }
    
    if (elapsed >= feedDuration) {
      stopFeeding();
    } else {
      if (!postFeedAlertActive) {
        displayFeedingStatus();
      } else {
        updateFeedingCountdownWhileAlert();
      }
    }
  }
  
  // Handle display modes (only when not feeding and no alerts)
  if (!isFeeding && !isManualFeeding && !preFeedAlertActive) {
    if (currentMode == MODE_DATETIME) {
      if (currentMillis - lastModeSwitch >= datetimeDuration) {
        currentMode = MODE_NEXT_FEED;
        feedingScreenStartTime = currentMillis;
        displayNextFeeding(feedingTimes, fishTypeStr);
      } else {
        int currentMin = rtc.minute();
        if (currentMin != lastMinute) {
          lastMinute = currentMin;
          updateDateTimeDisplay(fishTypeStr);
        }
      }
    } 
    else if (currentMode == MODE_NEXT_FEED) {
      if (currentMillis - feedingScreenStartTime >= nextFeedDuration) {
        currentMode = MODE_DATETIME;
        lastModeSwitch = currentMillis;
        updateDateTimeDisplay(fishTypeStr);
      }
    }
  }
  
  delay(50);
}

// Ultrasonic feed level monitoring
void checkFeedLevel() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return;
  
  float distance = duration * 0.0343 / 2.0;
  
  FeedLevelState newState;
  if (distance <= 5.0) {
    newState = FULL;
  } else if (distance <= 18) {
    newState = CRITICAL;
  } else {
    newState = EMPTY; // Triggered when distance is above 18cm
  }
  
  currentFeedState = newState;  
  
  if (newState == CRITICAL && lastReportedState != CRITICAL && lastReportedState != FULL) {
    sendFeedLevelAlert(CRITICAL, distance);
    lastReportedState = CRITICAL;
  }
  else if (newState == EMPTY && lastReportedState != EMPTY) {
    sendSMS(authorizedNumber, "Hopper empty!");
    lastReportedState = EMPTY;
  }
    else if (newState == NORMAL && (lastReportedState == CRITICAL || lastReportedState == FULL || lastReportedState == EMPTY)) {
      }  lastReportedState = NORMAL;
  }


void sendFeedLevelAlert(FeedLevelState state, float distance) {
  String message;
  if (state == CRITICAL) {
    message = "ALERT: Feed level CRITICAL! Refill soon.";
  }
  sendSMS(authorizedNumber, message);
}

void checkIncomingSMS() {
  if (Serial.available() > 0) {
    String response = Serial.readString();
    
    if (response.indexOf("+CMT:") != -1) {
      int startIdx = response.indexOf("\"") + 1;
      int endIdx = response.indexOf("\"", startIdx);
      String sender = response.substring(startIdx, endIdx);
      
      int commaCount = 0;
      int msgStart = 0;
      for (int i = 0; i < response.length(); i++) {
         if (response[i] == ',') {
            commaCount++;
           if (commaCount == 4) {
             msgStart = i + 2;
             break;
           }
         }
      }
      
      String message = response.substring(msgStart);
      message.trim();
      message.replace("\r", "");
      message.replace("\n", "");
      
      if (message.indexOf("FEED") != -1 || message.indexOf("feed") != -1) {
        if (!isFeeding && !isManualFeeding) {
          startManualFeedingFromSMS(sender);
        } else {
          sendSMS(sender, "System is already feeding. Please try again later.");
        }
      }
      else if (message.indexOf("STATUS") != -1 || message.indexOf("status") != -1) {
        sendStatusUpdate(sender);
      }
    }
  }
}

void startManualFeedingFromSMS(String senderNumber) {
  if (currentFeedState == EMPTY) {
    sendSMS(senderNumber, "ALERT: fish not fed. Hopper is empty!");
    return;
  }

  digitalWrite(LED_MANUAL, HIGH);
  manualFeedTriggered = true;
  
  feedServo.write(openAngle);
  delay(1000);
  
  isManualFeeding = true;
  manualFeedStartTime = millis();
  
  lcd.clear();
  lcd.print("MANUAL FEEDING");
  lcd.setCursor(0, 1);
  lcd.print("Remaining: 30s");
  
  digitalWrite(BUZZER, HIGH);
  delay(200);
  digitalWrite(BUZZER, LOW);
  
  char timeStr[20];
  sprintf(timeStr, "%02d:%02d:%02d", rtc.hour(), rtc.minute(), rtc.second());
  String message = "Manual feeding triggered via SMS at " + String(timeStr);
  sendSMS(senderNumber, message);
}

void sendStatusUpdate(String senderNumber) {
  const char* fishTypeStr = (currentFishType == GROWER) ? "Grower" : "Fingerling";
  String status = "Fish Feeder Status:\n";
  status += "Mode: " + String(fishTypeStr) + "\n";
  status += "Time: " + String(rtc.hour()) + ":" + String(rtc.minute()) + ":" + String(rtc.second()) + "\n";
  status += "Date: " + String(rtc.day()) + "/" + String(rtc.month()) + "/" + String(rtc.year()) + "\n";
  
  String levelStr;
  switch(currentFeedState) {
    case NORMAL: levelStr = "Normal (>15cm)"; break;
    case CRITICAL: levelStr = "CRITICAL (<=18cm)"; break;
    case FULL: levelStr = "FULL (<=5cm)"; break;
    case EMPTY: levelStr = "EMPTY (>18cm) - LOCKED"; break;
    default: levelStr = "Unknown";
  }
  status += "Feed Level: " + levelStr + "\n";
  
  if (isFeeding) {
    status += "Status: FEEDING IN PROGRESS";
  } else if (isManualFeeding) {
    status += "Status: MANUAL FEEDING ACTIVE";
  } else {
    status += "Status: IDLE";
  }
  
  sendSMS(senderNumber, status);
}

void sendSMS(String phoneNumber, String message) {
  Serial.println("AT+CMGF=1");
  delay(200);
  Serial.println("AT+CMGS=\"" + phoneNumber + "\"\r");
  delay(200);
  Serial.println(message);
  delay(100);
  Serial.println((char)26);
  delay(200);
}

void handleModeSwitch() {
  bool currentSwitchState = digitalRead(MODE_SWITCH_PIN);
  
  if (currentSwitchState != lastSwitchState) {
    FishType oldFishType = currentFishType;
    
    if (currentSwitchState == HIGH) {
      currentFishType = FINGERLING;
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
    } else {
      currentFishType = GROWER;
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
    }
    lastSwitchState = currentSwitchState;
    
    if (oldFishType != currentFishType) {
      const char* newMode = (currentFishType == GROWER) ? "Grower" : "Fingerling";
      char timeStr[20];
      sprintf(timeStr, "%02d:%02d:%02d", rtc.hour(), rtc.minute(), rtc.second());
      String message = "Mode changed to " + String(newMode) + " at " + String(timeStr);
      sendSMS(authorizedNumber, message);
    }
    
    const char* fishTypeStr = (currentFishType == GROWER) ? "GW" : "FNG";
    updateDateTimeDisplay(fishTypeStr);
  }
}

void handleManualButton() {
  int buttonState = digitalRead(MANUAL_BTN_PIN);
  
  if (buttonState == HIGH && !isFeeding && !isManualFeeding) {
    digitalWrite(LED_MANUAL, HIGH);
    manualFeedTriggered = true;
    startManualFeeding();
  }
}

void startManualFeeding() {
  if (currentFeedState == EMPTY) {
    lcd.clear();
    lcd.print("ERROR: HOPPER");
    lcd.setCursor(0, 1);
    lcd.print("EMPTY ");
    
    sendSMS(authorizedNumber, "ALERT: fish not fed. Hopper is empty!");
    digitalWrite(LED_MANUAL, LOW);
    manualFeedTriggered = false;
    return; // Exit early so the servo doesn't open
  }

  isManualFeeding = true;
  manualFeedStartTime = millis();
  
  feedServo.write(openAngle);
  delay(1000);
  
  lcd.clear();
  lcd.print("MANUAL FEEDING");
  lcd.setCursor(0, 1);
  lcd.print("Remaining: 30s");
  
  digitalWrite(BUZZER, HIGH);
  delay(200);
  digitalWrite(BUZZER, LOW);

  char timeStr[20];
  sprintf(timeStr, "%02d:%02d:%02d", rtc.hour(), rtc.minute(), rtc.second());
  String message = "Manual feeding triggered via button at " + String(timeStr);
  sendSMS(authorizedNumber, message);
}

void stopManualFeeding() {
  feedServo.write(closedAngle);
  delay(1000);
  
  isManualFeeding = false;
  manualFeedTriggered = false;
  digitalWrite(LED_MANUAL, LOW);
  
  currentMode = MODE_DATETIME;
  lastModeSwitch = millis();
  const char* fishTypeStr = (currentFishType == GROWER) ? "GW" : "FNG";
  updateDateTimeDisplay(fishTypeStr);
  
  digitalWrite(BUZZER, HIGH);
  delay(200);
  digitalWrite(BUZZER, LOW);
  delay(100);
  digitalWrite(BUZZER, HIGH);
  delay(200);
  digitalWrite(BUZZER, LOW);
}

void displayManualFeedingStatus() {
  unsigned long elapsed = millis() - manualFeedStartTime;
  unsigned long remaining = manualFeedDuration - elapsed;
  int secondsRemaining = remaining / 1000;
  
  lcd.clear();
  lcd.print("MANUAL FEEDING");
  lcd.setCursor(0, 1);
  lcd.print("Remaining: ");
  lcd.print(secondsRemaining);
  lcd.print("s");
}

void startFeeding(bool isManual) {
  if (currentFeedState == EMPTY) {
    sendSMS(authorizedNumber, "ALERT: fish not fed. Hopper is empty!");
    return; // Exit early so the servo doesn't open
  }
 
  digitalWrite(LED_FEEDING, HIGH);
  feedServo.write(openAngle);
  delay(1000);
  
  isFeeding = true;
  feedingStartTime = millis();
  lastFedHour = rtc.hour();
  lastFedMinute = rtc.minute();
  postFeedAlertActive = false;
  
  displayFeedingStatus();
  
  char timeStr[20];
  sprintf(timeStr, "%02d:%02d:%02d", rtc.hour(), rtc.minute(), rtc.second());
  const char* fishTypeStr = (currentFishType == GROWER) ? "Grower" : "Finger";
  String message = "Feeding started at " + String(timeStr) + " for " + String(fishTypeStr);
  sendSMS(authorizedNumber, message);
}

void stopFeeding() {
  digitalWrite(LED_FEEDING, LOW);
  feedServo.write(closedAngle);
  delay(1000);
  
  char timeStr[20];
  sprintf(timeStr, "%02d:%02d:%02d", rtc.hour(), rtc.minute(), rtc.second());
  String message = "Feeding completed at " + String(timeStr);
  sendSMS(authorizedNumber, message);
  
  isFeeding = false;
  preFeedAlertActive = false;
  postFeedAlertActive = false;
  digitalWrite(BUZZER, LOW);
  
  currentMode = MODE_DATETIME;
  lastModeSwitch = millis();
  const char* fishTypeStr = (currentFishType == GROWER) ? "GW" : "FNG";
  updateDateTimeDisplay(fishTypeStr);
}

void displayFeedingStatus() {
  lcd.clear();
  lcd.print("FEEDING NOW!");
  lcd.setCursor(0, 1);
  lcd.print("Remaining: ");
  
  unsigned long elapsed = millis() - feedingStartTime;
  unsigned long remaining = feedDuration - elapsed;
  int minutesRemaining = remaining / 60000;
  int secondsRemaining = (remaining % 60000) / 1000;
  
  lcd.print(minutesRemaining);
  lcd.print(":");
  if (secondsRemaining < 10) lcd.print("0");
  lcd.print(secondsRemaining);
}

void updateFeedingCountdownWhileAlert() {
  lcd.setCursor(11, 1);
  lcd.print("         ");
  lcd.setCursor(11, 1);
  
  unsigned long elapsed = millis() - feedingStartTime;
  unsigned long remaining = feedDuration - elapsed;
  int minutesRemaining = remaining / 60000;
  int secondsRemaining = (remaining % 60000) / 1000;
  
  lcd.print(minutesRemaining);
  lcd.print(":");
  if (secondsRemaining < 10) lcd.print("0");
  lcd.print(secondsRemaining);
}

void updateDateTimeDisplay(const char* fishType) {
  lcd.clear();
  lcd.print("Date: ");
  lcd.print(rtc.day());
  lcd.print('/');
  if (rtc.month() < 10) lcd.print('0');
  lcd.print(rtc.month());
  lcd.print('/');
  lcd.print(rtc.year());
  
  lcd.setCursor(0, 1);
  lcd.print("Time: ");
  if (rtc.hour() < 10) lcd.print('0');
  lcd.print(rtc.hour());
  lcd.print(':');
  if (rtc.minute() < 10) lcd.print('0');
  lcd.print(rtc.minute());
  lcd.print(" ");
  lcd.print(fishType);
}

void displayNextFeeding(const int* feedingTimes, const char* fishType) {
  int currentHour = rtc.hour();
  int currentMinute = rtc.minute();
  
  int nextFeedHour = -1;
  int limit = (currentFishType == GROWER) ? 3 : 4;
  for (int i = 0; i < limit; i++) {
    if (feedingTimes[i] > currentHour || (feedingTimes[i] == currentHour && currentMinute < feedMinute)) {
      nextFeedHour = feedingTimes[i];
      break;
    }
  }
  
  bool isTomorrow = (nextFeedHour == -1);
  if (isTomorrow) nextFeedHour = feedingTimes[0] + 24;
  
  int hoursUntil, minutesUntil;
  if (isTomorrow) {
    hoursUntil = (24 - currentHour) + (nextFeedHour - 24);
    minutesUntil = (60 - currentMinute) % 60;
    if (minutesUntil > 0) hoursUntil--;
  } else {
    hoursUntil = nextFeedHour - currentHour;
    minutesUntil = feedMinute - currentMinute;
    if (minutesUntil < 0) {
      minutesUntil += 60;
      hoursUntil--;
    }
  }
  
  int displayHour = isTomorrow ? nextFeedHour - 24 : nextFeedHour;
  
  lcd.clear();
  lcd.print("Next Feed ");
  if (displayHour < 10) lcd.print('0');
  lcd.print(displayHour);
  lcd.print(':');
  if (feedMinute < 10) lcd.print('0');
  lcd.print(feedMinute);
  
  lcd.setCursor(0, 1);
  if (hoursUntil > 0) {
    lcd.print("In: ");
    lcd.print(hoursUntil);
    lcd.print("h ");
    lcd.print(minutesUntil);
    lcd.print("m");
  } else if (hoursUntil == 0 && minutesUntil > 0) {
    lcd.print("In: ");
    lcd.print(minutesUntil);
    lcd.print(" minutes");
  } else if (isTomorrow) {
    lcd.print("Tomorrow ");
    if (displayHour < 10) lcd.print('0');
    lcd.print(displayHour);
    lcd.print(':');
    if (feedMinute < 10) lcd.print('0');
    lcd.print(feedMinute);
  }
}

void handleAlerts(unsigned long currentMillis) {
  if (preFeedAlertActive && !isFeeding) {
    if (currentMillis - preFeedAlertStart < alertDuration) {
      int beepNumber = (currentMillis - preFeedAlertStart) / 2000;
      if (beepNumber < 5 && beepNumber != beepCount) {
        beepCount = beepNumber;
        digitalWrite(BUZZER, HIGH);
        digitalWrite(LED_FEEDING, HIGH);
        delay(200);
        digitalWrite(BUZZER, LOW);
        digitalWrite(LED_FEEDING, LOW);
      }
    }
  }
  
  if (postFeedAlertActive && isFeeding) {
    if (currentMillis - postFeedAlertStart < alertDuration) {
      int beepNumber = (currentMillis - postFeedAlertStart) / 2000;
      if (beepNumber < 5 && beepNumber != beepCount) {
        beepCount = beepNumber;
        digitalWrite(BUZZER, HIGH);
        delay(200);
        digitalWrite(BUZZER, LOW);
      }
    }
  }
}

void testAlertPattern() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    digitalWrite(LED_FEEDING, HIGH);
    delay(200);
    digitalWrite(BUZZER, LOW);
    digitalWrite(LED_FEEDING, LOW);
    delay(100);
  }
}
