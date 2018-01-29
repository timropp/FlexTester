  // Include Adafruit libraries for TFT screen
    #include <Adafruit_HX8357.h>
    #include <Adafruit_GFX.h>
    #include <SPI.h>
    #include <SD.h>
    #include <gfxfont.h>
    #include <TouchScreen.h>
  
  // Include the Bounce2 library found here :
  // https://github.com/thomasfredericks/Bounce2
    #include <Bounce2.h>
  
  int const LED_PIN = 13; //onboard indicator LED
  int const DEBOUNCE_TIME = 5; // time needed for debouncing switches, in ms
  int const MOTOR = 43; //pin to control the motor relay
  int const MECHANICAL = 42; // pin to control relay for mechanical counter
  unsigned long const PAUSE_COUNT = 10000; //Pause the test at this count to re-tighten samples
  int const CHECK_BREAKS = 10000; // milliseconds between checking for broken samples
  int const DISP_FACTOR = 100; // factor to divide counts by when displaying them. Use 1 for testing. Use 100 for production to keep counts low.
  
  byte Pins[] = {8,7,6,5,4,3,2,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30}; //digital pin numbers for the switches, in order 1-24)
  int const NUMBUTTONS = sizeof(Pins); // stores the number of switches being used
  unsigned long count[NUMBUTTONS]; //array to store the count of each button
  bool broken[NUMBUTTONS]; //array to store if the specimen is broken
  unsigned long maxCount = 0; //max reading of the buttons
  unsigned long previousCount = 0; //previous max count
  unsigned long missedCount = 0;
  int brokenCount = 0; // number of broken samples
  int prevbrokenCount=0; // previous number of broken samples
  bool MechIncremented=false; // track if the mechanical counter has been incremented
  
  bool PAUSED = 0;
  bool SD_GOOD = 1;
  bool CLICKED = 0;
  
  Bounce *button; //sets up debouncers for each button
  
  int i; //junk variable for counting
  
  unsigned long startTime = 0; //record the start of the test
  unsigned long runTime = 0; //actual flexing time
  unsigned long elapsedTime = 0; //total run time
  unsigned long previousTime = 0; //time since last increase in count
  unsigned long restartTime = 0; //record the timer when testing continues
  unsigned long brokenTime = 0; //timer for checking for broken samples
  
  // settings for the touchscreen
  int X = 0;
  int Y = 0;
  #define YP A14  // must be an analog pin, use "An" notation!
  #define XM A15  // must be an analog pin, use "An" notation!
  #define YM 45   // can be a digital pin
  #define XP 44   // can be a digital pin
  // For better pressure precision, we need to know the resistance
  // between X+ and X- Use any multimeter to read it
  // For the one we're using, its 284 ohms across the X plate
  TouchScreen ts = TouchScreen(XP, YP, XM, YM, 284);
  
  // settings for TFT screen
  #define TFT_CS 49
  #define TFT_DC 48
  //also connect the hardware SPI pins
  // Use hardware SPI (on Mega, MISO 50, MOSI 51, SCK 52, SS 53 not connected) and the above for CS/DC
  Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC);
  #define SD_CS 47 // pin for accessing the SD card
  #define TFT_lite 46 // PWM pin for controlling the backlight on the TFT screen
  
  // This is calibration data for the raw touch data to the screen coordinates
  #define TS_MINX 110
  #define TS_MINY 80
  #define TS_MAXX 900
  #define TS_MAXY 940

