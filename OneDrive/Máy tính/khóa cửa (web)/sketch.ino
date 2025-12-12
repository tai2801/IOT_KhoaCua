#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <WiFi.h>
#include <WebServer.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= KEY PAD =================
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= PINOUT =================
const int servoPin = 5;
const int buzzerPin = 15;
const int greenLED = 17;
const int redLED = 16;

// ================= Servo control =================
const unsigned long servoPeriodUs = 20000UL;
volatile int targetAngle = 0; // 0..180
unsigned long lastPulseMicros = 0;

// ================= Password Logic =================
String password = "1234";
String input = "";
bool changingPassword = false;
unsigned long unlockTime = 0;
const unsigned long changeWindow = 5000UL;

// ================= Security Lock =================
int failedAttempts = 0;
bool systemLocked = false;
unsigned long lockStartTime = 0;
const unsigned long lockDuration = 180000UL;

// ================= Door State =================
unsigned long openUntil = 0;

// ================= Web Server + Logs =================
WebServer server(80);
String logs[20];
int logIndex = 0;

void addLog(String msg) {
  logs[logIndex] = msg;
  logIndex = (logIndex + 1) % 20;
}

// ================= WiFi =================
const char* ssid = "Wokwi-GUEST";
const char* passwordWifi = "";

// ================= Servo Functions =================
uint16_t angleToPulseUs(int angle) {
  return map(constrain(angle, 0, 180), 0, 180, 1000, 2000);
}

void servoTick() {
  unsigned long now = micros();
  if (now - lastPulseMicros >= servoPeriodUs) {
    lastPulseMicros = now;
    uint16_t pw = angleToPulseUs(targetAngle);
    digitalWrite(servoPin, HIGH);
    delayMicroseconds(pw);
    digitalWrite(servoPin, LOW);
  }
}

// ================= Buzzer =================
void buzzerBeep(int freq, int duration_ms) {
  if (freq <= 0) { noTone(buzzerPin); return; }
  tone(buzzerPin, freq);
  delay(duration_ms);
  noTone(buzzerPin);
}

// ================= LCD =================
void showInputOnLCD() {
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  if (changingPassword) lcd.print("New:");
  lcd.print(input);
}

void enterIdleDisplay() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Nhap mat khau:");
  lcd.setCursor(0,1);
  lcd.print("                ");
}

void showLockedMessage() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("HE THONG BI KHOA!");
  lcd.setCursor(0,1);
  lcd.print("Cho 3 phut...");
}

// ================= HTML Giao diện Web =================
String htmlPage() {
  String page = "<html><head><meta charset='UTF-8'><title>Cửa thông minh</title></head><body>";

  page += "<h2>Trạng thái cửa: <b>";
  page += (targetAngle == 90) ? "ĐANG MỞ" : "ĐANG ĐÓNG";
  page += "</b></h2>";

  page += "<h3>Mở cửa</h3>";
  page += "<form action='/open'><button>MỞ CỬA</button></form>";

  page += "<h3>Đổi mật khẩu</h3>";
  page += "<form action='/change' method='POST'>";
  page += "Mật khẩu mới: <input name='newpw'><br><br>";
  page += "<button>Đổi</button></form>";

  page += "<h3>Lịch sử mở cửa</h3><ul>";
  for (int i = 0; i < 20; i++)
    if (logs[i].length() > 0) page += "<li>" + logs[i] + "</li>";
  page += "</ul>";

  page += "</body></html>";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleOpenDoor() {
  targetAngle = 90;
  openUntil = millis() + 2000;
  addLog("Door opened from Web");
  server.send(200, "text/html", htmlPage());
}

void handleChangePassword() {
  if (!server.hasArg("newpw")) {
    server.send(400, "text/plain", "Thieu tham so newpw!");
    return;
  }
  String newpw = server.arg("newpw");
  if (newpw.length() < 4 || newpw.length() > 12) {
    server.send(200, "text/html", "MK khong hop le!");
    return;
  }
  password = newpw;
  addLog("Password changed from Web");
  server.send(200, "text/html", "Doi MK thanh cong!<br><a href='/'>Back</a>");
}

// ================= Process Submit =================
void processSubmit() {
  if (systemLocked) {
    showLockedMessage();
    return;
  }

  if (changingPassword) {
    if (input.length() >= 4 && input.length() <= 12) {
      password = input;
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Doi MK thanh cong");
      buzzerBeep(800, 300);
      addLog("Password changed via Keypad");
    } else {
      lcd.clear(); lcd.setCursor(0,0); lcd.print("MK ko hop le");
      buzzerBeep(1000, 300);
    }
    delay(800);
    changingPassword = false;
    input = "";
    enterIdleDisplay();
    return;
  }

  // Check password
  if (input == password) {
    failedAttempts = 0;
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Mo khoa thanh cong!");
    buzzerBeep(600, 300);
    digitalWrite(greenLED, HIGH);
    targetAngle = 90;
    openUntil = millis() + 2000;
    unlockTime = millis();
    addLog("Door opened via Keypad");
  } else {
    failedAttempts++;
    lcd.clear(); lcd.print("SAI MAT KHAU!");
    buzzerBeep(1000, 300);

    if (failedAttempts >= 5) {
      systemLocked = true;
      lockStartTime = millis();
      showLockedMessage();
      addLog("SYSTEM LOCKED!");
    }
  }
  input = "";
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Wire.begin(21,22);
  lcd.init();
  lcd.backlight();

  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(servoPin, OUTPUT);

  enterIdleDisplay();

  // ====== WiFi ======
  WiFi.begin(ssid, passwordWifi);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());

  // ====== Web Server ======
  server.on("/", handleRoot);
  server.on("/open", handleOpenDoor);
  server.on("/change", handleChangePassword);
  server.begin();
  Serial.println("Web server started");
}

// ================= LOOP =================
void loop() {
  servoTick();
  server.handleClient();

  if (systemLocked && millis() - lockStartTime >= lockDuration) {
    systemLocked = false;
    failedAttempts = 0;
    addLog("SYSTEM UNLOCKED");
    enterIdleDisplay();
  }

  if (openUntil && millis() >= openUntil) {
    targetAngle = 0;
    digitalWrite(greenLED, LOW);
    openUntil = 0;
    enterIdleDisplay();
  }

  char key = keypad.getKey();
  if (!key) return;

  if (key == '#') { processSubmit(); return; }
  if (key == '*') { input = ""; enterIdleDisplay(); return; }
  if (key == 'D') { if (input.length()) input.remove(input.length()-1); showInputOnLCD(); return; }
  if (key == 'A') {
    if (millis() - unlockTime <= changeWindow) {
      changingPassword = true;
      input = "";
      lcd.clear(); lcd.print("Nhap MK moi:");
    }
    return;
  }

  if (input.length() < 12) input += key;
  showInputOnLCD();
}
