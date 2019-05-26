#include <Buzzer.h>
#include <LiquidCrystal.h>
#include <Keypad.h>

typedef enum {
  IDLING,
  DRINKSEL,
  RATIOSEL,
  SIZESEL,
  CONFIRM,
  PAYMENT,
  SERVING
} STATE;

// pin configuration

struct PumpPins {
  byte one = 30;
  byte two = 31;
  byte three = 32;
};

struct LCDPins {
  byte rs = 33;
  byte en = 34;
  byte d4 = 4;
  byte d5 = 5;
  byte d6 = 6;
  byte d7 = 7;
};

struct KeypadPins {
  byte rows[4] = {29,28,27,26};
  byte cols[4] = {22,23,24,25};
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
const byte pirPin = 3;
const byte meterPin = 2;

// combine all pin configuration
PINs Pins = { pumpPins, keypadPins, lcdPins, nfcPins, meterPin, buttonPin, pirPin };

// global variables
Buzzer buzzer(35);
LiquidCrystal lcd(Pins.lcd.rs, Pins.lcd.en, Pins.lcd.d4, Pins.lcd.d5, Pins.lcd.d6, Pins.lcd.d7);
STATE currentState;

const long durationBeforeIdle = 20000;

char keyPressed;
unsigned long timeToIdle;

char drinkChoices[4] = {'1', '2', '3', '\0'};
char ratioChoices[4] = {'1', '2', '3', '\0'};
char sizeChoices[4] = {'1', '2', '3', '\0'};
char confirmChoices[3] = {'1', '2', '\0'};

char selectedDrink = '0';
char selectedSize = '0';
char selectedRatio = '0';

// meter
const double cap = 50.0f;  // l/min
const double kf = 5.5f;    // Hz per l/min

// keypad
const byte numRows= 4; //number of rows on the keypad
const byte numCols= 4; //number of columns on the keypad

//keymap defines the key pressed according to the row and columns just as appears on the keypad
char keymap[numRows][numCols]=
{
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
Keypad myKeypad= Keypad(makeKeymap(keymap), Pins.keypadP.rows, Pins.keypadP.cols, numRows, numCols);

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

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  
  changeCurrentState(DRINKSEL);
  
  // setup PIR interrupt
  pinMode(Pins.pir, INPUT);
  pinMode(Pins.button, INPUT_PULLUP);
  pinMode(Pins.meter, INPUT);
  attachInterrupt(digitalPinToInterrupt(Pins.pir), motionDetected, RISING);
  attachInterrupt(digitalPinToInterrupt(Pins.button), buttonDetected, RISING);

  noInterrupts(); // disable all interrupts

  interrupts(); // enable all interrupts
  
  pinMode(13, OUTPUT);
}

void loop() {

  keyPressed = getKeyPressed();
  
  int i = 0;
  int len = sizeof(notes) / sizeof(int);
  switch (currentState){
    case IDLING:
//      buzzer.begin(10);    
//      while (currentState == IDLING){
//        buzzer.sound(notes[i], noteDuration[i]); 
//        i++;
//        if (i >= len){
//          buzzer.end(2000);
//          i = 0;
//        }
//      }
//      buzzer.end(2000);
      break;
    case DRINKSEL:
      if (keyPressed != NO_KEY){
        if (validateKey(drinkChoices, keyPressed)){
          selectedDrink = keyPressed;
          debugln("Selected DRINK: ");
          debugln(selectedDrink);
          changeCurrentState(RATIOSEL);
        }
        else {
          debugln("Invalid input!", 2);
          debugln(keyPressed, 2);
          lcdPrint(1, String(keyPressed) + " is INVALID!");
        }
      }
      if (isTimeToIdle()){
        changeCurrentState(IDLING);
      }
      break;
    case RATIOSEL:
      if (keyPressed != NO_KEY){
        if (validateKey(ratioChoices, keyPressed)){
          selectedRatio = keyPressed;
          debugln("Selected RATIO: ");
          debugln(selectedRatio);
          changeCurrentState(SIZESEL);
        }
        else {
          debugln("Invalid input!", 2);
          debugln(keyPressed, 2);
          lcdPrint(1, String(keyPressed) + " is INVALID!");
        }
      }
      break;
    case SIZESEL:
      if (keyPressed != NO_KEY){
        if (validateKey(sizeChoices, keyPressed)){
          selectedSize = keyPressed;
          debugln("Selected SIZE: ");
          debugln(selectedSize);
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
      if (keyPressed != NO_KEY){
        if (validateKey(confirmChoices, keyPressed)){
          char confirm = keyPressed;
          debugln("Selected confirm: ");
          debugln(confirm);
          if (confirm == '1'){
            changeCurrentState(SERVING);
          } else if (confirm == '2'){
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
      lcdPrint(0, "Serving...");
      delay(1000);
      changeCurrentState(DRINKSEL);
      break;
  }
}

void setupTimer1(){
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
  if (currentState == newState)
    return false;
  debugln("Change state.");
  lcdClear();
  currentState = newState;
  switch (newState){
    case IDLING:
      debugln("IDLING");
      lcdPrint(0, "Idling...");
      break;
    case DRINKSEL:
      debugln("DRINKSEL");
      lcdPrint(0, "Drink (1,2,3):");
      resetBack();
      break;
    case RATIOSEL:
      debugln("RATIONSEL");
      lcdPrint(0, "Ratio (1,2,3):");
      break;
    case SIZESEL:
      debugln("SIZESEL");
      lcdPrint(0, "Size (1,2,3):");
      break;
    case CONFIRM:
      debugln("CONFIRM");
      lcdPrint(0, "Confirm (1:Y,2:N):");
      break;
    case PAYMENT:
      debugln("PAYMENT");
      break;
    case SERVING:
      debugln("SERVING");
      break;
    default:
      break;
  }
  if (newState == IDLING) {

  }
  else if (newState == DRINKSEL) {
    resetTimeToIdle();
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

void motionDetected(){
  debugln("Motion Detected");
  if (currentState == IDLING){
    changeCurrentState(DRINKSEL);
  }
}

void buttonDetected(){
  debugln("Button Detected");
  changeCurrentState(DRINKSEL);
}

void debugln(String str){
  debugln(str, 0);
}

void debugln(char* str){
  debugln(str, 0);
}

void debugln(String str, byte level){
  if (level == 0){
    str = "[DEBUG]" + str;
  }
  else if (level == 1){
    str = "[INFO]" + str;
  } else if (level == 2){
    str = "[WARN]" + str;
  } else if (level == 3){
    str = "[ERR]" + str;
  }
  Serial.println(str);
}


void debugln(char in){
  debugln(in, 0);
}

void debugln(char in, byte level){
  String str;
  if (level == 0){
    str = "[DEBUG]";
  }
  else if (level == 1){
    str = "[INFO]";
  } else if (level == 2){
    str = "[WARN]";
  } else if (level == 3){
    str = "[ERR]";
  }
  Serial.print(str);
  Serial.println(in);
}

void debugln(byte in){
  Serial.println(in);
}

void debug(char* str){
  Serial.print(str);
}

char getKeyPressed(){
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

void resetBack(){
  selectedDrink = '0';
  selectedRatio = '0';
  selectedSize = '0';
}

bool validateKey(String keys, char keyPressed){
  if (keys.indexOf(keyPressed) > -1)
    return true;
  return false;
}

void lcdClear(){
  lcd.clear();
}

void lcdPrint(int row, String str){
  lcd.setCursor(0, row);
  lcd.print(str);
}
