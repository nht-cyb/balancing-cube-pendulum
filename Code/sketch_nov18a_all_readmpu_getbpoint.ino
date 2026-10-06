#include <EEPROM.h>

#include <BTAddress.h>
#include <BTAdvertisedDevice.h>
#include <BTScan.h>
#include <BluetoothSerial.h>

#include <Wire.h>

BluetoothSerial SerialBT;

#define BUZZER        27
#define VBAT          34

#define BRAKE         26        //chan phanh chung cua 3 dong co

//khai bao chan dong co 1
#define DIR1          4         //chan tin hieu chuyen huong xoay dong co
#define PWM1          32        //chan truyen xung toc do xoay 
#define PWM1_CH       1         //khai bao kenh cua dong co

//khai bao chan dong co 2
#define DIR2          15
#define PWM2          25
#define PWM2_CH       0

//khai bao chan dong co 3
#define DIR3          5
#define PWM3          18
#define PWM3_CH       2

#define TIMER_BIT     8
#define BASE_FREQ     20000       //tan so hoat dong cua dong co

//khai bao cam bien MPU
#define SDA           21
#define SCL           22
#define CLK_SPD       100000
#define MPU6050       0x68        //dia chi thiet bi
#define ACCEL_CONFIG  0x1C        //dia chi kenh kenh do gia toc o cam bien
#define GYRO_CONFIG   0x1B        //dia chi kenh do van toc goc cua cam bien

#define PWR_MGMT_1    0x6B
#define PWR_MGMT_2    0x6C

//tham so can chinh cam bien
#define accSens       0           //0 = 2g, 1 = 4g, 2 = 8g, 3 = 16g
#define gyroSens      1           //0 = 250 rad/s, 1 = 500rad/s
                                  //2 = 1000rad/s, 3 = 2000rad/s

#define EEPROM_SIZE   64

float Gyro_amount = 0.1;

bool vertical = false;            //khai bao bien VTCB
int balancing_point = 0;
bool calibrating = false;         //bien qua trinh co dang luu VTCB hay k
bool calibrated = false;          //bien ktra da luu VTCB hay chua

// tham so dieu khien LQR
float K1 = 160;
float K2 = 10.50;
float K3 = 0.03;
int loop_time = 10;

struct OffsetObj{
  int ID1;          //VTCB dinh khoi lap phuong
  float X1;         //X va Y la hai gtri goc cua robot tai VTCB, bieu dien toa do cua robot trong kg
  float Y1;         //X = robot_angleX
                    //Y = robot_angleY
  int ID2;          //VTCB canh thu nhat
  float X2;
  float Y2;

  int ID3;          //VTCB canh thu hai
  float X3;
  float Y3;

  int ID4;          //VTCB canh thu ba
  float X4;
  float Y4;
};

OffsetObj offsets;

float alpha = 0.74;

int16_t AcX, AcY, AcZ, GyX, GyY, GyZ;
int16_t gyroX, gyroY, gyroZ, gyroYfilt, gyroZfilt;

int16_t GyZ_offset = 0;
int16_t GyY_offset = 0;
int16_t GyX_offset = 0;

int16_t GyZ_offset_sum = 0;
int16_t GyY_offset_sum = 0;
int16_t GyX_offset_sum = 0;

float robot_angleX, robot_angleY;
float angleX, angleY;
float Acc_angleX, Acc_angleY;

int32_t motor_speed_X, motor_speed_Y;

long currentT, previousT_1, previousT_2 = 0;

void writeTo(byte device, byte address, byte value)
{
  Wire.beginTransmission(device);
  Wire.write(address);
  Wire.write(value);
  Wire.endTransmission(true);
}

void beep(){
  digitalWrite(BUZZER, HIGH);
  delay(70);
  digitalWrite(BUZZER, LOW);
  delay(80);
}

void save(){
  EEPROM.put(0, offsets);
  EEPROM.commit();
  EEPROM.get(0, offsets);
  if(offsets.ID1 == 99 && offsets.ID2 == 99 && offsets.ID3 == 99 && offsets.ID4 == 99){
    calibrating = true;
  }
  calibrating = false;
  Serial.println("calibrating off");
  beep();
}