void setup() {

  pinMode(53,OUTPUT);
  digitalWrite(53,HIGH);
  
  //setup serial connection
  Serial.begin(57600); //serial connection to computer for debugging
  Serial.print("Button checker with ");
  Serial.print(NUMBUTTONS, DEC);
  Serial.println(" buttons");
  Serial.println(" ");

  // initialize the screen
  tft.begin(HX8357D);
  tft.fillScreen(HX8357_BLACK); // blank screen
  pinMode(TFT_lite, OUTPUT);
  analogWrite(TFT_lite,1023); //set the screen to full brightness. Use smaller values than 1023 to have dimmer screen

  Serial.print("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("failed, will try again in 1 second");
    delay(1000);
    if (!SD.begin(SD_CS)) {
      Serial.println("Failed a second time, giving up");
      SD_GOOD=0;
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
    broken[i]=LOW; // no samples are broken yet
    count[i]=0; //no flexes have happened yet
  }

  //Setup the LED :
  pinMode(LED_PIN, OUTPUT);

  //setup the relay pins
  pinMode(MOTOR, OUTPUT);
  digitalWrite(MOTOR, LOW);
  pinMode(MECHANICAL, OUTPUT);
  digitalWrite(MECHANICAL, LOW);

  digitalWrite(LED_PIN, HIGH); //turn on LED because we're starting

  if (SD_GOOD) {
    bmpDraw("FLEXO3.BMP", 0, 0); // load this image from the SD card as a loading screen
    // delay 5 seconds to enjoy the loading screen
    delay(5000);
    tft.setRotation(3);
  }
  else {
    tft.setRotation(3);
    tft.setTextColor(0xFFFF,0x0000);
    tft.setTextSize(3); 
    tft.setCursor(30,30);
    tft.println("Loading...");
  }
  ScreenStart();
  PAUSED=1;
  
  startTime = millis(); //record the current timer count as the start of the test
  previousTime = millis(); //record the current timer as the last count time
  brokenTime = millis();
}

void loop() {

  // get the touch screen data
  TSPoint p=ts.getPoint();
  if (p.z>10) { //the screen is being touched
    // p.x is the x location, p.y is the y location. Do something with that.
    X = map(p.y, TS_MAXY, TS_MINY, 0, tft.width()); // remap value to position on the screen
    Y = map(p.x, TS_MINX, TS_MAXX, 0, tft.height()); // remap value to position on the screen
    if ((Y>168) && (Y<318)) {
      if (X < 100) { //press is in the Go button region)
        PAUSED = 0;
        digitalWrite(MOTOR,HIGH); // start the motor
        DispPauseButton();
        EraseGoButton();
        restartTime=millis();
        previousTime=millis();
      }
      if ((X<206) && (X>100)) { // press is in the pause button region
        PAUSED = 1;
        digitalWrite(MOTOR,LOW); // stop the motor
        DispGoButton();
        ErasePauseButton();
        if (prevbrokenCount!=brokenCount) { // update counts on any broken samples
          //update screen with broken info
          DispBrokenCounters();
          prevbrokenCount=brokenCount;
        }
        runTime = runTime + (millis() - startTime); //add the runtime to the total
      }
    }
  }

  if (maxCount == PAUSE_COUNT && (millis() - restartTime) > 5000) { //if we've reached the pause point, pause the test - "millis() - restartTime) > 5000" is to ensure that we don't pause again on the next loop when we haven't had time for samples to flex again
    digitalWrite(MOTOR, LOW); //stop the motor
    PAUSED = 1;
    runTime = runTime + (millis() - startTime); //add the runtime to the total
    tft.fillScreen(0x0000);
    tft.setCursor(30,30);
    tft.setTextColor(0xFFFF);
    tft.println("Adjust samples");
    DispGoButton();

    while (PAUSED) {
      TSPoint p=ts.getPoint();
      if (p.z>10) { //the screen is being touched
        // p.x is the x location, p.y is the y location. Do something with that.
        X = map(p.y, TS_MAXY, TS_MINY, 0, tft.width()); // remap value to position on the screen
        Y = map(p.x, TS_MINX, TS_MAXX, 0, tft.height()); // remap value to position on the screen
        if ((Y>168) && (Y<318)) {
          if (X < 100) { //press is in the Go button region)
            PAUSED = 0;
          }
        }
      }
    }
    ScreenStart();
    EraseGoButton();
    DispPauseButton();
    DispBrokenCounters();
    digitalWrite(MOTOR, HIGH); //restart the motor and go on
    restartTime = millis(); //record the time that testing continued
    previousTime= millis();
  }

  if ((millis() - previousTime) > 10000 && !PAUSED) { //if no counts have increased in the last 10 seconds...
    digitalWrite(MOTOR, LOW); //stop the motor
    runTime = runTime + (millis() - restartTime); //add the elapsed time to the run total
    // update the screen with message / color
    DispCount();
    for (i=0; i<NUMBUTTONS; i++) { // mark all samples as broken so all the numbers get updated
      broken[i]=HIGH;
    }
    DispBrokenCounters();
    LogToSD();
    EraseGoButton();
    ErasePauseButton();
    tft.setCursor(5,160);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.println("Test");
    tft.setCursor(5,184);
    tft.println("Complete");
    while (HIGH) {
      // do nothing, so the program essentially stops
      // this should eventually be replaced with a restart option
    }
  }

  if ((millis()-brokenTime)>CHECK_BREAKS && !PAUSED) { // CHECK_BREAKS time has elapsed, so check for samples that have broken
    brokenCount=0;
    for (i=0; i<NUMBUTTONS; i++) {
      if ((maxCount-count[i])>(CHECK_BREAKS/1000/1.6*2)) { // if the difference between the max count and the sample is greater than seconds between checks divided by the 1.6 Hz that the machines runs at times 2 (2x for a pad), then it's broken
        broken[i]=HIGH;
        brokenCount+=1;
      }
    }
    DispCount(); // update the current counter on screen
    brokenTime=millis(); //reset the timer
  }

  if (prevbrokenCount!=brokenCount) {  
    //update screen with broken info
    prevbrokenCount=brokenCount;
    DispBrokenCounters();
  }

  if ((maxCount%DISP_FACTOR)==0 && !PAUSED && maxCount && !MechIncremented) {  // each time we've done DISP_COUNTER more stretches, increment the display on the mechanical counter and log to the SD card
    digitalWrite(MECHANICAL,HIGH);
    delay(20); // the mechanical counter needs a 20ms pulse via the relay to increment. I tried 10ms and it didn't always increment.
    digitalWrite(MECHANICAL,LOW);
    LogToSD();
    MechIncremented = true;
  }
  
  if (maxCount%DISP_FACTOR!=0) {
    MechIncremented = false;
  }

  // Loop through switches
  if ((millis() - previousTime)>625 && !PAUSED) { // it's been long enough that we missed a click, so increment the missed count counter
    missedCount++; 
  }

  for (i = 0; i < NUMBUTTONS; i++) {
    button[i].update(); //update reading from switch
    if (button[i].fell()) { //check if switch was activated
      count[i]++; //increase the count if it was
      CLICKED=1;
      previousTime = millis(); //record timer
    }
    if (count[i] > maxCount) { //keep track of the maximum value of any of the counts, since that's the current total cycles
      maxCount = count[i];
    }
  }

  if (CLICKED) {
    Serial.print(millis() - previousTime); //output the elapsed time
    Serial.print("\t");    
    for (i=0;i<NUMBUTTONS;i++) {
      Serial.print(count[i]); //output reading over serial connection
      Serial.print(","); //comma between readings      
    }
    Serial.println(missedCount); //missed reading counter and new line
    CLICKED=0;
  }

  //elapsedTime = millis() - startTime; //total time since the test began
  
}

#define BUFFPIXEL 20

void bmpDraw(char *filename, uint8_t x, uint16_t y) {

  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  goodBmp = false;       // Set to true on valid header parse
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h, row, col;
  uint8_t  r, g, b;
  uint32_t pos = 0, startTime = millis();

  if((x >= tft.width()) || (y >= tft.height())) return;

  Serial.println();
  Serial.print(F("Loading image '"));
  Serial.print(filename);
  Serial.println('\'');

  // Open requested file on SD card
  if ((bmpFile = SD.open(filename)) == NULL) {
    Serial.print(F("File not found"));
    return;
  }

  // Parse BMP header
  if(read16(bmpFile) == 0x4D42) { // BMP signature
    Serial.print(F("File size: ")); Serial.println(read32(bmpFile));
    (void)read32(bmpFile); // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile); // Start of image data
    Serial.print(F("Image Offset: ")); Serial.println(bmpImageoffset, DEC);
    // Read DIB header
    Serial.print(F("Header size: ")); Serial.println(read32(bmpFile));
    bmpWidth  = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if(read16(bmpFile) == 1) { // # planes -- must be '1'
      bmpDepth = read16(bmpFile); // bits per pixel
      Serial.print(F("Bit Depth: ")); Serial.println(bmpDepth);
      if((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed

        goodBmp = true; // Supported BMP format -- proceed!
        Serial.print(F("Image size: "));
        Serial.print(bmpWidth);
        Serial.print('x');
        Serial.println(bmpHeight);

        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;

        // If bmpHeight is negative, image is in top-down order.
        // This is not canon but has been observed in the wild.
        if(bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip      = false;
        }

        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if((x+w-1) >= tft.width())  w = tft.width()  - x;
        if((y+h-1) >= tft.height()) h = tft.height() - y;

        // Set TFT address window to clipped image bounds
        tft.setAddrWindow(x, y, x+w-1, y+h-1);

        for (row=0; row<h; row++) { // For each scanline...

          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if(flip) // Bitmap is stored bottom-to-top order (normal BMP)
            pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
          else     // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          if(bmpFile.position() != pos) { // Need seek?
            bmpFile.seek(pos);
            buffidx = sizeof(sdbuffer); // Force buffer reload
          }

          for (col=0; col<w; col++) { // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) { // Indeed
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0; // Set index to beginning
            }

            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            tft.pushColor(tft.color565(r,g,b));
          } // end pixel
        } // end scanline
        Serial.print(F("Loaded in "));
        Serial.print(millis() - startTime);
        Serial.println(" ms");
      } // end goodBmp
    }
  }

  bmpFile.close();
  if(!goodBmp) Serial.println(F("BMP format not recognized."));
}

