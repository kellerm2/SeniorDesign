#include <WiFi.h>
//#include <WebServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "time.h"
#include "webpage.h"
// #include <queue>
#include <ESPmDNS.h>


#define RXD2 7
#define TXD2 8





String lastCheckedTime = "";
bool receivingSchedules = false;
bool receivingLogs = false;

//WebServer server(80);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct PillSlot {
  String name = ""; 
  String t1 = "";
  String t2 = "";
  String t3 = "";
};
PillSlot slots[4];

int currentUser = 0;
String activeUser = "";
String pendingAuthUser = "";
String pendingChatID = "";
String editUser = "";


struct HistoryItem { String name; String time; String type; };
HistoryItem historyLog[50];
int historyCount = 0;  

const char dailyMessages[5][100] PROGMEM = {
"You're doing great today! Keep it up!",
"Remember to take a deep breath and smile.",
"Your health is a priority. Great job!",
"Small steps every day lead to big results.",
"You're on the right track. Have a fantastic day!"
};

const char streakMessages[5][100] PROGMEM = {
"3+ DAY STREAK! Your consistency is inspiring!",
"Incredible job! You haven't missed a beat all week!",
"Streak Alert! You are becoming a pro at this!",
"Keep that fire going! Your health is winning!",
"Milestone reached! Consistency is the key to success!"
};

unsigned long lastMessageMillis = 0; 
const unsigned long interval = 5 * 60 * 1000;

int currentStreak = 0;       
bool medsTakenToday = false; 
int lastStreakDay = -1;


void sendToSTM32(String command) {
  Serial.println("Sent to STM32: " + command); 
  Serial2.println(command + "\n");       
}

void addHistory(String name, String type) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return;
  char timeBuff[10];
  strftime(timeBuff, sizeof(timeBuff), "%I:%M:%S", &timeinfo);
  
  for (int i = 9; i > 0; i--) {
      historyLog[i] = historyLog[i - 1];
  }
  
  historyLog[0] = {name, String(timeBuff), type};
  if (historyCount < 10) historyCount++;
}


