#include <Buzzer.h>
#include <LiquidCrystal.h>
#include <Keypad.h>
#include <Wire.h>
#include <math.h>
#include <FlowMeter.h>
#include <LinkedList.h>
#include <EEPROM.h>

struct Tank {
  int add;
  int currentReading;
  int minReading;
  int maxReading;
};

// Calibration config

const long durationBeforeIdle = 20000;

char drinkChoices[5] = {'1', '2', '*', '#', '\0'};
char ratioChoices[4] = {'1', '2', '3', '\0'};
char sizeChoices[4] = {'1', '2', '3', '\0'};
char confirmChoices[3] = {'1', '2', '\0'};

unsigned int drinkSizes[3] = {120, 240, 360};

Tank tank1 = {0x76, 0, 3, 15};
Tank tank2 = {0x70, 0, 3, 12};
Tank tank3 = {0x72, 0, 3, 15};


// pin configuration

struct PumpPins {
  byte one = 30;
  byte two = 31;
  byte three = 32;
};

struct LCDPins {
  byte rs = 53;
  byte en = 52;
  byte d4 = 4;
  byte d5 = 5;
  byte d6 = 6;
  byte d7 = 7;
};

struct KeypadPins {
  byte rows[4] = {29, 28, 27, 26};
  byte cols[4] = {22, 23, 24, 25};
};

struct NFCPins {
  byte ss = 53;
  byte rst = 49;
};

struct PINs {
  PumpPins pump;
  KeypadPins keypadP;
  LCDPins lcd;
  NFCPins nfc;
  byte meter;
  byte button;
  byte pir;
};

PumpPins pumpPins;
KeypadPins keypadPins;
LCDPins lcdPins;
NFCPins nfcPins;
const byte buttonPin = 18;
const byte pirPin = 19;
const byte meterPin = 2;

// combine all pin configuration
PINs Pins = { pumpPins, keypadPins, lcdPins, nfcPins, meterPin, buttonPin, pirPin };

class DrawWaterTask {
  public:
    byte pin;
    long amountToDraw;
    long drewAmount;
};

struct SoldDrink {
  char drink;
  char sizeD;
  char ratio;
};

struct SavedTankInfo {
  int one;
  int two;
  int three;
};

typedef enum {
  IDLING,
  DRINKSEL,
  SIZESEL,
  RATIOSEL,
  CONFIRM,
  PAYMENT,
  SERVING,
  MAINTENANCE
} STATE;

// global variables
Buzzer buzzer(35);
LiquidCrystal lcd(Pins.lcd.rs, Pins.lcd.en, Pins.lcd.d4, Pins.lcd.d5, Pins.lcd.d6, Pins.lcd.d7);
STATE currentState;


char keyPressed;
unsigned long timeToIdle;

char selectedDrink = '0';
char selectedSize = '0';
char selectedRatio = '0';

int adjustedAmount = 5;

LinkedList<DrawWaterTask*> drawWaterTasks = LinkedList<DrawWaterTask*>();

byte buttonState;
byte lastButtonState = LOW;
unsigned long debounceDelay = 50;
unsigned long lastDebounceTime = 0;

// keypad
const byte numRows = 4; //number of rows on the keypad
const byte numCols = 4; //number of columns on the keypad

