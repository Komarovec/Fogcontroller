#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUDP.h>

//Wifi
const char* ssid = "KomarNetwork";
const char* password = "MYOP9317a";

//Access point
IPAddress apIP(192, 168, 1, 111);
const char* ssidAP = "FOGTest";
const char* passwordAP = "MYOP9317a";

//Server
String request = "";
char req[50];
char *pch;

//UDP Handling
char data[200] = {};
int packetsize = 0; 
String receiveddata = "";
WiFiUDP UDP;
int UDPPort = 8099;
int clientCount = 0; 

//FogMachine Control
const int heated = A0;
const int relay = D2;
int FogOn = 0, FogOff = 0;
bool timer = false;
bool fogging = false;
bool isLocked = false;
unsigned long waitTime = 0;
unsigned long syncTime = 0;
bool isHeated = false;

//Structure for clients
typedef struct {
  IPAddress ip;
  char *nick;
} CLIENTS;

CLIENTS clientList[50];

void refreshValues(IPAddress ip, int port);
void quickFog();
void setTimer();
void setRed();
void resetState();
void timerCheck();
void printClients();
void addToList(IPAddress ip, int port, char *nick);
int IPInList(IPAddress ip);
void broadcastUDP(char *data, int port);
void sendUDP(IPAddress ip, int port, char *data);
void UDPHandling();

//Functions
void refreshValues(IPAddress ip, int port) {
  if(analogRead(heated) > 600) {
    sendUDP(ip, port, "ready");
  }
  else if(analogRead(heated) < 100) {
    sendUDP(ip, port, "cold");
  }
  
  
  if(isLocked) {
    sendUDP(ip, port, "locked");
  }
  else if(fogging) {
    sendUDP(ip, port, "fog");
  }
  else if(timer) {
      char msg[50] = {};
      strcpy(msg,"timer:");
      int timeToWait = (int)(waitTime-millis())/1000;
      strcat(msg,String(timeToWait).c_str());
      sendUDP(ip, port, msg);
  }
  else {
    sendUDP(ip, port, "reset");
  }
}

void quickFog() {
  if(isLocked || !isHeated) return;
  fogging = true;
  broadcastUDP("fog", 8100);
  digitalWrite(relay, LOW);
  delay(2000);
  if(timer) {
    char msg[50] = {};
    strcpy(msg,"timer:");
    int timeToWait = (int)(waitTime-millis())/1000;
    strcat(msg,String(timeToWait).c_str());
    broadcastUDP(msg, 8100);
  }
  else
    broadcastUDP("reset", 8100);
  digitalWrite(relay, HIGH);
  fogging = false;
  return;
}

void setTimer() {
  if(isLocked || !isHeated) return;
  if(FogOn > 0 && FogOff > 0) {
      timer = true;
      waitTime = 0;
      if(fogging) {
        fogging = false;
        digitalWrite(relay, HIGH);
        Serial.println("Turning red off");
      }
      Serial.printf("FogOn: %d, FogOff: %d\n", FogOn, FogOff);
      char msg[50] = {};
      strcpy(msg,"timer:");
      strcat(msg,String(FogOff).c_str());
      fogging = true;
      broadcastUDP(msg, 8100);
   }
}

void setRed() {
  if(isLocked || !isHeated) return;
  Serial.print("Red Button: ");
  if(!timer) {
    fogging = !fogging;
    Serial.println(fogging);
    digitalWrite(relay, !fogging);

    if(fogging) {
      broadcastUDP("redh", 8100);
    }
    else {
      broadcastUDP("reset", 8100);
    }
  }
  else {
    Serial.println("Timer on");
  }
}

void resetState() {
  Serial.println("Timer Off");
  timer = false;
  fogging = false;
  digitalWrite(relay, HIGH);
  waitTime = 0;
  broadcastUDP("reset", 8100);
}

void timerCheck() {
  if(!timer || !(waitTime < millis())) return;
  if(fogging) {
    fogging = false;
    digitalWrite(relay, HIGH);
    waitTime = millis() + (FogOff*1000);
    
    char msg[50] = {};
    strcpy(msg,"timer:");
    strcat(msg,String(FogOff).c_str());
    broadcastUDP(msg, 8100);
    
    Serial.println("FogOff");
  }
  else {
    if(isHeated) {
      fogging = true;
      digitalWrite(relay, LOW);
      waitTime = millis() + (FogOn*1000);
  
      broadcastUDP("fog", 8100);
      
      Serial.println("FogOn");
    }
    else {
      fogging = false;
      digitalWrite(relay, HIGH);
      waitTime = millis() + (FogOff*1000);
  
      char msg[50] = {};
      strcpy(msg,"timer:");
      strcat(msg,String(FogOff).c_str());
      broadcastUDP(msg, 8100);
      
      Serial.println("Cant fog on timer - not heated");
    }
  }
}