uint16_t read16(File &f) {
  // These read 16- and 32-bit types from the SD card file.
  // BMP data is stored little-endian, Arduino is little-endian too.
  // May need to reverse subscript order if porting elsewhere.
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  // These read 16- and 32-bit types from the SD card file.
  // BMP data is stored little-endian, Arduino is little-endian too.
  // May need to reverse subscript order if porting elsewhere.
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void ScreenStart() {
  Serial.println("ScreenStart");
  //set up the display screen
  tft.fillScreen(0x0000);
  if (SD_GOOD) {
    // bmpDraw("Corner-L.BMP", 0, 0); // load this image from the SD card and display in the upper left
  }
  // draw logo in corner
  tft.fillRoundRect(3,3,160,60,30,0xF800);
  tft.fillRoundRect(6,6,154,54,27,0xffff);
  tft.fillRoundRect(9,9,148,48,24,0x0000);
  tft.setCursor(24,16);
  tft.setTextColor(0xffff);
  tft.setTextSize(5); 
  tft.print("TECH");
  
  for(int col=0;col<2;col++) { // draw the sample ID numbers on the screen
    for(int row=0;row<12;row++) {
      if(col==0) {
        tft.setCursor(216,row*8*3+16);  
      }
      if(col==1) {
        tft.setCursor(354,row*8*3+16);
      }
      tft.setTextColor(0xFFFF,0x0000);
      tft.setTextSize(3);      
      tft.print(padded(col*12+row+1,2));
      tft.print(".");
    }
  }  
  tft.setCursor(5,100);
  tft.println("Count:");
  DispCount(); 
  DispGoButton();
}

void DispCount() { // draw the current count
  tft.setTextColor(0xFFFF,0x0000);
  tft.setTextSize(3); 
  tft.setCursor(5,124);
  tft.println(padded(maxCount/DISP_FACTOR,6));
}

void DispBrokenCounters() { // draw the results for any broken samples
  Serial.print("DispBrokenCounters");
  Serial.print("\t");
  Serial.print("prev=");
  Serial.print(prevbrokenCount);
  Serial.print("\t");
  Serial.print("curr=");
  Serial.println(brokenCount);
  tft.setTextColor(0xFFFF,0x0000);
  tft.setTextSize(3);  
  for(int col=0;col<2;col++) { 
    for(int row=0;row<12;row++) {
      if (broken[col*12+row]) {
        if(col==0) {
          tft.setCursor(216+6*3*3,row*8*3+16);  
        }
        if(col==1) {
          tft.setCursor(354+6*3*3,row*8*3+16);
        }
        tft.println(padded(count[col*12+row]/DISP_FACTOR,4));  
      }
    }
  }   
}

String padded(long num, int len) { // pad a number with leading spaces. Syntax is padded(number-to-pad, total-spaces). ie padded(40,3) returns " 40"
  String buff=String(num);
    int count=buff.length();
    for(count; count<len; count++) {
      buff=" "+buff;
    }
  return buff;
}

void DispGoButton() {
  tft.fillRect(0,168,100,150,0x0000);
  tft.fillRect(0,168,100,128,0x0560);
  tft.fillRect(3,171,94,122,0x0660);
  tft.setCursor(34,220);
  tft.setTextColor(0xFFFF);
  tft.println("GO");
}

void DispPauseButton() {
  tft.fillRect(106,168,100,150,0x0000);
  tft.fillRect(106,168,100,128,0xA800);
  tft.fillRect(109,171,94,122,0xE800);
  tft.setCursor(112,220);
  tft.setTextColor(0xFFFF);
  tft.println("PAUSE");
}

void EraseGoButton() {
  tft.fillRect(0,168,100,150,0x0000);
}

void ErasePauseButton() {
  tft.fillRect(106,168,100,150,0x0000);
}

void LogToSD() {
  if (SD_GOOD) {
    String dataString="";
    File dataFile=SD.open("flexlog.txt",FILE_WRITE);
    if (dataFile) {
      dataString+=(millis() - previousTime); //output the elapsed time
      dataString+="\t";    
      for (i=0;i<NUMBUTTONS;i++) {
        dataString+=count[i];
        dataString+=","; //comma between readings      
      }
      dataFile.println(dataString);
      dataFile.close();
    }
  }
}

