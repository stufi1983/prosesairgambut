#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include <avr/interrupt.h>
#include <avr/io.h>

#define FILLING_START_BUTTON  3
#define START_BUTTON  5
#define STOP_BUTTON  31
#define SENSOR_LEVEL1  7
#define SENSOR_LEVEL2  9

#define SENSOR_UP_LEVEL 15
#define SENSOR_DOWN_LEVEL 13

#define SENSOR_DOWN_LEVEL_PRODUCT 11

#define SENSOR_PH1  A0
#define SENSOR_PH2  A4
#define SENSOR_TURB1  A2
#define SENSOR_TURB2  A8

#define POMPA_AC1_M1 44
#define POMPA_AC2_M2 46

#define POMPA_DC_M3 19 //32  //42
#define SELENOID_S1 40
#define UV_FILTER 38
#define ELEKTRODA 17
#define MOTOR_UV  42

volatile int state = 0;
#define MOTOR 46
#define BATASVOLUME 4000      //4000mL

#define KONSTAN   1.6


#define DEBOUNCE_TIME 50
#define TimerPompa3 150000    //mS
#define LAMAELEKTRODAON 50 //7200
#define JEDAPENGUKURANPH  5000 //mS

#define flowPin 2    //This is the input pin on the Arduino
double flowRate;    //This is the value we intend to calculate.
volatile int count; //This integer needs to be set as volatile to ensure it updates correctly during the interrupt process.

unsigned long startTime = millis();

#define PROSES_PENGISIAN_AIR          "Proses Pengisian Bak"
#define PESAN_PENGISIANTANGKIKOAGULAN "Pengisian Koagulan"
#define PROSES_PENGISIAN_AIR_PENUH    "Air Penuh"
#define PESAN_START1                   "TEKAN START"
#define PESAN_START2                   "UNTUK MULAI"
#define JUDUL                           "ALAT SIAP DIPAKAI"
#define PROSES_HASIL_PENGUKURAN_PH_PRODUK "Pengukuran Produk"

enum currentStatus {IDDLE = 0, ISITANGKIKOAGULASI, PENGUKURANAWALKOAGULASI, START, PANTAUAIRBAKU, ELEKTRODAON, PENGUKURANAKHIRKOAGULASI,
                    PROSESFILTRASI, UKURPHPRODUK, PROSESAKHIR
                   };
enum statusAir {KOSONG = 0, PENUH};
enum statusPompa {MULAI = 0, BERHENTI};
char mode = START;

long lastTime = millis();

LiquidCrystal_I2C lcd(0x27, 20, 4);

char lastMode = 255;
char bakAirBaku = KOSONG;

long pompa3lastTime;
long flowRatelastTime;

//--------------Sensor PH

const int analogInPin = A0;
int sensorValue = 0;
unsigned long int avgValue;
float b;
int buf[10], bufTurb[100], temp;
//-------------------------


//--------------------LCD
void lcdClearLine(char line) {
  // for (char i = 0; i < 20; i++) {
  lcd.setCursor(0, line);
  lcd.print("                    ");
  //}
}


float bacaPH1() {
  for (int i = 0; i < 10; i++)
  {
    buf[i] = analogRead(A0);

    delay(10);
  }

  avgValue = 0;
  for (int i = 2; i < 8; i++)
    avgValue += buf[i];
  float pHVol = (float)avgValue * 5.0 / 1024 / 6;

  float phValue = -5.70 * pHVol + 21.34;

  return (phValue);
}

int bacaTurb1() {
  for (int i = 0; i < 100; i++)
  {
    bufTurb[i] = analogRead(A2);
    delay(10);
  }

  avgValue = 0;
  for (int i = 0; i < 1; i++)
    avgValue += bufTurb[i];
  float turbVol = (float)avgValue * 5.0 / 1024 / 1;

  //y=ax2 + bx + c
  Serial.println(avgValue);
  Serial.println(turbVol);
  float turbValue = -1120.4 * square(turbVol) + 5742.3 * turbVol - 4352.8;
  if (turbVol < 2.5) turbValue = 3000;

  return ((int)turbValue);
}


