// FireBeetle 2 ESP32-S3 (N4), DFR1145. GPIO numbers, not D-label numbers.
// Arduino IDE: ESP32 by Espressif + Adafruit BNO055 and its dependencies.
// Encoder module supply/logic and BNO breakout voltage MUST be checked first.
// C pins are intentionally unused. No motor is driven by this sketch.
// Continuous ANGLES frames; use cube_bridge.py with the updated Hallway game.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO055.h>
#include <Preferences.h>
#include <math.h>
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select an ESP32-S3 board for FireBeetle 2 S3 N4."
#endif

struct Encoder {
  uint8_t a,b;
  char face;
  int8_t direction;
  volatile uint8_t previous;
  volatile int32_t count;
  volatile uint32_t invalid;
};
// Physical BACK->game A; RIGHT->B; LEFT->C; BOTTOM->D; TOP->E.
Encoder enc[5]={{4,5,'A',1,0,0,0},{6,8,'B',1,0,0,0},
               {10,11,'C',1,0,0,0},{7,18,'D',1,0,0,0},{9,14,'E',1,0,0,0}};
DRAM_ATTR const int8_t steps[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
Preferences prefs;
int32_t countsPerRev[5]={}; // Measured x4 counts, not advertised pulses/detents.
int32_t emittedQuarter[5]={},previousCounts[5]={};
bool playing=false, bnoOK=false;
Adafruit_BNO055 imu28(55,0x28),imu29(55,0x29);
Adafruit_BNO055* bnoSensor=nullptr;
uint32_t lastMotion=0,lastSample=0,lastPrint=0,lastShake=0,muteUntil=0,firstPeak=0;
uint8_t peaks=0;
uint32_t lastView=0,lastCenter=0;
bool centerPressed=false;
bool above=false;
constexpr float SHAKE_HIGH=6.0f, SHAKE_LOW=2.0f; // m/s^2, tune on real hardware.
constexpr uint32_t SHAKE_WINDOW=650, SHAKE_COOLDOWN=1500;
char command[96];uint8_t used=0;bool overflow=false;

void ARDUINO_ISR_ATTR readEncoder(void* arg) {
  Encoder* e=static_cast<Encoder*>(arg);
  portENTER_CRITICAL_ISR(&lock);
  uint8_t now=(digitalRead(e->a)<<1)|digitalRead(e->b);
  if ((now^e->previous)==3)++e->invalid;
  e->count+=steps[(e->previous<<2)|now]*e->direction;
  e->previous=now;
  portEXIT_CRITICAL_ISR(&lock);
}
void snapshot(int32_t* counts,uint32_t* errors) {
  portENTER_CRITICAL(&lock);
  for(int i=0;i<5;++i){counts[i]=enc[i].count;errors[i]=enc[i].invalid;}
  portEXIT_CRITICAL(&lock);
}
void zeroCounts() {
  portENTER_CRITICAL(&lock);
  for(int i=0;i<5;++i){enc[i].count=0;enc[i].invalid=0;}
  portEXIT_CRITICAL(&lock);
  for(int i=0;i<5;++i){emittedQuarter[i]=0;previousCounts[i]=0;}
  peaks=0;above=false;lastMotion=millis();
}
bool configured() {
  for(int i=0;i<5;++i)if(countsPerRev[i]<4||countsPerRev[i]>100000||countsPerRev[i]%4)return false;
  return true;
}
void handleCommand() {
  if(!strcmp(command,"CAL")){playing=false;zeroCounts();Serial.println("# CAL: rotate each face exactly one revolution; read counts.");}
  else if(!strcmp(command,"ZERO")){playing=false;zeroCounts();Serial.println("# Zero set. Reset the idle game separately, then send PLAY.");}
  else if(!strcmp(command,"PLAY")){playing=configured();Serial.println(playing?"# PLAY continuous mode":"# Set CPR first: measured x4 counts per revolution, each divisible by four.");}
  else if(!strncmp(command,"CPR,",4)) {
    long v[5];char trailing;
    if(sscanf(command,"CPR,%ld,%ld,%ld,%ld,%ld%c",&v[0],&v[1],&v[2],&v[3],&v[4],&trailing)==5) {
      bool good=true;for(int i=0;i<5;++i)good&=v[i]>=4&&v[i]<=100000&&v[i]%4==0;
      if(good){for(int i=0;i<5;++i){countsPerRev[i]=v[i];char key[8];snprintf(key,sizeof(key),"cpr%d",i);prefs.putInt(key,v[i]);}playing=false;zeroCounts();Serial.println("# CPR saved. Align physical zero and game zero before PLAY.");}
      else Serial.println("# Invalid CPR");
    }
  }
  else if(!strncmp(command,"HAPTIC,",7)) {
    // Reserve a feedback channel. Suppress detection if a future driver shakes the box.
    muteUntil=millis()+1800;peaks=0;above=false;
  }
}
void setup() {
  Serial.begin(115200);prefs.begin("roomcube",false);
  pinMode(47,INPUT_PULLUP); // D14 user button: hold box neutral and press to center view.
  for(int i=0;i<5;++i){
    char key[8];snprintf(key,sizeof(key),"cpr%d",i);countsPerRev[i]=prefs.getInt(key,0);
    pinMode(enc[i].a,INPUT_PULLUP);pinMode(enc[i].b,INPUT_PULLUP);
    enc[i].previous=(digitalRead(enc[i].a)<<1)|digitalRead(enc[i].b);
    attachInterruptArg(enc[i].a,readEncoder,&enc[i],CHANGE);
    attachInterruptArg(enc[i].b,readEncoder,&enc[i],CHANGE);
  }
  Wire.begin(1,2);Wire.setClock(100000);Wire.setTimeOut(30);
  if(imu28.begin(OPERATION_MODE_IMUPLUS))bnoSensor=&imu28;
  else if(imu29.begin(OPERATION_MODE_IMUPLUS))bnoSensor=&imu29;
  bnoOK=bnoSensor!=nullptr;
  Serial.println(bnoOK?"# BNO055 ready":"# BNO055 unavailable: encoders still work; shake disabled");
  zeroCounts();muteUntil=millis()+3000;
  Serial.println("# Commands: CAL / CPR,nA,nB,nC,nD,nE / ZERO / PLAY. Defaults to CAL.");
}
void loop() {
  uint32_t now=millis();
  bool pressed=digitalRead(47)==LOW;
  if(pressed&&!centerPressed&&now-lastCenter>300){Serial.println("CENTER");lastCenter=now;}
  centerPressed=pressed;
  while(Serial.available()){
    char c=Serial.read();if(c=='\r')continue;
    if(c=='\n'){if(!overflow){command[used]='\0';handleCommand();}used=0;overflow=false;}
    else if(used<sizeof(command)-1&&!overflow)command[used++]=c;
    else overflow=true;
  }
  int32_t counts[5];uint32_t errors[5];snapshot(counts,errors);
  for(int i=0;i<5;++i){
    if(counts[i]!=previousCounts[i]){lastMotion=now;previousCounts[i]=counts[i];}
  }
  if(playing&&now-lastPrint>=20){
    lastPrint=now;Serial.print("ANGLES");
    for(int i=0;i<5;++i){Serial.print(',');Serial.print((counts[i]%countsPerRev[i])*360.0/countsPerRev[i],3);}
    Serial.println();
  }
  if(!playing&&now-lastPrint>=250){lastPrint=now;for(int i=0;i<5;++i){Serial.print("# COUNT,");Serial.print(enc[i].face);Serial.print(',');Serial.print(counts[i]);Serial.print(",INVALID,");Serial.println(errors[i]);}}
  if(bnoOK&&now-lastSample>=10){
    lastSample=now;auto a=bnoSensor->getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    float magnitude=sqrtf(a.x()*a.x()+a.y()*a.y()+a.z()*a.z());
    if(now-lastView>=20 && int32_t(now-muteUntil)>=0 && isfinite(magnitude) && magnitude<SHAKE_LOW){
      lastView=now;auto q=bnoSensor->getQuat();
      Serial.print("ORIENT,");Serial.print(q.w(),6);Serial.print(',');Serial.print(q.x(),6);
      Serial.print(',');Serial.print(q.y(),6);Serial.print(',');Serial.println(q.z(),6);
    }
    bool allowed=playing && int32_t(now-muteUntil)>=0 && now-lastMotion>250 && now-lastShake>SHAKE_COOLDOWN;
    if(!allowed||!isfinite(magnitude)){peaks=0;above=false;return;}
    if(peaks&&now-firstPeak>SHAKE_WINDOW)peaks=0;
    if(magnitude<SHAKE_LOW)above=false;
    if(magnitude>SHAKE_HIGH&&!above){
      above=true;if(!peaks){firstPeak=now;peaks=1;}
      else if(now-firstPeak>=70){
        Serial.println("SHAKE");lastShake=now;peaks=0;
      }
    }
  }
}
