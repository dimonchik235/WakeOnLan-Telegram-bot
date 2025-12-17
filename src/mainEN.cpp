
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========== CONFIGURATION ==========
const char* ssid = "SSID";
const char* password = "Password";
const String botToken = "Bot token";
const String allowedUsers[] = {"1111111111", ""}; // User whitelist (Telegram IDs)

// WoL Settings
const String serverMAC = "A1:AA:1A:1A:11:A1"; // Server MAC address
const IPAddress serverIP(192, 168, 1, 228);   // Server local IP
const IPAddress broadcastIP(192, 168, 1, 255); // Broadcast IP

// Monitoring (configured for your server)
const int MAX_WAIT_TIME = 90;      // 90 seconds maximum (20-50 sec + margin)
const int CHECK_INTERVAL = 3;      // Check every 3 seconds
const int PROGRESS_UPDATE = 15;    // Progress update every 15 seconds

// ========== VARIABLES ==========
int lastUpdateId = 0;
uint8_t macArray[6];

// Monitoring
bool isMonitoring = false;
unsigned long wakeCommandTime = 0;     // Time when /wake command was received
unsigned long wolSentTime = 0;         // Time when WoL packet was sent
String monitoringChatID = "";
int lastProgressUpdate = 0;            // Last progress update time

// ========== FUNCTION PROTOTYPES ==========
void sendTelegram(String chatID, String message);

// ========== WoL FUNCTIONS ==========
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
  Serial.println("⚡ Sending WoL packet...");
  wolSentTime = millis(); // Record WoL send time
  
  WiFiUDP udp;
  udp.beginPacket(broadcastIP, 9);
  
  // Send 6 bytes of 0xFF (WoL magic packet header)
  for (int i = 0; i < 6; i++) udp.write(0xFF);
  
  // Send 16 repetitions of MAC address
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 6; j++) {
      udp.write(macArray[j]);
    }
  }
  
  bool success = (udp.endPacket() == 1);
  udp.stop();
  
  if (success) {
    Serial.println("✅ WoL sent successfully");
    // Calculate delay between command and WoL send
    unsigned long commandToWolDelay = (wolSentTime - wakeCommandTime);
    Serial.print("⏱️ Command→WoL delay: ");
    Serial.print(commandToWolDelay);
    Serial.println(" ms");
  } else {
    Serial.println("❌ WoL send error");
  }
  
  return success;
}

// ========== SERVER CHECK ==========
bool isServerOnline() {
  HTTPClient http;
  
  // Check multiple ports for reliability
  String urls[] = {
    "http://" + serverIP.toString(),
    "http://" + serverIP.toString() + ":80",
    "http://" + serverIP.toString() + ":443"
  };
  
  for (int i = 0; i < 3; i++) {
    http.begin(urls[i]);
    http.setTimeout(2000); // 2 seconds timeout per attempt
    
    int httpCode = http.GET();
    http.end();
    
    // If we get any response (even error) - server is alive
    if (httpCode > 0) {
      Serial.print("✅ Server responds on port ");
      Serial.print(i == 0 ? "80/443" : (i == 1 ? "80" : "443"));
      Serial.print(", code: ");
      Serial.println(httpCode);
      return true;
    }
    
    delay(100); // Small pause between attempts
  }
  
  // Alternative check via TCP connection
  WiFiClient client;
  bool portOpen = client.connect(serverIP, 22, 1000); // Check port 22 (SSH)
  client.stop();
  
  if (portOpen) {
    Serial.println("✅ Port 22 (SSH) open - server is alive");
    return true;
  }
  
  // Check port 80 directly via TCP
  bool port80Open = client.connect(serverIP, 80, 1000);
  client.stop();
  
  if (port80Open) {
    Serial.println("✅ Port 80 (HTTP) open - server is alive");
    return true;
  }
  
  Serial.println("❌ Server not responding on any port");
  return false;
}

