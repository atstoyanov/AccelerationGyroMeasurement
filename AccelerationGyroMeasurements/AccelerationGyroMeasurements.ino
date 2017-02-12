#include "Wire.h"
#include "Keypad.h"
#include "OLED_I2C.h"
#include <MsTimer2.h>

#define BUTTON_1 '1'
#define BUTTON_2 '2'
#define BUTTON_3 '3'
#define BUTTON_4 '4'

#define INTERRUPT_PIN 2
#define LED_PIN 13

#define FONT_WIDTH 6
#define FONT_HEIGHT 8
#define DISPLAY_WIDTH_PIXELS 128
#define DISPLAY_HEIGHT_PIXELS 64

#define BUTTON_1 '1'
#define BUTTON_2 '2'
#define BUTTON_3 '3'
#define BUTTON_4 '4'

#define MENU_HOME 1
#define MENU_HOME_MAX 2

#define ADXL345_ID                  0x01
#define ADXL345_ADDRESS             0x53
#define ADXL345_RA_POWER_CTL        0x2D
#define ADXL345_RA_DATA_FORMAT      0x31
#define ADXL345_RA_DATAX0           0x32
#define ADXL345_RA_DATAX1           0x33
#define ADXL345_RA_DATAY0           0x34
#define ADXL345_RA_DATAY1           0x35
#define ADXL345_RA_DATAZ0           0x36
#define ADXL345_RA_DATAZ1           0x37

#define MPU6050_ID                  0x00
#define MPU6050_ADDRESS 0x68
#define MPU6050_RA_GYRO_CONFIG 0x1B
#define MPU6050_RA_ACCEL_CONFIG 0x1C

#define LED_PIN 13

char *labelsXYZ[] = {"x=", "y=", "z="};
char *labelsYPR[] = {"pitch=", "roll=", "yaw="};

// Брой редове на клавиатурата
const byte ROWS = 2;
// Брой колони на клавиатурата
const byte COLS = 2;

// Описание на стойностите, на които отговарят бутоните на клавиатурата
char keys[ROWS][COLS] = {
    {'4', '3'},
    {'2', '1'}};

//пинове към които са свързани редовете на клавиатурата
byte rowPins[ROWS] = {4, 5};
//пинове към които са свързани колоните на клавиатурата
byte colPins[COLS] = {6, 7};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

OLED display(SDA, SCL, 10);
extern uint8_t SmallFont[];

long rawAcc[3];    // данни от акселерометъра в чист вид.
long rawGyro[3];   // данни от жироскопа в чист вид.
float realAcc[3];  // показания на акселерометъра в g.
float realGyro[3]; // показания на жироскопа в deg/s.

unsigned long interval; // интервал от време изминал от последното изчисление
float RwAcc[3];         // проекция нормализирания вектор на гравитацията върху x/y/z осите, измерен от акселерометъра.
float RwGyro[3];        // Rw получен при последните изчисления
float Awz[2];           // ъглите между проекцията на R върху  XZ/YZ равнините и оста Z (deg).

float RwEst[3]; //Rw изчислен от комбинирането на RwAcc и RwGyro

unsigned long lastMicros;
int temperature; //  използва се само при вземане на данните от MPU 6050.

float wGyro; // коефициент показващ каква тежест имат резултатите от жироскопа

// отмествания от реалната стойност за акселерометъра
long accOffsetX, accOffsetY, accOffsetZ;
// отмествания от реалната стойности за жироскопа
long gyroOffsetX, gyroOffsetY, gyroOffsetZ;

volatile bool updateDisplayNeeded = false; // показва дали данните на дисплея трябва да се опреснят.
volatile bool needCalibration = false;
volatile byte buttonPressed = 0;
volatile byte buttonHolded = 0;

bool firstSample = true; // Първо изчисление или не?

int accRange = 0, gyroRange = 3; // текущ обхват на акселерометъра и жироскопа

char ADXL345Name[] = "ADXL345";
char MPU6050Name[] = "MPU6050";

struct {
  byte id; // 0 MPU6050; 1 ADXL345
  char *name;
  int gRange;
  int aRange;

} sensor;

void interrupt()
{
  updateDisplayNeeded = true;
}

void setup()
{
  //Стартиране на серийната комуникация. Скорост 38400 bps
  Serial.begin(38400);
  // Издаване на съобщение за старт на инициализацията
  Serial.println("Initialization ...");

  // Стартиране на връзката с дисплея.
  display.begin();
  display.setFont(SmallFont);

  //Стартиране на I2C в режим мастър.
  Wire.begin();

  // конфигуриране на LED пина, като изход.
  pinMode(LED_PIN, OUTPUT);

  //Конфигуриране на MPU6050.
  setupMPU6050();
  
  //Конфигуриране на ADXL345.
  setupADXL345();

  //Калибриране на сензорите.
  calibrate();

  //Конфигуриране на клавиатурата
  keypad.addEventListener(keypadEvent);
  keypad.setHoldTime(1000);

  wGyro = 10;

  //Стартиране на брояча за опресняване на дисплея
  MsTimer2::set(100, interrupt);
  MsTimer2::start();

  // избор на активен сензор
  setActiveSensor(MPU6050_ID);

  //Съобщение в серийната конзола, че инициализацията е завършила
  Serial.println("Done!");
}