//---------------------Mode
void mode_start() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F(JUDUL));
  mode = IDDLE;
  Serial.println(F(JUDUL));
}

bool tampil = false;
void mode_idle() {
  long curTime = millis();
  if (curTime - lastTime > 1000) {
    if (tampil) tampil = false; else tampil = true;
    lastTime = curTime;
    if (tampil) {
      lcd.setCursor(0, 1);
      lcd.print(F(PESAN_START1));
      lcd.setCursor(0, 2);
      lcd.print(F(PESAN_START2));
    } else {
      lcdClearLine(1);
      lcdClearLine(2);
    }
  }

  lastMode = mode;

  digitalWrite(SENSOR_DOWN_LEVEL_PRODUCT,HIGH);
  if (!digitalRead(SENSOR_DOWN_LEVEL_PRODUCT)) {
      lcd.setCursor(0, 3);
      lcd.print(F("Produk Terisi Air"));
      delay(500);
    return;
  }else{
    lcdClearLine(3);
    }

  //tunggu START ditekan
  digitalWrite(START_BUTTON, HIGH);
  if (!digitalRead(START_BUTTON)) {
    delay(DEBOUNCE_TIME);
    if (!digitalRead(START_BUTTON)) {
      mode = ISITANGKIKOAGULASI;
      pompa3lastTime = millis();
    }
  }
}

long volTangkiKoagulasi = 0;
char TangkiKoagulasi = KOSONG;
char pompaGaram = MULAI;
void mode_isiTangkiKoagulasi() {

  if (lastMode != mode) {
    Serial.println(F("Proses pengisian tangki koagulan"));
    lcdClearLine(0);
    lcd.setCursor(0, 0);
    lcd.print(F(PESAN_PENGISIANTANGKIKOAGULAN));

    digitalWrite(POMPA_AC2_M2, HIGH);
    Serial.println(F("POMPA_AC2_M2 ON"));
    digitalWrite(POMPA_DC_M3, LOW);
    Serial.println(F("POMPA_DC_M3 ON"));

    //aktifkan timer flowrate
    flowRatelastTime = millis();
    EIMSK = 0x10;          //Enable only INT4

    volTangkiKoagulasi = 0;
    lcd.setCursor(0, 1);
    lcd.print(F("Tangki Koagulasi:"));

  }

  long curTime = millis();
  if (curTime - pompa3lastTime > TimerPompa3 && pompaGaram != BERHENTI) {
    digitalWrite(POMPA_DC_M3, HIGH);
    pompaGaram = BERHENTI;
    Serial.println(F("Pompa Garam Berhenti"));
    Serial.println(F("POMPA_DC_M3 OFF"));
    //pompa3lastTime = curTime;
  }

  //ukur flow sensor INT2
  if (curTime - flowRatelastTime > 1000 && volTangkiKoagulasi < 380) {
    flowRatelastTime = curTime;

    EIMSK = 0x00;          //Disable only INT4
    volTangkiKoagulasi += (state * KONSTAN);
    EIMSK = 0x10;          //Enable only INT4
    state = 0;
    Serial.println(volTangkiKoagulasi);

    if (TangkiKoagulasi == KOSONG) {
      lcdClearLine(2);
      lcd.setCursor(0, 2);
      lcd.print(volTangkiKoagulasi);
      lcd.print(F(" mL"));
    }

  }

  if (volTangkiKoagulasi >= 380 && TangkiKoagulasi == KOSONG) {
    Serial.println(F("Tangki Koagulasi Penuh"));
    digitalWrite(POMPA_AC2_M2, LOW);
    Serial.println(F("POMPA_AC2_M2 OFF"));
    lcdClearLine(2);
    lcd.setCursor(0, 2);
    lcd.print(F("380 L"));
    TangkiKoagulasi = PENUH;
  }

  digitalWrite(SENSOR_LEVEL2,HIGH);
  if (!digitalRead(SENSOR_LEVEL2)) {
    delay(DEBOUNCE_TIME);
    if (!digitalRead(SENSOR_LEVEL2)) {
      Serial.println(F("Tangki Koagulasi Penuh"));
      digitalWrite(POMPA_AC2_M2, LOW);
      Serial.println(F("POMPA_AC2_M2 OFF"));
      TangkiKoagulasi = PENUH;
    }
  }

  lastMode = mode;

  if (pompaGaram == BERHENTI && TangkiKoagulasi == PENUH) {
    mode = PENGUKURANAWALKOAGULASI;
  }
}

