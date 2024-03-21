#include "Wire.h"
#include "OBD9141.h"
#include "Adafruit_SSD1306.h"

#define RX_PIN 8
#define TX_PIN 9
Adafruit_SSD1306 display(128, 32, &Wire, 4);
byte actualTemp;
byte fuelLevel;
byte errorCount = 0;
byte errorCodeArray[5];
int errorListingBtn = 7;//d7
boolean errorListing;
uint32_t btnTimer = 0;
int errorResetBtn = 6;//d6
AltSoftSerial altSerial;
OBD9141 obd;
boolean obdInitState;
int erC = 0;

void setup() {
  Serial.begin(9600);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);//address
  pinMode(errorListingBtn, INPUT);
  pinMode(errorResetBtn, INPUT);
  display.clearDisplay();
  display.setTextSize(1, 2);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(30, 8);
  display.println("Hello Artyom");
  display.display();
  delay(3000);
  display.clearDisplay();
  obd.begin(altSerial, RX_PIN, TX_PIN);
}

void loop() {
  if (erC == 0) {
    obdInitState = obd.initKWP();
    erC = 1;
  }
  if (erC == 11) {
    erC = 0;
    displayInitError();
    return;
  }
  if (digitalRead(errorListingBtn) == HIGH && !errorListing && millis() - btnTimer > 200) {
    errorListing = true;
    btnTimer = millis();
  }
  if (errorListing) {
    displayErrorCodes();
  } else {
    displayData();
  }
  if (digitalRead(errorListingBtn) == HIGH && errorListing && millis() - btnTimer > 200) {
    errorListing = false;
    btnTimer = millis();
  }
  if (digitalRead(errorResetBtn) == HIGH && errorListing && millis() - btnTimer > 200) {
    errorListing = false;
    btnTimer = millis();
    resetErrors();
  }
}

void displayInitError() {
  display.setTextSize(1, 2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 8);
  display.println("K-line error");
  display.display();
  delay(2000);
  display.clearDisplay();
  display.setCursor(30, 8);
  display.println("Will restart");
  display.display();
  delay(2000);
  display.clearDisplay();
}
void displayData() {
  display.setTextSize(1, 2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 8);
  updateActualTemp();
  checkErrors();
  display.println("ENGINE TEMP: " + String(actualTemp));
  if (errorCount > 0) {
    display.setCursor(108, 8);
    display.println("<!>");
  }
  display.display();
  display.clearDisplay();
}
void displayErrorCodes() {
  String errors = "Errors: ";
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(errors);
  for (byte index = 0; index < errorCount; index++) {
    // retrieve the trouble code in its raw two byte value.
    int troubleCode = obd.getTroubleCode(index);
    Serial.print("trouble code ): ");
    Serial.println(troubleCode);
    // If it is equal to zero, it is not a real trouble code
    // but the ECU returned it, print an explanation.
    if (troubleCode != 0) {
      // convert the DTC bytes from the buffer into readable string
      OBD9141::decodeDTC(troubleCode, errorCodeArray);
      // Print the 5 readable ascii strings to the serial port.
      for (char c : errorCodeArray) display.print(c);
      display.print(" ");
    }
  }
  display.display();
  display.clearDisplay();
}

void resetErrors() {
  if (obd.clearTroubleCodes()) {
    errorCount = 0;
    errorListing = false;
    displayResult();
  }
}

void displayResult() {
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(55, 10);
  display.println("OK");
  display.display();
  delay(2000);
  display.clearDisplay();
}

void updateActualTemp() {
  if (obd.getCurrentPID(0x05, 1)) {
    actualTemp = obd.readUint8() - 40;//todo check if temp is negative
  } else {
    erC++;
  }

}

void checkErrors() {
  byte res = obd.readPendingTroubleCodes();
  for (byte index = 0; index < errorCount; index++) {
    int troubleCode = obd.getTroubleCode(index);
    if (troubleCode != 0) {
      errorCount++;
    }
  }
}
