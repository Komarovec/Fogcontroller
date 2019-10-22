#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Timer.h>

//Wifi
const char* ssid = "KomarNetwork";
const char* password = "****";

//Access point
IPAddress apIP(192, 168, 1, 111);
const char* ssidAP = "FOGTest";
const char* passwordAP = "****";

bool useAP = false;

//Timers
Timer qfTimer;


//Server
String request = "";
char req[50];
char *pch;

//TCP
WiFiServer server(22456); //TCP server
const int tPort = 22457;
WiFiClient client;
int clientCount = 0; 

//FogMachine Control
const int heated = A0;
const int relay = D2;
int FogOn = 0, FogOff = 0;
bool timer = false;
bool fogging = false;
bool isLocked = false;
bool quickFogging = false;
unsigned long waitTime = 0;
unsigned long syncTime = 0;
bool isHeated = false;

//Structure for clients
typedef struct {
  IPAddress ip;
  char *nick;
} CLIENTS;

CLIENTS clientList[50];

//Fuctions Init
void refreshValues(IPAddress ip, int port);
void endQuickFog();
void quickFog();
void setTimer();
void setRed();
void resetState();
void timerCheck();
void printClients();
void addToList(IPAddress ip, int port, char *nick);
int IPInList(IPAddress ip);
void broadcastTCP(char *data, int port);
void sendTCP(IPAddress ip, int port, char *data);
void TCPHandling();

//Functions
void refreshValues(IPAddress ip, int port) {
  if(analogRead(heated) > 600) {
    sendTCP(ip, port, "ready");
  }
  else if(analogRead(heated) < 100) {
    sendTCP(ip, port, "cold");
  }
  
  
  if(isLocked) {
    sendTCP(ip, port, "locked");
  }
  else if(fogging) {
    sendTCP(ip, port, "fog");
  }
  else if(timer) {
      char msg[50] = {};
      strcpy(msg,"timer:");
      int timeToWait = (int)(waitTime-millis())/1000;
      strcat(msg,String(timeToWait).c_str());
      sendTCP(ip, port, msg);
  }
  else {
    sendTCP(ip, port, "reset");
  }
}

void quickFog() {
  if(isLocked || !isHeated || quickFogging) return; //If fog cant operate
    
  //Fog
  fogging = true;
  quickFogging = true;
  broadcastTCP("fog", tPort);
  digitalWrite(relay, LOW);
  int afterFog = qfTimer.after(2000, endQuickFog);
}

void endQuickFog() {
  //Stop Fog
  quickFogging = false;
  if(timer) { //When Timer 
    char msg[50] = {};
    strcpy(msg,"timer:");
    int timeToWait = (int)(waitTime-millis())/1000;
    strcat(msg,String(timeToWait).c_str());
    broadcastTCP(msg, tPort);
  }
  else
  Serial.println("QuickFog Off");
  broadcastTCP("reset", tPort);
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
      broadcastTCP(msg, tPort);
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
      broadcastTCP("redh", tPort);
    }
    else {
      broadcastTCP("reset", tPort);
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
  broadcastTCP("reset", tPort);
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
    broadcastTCP(msg, tPort);
    
    Serial.println("FogOff");
  }
  else {
    if(isHeated) {
      fogging = true;
      digitalWrite(relay, LOW);
      waitTime = millis() + (FogOn*1000);
  
      broadcastTCP("fog", tPort);
      
      Serial.println("FogOn");
    }
    else {
      fogging = false;
      digitalWrite(relay, HIGH);
      waitTime = millis() + (FogOff*1000);
  
      char msg[50] = {};
      strcpy(msg,"timer:");
      strcat(msg,String(FogOff).c_str());
      broadcastTCP(msg, tPort);
      
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
  sendTCP(ip, port, "registered");
}

int IPInList(IPAddress ip) {
  for(int i = 0; i < clientCount; i++) {
    if(ip == clientList[i].ip) {
      return i;
    }
  }
  return -1;
}

//TCP Com
void broadcastTCP(char *data, int port) {
  for(int i = 0; i < clientCount; i++) {
    sendTCP(clientList[i].ip, port, data);
  }
}

void sendTCP(IPAddress ip, int port, char *msg) {
  client.connect(ip.toString(), port);
  client.print(msg);
  client.flush();
  client.stop();
}

void TCPHandling() {
  if (!client.connected()) {
    // try to connect to a new client
    client = server.available();
    delay(10);
  } 
  else {
    // read data from the connected client
    String data = "";
    char c;
    while(client.available() > 0) {
        c = client.read();
        data += c;
    }

    //If packet
    if(data != "") {
      Serial.printf("Packet received: ");
      Serial.println(data);
      IPAddress remoteip = client.remoteIP();
      int remoteport = tPort;
      if(data.indexOf("register") == 0) {
        char dt[50] = {};
        char *nick = NULL;
        strcpy(dt, data.c_str());
        pch = strtok(dt, ":");
        nick = strtok(NULL, ":");
        if(nick == NULL) {
          data="";
          Serial.println("Can't register without nick!");
          return;
        }
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
      else if(data == "LOCK") {
        if(!isLocked) {
          isLocked = true;
          digitalWrite(relay, HIGH);
          timer = false;
          fogging = false;
          broadcastTCP("locked",tPort);
        }
      }
      else if(data == "UNLOCK") {
        if(isLocked) {
          broadcastTCP("unlocked",tPort);
          isLocked = false;
        }
      }
      else if(data == "QF") {
        quickFog();
      }
      else if(data == "RED") {
        setRed();
      } 
      else if(data == "RESET") {
        resetState();
      }
      else if(data == "refresh") {
        refreshValues(remoteip, remoteport);
      }
      else if(data.indexOf("TIM") == 0) {
        char dt[50] = {};
        pch = NULL;
        strcpy(dt, data.c_str());
        pch = strtok(dt, ":");
        if(pch == NULL) {
          Serial.println("Can't set timer!");
          return;
        }
        pch = strtok(NULL, ":");
        if(pch == NULL) {
          Serial.println("Can't set timer!");
          return;
        }
        FogOn = atoi(pch);
        pch = strtok(NULL, ":");
        if(pch == NULL) {
          Serial.println("Can't set timer!");
          return;
        }
        FogOff = atoi(pch);
        setTimer(); 
      }
    }
  }
}

//Setup
void setup() {
  //Seriová linka
  Serial.begin(9600);

  if(!useAP) {
    //WiFi RouterConnect
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
  }
  else {
    //Access Point
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));   // subnet FF FF FF 00  
    WiFi.softAP(ssidAP, passwordAP);
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
  }

  //TCP Server
  server.begin();
  Serial.println("TCP Server started");

  //Relé
  pinMode(relay, OUTPUT);
  digitalWrite(relay, HIGH);
}

//Loop
void loop() {
  TCPHandling();
  timerCheck();
  qfTimer.update();

  //Check Heat
  if(analogRead(heated) > 600 && !isHeated) {
    isHeated = true;
    if(!fogging) {
      broadcastTCP("ready",tPort);
    }
    else if(timer && fogging) {
      broadcastTCP("ready",tPort);
      broadcastTCP("fog",tPort);
    }
    else {
      broadcastTCP("ready",tPort);
      broadcastTCP("redh",tPort);
    }
  }
  else if(analogRead(heated) < 100 && isHeated) {
    isHeated = false;
    broadcastTCP("cold",tPort);
  }
}
