#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <Trinamic_TMC2130.h>
#include <specialChar.h>

// Note: You also have to connect GND, 5V/VIO and VM.
//       A connection diagram can be found in the schematics.
#define EN_PIN    33 //enable (CFG6)
#define DIR_PIN   27 //direction
#define STEP_PIN  32 //step 

#define CS_PIN   14 //chip select
#define MOSI_PIN 26 //SDI/MOSI (ICSP: 4, Uno: 11, Mega: 51)
#define MISO_PIN 12 //SDO/MISO (ICSP: 1, Uno: 12, Mega: 50)
#define SCK_PIN  25 //CLK/SCK  (ICSP: 3, Uno: 13, Mega: 52)

#define BT_UP    15 // button up
#define BT_DOWN  2  // button down
#define BT_NEXT  13 // button next

#define STOPPED 0
#define CLOCKWISE 1
#define COUNTERCLOCKWISE 2

hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
double stepFrequency = 5925.9;
// In theory, earth rotation speed with 100*100 reducer and 200 step motor at 256 microsteps, is 168.75us per microstep
// (24*60*60*1000*1000)/(100*100*200*256).
int cursorPosition = 4;
int motorDir = COUNTERCLOCKWISE;
bool button_up_pressed = false;
bool button_down_pressed = false;
bool button_next_pressed = false;

Trinamic_TMC2130 myStepper(CS_PIN);

// LiquidCrystal(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);
LiquidCrystal lcd(4, 5, 16, 17, 18, 19);

void setup()
{
  Serial.begin(115200);

  lcd.begin(16,2);               // initialize the lcd 

  lcd.createChar(0, arrowUP);    // load character to the LCD
  lcd.createChar(1, anim00);
  lcd.createChar(2, anim02);
  lcd.createChar(3, anim04);
  lcd.createChar(4, anim06);

  pinMode(BT_UP, INPUT_PULLDOWN);
  pinMode(BT_DOWN, INPUT_PULLDOWN);
  pinMode(BT_NEXT, INPUT_PULLDOWN);

  //set pins
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH); //deactivate driver (LOW active)
  pinMode(DIR_PIN, OUTPUT);
  digitalWrite(DIR_PIN, LOW); //LOW or HIGH
  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  pinMode(MOSI_PIN, OUTPUT);
  digitalWrite(MOSI_PIN, LOW);
  pinMode(MISO_PIN, INPUT);
  digitalWrite(MISO_PIN, HIGH);
  pinMode(SCK_PIN, OUTPUT);
  digitalWrite(SCK_PIN, LOW);
  
  Serial.begin(115200);
  while (!Serial); 
  Serial.println("\nTMC2130 Eq motor\n");

  //init SPI
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));

  //set TMC2130 config
  // stepper
  myStepper.init();
  myStepper.set_mres(256); // ({1,2,4,8,16,32,64,128,256}) number of microsteps
  myStepper.set_IHOLD_IRUN(16,16,5); // ([0-31],[0-31],[0-5]) sets all currents to maximum
  myStepper.set_I_scale_analog(1); // ({0,1}) 0: I_REF internal, 1: sets I_REF to AIN
  myStepper.set_tbl(2); // ([0-3]) set comparator blank time to 16, 24, 36 or 54 clocks, 1 or 2 is recommended
  myStepper.set_toff(8); // ([0-15]) 0: driver disable, 1: use only with TBL>2, 2-15: off time setting during slow decay phase
  myStepper.set_en_pwm_mode(1); // stealthChop, silent mode

  //TMC2130 outputs on (LOW active)
  digitalWrite(EN_PIN, LOW);

  // PWM setup on STEP pin
  ledcSetup(0, stepFrequency, 8);
  ledcAttachPin(STEP_PIN, 0);
  ledcWrite(0, 128);
}

void loop()
{
  static uint32_t last_time=0;
  static int counter = 0;
  uint32_t ms = millis();
  double increment = 1;

  if (digitalRead(BT_UP)) button_up_pressed = true;
  if (digitalRead(BT_DOWN)) button_down_pressed = true;
  if (digitalRead(BT_NEXT)) button_next_pressed = true;

    
  if((ms-last_time) > 200) { //run every .2s
    last_time = ms;

    lcd.setCursor ( 0, 0 );
    lcd.print ( "        " );
    lcd.setCursor ( 0, 0 );
    if (stepFrequency<10000) lcd.setCursor ( 1, 0 );
    if (stepFrequency<1000) lcd.setCursor ( 2, 0 );
    if (stepFrequency<100) lcd.setCursor ( 3, 0 );
    if (stepFrequency<10) lcd.setCursor ( 4, 0 );
    
    lcd.print( stepFrequency, 1 ); 

    lcd.setCursor ( 0, 1 );
    lcd.print ( "             " );
    lcd.setCursor ( cursorPosition, 1 );
    lcd.print ( char(0) );

    lcd.setCursor ( 8, 0 );
    lcd.print ("Hz");

    if (motorDir == COUNTERCLOCKWISE) {
      lcd.setCursor ( 12, 0 );
      lcd.print ( char(1+ 3-(counter%4)) );
      counter++;
    }
    if (motorDir == CLOCKWISE) {
      lcd.setCursor ( 12, 0 );
      lcd.print ( char(1+ (counter%4)) );
      counter++;
    }
    
  }

  if(button_up_pressed || button_down_pressed || button_next_pressed) {

    if (cursorPosition == 0) increment = 10000;
    if (cursorPosition == 1) increment = 1000;
    if (cursorPosition == 2) increment = 100;
    if (cursorPosition == 3) increment = 10;
    if (cursorPosition == 4) increment = 1;

    if (cursorPosition == 6) increment = 0.1;

    if (cursorPosition == 12) increment = 0;

    if ( button_down_pressed && (stepFrequency-increment > 100) && cursorPosition != 12) {
      stepFrequency -= increment;
      ledcSetup(0, stepFrequency, 8);
      ledcAttachPin(STEP_PIN, 0);
      ledcWrite(0, 128);
    }
    if ( button_up_pressed  && (stepFrequency+increment < 99999)  && cursorPosition != 12) {
      stepFrequency += increment;
      ledcSetup(0, stepFrequency, 8);
      ledcAttachPin(STEP_PIN, 0);
      ledcWrite(0, 128);
    }
    if (button_next_pressed) {
      cursorPosition--;
      if ( cursorPosition == 5 ) cursorPosition = 4;
      if ( cursorPosition == 11 ) cursorPosition = 6;
      if ( cursorPosition < 0 ) cursorPosition = 12;
    }
    if (cursorPosition == 12 && (button_up_pressed || button_down_pressed) ) {
      motorDir++;
      if (motorDir > 2) motorDir = 0;

      if (motorDir == STOPPED) {
        digitalWrite(EN_PIN, HIGH);
      }
      if (motorDir == COUNTERCLOCKWISE) {
        digitalWrite(DIR_PIN, LOW);
        digitalWrite(EN_PIN, LOW);
      }
      if (motorDir == CLOCKWISE) {
        digitalWrite(DIR_PIN, HIGH);
        digitalWrite(EN_PIN, LOW);
      }
    }
    
    delay(200);
    button_up_pressed = false;
    button_down_pressed = false;
    button_next_pressed = false;
  }

  
}