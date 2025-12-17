#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========== КОНФИГУРАЦИЯ ==========
const char* ssid = "Вайфай";
const char* password = "Пароль вайфая";
const String botToken = "Апи бота";
const String allowedUsers[] = {"111111111", "111111111", ""}; //Вайтлист пользователей (в форме айди)

// WoL
const String serverMAC = "AA:AA:1A:1A:11:AA"; //Мак адрес сервера 
const IPAddress serverIP(192, 168, 1, 228); //Локальный айпи сервера
const IPAddress broadcastIP(192, 168, 1, 255); //Бродкаст айпи (Берешь айпи роутера и после последней точки меняешь на 3 цыфры из маски подсети)

// Мониторинг (настроено под ваш сервер)
const int MAX_WAIT_TIME = 90;      // 90 секунд максимум (20-50 сек + запас)
const int CHECK_INTERVAL = 3;      // Проверка каждые 3 секунды
const int PROGRESS_UPDATE = 15;    // Прогресс каждые 15 секунд

// ========== ПЕРЕМЕННЫЕ ==========
int lastUpdateId = 0;
uint8_t macArray[6];

// Мониторинг
bool isMonitoring = false;
unsigned long wakeCommandTime = 0;     // Время отправки команды /wake
unsigned long wolSentTime = 0;         // Время отправки WoL пакета
String monitoringChatID = "";
int lastProgressUpdate = 0;            // Последнее обновление прогресса

// ========== ПРОТОТИПЫ ФУНКЦИЙ ==========
void sendTelegram(String chatID, String message);

// ========== WoL ФУНКЦИИ ==========
void setupWOL() {
  String macStr = serverMAC;
  macStr.replace(":", "");
  
  for (int i = 0; i < 6; i++) {
    String byteStr = macStr.substring(i*2, i*2+2);
    macArray[i] = strtol(byteStr.c_str(), NULL, 16);
  }
  
  Serial.println("MAC: " + serverMAC);
}

bool sendWOL() {
  Serial.println("⚡ Отправляю WoL пакет...");
  wolSentTime = millis(); // Засекаем время отправки WoL
  
  WiFiUDP udp;
  udp.beginPacket(broadcastIP, 9);
  
  for (int i = 0; i < 6; i++) udp.write(0xFF);
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 6; j++) {
      udp.write(macArray[j]);
    }
  }
  
  bool success = (udp.endPacket() == 1);
  udp.stop();
  
  if (success) {
    Serial.println("✅ WoL отправлен успешно");
    // Рассчитываем задержку между командой и отправкой WoL
    unsigned long commandToWolDelay = (wolSentTime - wakeCommandTime);
    Serial.print("⏱️ Задержка команда→WoL: ");
    Serial.print(commandToWolDelay);
    Serial.println(" мс");
  } else {
    Serial.println("❌ Ошибка отправки WoL");
  }
  
  return success;
}

// ========== ПРОВЕРКА СЕРВЕРА ==========
bool isServerOnline() {
  HTTPClient http;
  
  // Проверяем несколько портов для надежности
  String urls[] = {
    "http://" + serverIP.toString(),
    "http://" + serverIP.toString() + ":80",
    "http://" + serverIP.toString() + ":443"
  };
  
  for (int i = 0; i < 3; i++) {
    http.begin(urls[i]);
    http.setTimeout(2000); // 2 секунды на попытку
    
    int httpCode = http.GET();
    http.end();
    
    // Если получили любой ответ (даже ошибку) - сервер жив
    if (httpCode > 0) {
      Serial.print("✅ Сервер отвечает на порт ");
      Serial.print(i == 0 ? "80/443" : (i == 1 ? "80" : "443"));
      Serial.print(", код: ");
      Serial.println(httpCode);
      return true;
    }
    
    delay(100); // Маленькая пауза между попытками
  }
  
  // Альтернативная проверка через подключение к порту (замена WiFi.ping)
  WiFiClient client;
  bool portOpen = client.connect(serverIP, 22, 1000); // Проверяем порт 22 (SSH)
  client.stop();
  
  if (portOpen) {
    Serial.println("✅ Порт 22 (SSH) открыт - сервер жив");
    return true;
  }
  
  // Проверяем порт 80 напрямую через TCP
  bool port80Open = client.connect(serverIP, 80, 1000);
  client.stop();
  
  if (port80Open) {
    Serial.println("✅ Порт 80 (HTTP) открыт - сервер жив");
    return true;
  }
  
  Serial.println("❌ Сервер не отвечает ни на один порт");
  return false;
}