void sendHistory(AsyncWebSocketClient *client) {
  StaticJsonDocument<1024> doc;
  doc["type"] = "history_full";
  
  JsonArray data = doc.createNestedArray("data");
  for(int i=0; i<historyCount; i++) {
     JsonObject item = data.createNestedObject();
     item["name"] = historyLog[i].name;
     item["time"] = historyLog[i].time;
     item["type"] = historyLog[i].type;
  }
  
  String output;
  serializeJson(doc, output);
  client->text(output);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, AsyncWebSocketClient *client) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    
    data[len] = 0;
    String message = (char*)data;
    Serial.println("Received: " + message);

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) { Serial.print("JSON Error"); return; }

    String type = doc["type"];
    int slot = doc["slot"];

    
    if (type == "signup" || type == "login") {
      String u = doc["username"].as<String>();
      pendingAuthUser = u; 
      
      historyCount = 0; 
      for(int i=0; i<4; i++) {
        slots[i].name = ""; slots[i].t1 = ""; slots[i].t2 = ""; slots[i].t3 = "";
      }

      if (type == "signup") {
        String r = doc["role"].as<String>();
        String p = doc["phone"].as<String>();

        pendingChatID = p;
        
        sendToSTM32("SIGNUP:" + u + ":" + r + ":" + p);
      }
      if (type == "login") sendToSTM32("LOGIN:" + u); 
    }
    
    else if (type == "set_user") {
      currentUser = doc["user"];
      Serial.println("Active user switched to: User " + String(currentUser));
    }

    else if (type == "get") {
       StaticJsonDocument<200> response;
       response["type"] = "slot_data";
       response["slot"] = slot;
       response["name"] = slots[slot].name;
       response["t1"] = slots[slot].t1;
       response["t2"] = slots[slot].t2;
       response["t3"] = slots[slot].t3;

       String output;
       serializeJson(response, output);
       ws.textAll(output);
    }
    else if (type == "load_patient") {
        String p = doc["patient"].as<String>();
        sendToSTM32("CHECK_PATIENT:" + p);
    }

    else if (type == "save") {
       slots[slot].name = doc["name"].as<String>();
       slots[slot].t1 = doc["t1"].as<String>();
       slots[slot].t2 = doc["t2"].as<String>();
       slots[slot].t3 = doc["t3"].as<String>();
       
       Serial.println("Saved Slot " + String(slot) + String(slots[slot].t1 + slots[slot].t2 + slots[slot].t3));
       if (slots[slot].t1 == "") {sendToSTM32("SCHED:" + String(slot) + ":" + String(slots[slot].t1) + ":0"); }
       else {sendToSTM32("SCHED:" + String(slot) + ":" + String(slots[slot].t1) + ":1");}
       delay(100); 
       if (slots[slot].t2 == "") {sendToSTM32("SCHED:" + String(slot) + ":" + String(slots[slot].t2) + ":0"); }
       else {sendToSTM32("SCHED:" + String(slot) + ":" + String(slots[slot].t2) + ":1");}
       delay(100); 
       if (slots[slot].t3 == "") {sendToSTM32("SCHED:" + String(slot) + ":" + String(slots[slot].t3) + ":0"); }
       else {sendToSTM32("SCHED:" + String(slot) + ":" + String(slots[slot].t3) + ":1");}
       delay(100); 

       sendToSTM32("SAVESD:" + editUser + ":" + slots[slot].name + ":" + String(slot) + ":" + slots[slot].t1 + ":" + "SCHEDULED");
       delay(100); 
       sendToSTM32("SAVESD:" + editUser + ":" + slots[slot].name + ":" + String(slot) + ":"+ slots[slot].t2 + ":" + "SCHEDULED");
       delay(100); 
       sendToSTM32("SAVESD:" + editUser + ":" + slots[slot].name + ":" + String(slot) + ":" + slots[slot].t3 + ":" + "SCHEDULED");
    

       StaticJsonDocument<200> response;
       response["type"] = "slot_data";
       response["slot"] = slot;
       response["name"] = slots[slot].name;
       response["t1"] = slots[slot].t1;
       response["t2"] = slots[slot].t2;
       response["t3"] = slots[slot].t3;

       String output;
       serializeJson(response, output);
       ws.textAll(output);
       
    }

    else if (type == "delete") {
       slots[slot].name = "";
       slots[slot].t1 = "";
       slots[slot].t2 = "";
       slots[slot].t3 = "";
       Serial.println("Deleted Slot " + String(slot));

       sendToSTM32("DELETE:" + editUser + ":" + String(slot));
       delay(100);
      sendToSTM32("DELETESD:" + editUser + ":" + String(slot));
       
       StaticJsonDocument<200> response;
       response["type"] = "slot_data";
       response["name"] = "";
       response["t1"] = ""; response["t2"] = ""; response["t3"] = "";
       String output;
       serializeJson(response, output);
       ws.textAll(output);
    }
    else if (type == "log_taken") {
       JsonArray pills = doc["pills"];
       
       struct tm timeinfo;
       bool hasTime = getLocalTime(&timeinfo);
       char tBuff[10]; 
       if(hasTime) strftime(tBuff, sizeof(tBuff), "%I:%M:%S", &timeinfo);

       for (JsonObject pill : pills) {
           String n = pill["name"].as<String>();
           addHistory(n, "Taken"); 
       }
       ws.textAll("{\"type\":\"refresh_history\"}");
    }
    else if (type == "snooze_alert") {
       JsonArray pills = doc["pills"];
       
       for (JsonObject pill : pills) {
           int slot = pill["slot"];
           String alertTime = pill["time"].as<String>(); 
           
           String snoozeCmd = "SNOOZE:" + String(slot) + ":" + alertTime;
           sendToSTM32(snoozeCmd);
           Serial.println("Initiated Snooze, sent to STM32: " + snoozeCmd);
           
           delay(100);
       }
       
       ws.textAll("{\"type\":\"close_alert\"}");
    }
    else if (type == "get_history") {
       sendHistory(client);
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if(type == WS_EVT_DATA) handleWebSocketMessage(arg, data, len, client);
  if(type == WS_EVT_CONNECT) {
     sendHistory(client);
  }
}

void insertHistoryFromSD(String name, String timeStr, String type) {
  for (int i = 9; i > 0; i--) {
      historyLog[i] = historyLog[i - 1];
  }
  
  historyLog[0] = {name, timeStr, type};
  if (historyCount < 10) historyCount++;
}