float nilaiPH1 = 8.0;
void mode_ukurPH1() {
  if (lastMode != mode) {
    lcdClearLine(2);
    lcd.setCursor(0, 2);
    lcd.print(F("Nilai pH :"));

    //baca pHDisini
    lcd.print(bacaPH1());
  }
}
float nilaiTurb1 = 80;
void mode_Turbidity1() {
  if (lastMode != mode) {
    lcdClearLine(3);
    lcd.setCursor(0, 3);
    lcd.print(F("Nilai TBD:"));

    //baca turbidity
    lcd.print(bacaTurb1());
    lcd.print(F(" NTU"));
  }
}

float nilaiPH2 = 7.0;
void mode_ukurPH2() {
  if (lastMode != mode) {
    lcdClearLine(2);
    lcd.setCursor(0, 2);
    lcd.print(F("Nilai pH :"));

    //baca pHDisini
    lcd.print(bacaPH1());
  }
}
float nilaiTurb2 = 90;
void mode_Turbidity2() {
  if (lastMode != mode) {
    lcdClearLine(3);
    lcd.setCursor(0, 3);
    lcd.print(F("Nilai TBD:"));

    //baca turbidity
    lcd.print(bacaTurb1());
    lcd.print(F(" NTU"));
  }
}

float nilaiPH3 = 7.0;
void mode_ukurPH3() {
  if (lastMode != mode) {
    lcdClearLine(2);
    lcd.setCursor(0, 2);
    lcd.print(F("Nilai pH  :"));

    //baca pHDisini
    lcd.print(nilaiPH3);
  }
}
float nilaiTurb3 = 100;
void mode_Turbidity3() {
  if (lastMode != mode) {
    lcdClearLine(3);
    lcd.setCursor(0, 3);
    lcd.print(F("Kejernihan:"));

    //baca turbidity
    lcd.print((int)nilaiTurb3);
    lcd.print(F("NTU"));
  }
}

byte jam = 0;
byte menit = 0;
byte detik = 0;
byte total = 0;

void mode_elektrodaOn() {
  if (mode != lastMode) {
    Serial.println(F("Proses elektro koagulasi"));
    lcdClearLine(0);
    lcd.setCursor(0, 0); lcd.print(F("Elektrokoagulasi:"));
    pinMode (ELEKTRODA, LOW);
    Serial.println(F("ELEKTRODA ON"));
    lastMode = mode;
  }

  if (total < LAMAELEKTRODAON) {
    long curTime = millis();
    if (curTime - lastTime > 1000) {
      lastTime = curTime;
      total++;

      detik++;

      if (detik > 59) {
        detik = 0;
        menit++;
      }
      if (menit > 59) {
        menit = 0;
        jam++;
      }

      lcdClearLine(1);
      lcd.setCursor(0, 1);
      lcd.print(jam);
      lcd.print(":"); if (menit < 10) lcd.print("0");
      lcd.print(menit);
      lcd.print(":"); if (detik < 10) lcd.print("0");
      lcd.print(detik);
    }
  }
  else {

    Serial.println(F("Proses elektro koagulasi selesai"));
    pinMode (ELEKTRODA, HIGH);
    Serial.println(F("ELEKTRODA OFF"));
    mode = PENGUKURANAKHIRKOAGULASI;

  }
}