// ========== МОНИТОРИНГ ЗАГРУЗКИ ==========
void checkServerMonitoring() {
  if (!isMonitoring) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsedSeconds = (currentTime - wakeCommandTime) / 1000;
  unsigned long timeSinceWoL = (currentTime - wolSentTime) / 1000;
  
  // Отправляем прогресс каждые PROGRESS_UPDATE секунд
  if (elapsedSeconds >= lastProgressUpdate + PROGRESS_UPDATE) {
    lastProgressUpdate = elapsedSeconds;
    
    String progressMsg = "⏳ Мониторинг: ";
    progressMsg += String(elapsedSeconds) + " сек с команды\n";
    progressMsg += "WoL отправлен " + String(timeSinceWoL) + " сек назад\n";
    
    // Прогресс-бар (ИСПРАВЛЕНА СТРОКА С min)
    int progressPercent = std::min(100, static_cast<int>((elapsedSeconds * 100) / MAX_WAIT_TIME));
    progressMsg += "[";
    for (int i = 0; i < 10; i++) {
      progressMsg += (i < progressPercent / 10) ? "█" : "░";
    }
    progressMsg += "] " + String(progressPercent) + "%";
    
    sendTelegram(monitoringChatID, progressMsg);
    Serial.print("📊 Прогресс: ");
    Serial.print(elapsedSeconds);
    Serial.print(" сек (");
    Serial.print(progressPercent);
    Serial.println("%)");
  }
  
  // Проверяем сервер каждые CHECK_INTERVAL секунд
  if (elapsedSeconds % CHECK_INTERVAL == 0) {
    Serial.print("🔍 Проверка сервера... ");
    Serial.print(elapsedSeconds);
    Serial.println(" сек");
    
    if (isServerOnline()) {
      // Сервер загрузился!
      unsigned long totalBootTime = (currentTime - wakeCommandTime) / 1000;
      unsigned long wolToBootTime = (currentTime - wolSentTime) / 1000;
      
      String successMsg = "🎉 СЕРВЕР ЗАГРУЗИЛСЯ!\n\n";
      successMsg += "📊 Статистика загрузки:\n";
      successMsg += "• Общее время: " + String(totalBootTime) + " сек\n";
      successMsg += "• WoL→Загрузка: " + String(wolToBootTime) + " сек\n";
      successMsg += "• IP: " + serverIP.toString() + "\n";
      successMsg += "• MAC: " + serverMAC + "\n\n";
      
      if (wolToBootTime < 30) {
        successMsg += "⚡ Быстрая загрузка!";
      } else if (wolToBootTime < 60) {
        successMsg += "🐢 Нормальная загрузка";
      } else {
        successMsg += "⚠️ Долгая загрузка, проверьте сервер";
      }
      
      sendTelegram(monitoringChatID, successMsg);
      isMonitoring = false;
      
      Serial.print("✅ Сервер загрузился за ");
      Serial.print(totalBootTime);
      Serial.println(" секунд");
    }
    else if (elapsedSeconds >= MAX_WAIT_TIME) {
      // Таймаут
      String timeoutMsg = "⏰ ТАЙМАУТ!\n\n";
      timeoutMsg += "Сервер не загрузился за " + String(MAX_WAIT_TIME) + " сек\n";
      timeoutMsg += "WoL отправлен " + String(timeSinceWoL) + " сек назад\n\n";
      timeoutMsg += "Возможные проблемы:\n";
      timeoutMsg += "1. WoL не настроен в BIOS\n";
      timeoutMsg += "2. Сервер завис при загрузке\n";
      timeoutMsg += "3. Проблемы с питанием\n";
      timeoutMsg += "4. Долгая POST-проверка\n\n";
      timeoutMsg += "Попробуйте команду /wake ещё раз";
      
      sendTelegram(monitoringChatID, timeoutMsg);
      isMonitoring = false;
      
      Serial.println("❌ Мониторинг: таймаут");
    }
  }
}