void setup() {
  Serial.begin(115200);

  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("Serial2 Started for STM32");

  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  if (!MDNS.begin("smartpilldispenser")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started. Access at http://smartpilldispenser.local");
  }
  
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  sendTelegramMessage("Pill+Dispenser+Online");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Waiting for time...");
  struct tm timeinfo;
  while(!getLocalTime(&timeinfo)){ Serial.print("."); delay(500); }
  Serial.println("\nTime Synchronized!");

  
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", page_html);
  });

  server.begin();
}

void sendTelegramMessage(String message) {
  if (currentChatID == "") {
    Serial.println("No Chat ID set for this user. Skipping Telegram alert.");
    return; 
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient https;
    
    String url = "https://api.telegram.org/bot" + String(botToken) + 
                 "/sendMessage?chat_id=" + currentChatID + 
                 "&text=" + message;
                 
    if (https.begin(client, url)) {
      int httpCode = https.GET();
      
      if (httpCode > 0) {
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    } else {
      Serial.println("[HTTPS] Unable to connect");
    }
  } else {
    Serial.println("Error: WiFi not connected");
  }
}

void checkAndSendDailyMessage() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return;

  unsigned long currentMillis = millis();
  int currentDay = timeinfo.tm_mday;

  if (lastStreakDay != -1 && currentDay != lastStreakDay) {
    if (medsTakenToday) {
      currentStreak++; 
    } else {
      currentStreak = 0; 
    }
    
    medsTakenToday = false;
    lastStreakDay = currentDay;
  }
  
  if (lastStreakDay == -1) lastStreakDay = currentDay;

  if (currentMillis - lastMessageMillis >= interval) {
    String msgToSend = "";
    int randomIndex = random(0, 5); 

    if (currentStreak >= 3) {
      msgToSend = String(streakMessages[randomIndex]);
    } else {
      msgToSend = String(dailyMessages[randomIndex]);
    }

    msgToSend.replace(" ", "+");
    sendTelegramMessage(msgToSend);
    
    Serial.println("Demo Message Sent! Current Real-World Streak: " + String(currentStreak));
    lastMessageMillis = currentMillis;
  }
}