char tangkiFiltrasiLevelBawah = false;
long tangkiFiltrasiLastTime = millis();
char tangkiFiltrasi = KOSONG;
void mode_prosesFiltrasi() {
  long curTime = millis();
  if (lastMode != mode) {
    lcd.setCursor(0, 0);
    lcd.print("Proses Filtrasi");
    lcd.setCursor(0, 1);
    lcd.print("sedang berlangsung");

    //Selenoid dibuka
    Serial.println(F("Proses pengisian bak filtrasi"));
    digitalWrite(SELENOID_S1, HIGH);
    Serial.println(F("SELENOID_S1 ON"));

    Serial.println(F("Filter UV dialirkan"));
    Serial.println(F("MOTOR_UV ON"));

    Serial.println(F("Filter UV diaktifkan"));
    digitalWrite(UV_FILTER, LOW);
    Serial.println(F("UV_FILTER ON"));

  }
  lastMode = mode;

  ///----Bagian Selenoid---
  //Matikan selenoid jika tampungan penuh
  digitalWrite(SENSOR_UP_LEVEL,HIGH);
  if (!digitalRead(SENSOR_UP_LEVEL) && tangkiFiltrasi == KOSONG) {
    Serial.println(F("Tangki filtrasi penuh"));
    digitalWrite(SELENOID_S1, LOW);
    Serial.println(F("SELENOID_S1 OFF"));
    tangkiFiltrasi = PENUH;
    flowRatelastTime = millis(); //start timer
  }

  //aktifkan selenoid jika sudah 5 detik untuk menjaga ketinggian air
  if (tangkiFiltrasi == PENUH) {
    curTime = millis();
    if (curTime - flowRatelastTime > 5000) {
      Serial.println(F("Pengisian ulangtangki filtrasi"));
      flowRatelastTime = curTime;
      tangkiFiltrasi = KOSONG;
      digitalWrite(SELENOID_S1, HIGH);
      Serial.println(F("SELENOID_S1 ON"));


    }
  }



  ///----Bagian pompa UV ----
  //Jika ada air MOTOR_UV ON
  if (digitalRead(SENSOR_DOWN_LEVEL)) {
    digitalWrite(MOTOR_UV, LOW);
    //Serial.println(F("MOTOR_UV ON"));
  } else {
    digitalWrite(MOTOR_UV, HIGH);
    //Serial.println(F("MOTOR_UV OFF"));
  }

  //Jika tidak ada air dan pengisian sudah selesai matikan selenoid dan pompa UV
  digitalWrite(SENSOR_DOWN_LEVEL,HIGH);
  if (tangkiFiltrasi == KOSONG && !digitalRead(SENSOR_DOWN_LEVEL)) {
    Serial.println(F("Tangki filter telah kosong, matikan motor UV, UV dan Selenoid"));
    digitalWrite(MOTOR_UV, HIGH);
    Serial.println(F("MOTOR_UV OFF"));
    pinMode(SELENOID_S1, LOW);
    Serial.println(F("SELENOID_S1 OFF"));
    digitalWrite(UV_FILTER, HIGH);
    Serial.println(F("UV_FILTER OFF"));
    mode = UKURPHPRODUK;
  }


}

//-------------------SetIO
void setPinIO() {
  //input
  pinMode(START_BUTTON  , INPUT_PULLUP);
  pinMode(STOP_BUTTON  , INPUT_PULLUP);
  pinMode(SENSOR_LEVEL1, INPUT_PULLUP);
  pinMode(SENSOR_LEVEL2, INPUT_PULLUP);
  pinMode(SENSOR_UP_LEVEL, INPUT_PULLUP);
  pinMode(SENSOR_DOWN_LEVEL, INPUT_PULLUP);
  pinMode(SENSOR_DOWN_LEVEL_PRODUCT, INPUT_PULLUP);

  //ouput
  pinMode( POMPA_AC1_M1, OUTPUT);
  pinMode( POMPA_AC2_M2, OUTPUT);
  pinMode( POMPA_DC_M3, OUTPUT);
  pinMode( SELENOID_S1, OUTPUT);
  pinMode( UV_FILTER, OUTPUT);
  pinMode (MOTOR_UV, OUTPUT);

  digitalWrite( POMPA_AC1_M1, LOW);
  digitalWrite( POMPA_AC2_M2, LOW);
  digitalWrite( POMPA_DC_M3, HIGH);
  digitalWrite( SELENOID_S1, LOW);
  digitalWrite( UV_FILTER, HIGH);
  digitalWrite(ELEKTRODA, HIGH);
  digitalWrite (MOTOR_UV, HIGH);
  digitalWrite(SENSOR_DOWN_LEVEL_PRODUCT, LOW);

  pinMode(flowPin, INPUT);           //Sets the pin as an input


}

