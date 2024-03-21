#include "OBD9141.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include <sd_defines.h>
#include <sd_diskio.h>
#include <SD.h>
#include <SPI.h>
#include <CRC.h>


#define RX_PIN 16  //8//3
#define TX_PIN 17  //9//1
#define PIN_DC 21
#define PIN_RST 22
#define PIN_SPI_SS 5

U8G2_SH1122_256X64_F_4W_HW_SPI u8g2(U8G2_R0, PIN_SPI_SS, PIN_DC, PIN_RST);
OBD9141 obd;

bool obdInitState;
byte obdErrorCount;
bool notInited = true;
bool obdDisabled;

const byte menuBtn = 32;
const byte okBtn = 33;
const byte sdCardPin = 4;

uint32_t btnTimer = 0;
bool isMenuBtnPressed;
bool isMenuSelected;
const byte pageMain = 0;
const byte page1 = 1;
const byte page2 = 2;
const byte page3 = 3;
const byte page4 = 4;
const byte pageErrors = 5;
const byte pageConfigReset = 6;
const byte pageDisableObd = 7;
const byte pageBack = 8;
byte menuPageNumber = pageMain;
bool isOkBtnPressed;
bool isSubMenuSelected;
bool isResetErrorSelected;

boolean isErrorListingSelected;
byte errorListingIndex;
byte errorCount;
uint8_t errorCodeArray[5];

File saveFile;
unsigned long lastSaveTimestampt;
unsigned long actualSaveTimestampt;
const int saveTimeout = 300000;
struct attribute((__packed)) saveData {
  double fuel;
  double oddometr;
  uint8_t crc;
};
saveData data;

//=======================================================
const double fuelGramsPerLiter = 750;  // !!константа - грамм бензина в 1 литре 95 бензина
const double airFuelRatio = 14.7;      // !!константа расхода 14,7 воздуха к 1 литра
long actualTemp, carSpeed;
int rpm;
double lp100, engineLoad, ltft, stft, maf, fuelCoef, fuelGramsPerSecond, fuelLitersPerSecond, lph,
  measurementDuration, actualOdometr, odometrAcculmulator, actualFuelConsume, fuelConsumeAccumulator;
byte fss, throttlePosition;
unsigned long actualTimeFromStart, previousTimeFromStart;
//=======================================================


void readSavedDataFromSd() {
  if (readStructFromFile(F("/primary.txt"))) {
    fuelConsumeAccumulator = data.fuel;
    odometrAcculmulator = data.oddometr;
  } else if (readStructFromFile(F("/backup.txt"))) {
    fuelConsumeAccumulator = data.fuel;
    odometrAcculmulator = data.oddometr;
  }
}

bool readStructFromFile(String fileName) {
  bool result = false;
  if (SD.exists(fileName)) {
    saveFile = SD.open(fileName);
    if (saveFile) {
      while (saveFile.available()) {
        saveFile.read((byte*)&data, sizeof(data));
      }
      saveFile.close();
      //-1 отбрасывать при расчетах байт, содержащий хеш сумму
      if (calcCRC8((byte*)&data, sizeof(data) - 1) == data.crc) {
        fuelConsumeAccumulator = data.fuel;
        odometrAcculmulator = data.oddometr;
        result = true;
      }
    }
  }
  return result;
}

void setup() {
  pinMode(menuBtn, INPUT);
  //    digitalWrite(menuBtn, 0); надо или нет хз
  pinMode(okBtn, INPUT);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFont(u8g2_font_10x20_t_cyrillic);
  if (!SD.begin(sdCardPin)) {
    return;
  }
  obd.begin(Serial2, RX_PIN, TX_PIN);
  readSavedDataFromSd();
}

bool btnsPress = false;
void selectBtnHandler() {
  bool menuBtnState = digitalRead(menuBtn);
  if (menuBtnState == LOW && !isMenuBtnPressed && millis() - btnTimer > 50) {
    isMenuSelected = true;
    isMenuBtnPressed = true;
    btnsPress = true;
    if (isSubMenuSelected) {
      if (isErrorListingSelected) {
        errorListingIndex++;
        if (errorListingIndex > errorCount) {
          errorListingIndex = 0;
        }
      }
    } else {
      menuPageNumber++;
      if (menuPageNumber > pageBack) {
        menuPageNumber = page1;
      }
    }
    btnTimer = millis();
  }
  if (menuBtnState == HIGH && isMenuBtnPressed && millis() - btnTimer > 50) {
    isMenuBtnPressed = false;
    btnsPress = false;
    btnTimer = millis();
  }
}

