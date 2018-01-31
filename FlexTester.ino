#include <SPI.h>
#include <SD.h>

// Include the Bounce2 library found here :
// https://github.com/thomasfredericks/Bounce2
#include <Bounce2.h>

bool TESTMODE=false; // if in test mode, additional info is output to serial monitor and there's a delay at the end of each loop

int const LED_PIN = 13; //onboard indicator LED
int const DEBOUNCE_TIME = 5; // time needed for debouncing switches, in ms
int const MOTOR = 43; //pin to control the motor relay
int const MECHANICAL = 42; // pin to control relay for mechanical counter
int const SD_CS = 47; // pin for SD select
int const LED_RED = 10;
int const LED_BLUE = 12;
int const LED_GREEN = 11;
int const BUTTON_START = 48;
unsigned long const PAUSE_COUNT = 10000; //Pause the test at this count to re-tighten samples
int const CHECK_BREAKS = 10000; // milliseconds between checking for broken samples
int const DISP_FACTOR = 100; // factor to divide counts by when displaying them. Use 1 for testing. Use 100 for production to keep counts low.

byte Pins[] = {8, 7, 6, 5, 4, 3, 2, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30}; //digital pin numbers for the switches, in order 1-24)
int const NUMBUTTONS = sizeof(Pins); // stores the number of switches being used
unsigned long count[NUMBUTTONS]; //array to store the count of each button
bool broken[NUMBUTTONS]; //array to store if the specimen is broken
unsigned long maxCount = 0; //max reading of the buttons
unsigned long previousCount = 0; //previous max count
unsigned long missedCount = 0;
int brokenCount = 0; // number of broken samples
int prevbrokenCount = 0; // previous number of broken samples
bool MechIncremented = false; // track if the mechanical counter has been incremented
bool SD_GOOD = 1;

bool PAUSED = 0;
bool CLICKED = 0;

Bounce *button; //sets up debouncers for each button
Bounce ButtonStart=Bounce();

int i; //junk variable for counting

unsigned long startTime = 0; //record the start of the test
unsigned long runTime = 0; //actual flexing time
unsigned long elapsedTime = 0; //total run time
unsigned long previousTime = 0; //time since last increase in count
unsigned long restartTime = 0; //record the timer when testing continues
unsigned long brokenTime = 0; //timer for checking for broken samples


void setup() {

  //setup serial connection
  Serial.begin(57600); //serial connection to computer for debugging
  Serial.print("Button checker with ");
  Serial.print(NUMBUTTONS, DEC);
  Serial.println(" buttons");
  Serial.println(" ");

  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("failed, will try again in 1 second");
    delay(1000);
    if (!SD.begin(SD_CS)) {
      Serial.println("Failed a second time, giving up");
      SD_GOOD = 0;
    }
  }

  button = (Bounce *) malloc(sizeof(Bounce) * NUMBUTTONS); //allocate memory for bounce array

  //first, we need to set up the buttons
  for (i = 0; i < NUMBUTTONS; i++) {
    button[i] = Bounce();
    // Setup the buttons with an internal pull-up :
    pinMode(Pins[i], INPUT_PULLUP);
    // After setting up the button, setup the Bounce instance :
    button[i].attach(Pins[i]);
    button[i].interval(DEBOUNCE_TIME);
    broken[i] = LOW; // no samples are broken yet
    count[i] = 0; //no flexes have happened yet
  }

  //Setup the LED :
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  pinMode(BUTTON_START, INPUT_PULLUP);
  ButtonStart.attach(BUTTON_START);
  ButtonStart.interval(DEBOUNCE_TIME);

  //setup the relay pins
  pinMode(MOTOR, OUTPUT);
  digitalWrite(MOTOR, LOW);
  pinMode(MECHANICAL, OUTPUT);
  digitalWrite(MECHANICAL, LOW);

  digitalWrite(LED_PIN, HIGH); //turn on LED because we're starting
  setColor(255, 0, 0); // LED on as red since we're not ready to start
  Serial.print("Button status: ");
  Serial.println(digitalRead(BUTTON_START));

  PAUSED = 1;

  startTime = millis(); //record the current timer count as the start of the test
  previousTime = millis(); //record the current timer as the last count time
  brokenTime = millis();

  setColor(255, 255, 0); // LED on as yellow since we're ready but not running the test
}

