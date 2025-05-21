#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <MAX6675.h>

// Пины подключения
const int thermoCLK = 18;
const int thermoCS = 5;
const int thermoDO = 19;
const int heaterPin = 25; // реле или SSR
const char* ssid = "YourSSID";
const char* password = "YourPassword";

MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);
AsyncWebServer server(80);

unsigned long lastRead = 0;
float currentTemp = 0;
float targetTemp = 0;
int dutyCycle = 0;

struct ProgramStep {
  String mode;
  float set_temperature;
  unsigned long hold_time; // секунды
  int power;
};

std::vector<ProgramStep> program;
int currentStep = 0;
unsigned long stepStart = 0;
bool running = false;
unsigned long modeTime = 0;   // Время с начала режима
unsigned long leftTime = 0;   // Время удержания
unsigned long tempReachedTime = 0; // Время достижения целевой температуры

void setup() {
  Serial.begin(115200);
  pinMode(heaterPin, OUTPUT);
  digitalWrite(heaterPin, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("Connected: " + WiFi.localIP().toString());

  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount file system");
    return;
  }

  setupServer();
  server.begin();
}

void setupServer() {
  // Обработка запросов на главную страницу
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    // Отправка HTML страницы
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // Обработка статуса
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;

    // Собираем все данные о состоянии
    doc["temperature"] = currentTemp;
    doc["mode"] = running ? program[currentStep].mode : "останов";
    doc["set_temperature"] = targetTemp;
    doc["mode_time"] = modeTime; // Время с начала режима
    doc["left_time"] = leftTime; // Время удержания
    doc["total_time"] = (millis() - stepStart) / 1000; // Общее время с начала программы
    doc["duty_cycle"] = dutyCycle;

    String json;
    serializeJson(doc, json);  // Преобразуем объект в строку JSON
    request->send(200, "application/json", json);  // Отправляем JSON как ответ
  });

  // Обработка запроса на файл CSS
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });

  // Обработка запроса на файл JS
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/script.js", "application/javascript");
  });
}

void loop() {
  // Проверяем, прошло ли 1 секунда с последнего опроса температуры
  if (millis() - lastRead > 1000) {
    lastRead = millis();
    currentTemp = thermocouple.readCelsius(); // Чтение температуры
  }

  // Если программа запущена и текущий шаг существует
  if (running && currentStep < program.size()) {
    auto& step = program[currentStep];

    targetTemp = step.set_temperature;
    dutyCycle = step.power;

    // Запускаем нагрев, если температура меньше целевой
    if (currentTemp < targetTemp) {
      digitalWrite(heaterPin, HIGH); // Включить нагреватель
    } else {
      digitalWrite(heaterPin, LOW); // Выключить нагреватель
    }

    // Если температура достигла целевой и ещё не начали отсчёт удержания
    if (currentTemp >= targetTemp && tempReachedTime == 0) {
      tempReachedTime = millis(); // Запоминаем время, когда температура достигла целевой
    }

    // Если температура достигнута и время удержания прошло
    if (tempReachedTime > 0 && millis() - tempReachedTime >= step.hold_time * 1000UL) {
      // Переход к следующему шагу
      currentStep++;
      stepStart = millis(); // Обновляем время старта следующего шага
      tempReachedTime = 0; // Сбрасываем временную метку
    }

    // Если программа в процессе, обновляем timeLeft и modeTime
    if (currentStep < program.size()) {
      // modeTime: время от начала шага
      modeTime = (millis() - stepStart) / 1000;

      // leftTime: отсчёт времени удержания
      if (tempReachedTime > 0) {
        leftTime = (step.hold_time * 1000UL - (millis() - tempReachedTime)) / 1000;
      } else {
        leftTime = 0;
      }
    }
  } else {
    // Если программа завершена, выключаем нагреватель
    digitalWrite(heaterPin, LOW);
  }
}