void angle_setup(){
  Wire.begin();
  delay(100);
  writeTo(MPU6050, PWR_MGMT_1, 0);                  //specifying output scaling for accelerometer and gyroscrope
  writeTo(MPU6050, ACCEL_CONFIG, accSens << 3);     //dat gia tri do cho gia toc ke
  writeTo(MPU6050, GYRO_CONFIG, gyroSens << 3);
  delay(100);

  for(int i = 0; i < 1024; i++){
    angle_calc();
    GyZ_offset_sum += GyZ;
    delay(3);
  }
  GyZ_offset = GyZ_offset_sum >> 10;
  Serial.print("GyZ offset value = ");
  Serial.println(GyZ_offset);
  beep();

  for(int i = 0; i < 1024; i++){
    angle_calc();
    GyY_offset_sum += GyY;
    delay(3);
  }
  GyY_offset = GyY_offset_sum >> 10;
  Serial.print("GyY offset value = ");
  Serial.println(GyY_offset);
  beep();

  for(int i = 0; i < 1024; i++){
    angle_calc();
    GyX_offset_sum += GyX;
    delay(3);
  }
  GyX_offset = GyX_offset_sum >> 10;
  Serial.print("GyX offset value = ");
  Serial.println(GyX_offset);
  beep();
  beep();
}

void angle_calc(){
  Wire.beginTransmission(MPU6050);
  Wire.write(0x43);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050, 6, true);
  GyX = Wire.read() << 8 | Wire.read();  //0x43(GYRO_XOUT_H) && 0x44(GYRO_XOUT_L)
  GyY = Wire.read() << 8 | Wire.read();  //0x45(GYRO_XOUT_H) && 0x46(GYRO_XOUT_L)
  GyZ = Wire.read() << 8 | Wire.read();  //0x47(GYRO_XOUT_H) && 0x48(GYRO_XOUT_L)
//  Serial.print("GyX value = ");Serial.println(GyX);
//  Serial.print("GyY value = ");Serial.println(GyY);
//  Serial.print("GyZ value = ");Serial.println(GyZ);

  Wire.beginTransmission(MPU6050);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050, 6, true);
  AcX = Wire.read() << 8 | Wire.read();  //0x3B(ACCEL_XOUT_H) && 0x3C(ACCEL_XOUT_L)
  AcY = Wire.read() << 8 | Wire.read();  //0x3D(ACCEL_XOUT_H) && 0x3E(ACCEL_XOUT_L)
  AcZ = Wire.read() << 8 | Wire.read();  //0x3F(ACCEL_XOUT_H) && 0x40(ACCEL_XOUT_L)
//  Serial.print("AcX value = ");Serial.println(AcX);
//  Serial.print("AcY value = ");Serial.println(AcY);
//  Serial.print("AcZ value = ");Serial.println(AcZ);

  GyZ -= GyZ_offset;      //add MPU offset values
  GyY -= GyY_offset;
  GyX -= GyX_offset;

  robot_angleX += GyZ * loop_time / 1000 / 65.536;
  Acc_angleX = atan2(AcY, - AcX) * 57.2958;     //angle from acc. value * 57.2958 (deg/rad)
  robot_angleX = robot_angleX * Gyro_amount + Acc_angleX * (1.0 - Gyro_amount);

  robot_angleY += GyY * loop_time / 1000 / 65.536;
  Acc_angleY = -atan2(AcZ, - AcX) * 57.2958;     //angle from acc. value * 57.2958 (deg/rad)
  robot_angleY = robot_angleY * Gyro_amount + Acc_angleY * (1.0 - Gyro_amount);  

  angleX = robot_angleX;
  angleY = robot_angleY;
  Serial.print("AngleX: "); Serial.print(angleX); 
  Serial.print("AngleY: "); Serial.println(angleY); 

  if(abs(angleX - offsets.X2) < 2 && abs(angleY - offsets.Y2) < 0.6){
    balancing_point = 2;
    if(!vertical) beep();
    vertical = true;
  }
  else if(abs(angleX - offsets.X3) < 2 && abs(angleY - offsets.Y3) < 0.6){
    balancing_point = 3;
    if(!vertical) beep();
    vertical = true;
  }
  else if(abs(angleX - offsets.X4) < 0.6 && abs(angleY - offsets.Y4) < 2){
    balancing_point = 4;
    if(!vertical) beep();
    vertical = true;
  }
  else if(abs(angleX - offsets.X1) < 0.4 && abs(angleY - offsets.Y1) < 0.4){
    balancing_point = 1;
    if(!vertical) beep();
    vertical = true;
  }
}