// ========== BOOT MONITORING ==========
void checkServerMonitoring() {
  if (!isMonitoring) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsedSeconds = (currentTime - wakeCommandTime) / 1000;
  unsigned long timeSinceWoL = (currentTime - wolSentTime) / 1000;
  
  // Send progress every PROGRESS_UPDATE seconds
  if (elapsedSeconds >= lastProgressUpdate + PROGRESS_UPDATE) {
    lastProgressUpdate = elapsedSeconds;
    
    String progressMsg = "⏳ Monitoring: ";
    progressMsg += String(elapsedSeconds) + " sec since command\n";
    progressMsg += "WoL sent " + String(timeSinceWoL) + " sec ago\n";
    
    // Progress bar
    int progressPercent = std::min(100, static_cast<int>((elapsedSeconds * 100) / MAX_WAIT_TIME));
    progressMsg += "[";
    for (int i = 0; i < 10; i++) {
      progressMsg += (i < progressPercent / 10) ? "█" : "░";
    }
    progressMsg += "] " + String(progressPercent) + "%";
    
    sendTelegram(monitoringChatID, progressMsg);
    Serial.print("📊 Progress: ");
    Serial.print(elapsedSeconds);
    Serial.print(" sec (");
    Serial.print(progressPercent);
    Serial.println("%)");
  }
  
  // Check server every CHECK_INTERVAL seconds
  if (elapsedSeconds % CHECK_INTERVAL == 0) {
    Serial.print("🔍 Checking server... ");
    Serial.print(elapsedSeconds);
    Serial.println(" sec");
    
    if (isServerOnline()) {
      // Server has booted!
      unsigned long totalBootTime = (currentTime - wakeCommandTime) / 1000;
      unsigned long wolToBootTime = (currentTime - wolSentTime) / 1000;
      
      String successMsg = "🎉 SERVER HAS BOOTED!\n\n";
      successMsg += "📊 Boot statistics:\n";
      successMsg += "• Total time: " + String(totalBootTime) + " sec\n";
      successMsg += "• WoL→Boot: " + String(wolToBootTime) + " sec\n";
      successMsg += "• IP: " + serverIP.toString() + "\n";
      successMsg += "• MAC: " + serverMAC + "\n\n";
      
      if (wolToBootTime < 30) {
        successMsg += "⚡ Fast boot!";
      } else if (wolToBootTime < 60) {
        successMsg += "🐢 Normal boot";
      } else {
        successMsg += "⚠️ Slow boot, check the server";
      }
      
      sendTelegram(monitoringChatID, successMsg);
      isMonitoring = false;
      
      Serial.print("✅ Server booted in ");
      Serial.print(totalBootTime);
      Serial.println(" seconds");
    }
    else if (elapsedSeconds >= MAX_WAIT_TIME) {
      // Timeout
      String timeoutMsg = "⏰ TIMEOUT!\n\n";
      timeoutMsg += "Server didn't boot in " + String(MAX_WAIT_TIME) + " sec\n";
      timeoutMsg += "WoL sent " + String(timeSinceWoL) + " sec ago\n\n";
      timeoutMsg += "Possible issues:\n";
      timeoutMsg += "1. WoL not configured in BIOS\n";
      timeoutMsg += "2. Server stuck during boot\n";
      timeoutMsg += "3. Power issues\n";
      timeoutMsg += "4. Long POST check\n\n";
      timeoutMsg += "Try /wake command again";
      
      sendTelegram(monitoringChatID, timeoutMsg);
      isMonitoring = false;
      
      Serial.println("❌ Monitoring: timeout");
    }
  }
}

// ========== TELEGRAM FUNCTIONS ==========
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
          
          Serial.print("📨 Command: ");
          Serial.println(text);
          
          // Delete message from history
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