// ========== TELEGRAM ФУНКЦИИ ==========
String getTelegramUpdate() {
  HTTPClient http;
  
  String url = "https://api.telegram.org/bot" + botToken + "/getUpdates?timeout=1&limit=1";
  
  http.begin(url);
  http.setTimeout(3000);
  
  if (http.GET() == 200) {
    String response = http.getString();
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      if (doc.containsKey("result") && doc["result"].size() > 0) {
        JsonObject result = doc["result"][0];
        int update_id = result["update_id"].as<int>();
        
        lastUpdateId = update_id;
        
        if (result.containsKey("message")) {
          String chatID = result["message"]["chat"]["id"].as<String>();
          String text = result["message"]["text"].as<String>();
          
          Serial.print("📨 Команда: ");
          Serial.println(text);
          
          // Удаляем сообщение из истории
          String deleteUrl = "https://api.telegram.org/bot" + botToken + 
                           "/getUpdates?offset=" + String(update_id + 1);
          HTTPClient http2;
          http2.begin(deleteUrl);
          http2.GET();
          http2.end();
          
          http.end();
          return chatID + "|" + text;
        }
      }
    }
  }
  
  http.end();
  return "";
}

void sendTelegram(String chatID, String message) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  message.replace(" ", "%20");
  message.replace("\n", "%0A");
  
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + botToken + 
               "/sendMessage?chat_id=" + chatID + "&text=" + message;
  
  http.begin(url);
  http.setTimeout(5000);
  http.GET();
  http.end();
}