void loop()
{
  // Прочита се състоянието на бутоните
  readButtons();
  // Отработва се състояниет на бутоните, ако има натиснат или задържан бутон
  processButtons();
  // чете се сензора 
  readSensor();
  // Конвертиране на данните от чист вид в удобен за ползавне
  // От акселерометъра в g.
  // От жироскопа в градуси за секунда (deg/s).
  rawToReal();
  // Изчислява се наклона на устройството.
  getEstimatedInclination();
  
  // Опреснява се информацията на дисплея
  updateDisplay();

  // Изпращт се данни през серийния интерфейс към PC.
  sendData();

  // Променя се състоянието на индикиращия LED. Той служи като индикация, че програмата не е спряла.
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void getEstimatedInclination()
{
  static int i, w;
  static float tmpf, tmpf2;
  static unsigned long newMicros;
  static char signRzGyro;

  // Запазва се времето когато е направено измерването
  newMicros = micros();

  // Изчисляване на интервала от време от предишното измерване
  interval = newMicros - lastMicros;
  // Запазване за следващото измерване. При първото измерване тази стойност ще е невалидна, но в този случай тя не влиза в изчисленията.
  lastMicros = newMicros;

  // Получаване на данните за акселерометъра в g.
  for (w = 0; w <= 2; w++)
  {
    // вектор RwAcc
    RwAcc[w] = realAcc[w];
  }

  // нормализиране на вектора (новият вектор има същата посока и дължина 1)
  normalize3DVector(RwAcc);

  if (firstSample)
  {
    // Първа стойност. Вземат се данните от акселерометъра
    for (w = 0; w <= 2; w++)
      RwEst[w] = RwAcc[w];
  }
  else
  {
    // Изчисляване на RwGyro вектора
    if (abs(RwEst[2]) < 0.1)
    {
      // Стойността на Rz е твърде малка, тъй като тази стойност се взема за референция за изчисляването на Axz, Ayz,  флуктоациите на нейната грешка, ще бъдат усилени,
      // което ще доведе до грешни резлутати. В този случай можем да я пренебрегнем и за тази цел ще използваме стойностите от предишното изчисление.
      for (w = 0; w <= 2; w++)
        RwGyro[w] = RwEst[w];
    }
    else
    {
      // изчисляване на ъглите между проекцията на R върху ZX/ZY и оста Z, базирани на последните изчисления RwEst
      for (w = 0; w <= 1; w++)
      {
        tmpf = realGyro[w] / 1000.0;                     // текуща стойност на жироскопа в deg/ms
        tmpf *= interval / 1000.0f;                      // промяна на ъгъла в градуси
        Awz[w] = atan2(RwEst[w], RwEst[2]) * RAD_TO_DEG; // изчисляване на ъглите в градуси
        Awz[w] += tmpf;                                  // нова стойност за ъгъла, спрямо жироскопа
      }
      // Определяне знака на RzGyro, като проверяваме в кой квадрант лежи ъгъла Axz.
      // RzGyro е положителен ако Axz е в границите -90 до 90 градуса => cos(Awz) >= 0
      signRzGyro = (cos(Awz[0] * DEG_TO_RAD) >= 0) ? 1 : -1;

      //Обратно пресмятане на RwGyro от ъглите Awz.
      for (w = 0; w <= 1; w++)
      {
        RwGyro[w] = sin(Awz[w] * DEG_TO_RAD);
        RwGyro[w] /= sqrt(1 + squared(cos(Awz[w] * DEG_TO_RAD)) * squared(tan(Awz[1 - w] * DEG_TO_RAD)));
      }
      RwGyro[2] = signRzGyro * sqrt(1 - squared(RwGyro[0]) - squared(RwGyro[1]));
    }

    // комбиниране на данните от Акселерометъра и жироскопа
    for (w = 0; w <= 2; w++)
      RwEst[w] = (RwAcc[w] + wGyro * RwGyro[w]) / (1 + wGyro);

    normalize3DVector(RwEst);
  }

  // Преизчисляване на ъглите след променените стойности, за визуализация.
  for (w = 0; w <= 1; w++)
  {
    Awz[w] = atan2(RwEst[w], RwEst[2]) * RAD_TO_DEG;
  }

  firstSample = false;
}

// функция за нормализиране на вектор.
void normalize3DVector(float *vector)
{
  static float R;
  R = sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
  vector[0] /= R;
  vector[1] /= R;
  vector[2] /= R;
}

// функция за изчесление на квадрат на число с плаваща запетая.
float squared(float x)
{
  return x * x;
}

void setupMPU6050()
{
  // изкарване на от режим Sleep MPU-6050
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(0x6B);
  Wire.write(0x01);
  Wire.endTransmission();
  // задаване на най-нисък обхват на работа
  setFullScaleAccRange(accRange);
  setFullScaleGyroRange(gyroRange); //2000 deg/s
}

void setFullScaleAccRange(uint8_t range)
{
  // задаване на обхват на акселерометъра
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(MPU6050_RA_ACCEL_CONFIG);
  // битове 3 и 4 определят обхвата
  Wire.write(range << 3);
  Wire.endTransmission();
}

void setFullScaleGyroRange(uint8_t range)
{
  // задаване на обхват на жироскопа
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(MPU6050_RA_GYRO_CONFIG);
  // битове 3 и 4 определят обхвата
  Wire.write(range << 3);
  Wire.endTransmission();
}

// четене на данните от MPU6050
void readMPU6050()
{
  // Начало на комуникацията с MPU-6050 по I2C.
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(0x3B);                      // Изпращане на адреса на регистъра от който ще се чете
  Wire.endTransmission();                // Край на предааването
  Wire.requestFrom(MPU6050_ADDRESS, 14); // Заявка за 14 байта от MPU-6050
  while (Wire.available() < 14)          // Изчакване докато бойтовете са получени
  {
  };
  // Измерените стойности от сензора са 16 битови. Четенето по I2C е на части от по 8 бита.
  // за това се налага събирането на два последователни байта в една стойностт.
  rawAcc[0] = Wire.read() << 8 | Wire.read();
  rawAcc[1] = Wire.read() << 8 | Wire.read();
  rawAcc[2] = Wire.read() << 8 | Wire.read();
  temperature = Wire.read() << 8 | Wire.read();
  rawGyro[0] = Wire.read() << 8 | Wire.read();
  rawGyro[1] = Wire.read() << 8 | Wire.read();
  rawGyro[2] = Wire.read() << 8 | Wire.read();
}

void setupADXL345() 
{
 
  Wire.beginTransmission(ADXL345_ADDRESS);
  Wire.write(ADXL345_RA_POWER_CTL);
  Wire.write(0x00);
  Wire.endTransmission();
  // включване на акселерометъра в режим на измерване
  Wire.beginTransmission(ADXL345_ADDRESS);
  Wire.write(ADXL345_RA_POWER_CTL);
  Wire.write(0x18); 
  Wire.endTransmission();
  setADXL345Range(accRange);
}

void setADXL345Range(uint8_t range)
{
  Wire.beginTransmission(ADXL345_ADDRESS);
  Wire.write(ADXL345_RA_DATA_FORMAT);
  Wire.write(range);
  Wire.endTransmission();
}

void readADXL345()
{
  // Начало на комуникацията с MPU-6050 по I2C.
  Wire.beginTransmission(ADXL345_ADDRESS);
  Wire.write(ADXL345_RA_DATAX0);                      // Изпращане на адреса на регистъра от който ще се чете
  Wire.endTransmission();                // Край на предааването
  Wire.requestFrom(ADXL345_ADDRESS, 6); // Заявка за 6 байта от ADXL345
  while (Wire.available() < 6)          // Изчакване докато бойтовете са получени
  {
  };
  // Измерените стойности от сензора са 16 битови. Четенето по I2C е на части от по 8 бита.
  // за това се налага събирането на два последователни байта в една стойностт.
  rawAcc[0] = Wire.read() | Wire.read() << 8;
  rawAcc[1] = Wire.read() | Wire.read() << 8;
  rawAcc[2] = Wire.read() | Wire.read() << 8;
  temperature = Wire.read() << 8 | Wire.read();
  rawGyro[0] = 0;
  rawGyro[1] = 0;
  rawGyro[2] = 0;
}

void rawToReal()
{
  for (int i = 0; i < 3; i++)
  {
    realGyro[i] = rawToRealGyro(rawGyro[i]);
    realAcc[i] = rawToRealAcc(rawAcc[i]);
  }
}

// рестарт
void reset()
{
  firstSample = true;
  accRange = 0;
  gyroRange = 3;
  setFullScaleAccRange(accRange);
  setFullScaleGyroRange(gyroRange);
}

// показване на данните на екрана
void displayReadings()
{

  display.print("Accel", colPos(0), rowPos(2));
  display.print("Angles", colPos(9), rowPos(2));

  for (int i = 0; i < 3; i++)
  {
    display.print(labelsXYZ[i], colPos(0), rowPos(3 + i));
    if (realAcc[i] < 0.0)
      display.print("-", colPos(2), rowPos(3 + i));
    display.printNumF(abs(realAcc[i]), 2, colPos(3), rowPos(3 + i));
  }

  for (int i = 0; i < 2; i++)
  {
    display.print(labelsYPR[i], colPos(9), rowPos(3 + i));
    if (Awz[i] < 0.0)
      display.print("-", colPos(15), rowPos(3 + i));
    display.printNumF(abs(Awz[i]), 2, colPos(16), rowPos(3 + i));
  }
}

// изчисляване на позицията на редовете на екрана
int rowPos(int row)
{
  return row * FONT_HEIGHT;
}

// изчисляване на позицията на колоните на екрана
int colPos(int col)
{
  return col * FONT_WIDTH;
}

// показване на най-долния ред с текст над бутоните.
void displayButtons()
{
  display.drawRect(0, 63 - 10, 127, 63);
  display.drawLine(31, 63 - 10, 31, 63);
  display.drawLine(63, 63 - 10, 63, 63);
  display.drawLine(95, 63 - 10, 95, 63);
  display.print("Mode", 2, 63 - 8);
  display.print("Rst", 97, 63 - 8);
}

void calibrateGyro()
{

  display.clrScr();
  display.print("Calibrating gyro", 0, 0);
  display.update();

  for (int cal_int = 0; cal_int < 500; cal_int++)
  { 
    // Взимаме 500 стойности
    readMPU6050(); 
    gyroOffsetX += rawGyro[0];    
    gyroOffsetY += rawGyro[1];    
    gyroOffsetZ += rawGyro[2];    
    delay(3);                     // Забавяне от 3 микро секунди.
  }

  // изчисляваме средната стойност от събраните данни
  gyroOffsetX /= 500; 
  gyroOffsetY /= 500; 
  gyroOffsetZ /= 500; 
}

void updateDisplay()
{
  if (!updateDisplayNeeded)
    return;

  updateDisplayNeeded = false;

  display.clrScr();

  displayButtons();

  displayReadings();
  display.print(sensor.name, CENTER, 0);
    
  display.update();
}

// изчисляване на показанията на акселерометъра в g. Обхвата се взема в предвид.
double rawToRealAcc(int16_t value)
{
  return (double)value / (sensor.aRange / pow(2, accRange));
}

// изчисляване на показанията на жироскопа в deg/s. Обхвата се взема в предвид.
double rawToRealGyro(int16_t value)
{
  return (double)value / (sensor.gRange / (250 * pow(2, gyroRange)));
}

void readButtons()
{
  char key = keypad.getKey();
}

void keypadEvent(KeypadEvent key)
{
  switch (keypad.getState())
  {
  case PRESSED:
    break;
  case RELEASED:
    buttonPressed = key;
  case HOLD:
    buttonHolded = key;
  }
}

void processButtons()
{
  if (!processHoldButton())
    processPressButton();
}

bool processHoldButton()
{
  if (buttonHolded)
  {
    switch (buttonHolded)
    {
    case BUTTON_1:
      calibrate();
      break;
    case BUTTON_2:
      changeConnection();
      break;
    case BUTTON_3:
      break;
    case BUTTON_4:
      break;
    }
    buttonHolded = 0;
    buttonPressed = 0;
    return true;
  }
  return false;
}

void processPressButton()
{
  if (buttonPressed)
  {
    switch (buttonPressed)
    {
    case BUTTON_1:
      Serial.println("Resetting ...");
      break;
    case BUTTON_2:
      changeRange();
      break;
    case BUTTON_3:
      changeSensor();
      break;
    case BUTTON_4:
      changeMode();
      break;
    }
    buttonPressed = 0;
  }
}

void calibrate()
{
  Serial.println("Calibrating ...");
}

void changeSensor()
{
  Serial.println("Changing sensors ...");
}

void changeMode()
{
  Serial.println("Changing mode ...");
}

void changeConnection()
{
  Serial.println("Changing connection ...");
}

void changeRange()
{
  Serial.println("Changing range ...");
}

void sendData()
{

}

void readSensor()
{
  if(sensor.id == MPU6050_ID)
    readMPU6050();
  else
    readADXL345();
}

void setActiveSensor(byte id)
{
  if(id == MPU6050_ID)
  {
    sensor.id = id;
    sensor.name = MPU6050Name;
    sensor.gRange = 32768;
    sensor.aRange = 16384;
  }
  else if(id == ADXL345_ID)
  {
    sensor.id = id;
    sensor.name = ADXL345Name;
    sensor.gRange = 512; //не може да бъде нула
    sensor.aRange = 256;
  }
}