void loop() {

  // check if button is pressed
  checkButton();

  if (maxCount == PAUSE_COUNT && (millis() - restartTime) > 5000) { //if we've reached the pause point, pause the test - "millis() - restartTime) > 5000" is to ensure that we don't pause again on the next loop when we haven't had time for samples to flex again
    digitalWrite(MOTOR, LOW); //stop the motor
    PAUSED = 1;
    runTime = runTime + (millis() - startTime); //add the runtime to the total

    while (PAUSED) {
      checkButton();
      delay(500);
    }
    digitalWrite(MOTOR, HIGH); //restart the motor and go on
    restartTime = millis(); //record the time that testing continued
    previousTime = millis();
  }

  if ((millis() - previousTime) > 10000 && !PAUSED) { //if no counts have increased in the last 10 seconds...
    digitalWrite(MOTOR, LOW); //stop the motor
    runTime = runTime + (millis() - restartTime); //add the elapsed time to the run total
    // update the screen with message / color
    for (i = 0; i < NUMBUTTONS; i++) { // mark all samples as broken so all the numbers get updated
      broken[i] = HIGH;
    }
    LogToSD();
    while (HIGH) {
      // flash LED so user knows testing is done
      setColor(80,0,80);
      delay(500);
      setColor(0,255,255);
      delay(500);
      setColor(0,0,255);
      delay(500);
    }
  }

  if ((millis() - brokenTime) > CHECK_BREAKS && !PAUSED) { // CHECK_BREAKS time has elapsed, so check for samples that have broken
    brokenCount = 0;
    for (i = 0; i < NUMBUTTONS; i++) {
      if ((maxCount - count[i]) > (CHECK_BREAKS / 1000 / 1.6 * 2)) { // if the difference between the max count and the sample is greater than seconds between checks divided by the 1.6 Hz that the machines runs at times 2 (2x for a pad), then it's broken
        broken[i] = HIGH;
        brokenCount += 1;
      }
    }
    brokenTime = millis(); //reset the timer
  }

  if ((maxCount % DISP_FACTOR) == 0 && !PAUSED && maxCount && !MechIncremented) { // each time we've done DISP_COUNTER more stretches, increment the display on the mechanical counter and log to the SD card
    digitalWrite(MECHANICAL, HIGH);
    delay(20); // the mechanical counter needs a 20ms pulse via the relay to increment. I tried 10ms and it didn't always increment.
    digitalWrite(MECHANICAL, LOW);
    LogToSD();
    MechIncremented = true;
  }

  if (maxCount % DISP_FACTOR != 0) {
    MechIncremented = false;
  }

  // Loop through switches
  if ((millis() - previousTime) > 625 && !PAUSED) { // it's been long enough that we missed a click, so increment the missed count counter
    missedCount++;
  }

  for (i = 0; i < NUMBUTTONS; i++) {
    button[i].update(); //update reading from switch
    if (button[i].fell()) { //check if switch was activated
      count[i]++; //increase the count if it was
      CLICKED = 1;
      previousTime = millis(); //record timer
    }
    if (count[i] > maxCount) { //keep track of the maximum value of any of the counts, since that's the current total cycles
      maxCount = count[i];
    }
  }

  if (CLICKED) {
    Serial.print(millis() - previousTime); //output the elapsed time
    Serial.print("\t");
    for (i = 0; i < NUMBUTTONS; i++) {
      Serial.print(count[i]); //output reading over serial connection
      Serial.print(","); //comma between readings
    }
    Serial.println(missedCount); //missed reading counter and new line
    CLICKED = 0;
  }

  //elapsedTime = millis() - startTime; //total time since the test began
}

String padded(long num, int len) { // pad a number with leading spaces. Syntax is padded(number-to-pad, total-spaces). ie padded(40,3) returns " 40"
  String buff = String(num);
  int count = buff.length();
  for (count; count < len; count++) {
    buff = " " + buff;
  }
  return buff;
}

void LogToSD() {
  if (SD_GOOD) {
    String dataString = "";
    File dataFile = SD.open("flexlog.txt", FILE_WRITE);
    if (dataFile) {
      dataString += (millis() - previousTime); //output the elapsed time
      dataString += "\t";
      for (i = 0; i < NUMBUTTONS; i++) {
        dataString += count[i];
        dataString += ","; //comma between readings
      }
      dataFile.println(dataString);
      dataFile.close();
    }
  Serial.println("Logged to SD");
  }
}

void setColor(int red, int green, int blue)
{
#ifdef COMMON_ANODE
  red = 255 - red;
  green = 255 - green;
  blue = 255 - blue;
#endif
  analogWrite(LED_RED, red);
  analogWrite(LED_GREEN, green);
  analogWrite(LED_BLUE, blue);
}

void checkButton()
{
  ButtonStart.update();
  if (ButtonStart.fell()) { // button is pressed
    Serial.println("Button pressed.");
    if (PAUSED) { // system is currently paused, so we need to start it
      PAUSED = 0;
      digitalWrite(MOTOR, HIGH); // start the motor
      restartTime = millis();
      previousTime = millis();
      setColor(0, 255, 0);
    }
    else { // system is running, so we need to pause it
      PAUSED = 1;
      digitalWrite(MOTOR, LOW); // stop the motor
      runTime = runTime + (millis() - startTime); //add the runtime to the total
      setColor(255,255,0);
    }
  }
}