// ========== ОБРАБОТКА КОМАНД ==========
void processCommand(String chatID, String text) {
  // Проверка вайтлиста
  bool allowed = false;
  for (int i = 0; i < sizeof(allowedUsers)/sizeof(allowedUsers[0]); i++) {
    if (allowedUsers[i] == chatID) {
      allowed = true;
      break;
    }
  }
  
  if (!allowed) {
    sendTelegram(chatID, "⛔ Доступ запрещен");
    return;
  }
  
  Serial.print("Обработка: ");
  Serial.println(text);
  
  if (text == "/start" || text == "/help") {
    String msg = "🤖 WoL Bot с детальным мониторингом\n\n";
    msg += "📊 Команды:\n";
    msg += "/wake - включить + мониторинг загрузки\n";
    msg += "/wakeonly - только WoL\n";
    msg += "/status - статус системы\n";
    msg += "/check - проверить сервер сейчас\n";
    msg += "/timing - статистика времени\n";
    msg += "/ping - проверка связи\n";
    msg += "/clear - очистить историю\n\n";
    msg += "⚙️ Настройки мониторинга:\n";
    msg += "• Макс. время: " + String(MAX_WAIT_TIME) + " сек\n";
    msg += "• Сервер: " + serverIP.toString() + "\n";
    msg += "• Обычная загрузка: 20-50 секунд";
    sendTelegram(chatID, msg);
  }
  else if (text == "/wake") {
    wakeCommandTime = millis(); // Засекаем время команды
    lastProgressUpdate = 0;
    
    sendTelegram(chatID, "🔌 Команда получена, отправляю WoL...");
    
    if (sendWOL()) {
      // Запускаем мониторинг
      isMonitoring = true;
      monitoringChatID = chatID;
      
      String msg = "✅ WoL отправлен!\n\n";
      msg += "📊 Начинаю мониторинг загрузки:\n";
      msg += "• Ожидаемое время: 20-50 секунд\n";
      msg += "• Максимум: " + String(MAX_WAIT_TIME) + " секунд\n";
      msg += "• Проверка каждые " + String(CHECK_INTERVAL) + " сек\n";
      msg += "• Прогресс каждые " + String(PROGRESS_UPDATE) + " сек\n\n";
      msg += "Я сообщу когда сервер загрузится со статистикой времени!";
      sendTelegram(chatID, msg);
      
      Serial.println("🔍 Мониторинг запущен");
    } else {
      sendTelegram(chatID, "❌ Ошибка отправки WoL");
    }
  }
  else if (text == "/wakeonly") {
    sendTelegram(chatID, "🔌 Отправляю WoL без мониторинга...");
    
    if (sendWOL()) {
      sendTelegram(chatID, "✅ WoL отправлен на " + serverIP.toString());
    } else {
      sendTelegram(chatID, "❌ Ошибка WoL");
    }
  }
  else if (text == "/status") {
    String status = "📊 Статус системы:\n";
    status += "WiFi: " + String(WiFi.RSSI()) + " dBm\n";
    status += "IP ESP: " + WiFi.localIP().toString() + "\n";
    status += "Сервер: " + serverIP.toString() + "\n";
    
    if (isMonitoring) {
      unsigned long elapsed = (millis() - wakeCommandTime) / 1000;
      status += "Мониторинг: АКТИВЕН " + String(elapsed) + " сек\n";
    } else {
      status += "Мониторинг: выключен\n";
    }
    
    status += "lastUpdateId: " + String(lastUpdateId);
    sendTelegram(chatID, status);
  }
  else if (text == "/check") {
    sendTelegram(chatID, "🔍 Проверяю сервер...");
    
    if (isServerOnline()) {
      sendTelegram(chatID, "✅ Сервер онлайн! " + serverIP.toString());
    } else {
      sendTelegram(chatID, "❌ Сервер оффлайн " + serverIP.toString());
    }
  }
  else if (text == "/timing") {
    if (wolSentTime > 0) {
      unsigned long now = millis();
      unsigned long commandToWol = (wolSentTime - wakeCommandTime);
      unsigned long wolToNow = (now - wolSentTime);
      
      String timing = "⏱️ Статистика времени:\n\n";
      timing += "• Команда→WoL: " + String(commandToWol) + " мс\n";
      timing += "• WoL→Сейчас: " + String(wolToNow / 1000) + " сек\n";
      timing += "• Общее: " + String((now - wakeCommandTime) / 1000) + " сек\n\n";
      
      if (isMonitoring) {
        timing += "📡 Мониторинг активен";
      } else if (wolSentTime > 0) {
        timing += "✅ WoL был отправлен";
      }
      
      sendTelegram(chatID, timing);
    } else {
      sendTelegram(chatID, "ℹ️ WoL ещё не отправлялся");
    }
  }
  else if (text == "/ping") {
    sendTelegram(chatID, "🏓 Pong! " + String(millis()) + " мс");
  }
  else if (text == "/clear") {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=-1";
    http.begin(url);
    http.GET();
    http.end();
    
    lastUpdateId = 0;
    sendTelegram(chatID, "🗑️ История очищена");
  }
  else {
    sendTelegram(chatID, "❓ Неизвестная команда: " + text);
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== WoL Bot с таймингом загрузки ===");
  
  // WiFi
  Serial.print("WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ WiFi ошибка");
    while(1) delay(1000);
  }
  
  Serial.println("\n✅ WiFi подключен");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP().toString());
  
  setupWOL();
  
  // Очистка истории Telegram
  Serial.println("🧹 Очищаю историю...");
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=-1";
  http.begin(url);
  http.GET();
  http.end();
  delay(1000);
  
  Serial.println("✅ Бот запущен");
  Serial.println("Ожидаемое время загрузки сервера: 20-50 секунд");
}

// ========== LOOP ==========
void loop() {
  // Проверка Telegram
  String update = getTelegramUpdate();
  
  if (update != "") {
    int separator = update.indexOf("|");
    if (separator > 0) {
      String chatID = update.substring(0, separator);
      String text = update.substring(separator + 1);
      
      processCommand(chatID, text);
    }
  }
  
  // Проверка мониторинга
  checkServerMonitoring();
  
  delay(2000);
}