void XY_to_threeWay(float pwm_X, float pwm_Y){
  int16_t m1 = round(0.5 * pwm_X - 0.75 * pwm_Y);
  int16_t m2 = round(0.5 * pwm_X + 0.75 * pwm_Y);
  int16_t m3 = - pwm_X;

  m1 = constrain(m1, -255, 255);
  m2 = constrain(m2, -255, 255);
  m3 = constrain(m3, -255, 255);

  Motor1_control(m1);
  Motor2_control(m2);
  Motor3_control(m3);
}

void batteryVoltage(double voltage){
  //Serial.print("batt: "); Serial.println(voltage);
  if(voltage > 8 && voltage <= 9.5){
    digitalWrite(BUZZER, HIGH);
  }
  else{
    digitalWrite(BUZZER, LOW);
  }
}

//ham truyen tin hieu pwm vao kenh cua dong co
void pwmSet(uint8_t channel, uint32_t value){
  ledcWrite(channel, value);
}

//ham dieu khien dong co
void Motor1_control(int sp){
  if(sp < 0){
    digitalWrite(DIR1, LOW);
  sp = -sp;
  }
  else{
    digitalWrite(DIR1, HIGH);
  }
  pwmSet(PWM1_CH, sp > 255 ? 255 : 255 - sp);
}

void Motor2_control(int sp){
  if(sp < 0){
    digitalWrite(DIR2, LOW);
  sp = -sp;
  }
  else{
    digitalWrite(DIR2, HIGH);
  }
  pwmSet(PWM2_CH, sp > 255 ? 255 : 255 - sp);
}

void Motor3_control(int sp){
  if(sp < 0){
    digitalWrite(DIR3, LOW);
  sp = -sp;
  }
  else{
    digitalWrite(DIR3, HIGH);
  }
  pwmSet(PWM3_CH, sp > 255 ? 255 : 255 - sp);
}

int Tuning(){
  if(!SerialBT.available())
    return 0;
  char param = SerialBT.read();      //get parameter byte
  if(!SerialBT.available())
    return 0;
  char cmd = SerialBT.read(); 
  switch(param){
    case 'p':
      if(cmd == '+')    K1 += 1;
      if(cmd == '-')    K1 -= 1;
      printValues();
      break;
    case 'i':
      if(cmd == '+')    K2 += 0.05;
      if(cmd == '-')    K2 -= 0.05;
      printValues();
      break;
    case 's':
      if(cmd == '+')    K3 += 0.05;
      if(cmd == '-')    K3 -= 0.05;
      printValues();
      break;
    case 'c':
      if(cmd == '+' && !calibrating){
        calibrating  = true;
        SerialBT.println("calibrating on");
      }  
      if(cmd == '-' && calibrating){
        SerialBT.print("X: "); SerialBT.print(robot_angleX);
        SerialBT.print("Y: "); SerialBT.print(robot_angleY);
        if(abs(robot_angleX) < 10 && abs(robot_angleY) < 10){
          offsets.ID1 = 99;
          offsets.X1 = robot_angleX;
          offsets.Y1 = robot_angleY;
          SerialBT.println("Vertex Equilibrium.");
          save();
        }
        else if(robot_angleX > - 45 && robot_angleX < -25 && robot_angleY > - 30 && robot_angleY < -10){
          offsets.ID2 = 99;
          offsets.X2 = robot_angleX;
          offsets.Y2 = robot_angleY;
          SerialBT.println("First Edge Equilibrium.");
          save();
        }
        else if(robot_angleX > 20 && robot_angleX < 40 && robot_angleY > - 30 && robot_angleY < -10){
          offsets.ID3 = 99;
          offsets.X3 = robot_angleX;
          offsets.Y3 = robot_angleY;
          SerialBT.println("Second Edge Equilibrium.");
          save();
        }
        else if(abs(robot_angleX) <15 && robot_angleY > 30 && robot_angleY < 50){
          offsets.ID4 = 99;
          offsets.X4 = robot_angleX;
          offsets.Y4 = robot_angleY;
          SerialBT.println("Third Edge Equilibrium.");
          save();
        }
        else{
          SerialBT.println("The angles are wrong");
          beep();
          beep();
        }        
      } 
      break;
  }
}

void printValues(){
  SerialBT.print("K1: "); SerialBT.print(K1);
  SerialBT.print("K2: "); SerialBT.print(K2);
  SerialBT.print("K3: "); SerialBT.println(K3,4);  
}