void okBtnHandler() {
  bool okBtnState = digitalRead(okBtn);
  if (okBtnState == LOW && !isOkBtnPressed && millis() - btnTimer > 50) {
    btnsPress = true;
    if (obdDisabled) {
      obdDisabled = false;
      return;
    }
    if (menuPageNumber >= pageErrors) {
      isSubMenuSelected = true;
    }
    if (isErrorListingSelected && errorListingIndex == errorCount) {
      isResetErrorSelected = true;
    }
    isOkBtnPressed = true;
    btnTimer = millis();
  }
  if (okBtnState == HIGH && isOkBtnPressed && millis() - btnTimer > 50) {
    isOkBtnPressed = false;
    btnsPress = false;
    btnTimer = millis();
  }
}

void loop() {
  if (obdDisabled) {
    displayDisableStatus();
    okBtnHandler();
    return;
  }
  //    todo мутная логика со счетчиками
  if (notInited) {
    obdInitState = obd.initKWP();
    if (!obdInitState) {
      displayInitError();
      return;
    }
    notInited = false;
  }
  if (obdErrorCount == 11) {
    displayInitError();
    obdErrorCount = 0;
    notInited = true;
    return;
  }

  selectBtnHandler();
  okBtnHandler();

  if (isMenuSelected) {
    if (isSubMenuSelected && menuPageNumber == pageErrors) {
      if (errorCount == 0) {
        printNoErrors();
        isSubMenuSelected = false;
      } else {
        isErrorListingSelected = true;
        if (isResetErrorSelected) {
          if (resetErrors()) {
            printResetOk();
            isErrorListingSelected = false;
            errorListingIndex = 0;
            isResetErrorSelected = false;
          } else {
            printResetError();
            isResetErrorSelected = false;
          }
        } else {
          printErrors();
        }
      }
    } else if (isSubMenuSelected && menuPageNumber == pageConfigReset) {
      if (resetSettings()) {
        printResetOk();
      } else {
        printResetError();
      }
      isSubMenuSelected = false;
    } else if (isSubMenuSelected && menuPageNumber == pageDisableObd) {
      isMenuSelected = false;
      isSubMenuSelected = false;
      menuPageNumber = pageMain;
      obdDisabled = true;
    } else if (isSubMenuSelected && menuPageNumber == pageBack) {
      isMenuSelected = false;
      menuPageNumber = pageMain;
      isSubMenuSelected = false;
    } else {
      printMenuPage();
    }
  } else {
    printMainPage();
  }
  if (lastSaveTimestampt == 0) {
    lastSaveTimestampt = millis();
  }
  actualSaveTimestampt = millis();
  if (actualSaveTimestampt - lastSaveTimestampt > saveTimeout) {
    saveDataToSd();
    lastSaveTimestampt = actualSaveTimestampt;
  }
  updateSensorsData();
  sensorsDataProcess();
}

//пропускаем не самые важные датчики, чтоб не было лагов при управлении бортовиком
byte skipCounter;
void updateSensorsData() {
  if (skipCounter == 15) {
    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateActualTemp();
    } else {
      return;
    }
  }
  if (skipCounter == 5) {
    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateRpm();
    } else {
      return;
    }

    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateEngineLoad();
    } else {
      return;
    }

    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateFuelSystemStatus();
    } else {
      return;
    }

    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateLongTermFuelTrim();
    } else {
      return;
    }

    if (fss == 2) {
      selectBtnHandler();
      okBtnHandler();
      if (!btnsPress) {
        updateShortTermFuelTrim();
      } else {
        return;
      }
    }
    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateMassAirFlow();
    } else {
      return;
    }
    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateSpeed();
    } else {
      return;
    }
  }
  if (isMenuSelected && menuPageNumber == page1) {
    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateThrottlePosition();
    } else {
      return;
    }
  }
  skipCounter++;
  if (skipCounter == 20) {
    selectBtnHandler();
    okBtnHandler();
    if (!btnsPress) {
      updateErrorCount();
    } else {
      return;
    }
    skipCounter = 0;
  }
}