// ========== COMMAND PROCESSING ==========
void processCommand(String chatID, String text) {
  // Check whitelist
  bool allowed = false;
  for (int i = 0; i < sizeof(allowedUsers)/sizeof(allowedUsers[0]); i++) {
    if (allowedUsers[i] == chatID) {
      allowed = true;
      break;
    }
  }
  
  if (!allowed) {
    sendTelegram(chatID, "⛔ Access denied");
    return;
  }
  
  Serial.print("Processing: ");
  Serial.println(text);
  
  if (text == "/start" || text == "/help") {
    String msg = "🤖 WoL Bot with detailed monitoring\n\n";
    msg += "📊 Commands:\n";
    msg += "/wake - turn on + boot monitoring\n";
    msg += "/wakeonly - WoL only (no monitoring)\n";
    msg += "/status - system status\n";
    msg += "/check - check server now\n";
    msg += "/timing - timing statistics\n";
    msg += "/ping - connection test\n";
    msg += "/clear - clear history\n\n";
    msg += "⚙️ Monitoring settings:\n";
    msg += "• Max time: " + String(MAX_WAIT_TIME) + " sec\n";
    msg += "• Server: " + serverIP.toString() + "\n";
    msg += "• Normal boot: 20-50 seconds";
    sendTelegram(chatID, msg);
  }
  else if (text == "/wake") {
    wakeCommandTime = millis(); // Record command time
    lastProgressUpdate = 0;
    
    sendTelegram(chatID, "🔌 Command received, sending WoL...");
    
    if (sendWOL()) {
      // Start monitoring
      isMonitoring = true;
      monitoringChatID = chatID;
      
      String msg = "✅ WoL sent!\n\n";
      msg += "📊 Starting boot monitoring:\n";
      msg += "• Expected time: 20-50 seconds\n";
      msg += "• Maximum: " + String(MAX_WAIT_TIME) + " seconds\n";
      msg += "• Check every " + String(CHECK_INTERVAL) + " sec\n";
      msg += "• Progress every " + String(PROGRESS_UPDATE) + " sec\n\n";
      msg += "I'll notify you when server boots with timing statistics!";
      sendTelegram(chatID, msg);
      
      Serial.println("🔍 Monitoring started");
    } else {
      sendTelegram(chatID, "❌ WoL send error");
    }
  }
  else if (text == "/wakeonly") {
    sendTelegram(chatID, "🔌 Sending WoL without monitoring...");
    
    if (sendWOL()) {
      sendTelegram(chatID, "✅ WoL sent to " + serverIP.toString());
    } else {
      sendTelegram(chatID, "❌ WoL error");
    }
  }
  else if (text == "/status") {
    String status = "📊 System status:\n";
    status += "WiFi: " + String(WiFi.RSSI()) + " dBm\n";
    status += "ESP IP: " + WiFi.localIP().toString() + "\n";
    status += "Server: " + serverIP.toString() + "\n";
    
    if (isMonitoring) {
      unsigned long elapsed = (millis() - wakeCommandTime) / 1000;
      status += "Monitoring: ACTIVE " + String(elapsed) + " sec\n";
    } else {
      status += "Monitoring: disabled\n";
    }
    
    status += "lastUpdateId: " + String(lastUpdateId);
    sendTelegram(chatID, status);
  }
  else if (text == "/check") {
    sendTelegram(chatID, "🔍 Checking server...");
    
    if (isServerOnline()) {
      sendTelegram(chatID, "✅ Server online! " + serverIP.toString());
    } else {
      sendTelegram(chatID, "❌ Server offline " + serverIP.toString());
    }
  }
  else if (text == "/timing") {
    if (wolSentTime > 0) {
      unsigned long now = millis();
      unsigned long commandToWol = (wolSentTime - wakeCommandTime);
      unsigned long wolToNow = (now - wolSentTime);
      
      String timing = "⏱️ Timing statistics:\n\n";
      timing += "• Command→WoL: " + String(commandToWol) + " ms\n";
      timing += "• WoL→Now: " + String(wolToNow / 1000) + " sec\n";
      timing += "• Total: " + String((now - wakeCommandTime) / 1000) + " sec\n\n";
      
      if (isMonitoring) {
        timing += "📡 Monitoring active";
      } else if (wolSentTime > 0) {
        timing += "✅ WoL was sent";
      }
      
      sendTelegram(chatID, timing);
    } else {
      sendTelegram(chatID, "ℹ️ WoL hasn't been sent yet");
    }
  }
  else if (text == "/ping") {
    sendTelegram(chatID, "🏓 Pong! " + String(millis()) + " ms");
  }
  else if (text == "/clear") {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=-1";
    http.begin(url);
    http.GET();
    http.end();
    
    lastUpdateId = 0;
    sendTelegram(chatID, "🗑️ History cleared");
  }
  else {
    sendTelegram(chatID, "❓ Unknown command: " + text);
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== WoL Bot with boot timing ===");
  
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
    Serial.println("\n❌ WiFi error");
    while(1) delay(1000);
  }
  
  Serial.println("\n✅ WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP().toString());
  
  setupWOL();
  
  // Clear Telegram history
  Serial.println("🧹 Clearing history...");
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + botToken + "/getUpdates?offset=-1";
  http.begin(url);
  http.GET();
  http.end();
  delay(1000);
  
  Serial.println("✅ Bot started");
  Serial.println("Expected server boot time: 20-50 seconds");
}

// ========== LOOP ==========
void loop() {
  // Check Telegram
  String update = getTelegramUpdate();
  
  if (update != "") {
    int separator = update.indexOf("|");
    if (separator > 0) {
      String chatID = update.substring(0, separator);
      String text = update.substring(separator + 1);
      
      processCommand(chatID, text);
    }
  }
  
  // Check monitoring
  checkServerMonitoring();
  
  delay(2000);
}