void setup(){
  // put your setup code here, to run once:
  Serial.begin(115200);
  SerialBT.begin("NHT-ESP32");      //ten thiet bi bluetooth
  EEPROM.begin(EEPROM_SIZE);

  pinMode(BUZZER, OUTPUT);

  Wire.begin(SDA, SCL, CLK_SPD); // sda, scl, clock speed
  Wire.beginTransmission(MPU6050);
  Wire.write(PWR_MGMT_1);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU−6050)
  Wire.endTransmission(true);
  

  pinMode(BRAKE, OUTPUT);
  digitalWrite(BRAKE, HIGH);

  pinMode(DIR1, OUTPUT);
  ledcSetup(PWM1_CH, BASE_FREQ, TIMER_BIT);
  ledcAttachPin(PWM1, PWM1_CH);
  Motor1_control(0);

  pinMode(DIR2, OUTPUT);
  ledcSetup(PWM2_CH, BASE_FREQ, TIMER_BIT);
  ledcAttachPin(PWM2, PWM2_CH);
  Motor2_control(0);

  pinMode(DIR3, OUTPUT);
  ledcSetup(PWM3_CH, BASE_FREQ, TIMER_BIT);
  ledcAttachPin(PWM3, PWM3_CH);
  Motor3_control(0);

  EEPROM.get(0, offsets);
  if(offsets.ID1 =- 99 && offsets.ID2 == 99 && offsets.ID3 == 99 && offsets.ID4 == 99)
    calibrated = true;
  else calibrated = false;

    delay(2000);
    digitalWrite(BUZZER, HIGH);
    delay(70);
    digitalWrite(BUZZER, LOW);
    angle_setup();
    
    Serial.println("Setup complete");
}

void loop() {

  // put your main code here, to run repeatedly:
  currentT = millis();

  if(currentT - previousT_1 >= loop_time){
    Tuning();
    angle_calc();
    if(balancing_point == 1){
      angleX -= offsets.X1;
      angleY -= offsets.Y1;
      if(abs(angleX) > 8 || abs(angleY) > 8 )
        vertical = false;
    }
    else if(balancing_point == 2){
      angleX -= offsets.X2;
      angleY -= offsets.Y2;
      if(abs(angleY) >5)
        vertical = false;
    }
    else if(balancing_point == 3){
      angleX -= offsets.X3;
      angleY -= offsets.Y3;
      if(abs(angleY) > 5) 
        vertical = false;
    }
    else if(balancing_point == 4){
      angleX -= offsets.X4;
      angleY -= offsets.Y4;
      if(abs(angleX) > 5)
        vertical = false;
    }

    if(abs(angleX) < 8 || abs(angleY) < 8){
      Gyro_amount = 0.996;          //fast restore angle
    }      
    else 
      Gyro_amount = 0.1;

  if(vertical && calibrated && !calibrating){
    digitalWrite(BRAKE, HIGH);
    gyroZ = GyZ / 131.0;              //convert to deg/s
    gyroY = GyY / 131.0;              //angular veclocity 
    gyroX = GyX / 131.0;

    gyroYfilt = alpha * gyroY + (1 - alpha) * gyroYfilt;
    gyroZfilt = alpha * gyroZ + (1 - alpha) * gyroZfilt;

    int pwm_X = constrain(K1 * angleX + K2 * gyroZfilt + K3 * motor_speed_X, -255, 255);
    int pwm_Y = constrain(K1 * angleY + K2 * gyroYfilt + K3 * motor_speed_Y, -255, 255);
    motor_speed_X += pwm_X;
    motor_speed_Y += pwm_Y;

    if(balancing_point == 1){
    //  XY_to_threeWay(-pwm_X, -pwm_Y);
    }    
    else if(balancing_point == 2){
    //  Motor1_control(pwm_Y);
    }
    else if(balancing_point == 3){
    //  Motor2_control(-pwm_Y);
    }
    else if(balancing_point == 4){
    //  Motor3_control(pwm_X);
    }
  }
  else{
    XY_to_threeWay(0,0);
    digitalWrite(BRAKE, LOW);
    motor_speed_X = 0;
    motor_speed_Y = 0;
  }
  previousT_1 = currentT;
  }

  if(currentT - previousT_2 >= 2000){
    batteryVoltage((double)analogRead(VBAT)/127);
    if(!calibrated && !calibrating){
      SerialBT.println("Calibrate the balancing point first...");
    } 
  previousT_2 = currentT;
 
 }
}