void loop() {
  ws.cleanupClients();
  checkAndSendDailyMessage();
  
  if (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    Serial.println("RX from STM32: " + msg);

    if (msg.startsWith("DISPENSED:")) {
      int p1 = msg.indexOf(':');
      int p2 = msg.indexOf(':', p1 + 1);
      int p3 = msg.indexOf(':', p2 + 1);
      int p4 = msg.indexOf(':', p3+1);

      if (p1 != -1 && p2 != -1 && p3 != -1 && p4 != -1) {
        String not_printed_name = msg.substring(p1+1,p2);
        int slotNum = msg.substring(p2 + 1, p3).toInt();
        String hh = msg.substring(p3 + 1, p4);
        String mm = msg.substring(p4 + 1);
        String timeStr = hh + ":" + mm;

        if (slotNum >= 0 && slotNum <= 3 && slots[slotNum].name != "") {
          String pillName = slots[slotNum].name;

          addHistory(pillName, "Dispensed");

          StaticJsonDocument<200> alertDoc;
          alertDoc["type"] = "alert";
          alertDoc["msg"] = pillName;
          alertDoc["slot"] = slotNum;
          alertDoc["time"] = timeStr;
          String output;
          serializeJson(alertDoc, output);
          ws.textAll(output);

          String telegramMsg = "Time to take: " + pillName;
          telegramMsg.replace(" ", "+");
          sendTelegramMessage(telegramMsg);

          ws.textAll("{\"type\":\"refresh_history\"}");
        }
      }
    }
    else if (msg.startsWith("TAKEN:")) {
      ws.textAll("{\"type\":\"hardware_taken\"}");
      Serial.println("Cup removed! Instructed webpage to log all active pills as taken.");
      medsTakenToday = true;
    }
    
    else if (msg.startsWith("ACTIVE_USER:")) {
      activeUser = msg.substring(String("ACTIVE_USER:").length());
      activeUser.trim();
      Serial.println("STM32 confirmed active user: " + activeUser);
    }

    else if (msg.startsWith("LOGIN_ACK:")) {
      if (msg.indexOf("SUCCESS") != -1) {
        activeUser = pendingAuthUser;
        editUser = activeUser;
        
        String extractedRole = "Regular";
        
        int p1 = msg.indexOf(':');
        int p2 = msg.indexOf(':', p1 + 1);
        int p3 = msg.indexOf(':', p2 + 1);

        if(p2 != -1 && p3 != -1) { 
           extractedRole = msg.substring(p2 + 1, p3);
           currentChatID = msg.substring(p3 + 1);
           currentChatID.trim();
        } 
        
        else if (p2 != -1) { 
           extractedRole = msg.substring(p2 + 1);
        }
        
        extractedRole.trim();


        activeUser = pendingAuthUser;
        
        historyCount = 0;
        for (int i = 0; i < 4; i++) {
          slots[i].name = "";
          slots[i].t1 = "";
          slots[i].t2 = "";
          slots[i].t3 = "";
        }
        ws.textAll("{\"type\":\"auth_success\",\"username\":\"" + activeUser + "\",\"role\":\"" + extractedRole + "\"}");
        Serial.println("Login Successful for: " + activeUser + " (" + extractedRole + ")");
        Serial.println("Waiting for STM32 SD data...");
  }     else {
    ws.textAll("{\"type\":\"auth_fail\",\"msg\":\"Login failed. User not found.\"}");
    Serial.println("Login Failed.");
    }
      pendingAuthUser = ""; 
  }
    
    else if (msg.startsWith("SIGNUP_ACK:")) {
      if (msg.indexOf("SUCCESS") != -1) {
        activeUser = pendingAuthUser;
        editUser = activeUser;

        currentChatID = pendingChatID;
        
        String extractedRole = "Regular";
        int lastColon = msg.lastIndexOf(':');
        if(lastColon > msg.indexOf(':')) {
           extractedRole = msg.substring(lastColon + 1);
           extractedRole.trim();
        }

        ws.textAll("{\"type\":\"auth_success\",\"username\":\"" + activeUser + "\",\"role\":\"" + extractedRole + "\"}");
        Serial.println("Signup Successful for: " + activeUser + " (" + extractedRole + ")");
      } else {
        ws.textAll("{\"type\":\"auth_fail\",\"msg\":\"Signup failed. Username might be taken.\"}");
      }
      pendingAuthUser = ""; 
    }
    else if (msg == "SCHED_START") {
      receivingSchedules = true;
      Serial.println("Started receiving schedules from STM32");
    }
    else if (msg == "SCHED_END") {
      receivingSchedules = false;
      Serial.println("Finished receiving schedules from STM32");
      ws.textAll("{\"type\":\"refresh_slots\"}");
    }
    else if (msg == "LOG_START") {
      receivingLogs = true;
      historyCount = 0;
  
      Serial.println("Started receiving logs from STM32");
    }
    else if (msg == "LOG_END") {
      receivingLogs = false;
      Serial.println("Finished receiving logs from STM32");
      ws.textAll("{\"type\":\"refresh_history\"}");
    }
    else if (receivingLogs) {
  

  int p1 = msg.indexOf(',');
  int p2 = msg.indexOf(',', p1 + 1);
  int p3 = msg.indexOf(',', p2 + 1);
  int p4 = msg.indexOf(',', p3 + 1);


  if (p1 != -1 && p2 != -1 && p3 != -1 && p4 != -1 && historyCount < 50) {
    String actualTime = msg.substring(0, p1);
    String dispStr = msg.substring(p1 + 1, p2);
    String name = msg.substring(p2 + 1, p3);
    String schedTime = msg.substring(p3 + 1, p4);
    String status = msg.substring(p4 + 1);

    int disp = dispStr.toInt();

    historyLog[historyCount].name = name;
    historyLog[historyCount].time = actualTime;
    historyLog[historyCount].type = status;

    historyCount++;

    Serial.println("Loaded history row: " + actualTime + " | Disp " +
               String(disp) + " | " +
               schedTime + " | " +
               status + " | " +
               name);
  }
}
    else if (msg.startsWith("SNOOZE_OVER:")) {
       int firstColon = msg.indexOf(':');
       int secondColon = msg.indexOf(':', firstColon + 1);
       
       if (firstColon != -1 && secondColon != -1) {
           int slotNum = msg.substring(firstColon + 1, secondColon).toInt();
           String timeStr = msg.substring(secondColon + 1);
           
           if (slotNum >= 0 && slotNum <= 3 && slots[slotNum].name != "") {
               String pillName = slots[slotNum].name;
               
               StaticJsonDocument<200> alertDoc;
               alertDoc["type"] = "alert";
               alertDoc["msg"] = pillName;
               alertDoc["slot"] = slotNum;
               alertDoc["time"] = timeStr; 
               String output; serializeJson(alertDoc, output);
               ws.textAll(output);

               String telegramMsg = "Snooze over! Time to take: " + pillName;
               telegramMsg.replace(" ", "+");
               sendTelegramMessage(telegramMsg);
               
               Serial.println("Snooze is over for Slot " + String(slotNum) + " (" + timeStr + "). Alerted web and Telegram.");
           }
       }
    }
    
    else if (msg.startsWith("LOAD_SLOT:") && receivingSchedules) {
      
      String data = msg.substring(String("LOAD_SLOT:").length());

      int p1 = data.indexOf(',');
      int p2 = data.indexOf(',', p1 + 1);
      int p3 = data.indexOf(',', p2 + 1);
      int p4 = data.indexOf(',', p3 + 1);


      if (p1 != -1 && p2 != -1 && p3 != -1 && p4 != -1) {
        int slotNum = data.substring(0, p1).toInt();
        String name = data.substring(p1 + 1, p2);
        String t1 = data.substring(p2 + 1, p3);
        String t2 = data.substring(p3 + 1, p4);
        String t3 = data.substring(p4 + 1);

        slots[slotNum].name = (name == "none") ? "" : name;
        slots[slotNum].t1 = (t1 == "none") ? "" : t1;
        slots[slotNum].t2 = (t2 == "none") ? "" : t2;
        slots[slotNum].t3 = (t3 == "none") ? "" : t3;

        Serial.println("Loaded slot " + String(slotNum) + " for " + name);
        }
    }
    
    else if (msg.startsWith("LOAD_HISTORY:")) {
      
      String data = msg.substring(13);
      
      int c1 = data.indexOf(',');
      int c2 = data.indexOf(',', c1 + 1);
      
      if (c1 != -1 && c2 != -1) {
         String name = data.substring(0, c1);
         String timeStr = data.substring(c1 + 1, c2);
         String statusType = data.substring(c2 + 1); 
         
         statusType.trim(); 

         insertHistoryFromSD(name, timeStr, statusType);
         Serial.println("Loaded History: " + name + " was " + statusType + " at " + timeStr);
      }
    }
    
    else if (msg.startsWith("PATIENT_ACK:")) {
      int firstColon = msg.indexOf(':');
      int secondColon = msg.indexOf(':', firstColon + 1);
      
      if (firstColon != -1 && secondColon != -1) {
        String status = msg.substring(firstColon + 1, secondColon);
        String pName = msg.substring(secondColon + 1);
        pName.trim();

        if (status == "SUCCESS") {
          editUser = pName;
          
          historyCount = 0;
          for (int i = 0; i < 4; i++) {
            slots[i].name = ""; slots[i].t1 = ""; slots[i].t2 = ""; slots[i].t3 = "";
          }
          ws.textAll("{\"type\":\"patient_success\",\"patient\":\"" + pName + "\"}");
          Serial.println("Doctor now managing patient: " + pName);
        } else {
          ws.textAll("{\"type\":\"patient_fail\"}");
        }
      }
    }
    else if (msg == "GET_TIME") {
      
      struct tm timeinfo;
      while(!getLocalTime(&timeinfo)){ Serial.print("."); delay(500); }
      uint8_t timeData[6];
      timeData[0] = (uint8_t)timeinfo.tm_mon;
      timeData[1] = (uint8_t)timeinfo.tm_mday;
      timeData[2] = (uint8_t)timeinfo.tm_year % 100;
      timeData[3] = (uint8_t)timeinfo.tm_hour;
      timeData[4] = (uint8_t)timeinfo.tm_min;
      timeData[5] = (uint8_t)timeinfo.tm_sec;
      
      delay(50);
      Serial2.write(timeData, 6);
      delay(50);
      Serial.printf("Sent time to STM32: %d/%d/%d %d:%d:%d\n", 
                    timeData[0], timeData[1], timeData[2], 
                    timeData[3], timeData[4], timeData[5]);
      delay(50);
    }
  }
}