void cekLevelAirBaku() {
  
  digitalWrite(FILLING_START_BUTTON, HIGH);
  if (!digitalRead(FILLING_START_BUTTON)) {
    delay(DEBOUNCE_TIME);
    if (!digitalRead(FILLING_START_BUTTON)) {
      digitalWrite(POMPA_AC1_M1, HIGH);
      lcdClearLine(0);
      lcd.setCursor(0, 0);
      lcd.print(F(PROSES_PENGISIAN_AIR));
      bakAirBaku = KOSONG;
    }
  }

  digitalWrite(SENSOR_LEVEL1, HIGH);
  if (!digitalRead(SENSOR_LEVEL1)) {
    
    if (!digitalRead(SENSOR_LEVEL1)) {
  Serial.println(digitalRead(SENSOR_LEVEL1));

      digitalWrite(POMPA_AC1_M1, LOW);
      if (bakAirBaku == KOSONG) {
        lcdClearLine(0);
        lcd.setCursor(0, 0);
        lcd.print(F(PROSES_PENGISIAN_AIR_PENUH));
      }
      bakAirBaku = PENUH;
    }
  }
}


void mode_ukurPHProduk() {
  Serial.println(F("Pengukuran produk setelah filtrasi"));
  lcdClearLine(0);
  lcd.setCursor(0, 0);
  lcd.print(F("Proses Selesai"));
  lcd.print(F(PROSES_HASIL_PENGUKURAN_PH_PRODUK));
  lcdClearLine(1);
  mode_ukurPH3();
  mode_Turbidity3();
  mode = PROSESAKHIR;

}



ISR(INT4_vect) {
  state++;
}

void setup() {
  setPinIO();
  lcd.init();                      // initialize the lcd
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Init....."));



  lcd.print(F("OK"));
  lcd.setCursor(0, 0);
  lcd.print(F("OK"));
  lcd.setCursor(0, 0);
  Serial.begin(115200);

  pinMode(2, INPUT);     //Button input tied to INT4
  EICRB = 0x11;          //INT4, triggered on rissing
        lcdClearLine(0);
        lcd.setCursor(0, 0);
        lcd.print(F("Mulai..."));

}

void loop() {

  //Luar proses
  cekLevelAirBaku();

  //Dalam proses
  switch (mode) {
    case IDDLE:
      mode_idle();
      break;
    case ISITANGKIKOAGULASI:
      mode_isiTangkiKoagulasi();
      break;
    case PENGUKURANAWALKOAGULASI:
      Serial.println(F("Pengukuran produk mentah"));
      mode_ukurPH1();
      mode_Turbidity1();
      delay(1000);
      //lastMode=mode;
      //mode = ELEKTRODAON;
      break;
    case ELEKTRODAON:
      mode_elektrodaOn();
      break;
    case PENGUKURANAKHIRKOAGULASI:
      Serial.println(F("Pengukuran produk setelah koagulasi"));
      mode_ukurPH2();
      mode_Turbidity2();
      //lastMode=mode;
      delay(JEDAPENGUKURANPH);
      mode = PROSESFILTRASI;
      break;
    case PROSESFILTRASI:
      mode_prosesFiltrasi();
      break;
    case UKURPHPRODUK:
      mode_ukurPHProduk();
      break;
    case START:
      mode_start();
      break;
    default:
      break;
  }
}