void printClients() {
  Serial.println("Client list: ");
  for(int i = 0; i < clientCount; i++) {
    Serial.print(i);
    Serial.print(". ");
    Serial.print(clientList[i].ip);
    Serial.print(" ");
    Serial.println(clientList[i].nick);
  }
}

void addToList(IPAddress ip, int port, char *nick) {
  int i = IPInList(ip);
  if(i >= 0) {
    refreshValues(ip, port);
  }
  else {
    clientList[clientCount].ip = ip;
    clientCount++;
    refreshValues(ip, port);
  }
  clientList[clientCount].nick = nick;
  sendUDP(ip, port, "registered");
}

int IPInList(IPAddress ip) {
  for(int i = 0; i < clientCount; i++) {
    if(ip == clientList[i].ip) {
      return i;
    }
  }
  return -1;
}

//UDP Com
void broadcastUDP(char *data, int port) {
  for(int i = 0; i < clientCount; i++) {
    sendUDP(clientList[i].ip, port, data);
  }
}

void sendUDP(IPAddress ip, int port, char *data) {
    UDP.beginPacket(ip, port);
    UDP.write(data);
    UDP.endPacket();
}

void UDPHandling() {
    char message = UDP.parsePacket();
    packetsize = UDP.available();
    IPAddress remoteip;
    int remoteport;
    if(message) { 
      Serial.printf("Packet received!\n");
      UDP.read(data,packetsize);
      delay(100);
      remoteip = UDP.remoteIP();
      remoteport = UDP.remotePort();
      delay(100);
    }

    if(packetsize) {
      for (int i=0;packetsize > i ;i++) {
        receiveddata+= (char)data[i];
      } 
      Serial.println(receiveddata);

      //Register
      if(receiveddata.indexOf("register") == 0) {
        char dt[50] = {};
        char *nick;
        strcpy(dt, receiveddata.c_str());
        pch = strtok(dt, ":");
        nick = strtok(NULL, ":");
        Serial.printf("registering: %s\n", nick);
        addToList(remoteip, remoteport, nick);
        refreshValues(remoteip, remoteport);
        printClients();
      }
      if(IPInList(remoteip) == -1) {
        clientList[clientCount].ip = remoteip;
        clientCount++;
        printClients();
      }
      else if(receiveddata == "LOCK") {
        if(!isLocked) {
          isLocked = true;
          digitalWrite(relay, HIGH);
          timer = false;
          fogging = false;
          broadcastUDP("locked",8100);
        }
      }
      else if(receiveddata == "UNLOCK") {
        if(isLocked) {
          broadcastUDP("unlocked",8100);
          isLocked = false;
        }
      }
      else if(receiveddata == "QF") {
        quickFog();
      }
      else if(receiveddata == "RED") {
        setRed();
      } 
      else if(receiveddata == "RESET") {
        resetState();
      }
      else if(receiveddata == "refresh") {
        refreshValues(remoteip, remoteport);
      }
      else if(receiveddata.indexOf("TIM") == 0) {
        char dt[50] = {};
        strcpy(dt, receiveddata.c_str());
        pch = strtok(dt, ":");
        pch = strtok(NULL, ":");
        FogOn = atoi(pch);
        pch = strtok(NULL, ":");
        FogOff = atoi(pch);
        setTimer(); 
      }
     
      receiveddata="";
    }
    delay(100);
}

//Setup
void setup() {
  //Seriová linka
  Serial.begin(115200);

  //Wifi RouterConnect
  /*
  Serial.print("Connecting to ");
  Serial.println(ssid); 
  WiFi.begin(ssid, password); 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);                          
    Serial.print(".");                    
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  */

  //Access Point
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));   // subnet FF FF FF 00  
  WiFi.softAP(ssidAP, passwordAP);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  //Server 
  UDP.begin(UDPPort);
  Serial.println("UDP Server started");

  //Relé
  pinMode(relay, OUTPUT);
  digitalWrite(relay, HIGH);
}

//Loop
void loop() {
  UDPHandling();
  timerCheck();

  //Check Heat
  if(analogRead(heated) > 600 && !isHeated) {
    isHeated = true;
    if(!fogging) {
      broadcastUDP("ready",8100);
    }
    else if(timer && fogging) {
      broadcastUDP("ready",8100);
      broadcastUDP("fog",8100);
    }
    else {
      broadcastUDP("ready",8100);
      broadcastUDP("redh",8100);
    }
  }
  else if(analogRead(heated) < 100 && isHeated) {
    isHeated = false;
    broadcastUDP("cold",8100);
  }
}