void updateActualTemp() {
  if (obd.getCurrentPID(0x05, 1)) {
    actualTemp = obd.readUint8() - 40;
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateRpm() {
  if (obd.getCurrentPID(0x0C, 2)) {
    rpm = obd.readUint16() / 4;
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateEngineLoad() {
  if (obd.getCurrentPID(0x04, 1)) {
    engineLoad = obd.readUint8() * 100 / 255;
    obdErrorCount = 0;

  } else {
    obdErrorCount++;
  }
}
//|--|-------------------------------------------------------------------------------------------|
//|0 | The motor is off                                                                          |
//|1 | Open loop due to insufficient engine temperature                                          |
//|2 | Closed loop, using oxygen sensor feedback to determine fuel mix                           |
//|4 | Open loop due to engine load OR fuel cut due to deceleration                              |
//|8 | Open loop due to system failure                                                           |
//|16| Closed loop, using at least one oxygen sensor but there is a fault in the feedback system |
//|--|-------------------------------------------------------------------------------------------|
void updateFuelSystemStatus() {
  if (obd.getCurrentPID(0x03, 2)) {
    fss = obd.readUint8();
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateLongTermFuelTrim() {
  if (obd.getCurrentPID(0x07, 1)) {
    ltft = double((obd.readUint8() - 128) * 100) / 128;
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateShortTermFuelTrim() {
  if (obd.getCurrentPID(0x06, 1)) {
    stft = double((obd.readUint8() - 128) * 100) / 128;
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateMassAirFlow() {
  if (obd.getCurrentPID(0x10, 2)) {
    maf = double(obd.readUint16()) / 100;
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateSpeed() {
  if (obd.getCurrentPID(0x0D, 1)) {
    carSpeed = obd.readUint8();
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateThrottlePosition() {
  if (obd.getCurrentPID(0x11, 1)) {
    throttlePosition = obd.readUint8() * 100 / 255;
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

void updateErrorCount() {
  if (obd.readPendingTroubleCodes()) {
    for (byte index = 0; index < errorCount; index++) {
      int troubleCode = obd.getTroubleCode(index);
      if (troubleCode != 0) {
        errorCount++;
      }
    }
    obdErrorCount = 0;
  } else {
    obdErrorCount++;
  }
}

bool resetErrors() {
  if (obd.clearTroubleCodes()) {
    errorCount = 0;
    return true;
  } else {
    return false;
  }
}

void fillErrorCodeArray(byte index) {
  //     retrieve the trouble code in its raw two byte value.
  int troubleCode = obd.getTroubleCode(index);
  // If it is equal to zero, it is not a real trouble code
  // but the ECU returned it, print an explanation.
  if (troubleCode != 0) {
    //       convert the DTC bytes from the buffer into readable string
    OBD9141::decodeDTC(troubleCode, errorCodeArray);
  }
}

void sensorsDataProcess() {
  if (rpm > 400) {
    if (engineLoad < 20 && fss == 4) {
      //noop если тормозим двигателем
    } else {
      if (fss == 2) {
        fuelCoef = double(100.0 + (ltft + stft)) / 100.0;  // коэффициент корректировки расхода по ShortTermFuelTrim и LongTermFuelTrim при closed loop
      } else {
        fuelCoef = double(100.0 + ltft) / 100.0;  // коэффициент корректировки расхода по LongTermFuelTrim
      }
      fuelGramsPerSecond = double(maf / airFuelRatio) * fuelCoef;    // Получаем расход грамм бензина в секунду в соотношении 14,7 воздуха/к 1 литра бензина, корректировка fuelCoef
      fuelLitersPerSecond = fuelGramsPerSecond / fuelGramsPerLiter;  // Переводим граммы бензина в литры
      lph = fuelLitersPerSecond * 3600;                              //сек в 1 часе
    }

    if (previousTimeFromStart == 0) {
      previousTimeFromStart = millis();  // выполнится один раз при появлении оборотов
    }

    actualTimeFromStart = millis();                                                        // время со старта программы в мс
    measurementDuration = (double(actualTimeFromStart - previousTimeFromStart) / 1000.0);  // прошло время с последнего расчета скорости, расхода  - в сек
    if (measurementDuration > 10) {                                                        //todo видимо скипать если времени прошло много
      measurementDuration = 0;
    }
    previousTimeFromStart = actualTimeFromStart;

    if (carSpeed > 0) {
      actualOdometr = double((double(carSpeed * 1000.0) / 3600.0) * measurementDuration) / 1000.0;
      odometrAcculmulator += actualOdometr;
      actualFuelConsume = fuelLitersPerSecond * measurementDuration;
      fuelConsumeAccumulator += actualFuelConsume;
    }
  }

  if (odometrAcculmulator > 0) {
    lp100 = (fuelConsumeAccumulator / odometrAcculmulator) * 100.0;  //расход бензина на 100 км (в литрах)
  }
}

bool resetSettings() {
  SD.remove(F("/primary.txt"));
  SD.remove(F("/backup.txt"));
  fuelConsumeAccumulator = 0;
  odometrAcculmulator = 0;
  return true;
}

void saveDataToSd() {
  data.fuel = fuelConsumeAccumulator;
  data.oddometr = odometrAcculmulator;
  data.crc = calcCRC8((byte*)&data, sizeof(data) - 1);
  SD.remove(F("/primary.txt"));
  writeStructToFile(F("/primary.txt"));
  SD.remove(F("/backup.txt"));
  writeStructToFile(F("/backup.txt"));
}

void writeStructToFile(String fileName) {
  saveFile = SD.open(fileName, FILE_WRITE);
  if (saveFile) {
    saveFile.write((byte*)&data, sizeof(data));
    saveFile.close();
  }
}

void displayDisableStatus() {
  u8g2.clearBuffer();
  u8g2.setCursor(72, 36);
  u8g2.print(F("БК выключен"));
  u8g2.sendBuffer();
}

//36 - ~ центр для шрифта 10х20 в нижнем регистре;
//39 - ~ центр для шрифта 10х20 в верхнем регистре, цифры и символы;
void displayInitError() {
  u8g2.clearBuffer();
  delay(2000);
  u8g2.setCursor(62, 36);
  u8g2.print(F("K-line ошибка"));
  u8g2.sendBuffer();
  delay(2000);
  u8g2.clearBuffer();
  u8g2.setCursor(77, 36);
  u8g2.print(F("Перезапуск"));
  u8g2.sendBuffer();
}

void printNoErrors() {
  u8g2.clearBuffer();
  u8g2.setCursor(77, 36);
  u8g2.print(F("Нет ошибок"));
  u8g2.sendBuffer();
  delay(2000);  //todo уйти от delay в пользу millis но тогда надо не забыть иначе обновить переменные
}

void printResetOk() {
  u8g2.clearBuffer();
  u8g2.setCursor(117, 36);
  u8g2.print(F("Ok"));
  u8g2.sendBuffer();
  delay(2000);  //todo уйти от delay в пользу millis
}

void printResetError() {
  u8g2.clearBuffer();
  u8g2.setCursor(97, 36);
  u8g2.print(F("Ошибка"));
  u8g2.sendBuffer();
  delay(2000);  //todo уйти от delay в пользу millis
}

void printErrors() {
  u8g2.clearBuffer();
  if (errorListingIndex == errorCount) {
    u8g2.setCursor(67, 36);
    u8g2.print(F("Сброс ошибок"));
  } else {
    fillErrorCodeArray(errorListingIndex);
    u8g2.setCursor(97, 26);
    u8g2.print(errorListingIndex + 1);
    u8g2.print(F(" из "));
    u8g2.print(errorCount);
    u8g2.setCursor(102, 52);
    for (byte i; i < 5; i++) {
      u8g2.print((char)errorCodeArray[i]);
    }
  }
  u8g2.sendBuffer();
}

void printMenuPage() {
  u8g2.clearBuffer();
  if (menuPageNumber == page1) {
    u8g2.setCursor(8, 26);
    if (engineLoad >= 100) {
      u8g2.print(F("Нагрузка на двиг:  "));
    } else if (engineLoad < 10) {
      u8g2.print(F("Нагрузка на двиг:    "));
    } else {
      u8g2.print(F("Нагрузка на двиг:   "));
    }
    u8g2.print(engineLoad, 1);
    u8g2.setCursor(8, 52);
    if (throttlePosition >= 100) {
      u8g2.print(F("Дроссельная заслонка:"));
    } else if (throttlePosition < 10) {
      u8g2.print(F("Дроссельная заслонка:  "));
    } else {
      u8g2.print(F("Дроссельная заслонка: "));
    }
    u8g2.print(throttlePosition);
  } else if (menuPageNumber == page2) {
    u8g2.setCursor(8, 26);
    if (maf >= 100) {
      u8g2.print(F("Масc расход возд: "));
    } else if (maf < 10) {
      u8g2.print(F("Масc расход возд:   "));
    } else {
      u8g2.print(F("Масc расход возд:  "));
    }
    u8g2.print(maf);
    u8g2.setCursor(8, 52);
    if (fss > 9) {
      u8g2.print(F("Статус топл системы:  "));
    } else {
      u8g2.print(F("Статус топл системы:   "));
    }
    u8g2.print(fss);
  } else if (menuPageNumber == page3) {
    u8g2.setCursor(8, 26);
    if (stft < -99) {
      u8g2.print(F("Кр топл кор-ция:  "));
    } else if (stft > -100 && stft < -9) {
      u8g2.print(F("Кр топл кор-ция:   "));
    } else if (stft > -10 && stft < 0) {
      u8g2.print(F("Кр топл кор-ция:    "));
    } else if (stft > -1 && stft < 10) {
      u8g2.print(F("Кр топл кор-ция:     "));
    } else if (stft > 9 && stft < 100) {
      u8g2.print(F("Кр топл кор-ция:    "));
    } else {
      u8g2.print(F("Кр топл кор-ция:   "));
    }
    u8g2.print(stft, 1);
    u8g2.setCursor(8, 52);
    if (ltft < -99) {
      u8g2.print(F("Дол топл кор-ция: "));
    } else if (ltft > -100 && ltft < -9) {
      u8g2.print(F("Дол топл кор-ция:  "));
    } else if (ltft > -10 && ltft < 0) {
      u8g2.print(F("Дол топл кор-ция:   "));
    } else if (ltft > -1 && ltft < 10) {
      u8g2.print(F("Дол топл кор-ция:    "));
    } else if (ltft > 9 && ltft < 100) {
      u8g2.print(F("Дол топл кор-ция:   "));
    } else {
      u8g2.print(F("Дол топл кор-ция:  "));
    }
    u8g2.print(ltft, 1);
  } else if (menuPageNumber == page4) {
    u8g2.setCursor(8, 26);
    if (carSpeed >= 100) {
      u8g2.print(F("Скорость:            "));
    } else if (carSpeed < 10) {
      u8g2.print(F("Скорость:              "));
    } else {
      u8g2.print(F("Скорость:             "));
    }
    u8g2.print(carSpeed);
    u8g2.setCursor(8, 52);
    if (odometrAcculmulator < 10) {
      u8g2.print(F("Пробег:             "));
    } else if (odometrAcculmulator < 100) {
      u8g2.print(F("Пробег:            "));
    } else if (odometrAcculmulator < 1000) {
      u8g2.print(F("Пробег:           "));
    } else if (odometrAcculmulator < 10000) {
      u8g2.print(F("Пробег:          "));
    } else {
      u8g2.print(F("Пробег:         "));
    }
    u8g2.print(odometrAcculmulator, 2);
  } else if (menuPageNumber == pageErrors) {
    u8g2.setCursor(97, 36);
    u8g2.print(F("Ошибки"));
  } else if (menuPageNumber == pageConfigReset) {
    u8g2.setCursor(57, 36);
    u8g2.print(F("Сброс Настроек"));
  } else if (menuPageNumber == pageDisableObd) {
    u8g2.setCursor(67, 36);
    u8g2.print(F("Выключить БК"));
  } else {
    u8g2.setCursor(102, 36);
    u8g2.print(F("Назад"));
  }
  u8g2.sendBuffer();
}

void printMainPage() {
  u8g2.clearBuffer();
  u8g2.setCursor(8, 26);
  if (actualTemp <= -10 actualTemp >= 100) {
    u8g2.print(F("Тдв:  "));
  } else if (actualTemp <= -1 actualTemp >= 10) {
    u8g2.print(F("Тдв:   "));
  } else {
    u8g2.print(F("Тдв:    "));
  }
  u8g2.print(actualTemp);
  u8g2.setCursor(8, 52);
  if (rpm >= 1000) {
    u8g2.print(F("Одв: "));
  } else if (rpm >= 100 && rpm < 1000) {
    u8g2.print(F("Одв:  "));
  } else if (rpm >= 10 && rpm < 100) {
    u8g2.print(F("Одв:   "));
  } else {
    u8g2.print(F("Одв:    "));
  }
  u8g2.print(rpm);
  u8g2.setCursor(147, 26);
  if (lph >= 10) {
    u8g2.print(F("Л/ч:  "));
  } else {
    u8g2.print(F("Л/ч:   "));
  }
  u8g2.print(lph, 1);
  u8g2.setCursor(147, 52);
  if (lp100 >= 10) {
    u8g2.print(F("Л/100:"));
  } else {
    u8g2.print(F("Л/100: "));
  }
  u8g2.print(lp100, 1);
  if (errorCount > 0) {
    u8g2.setCursor(107, 39);
    u8g2.print(F("<!>"));
  }
  u8g2.sendBuffer();
}