//keymap defines the key pressed according to the row and columns just as appears on the keypad
char keymap[numRows][numCols] =
{
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
Keypad myKeypad = Keypad(makeKeymap(keymap), Pins.keypadP.rows, Pins.keypadP.cols, numRows, numCols);


FlowSensorProperties meterSensor = {1.0f, 7.5f,
  { 0.01037, 0.00957, 0.00980, 0.00952, 0.01026,
    0.00950, 0.00963, 0.00901, 0.01015, 0.00948
  }
};
FlowMeter meter = FlowMeter(2, meterSensor);

long period = 1000;   // one second (in milliseconds)
long lastMeterTime = 0;


// buzzer sound
int notes[] = {
  NOTE_A3,
  NOTE_A3,
  NOTE_A3,
  NOTE_F3,
  NOTE_C4
};
unsigned int noteDuration[] = {
  500,
  500,
  500,
  375,
  125
};

bool justWakeup = false;
unsigned long lastTime = 0;

// EEPROM
byte addressIndexSoldDrink = 0;
byte addressIndexTankInfo = 1;
byte addressTotalSoldDrink = 2;
byte addressTotalTankInfo = 3;
int startAddressSoldDrink = 200;
int startAddressTankInfo = 400;
unsigned long lastSavedTankInfoTime = 0;
unsigned long tankInfoSaveInterval = 60000;

void setup() {
  //EEPROM.write(addressIndexSoldDrink, 0);
  //EEPROM.write(addressTotalSoldDrink, 0);
  Serial.begin(9600);
  lcd.begin(16, 2);
  Wire.begin();

  changeCurrentState(DRINKSEL);

  pinMode(Pins.pump.one, OUTPUT);
  pinMode(Pins.pump.two, OUTPUT);
  pinMode(Pins.pump.three, OUTPUT);
  // setup PIR interrupt
  pinMode(Pins.pir, INPUT_PULLUP);
  pinMode(Pins.button, INPUT_PULLUP);
  pinMode(Pins.meter, INPUT);
  attachInterrupt(digitalPinToInterrupt(Pins.pir), motionDetected, RISING);
  //attachInterrupt(digitalPinToInterrupt(Pins.button), buttonDetected, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins.meter), meterISR, RISING);

  noInterrupts(); // disable all interrupts

  interrupts(); // enable all interrupts

  pinMode(13, OUTPUT);

//  buzzer.begin(100);
//  buzzer.sound(0, 80);
//  buzzer.sound(NOTE_F7, 80);
//  buzzer.sound(NOTE_G7, 80);
//  buzzer.sound(0, 80);
//  buzzer.sound(NOTE_E7, 80);
//  buzzer.sound(0, 80);
//  buzzer.sound(NOTE_C7, 80);
//  buzzer.sound(NOTE_D7, 80);
//  buzzer.sound(NOTE_B6, 80);
//  buzzer.sound(0, 160);
//  buzzer.end(2000);
}

void loop() {

  if (justWakeup) {
    buzzer.begin(100);
    buzzer.sound(0, 80);
    buzzer.sound(NOTE_B6, 80);
    buzzer.sound(0, 80);
    buzzer.sound(NOTE_AS6, 80);
    buzzer.sound(NOTE_A6, 80);
    buzzer.sound(0, 80);
    buzzer.sound(NOTE_G6, 100);
    buzzer.sound(NOTE_E7, 100);
    buzzer.sound(NOTE_G7, 100);
    buzzer.sound(NOTE_A7, 80);
    buzzer.sound(0, 80);
    buzzer.sound(NOTE_F7, 80);
    buzzer.sound(NOTE_G7, 80);
    buzzer.end(2000);
    justWakeup = false;
  }

  //Serial.print(digitalRead(Pins.pir));
  keyPressed = getKeyPressed();
  byte buttonReading = digitalRead(Pins.button);
  if (buttonReading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    //if (buttonReading != buttonState) {
    buttonState = buttonReading;
    if (buttonState == LOW) {
      buttonDetected();
    }
    //}

  }
  if ((millis() - lastSavedTankInfoTime) > tankInfoSaveInterval) {
    for (int i = 0; i < 10; i++) {
      tank1.currentReading = readTank(tank1);
      tank2.currentReading = readTank(tank2);
      tank3.currentReading = readTank(tank3);
    }
    debugln(String(tank1.currentReading));
    debugln(String(tank2.currentReading));
    debugln(String(tank3.currentReading));
    SavedTankInfo info  = { getTankStatus(tank1), getTankStatus(tank2), getTankStatus(tank3)};
    saveTankInfo(info);
    debugln("Save Tank Info...");
    lastSavedTankInfoTime = millis();
  }

  int i = 0;
  int len = sizeof(notes) / sizeof(int);
  switch (currentState) {
    case IDLING:
      if (keyPressed != NO_KEY) {
        changeCurrentState(DRINKSEL);
      }
      break;
    case DRINKSEL:
      if (keyPressed != NO_KEY) {
        if (validateKey(drinkChoices, keyPressed)) {
          if (keyPressed == '#') {
            changeCurrentState(MAINTENANCE);
            break;
          } else if (keyPressed == '*') {
            printSavedSoldDrink();
            printSavedTankInfo();
            break;
          }
          selectedDrink = keyPressed;
          debugln("Selected DRINK: ");
          debugln(selectedDrink);
          changeCurrentState(SIZESEL);
        }
        else {
          debugln("Invalid input!", 2);
          debugln(keyPressed, 2);
          lcdPrint(1, String(keyPressed) + " is INVALID!");
        }
      }
      if (isTimeToIdle()) {
        changeCurrentState(IDLING);
      }
      break;
    case SIZESEL:
      if (keyPressed != NO_KEY) {
        if (validateKey(sizeChoices, keyPressed)) {
          selectedSize = keyPressed;
          debugln("Selected SIZE: ");
          debugln(selectedSize);
          changeCurrentState(RATIOSEL);
        }
        else {
          debugln("Invalid input!", 2);
          debugln(keyPressed, 2);
          lcdPrint(1, String(keyPressed) + " is INVALID!");
        }
      }
      break;
    case RATIOSEL:
      if (keyPressed != NO_KEY) {
        if (validateKey(ratioChoices, keyPressed)) {
          selectedRatio = keyPressed;
          debugln("Selected RATIO: ");
          debugln(selectedRatio);
          changeCurrentState(CONFIRM);
        }
        else {
          debugln("Invalid input!", 2);
          debugln(keyPressed, 2);
          lcdPrint(1, String(keyPressed) + " is INVALID!");
        }
      }
      break;
    case CONFIRM:
      if (keyPressed != NO_KEY) {
        if (validateKey(confirmChoices, keyPressed)) {
          char confirm = keyPressed;
          debugln("Selected confirm: ");
          debugln(confirm);
          if (confirm == '1') {
            createDrawWaterTasks(selectedDrink, selectedSize, selectedRatio);
            SoldDrink drink = {selectedDrink, selectedSize, selectedRatio};
            saveSoldDrink(drink);
            changeCurrentState(SERVING);
          } else if (confirm == '2') {
            changeCurrentState(DRINKSEL);
          } else {
            debugln("Invalid input!", 2);
            debugln(keyPressed, 2);
            lcdPrint(1, String(keyPressed) + " is INVALID!");
          }
        }
      }
      break;
    case PAYMENT:
      break;
    case SERVING:
      if (drawWaterTasks.size() > 0) {
        DrawWaterTask* task = drawWaterTasks.get(0);
        if ((task->drewAmount + adjustedAmount) > task->amountToDraw) {
          digitalWrite(task->pin, LOW);
          debugln("Finish a draw water task.");
          debugln("Drew amount: ");
          debugln(String(task->drewAmount));
          drawWaterTasks.remove(0);

        } else {
          digitalWrite(task->pin, HIGH);
          long currentTime = millis();
          long duration = currentTime - lastMeterTime;
          // wait between display updates
          if (duration >= period) {
            // process the counted ticks
            meter.tick(duration);
            task->drewAmount += round(meter.getTotalVolume() * 1000);
            // prepare for next cycle
            lastMeterTime = currentTime;
          }
        }
      } else {
        if (selectedDrink == '1') {
          int time = 500;
          buzzer.begin(10);
          buzzer.sound(NOTE_G3, time / 2);
          buzzer.sound(NOTE_E4, time / 2);
          buzzer.sound(NOTE_D4, time / 2);
          buzzer.sound(NOTE_C4, time / 2);
          buzzer.end(2000);
        } else if (selectedDrink == '2') {
          buzzer.begin(10);
          buzzer.sound(NOTE_E7, 80);
          buzzer.sound(NOTE_E7, 80);
          buzzer.sound(0, 80);
          buzzer.sound(NOTE_E7, 80);
          buzzer.sound(0, 80);
          buzzer.sound(NOTE_C7, 80);
          buzzer.sound(NOTE_E7, 80);
          buzzer.sound(0, 80);
          buzzer.sound(NOTE_G7, 80);
          buzzer.sound(0, 240);
          buzzer.sound(NOTE_G6, 80);
          buzzer.sound(0, 240);
          buzzer.end(2000);
        }
        changeCurrentState(DRINKSEL);
      }
      //      if (amountToDraw == 0) {
      //        currentDrawPin = Pins.pump.two;
      //        amountToDraw = 200 * 100;
      //        currentDrewAmount = 0;
      //      }
      //      if (currentDrewAmount < amountToDraw) {
      //        digitalWrite(currentDrawPin, HIGH);
      //        currentDrewAmount += 1;
      //      } else {
      //        digitalWrite(currentDrawPin, LOW);
      //        amountToDraw = 0;
      //        currentDrewAmount = 0;
      //        // completed
      //        changeCurrentState(DRINKSEL);
      //      }

      break;
    case MAINTENANCE:
      if (keyPressed != NO_KEY) {
        changeCurrentState(DRINKSEL);
      }
      if ((millis() - lastTime) < 1000) {
        break;
      }
      lastTime = millis();
      lcdClear();
      lcdPrint(0, "Tank Info:");
      tank1.currentReading = readTank(tank1);
      debugln("Tank 1:");
      debugln(String(tank1.currentReading));
      debugln("Level:");
      debugln(String(getTankStatus(tank1)));
      tank2.currentReading = readTank(tank2);
      debugln("Tank 2:");
      debugln(String(tank2.currentReading));
      debugln("Level:");
      debugln(String(getTankStatus(tank2)));
      tank3.currentReading = readTank(tank3);
      debugln("Tank 3:");
      debugln(String(tank3.currentReading));
      debugln("Level:");
      debugln(String(getTankStatus(tank3)));

      String str = "" + String(getTankStatus(tank1)) + " " + String(getTankStatus(tank2)) + " " + String(getTankStatus(tank3));
      lcdPrint(1, str);


      break;
  }
}

void setupTimer1() {
  TCCR1A = 0; // clear content of the timer control register A
  TCCR1B = 0; // clear content of the timer control register B
  TCNT1  = 0; // reset the timer/counter value

  //OCR1A = 3840; // set the compare match register value (16MHz/1024/1Hz)
  OCR1A = 7811;
  TCCR1B |= (1 << WGM12); // set timer to CTC mode
  TCCR1B |= (1 << CS12) | (1 << CS10); // use 1024 prescaler

  TIMSK1 |= (1 << OCIE1A); // enable timer compare interrupt
}

bool changeCurrentState(STATE newState) {
  resetTimeToIdle();
  debugln("Change state.");
  if (currentState == newState) {
    debugln("Same state.");
    return false;
  }
  lcdClear();
  currentState = newState;

  if (newState == IDLING) {
    debugln("IDLING");
    lcdPrint(0, "Idling...");
  }
  else if (newState == DRINKSEL) {
    debugln("DRINKSEL");
    lcdPrint(0, "Drink (1,2):");
    resetBack();
  }
  else if (newState == RATIOSEL) {
    debugln("RATIONSEL");
    lcdPrint(0, "Ratio (1,2,3):");
  }
  else if (newState == SIZESEL) {
    debugln("SIZESEL");
    lcdPrint(0, "Size (1,2,3):");
  }
  else if (newState == CONFIRM) {
    debugln("CONFIRM");
    lcdPrint(0, "Confirm (1:Y,2:N):");
    String str = drinkToString(selectedDrink) + "(" + sizeToString(selectedSize) + "," + ratioToString(selectedRatio) + ")";
    lcdPrint(1, str);
  }
  else if (newState == PAYMENT) {
    debugln("PAYMENT");
  }
  else if (newState == SERVING) {
    debugln("SERVING");
    lcdPrint(0, "Serving...");
    String str = drinkToString(selectedDrink) + "(" + sizeToString(selectedSize) + "," + ratioToString(selectedRatio) + ")";
    lcdPrint(1, str);
  }
  else if (newState == MAINTENANCE) {
    debugln("MAINTENANCE");
    lcdPrint(0, "Tank Info:");
  }
  else {
    debugln("UNDEFINED State", 3);
  }
}

void resetTimeToIdle() {
  timeToIdle = millis() + durationBeforeIdle;
}

bool isTimeToIdle() {
  if (millis() > timeToIdle)
    return true;
  return false;
}

void motionDetected() {
  if (currentState == SERVING)
    return;
  debugln("Motion Detected");
  if (currentState == IDLING) {
    changeCurrentState(DRINKSEL);
    justWakeup = true;
  }
}

void buttonDetected() {
  debugln("Button Detected");
  changeCurrentState(DRINKSEL);
}

void debugln(String str) {
  debugln(str, 0);
}

void debugln(char* str) {
  debugln(str, 0);
}

void debugln(String str, byte level) {
  if (level == 0) {
    str = "[DEBUG]" + str;
  }
  else if (level == 1) {
    str = "[INFO]" + str;
  } else if (level == 2) {
    str = "[WARN]" + str;
  } else if (level == 3) {
    str = "[ERR]" + str;
  }
  Serial.println(str);
}


void debugln(char in) {
  debugln(in, 0);
}

void debugln(char in, byte level) {
  String str;
  if (level == 0) {
    str = "[DEBUG]";
  }
  else if (level == 1) {
    str = "[INFO]";
  } else if (level == 2) {
    str = "[WARN]";
  } else if (level == 3) {
    str = "[ERR]";
  }
  Serial.print(str);
  Serial.println(in);
}

void debugln(byte in) {
  Serial.println(in);
}

void debug(char* str) {
  Serial.print(str);
}

char getKeyPressed() {
  char keypressed = myKeypad.getKey();
  if (keypressed != NO_KEY)
  {
    debugln("Key Pressed: ");
    debugln(keypressed);
  }
  return keypressed;
}

//byte charToByte(char in){
//  byte out = in - '0';
//  return out;
//}

void resetBack() {
  selectedDrink = '0';
  selectedRatio = '0';
  selectedSize = '0';
  drawWaterTasks.clear();
  digitalWrite(Pins.pump.one, LOW);
  digitalWrite(Pins.pump.two, LOW);
  digitalWrite(Pins.pump.three, LOW);
}

bool validateKey(String keys, char keyPressed) {
  //debugln(keys, 3);
  if (keys.indexOf(keyPressed) > -1)
    return true;
  return false;
}

void lcdClear() {
  lcd.clear();
}

void lcdPrint(int row, String str) {
  lcd.setCursor(0, row);
  lcd.print(str);
}

int readTank(Tank tank) {
  int reading = 0;
  // step 1: instruct sensor to read echoes
  Wire.beginTransmission(tank.add); // transmit to device #112 (0x70)
  // the address specified in the datasheet is 224 (0xE0)
  // but i2c adressing uses the high 7 bits so it's 112
  Wire.write(byte(0x00));      // sets register pointer to the command register (0x00)
  Wire.write(byte(0x51));      // command sensor to measure in "inches" (0x50)
  // use 0x51 for centimeters
  // use 0x52 for ping microseconds
  Wire.endTransmission();      // stop transmitting

  // step 2: wait for readings to happen
  delay(70);                   // datasheet suggests at least 65 milliseconds

  // step 3: instruct sensor to return a particular echo reading
  Wire.beginTransmission(tank.add); // transmit to device #112
  Wire.write(byte(0x02));      // sets register pointer to echo #1 register (0x02)
  Wire.endTransmission();      // stop transmitting

  // step 4: request reading from sensor
  Wire.requestFrom(tank.add, 2);    // request 2 bytes from slave device #112

  // step 5: receive reading from sensor
  if (2 <= Wire.available()) { // if two bytes were received
    reading = Wire.read();  // receive high byte (overwrites previous reading)
    reading = reading << 8;    // shift high byte to be high 8 bits
    reading |= Wire.read(); // receive low byte as lower 8 bits
  }
  return reading;
}

int getTankStatus(Tank tank) {
  float up = (float)(tank.currentReading - tank.minReading);
  if (up < 0)
    up = 0.0f;
  float down = (float)(tank.maxReading - tank.minReading);
  return round((1 - up / down) * 100);
}

void createDrawWaterTasks(char selectedDrink, char selectedSize, char selectedRatio) {
  debugln("Create draw water tasks.");
  DrawWaterTask *task1 = new DrawWaterTask();
  DrawWaterTask *task2 = new DrawWaterTask();
  if (selectedDrink == '1') {
    task1->pin = Pins.pump.one;
    task2->pin = Pins.pump.three;
  } else if (selectedDrink == '2') {
    task1->pin = Pins.pump.two;
    task2->pin = Pins.pump.three;
  } else {
    debugln("Invalid selectedDrink to create tasks", 3);
    debugln(String(selectedDrink), 3);
  }

  float left = 0.0f;
  float right = 0.0f;
  switch (selectedRatio) {
    case '1':
      left = 1.0f;
      break;
    case '2':
      left = 0.7f;
      right = 1 - left;
      break;
    case '3':
      left = 0.5f;
      right = 1 - left;
      break;
    default:
      debugln("Invalid selectedRatio to create tasks", 3);
      debugln(String(selectedRatio), 3);
      break;
  }
  int ind = 0;
  switch (selectedSize) {
    case '1':
      ind = 0;
      break;
    case '2':
      ind = 1;
      break;
    case '3':
      ind = 2;
      break;
    default:
      debugln("Invalid selectedSize to create tasks", 3);
      debugln(String(selectedSize), 3);
      break;
  }
  task1->amountToDraw = round((float)drinkSizes[ind] * left);
  task2->amountToDraw = round((float)drinkSizes[ind] * right);
  drawWaterTasks.add(task1);
  drawWaterTasks.add(task2);
  //return lst;
}

void meterISR() {
  meter.count();
}

void saveSoldDrink(SoldDrink drink) {
  byte ind = EEPROM.read(addressIndexSoldDrink);
  byte total = EEPROM.read(addressTotalSoldDrink);
  ind = ind % 10;
  total = total % 10;
  EEPROM.put(startAddressSoldDrink + sizeof(SoldDrink) * ind, drink);
  ind += 1;
  ind = ind % 10;
  total += 1;
  if (total > 10)
    total = 10;
  EEPROM.write(addressIndexSoldDrink, ind);
  EEPROM.write(addressTotalSoldDrink, total);
}

void printSavedSoldDrink() {
  byte total = EEPROM.read(addressTotalSoldDrink);
  if (total > 10)
    total = 10;
  SoldDrink drink;
  Serial.println("Reading from memory...");
  Serial.println("Saved Sold Drinks:");
  for (byte i = 0; i < total; i++) {
    EEPROM.get(startAddressSoldDrink + sizeof(SoldDrink)  * i, drink);
    String str = "Drink: " + drinkToString(drink.drink) + ", Size: " + sizeToString(drink.sizeD) + ", Ratio: " + ratioToString(drink.ratio);
    Serial.println(str);
  }
}

void saveTankInfo(SavedTankInfo info) {
  byte ind = EEPROM.read(addressIndexTankInfo);
  byte total = EEPROM.read(addressTotalTankInfo);
  ind = ind % 10;
  total = total % 10;
  EEPROM.put(startAddressTankInfo + sizeof(info) * ind, info);
  ind += 1;
  ind = ind % 10;
  total += 1;
  if (total > 10)
    total = 10;
  EEPROM.write(addressIndexTankInfo, ind);
  EEPROM.write(addressTotalTankInfo, total);
}

void printSavedTankInfo() {
  byte total = EEPROM.read(addressTotalTankInfo);
  if (total > 10)
    total = 10;
  SavedTankInfo info;
  Serial.println("Reading from memory...");
  Serial.println("Saved Tank Info:");
  for (byte i = 0; i < total; i++) {
    EEPROM.get(startAddressTankInfo + sizeof(SavedTankInfo)  * i, info);
    String str = "1: " + String(info.one) + ", 2: " + String(info.three) + ", 3: " + String(info.three);
    Serial.println(str);
  }
}

String drinkToString(char drink) {
  if (drink == '1')
    return "Coffee";
  else if (drink == '2')
    return "Tea";
  else
    debugln("No available drink in system", 3);
}

String sizeToString(char sizeD) {
  if (sizeD == '1')
    return "S";
  else if (sizeD == '2')
    return "M";
  else if (sizeD == '3')
    return "L";
  else
    debugln("No available size in system", 3);
}

String ratioToString(char ratio) {
  if (ratio == '1')
    return "1:0";
  else if (ratio == '2')
    return "7:3";
  else if (ratio == '3')
    return "1:1";
  else
    debugln("No available ratio in system", 3);
}
