#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include <Preferences.h>

#define CONNECTION_TIMEOUT 10
#define FREQUENCY 432 //in  Hz
#define DOT_TIME 100 //ms
#define DASH_TIME 300 //ms
#define CHAR_LIMIT 80
#define BOT_TOKEN "6386822226:AAHlNWddb87oV1AYglbRc4v2N2p7kl5YSIA"
#define CHAT_ID "1469540284"

//Bot config
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

//Mail service
String recipientMail;

//RF chip instance
RF24 radio(4, 5);
const byte address[6] = "00001";

//Available Access Point
const char* ssid = "POCO";
const char* password = "solidpass";

//ESP32 AP-mode Access Point
const char* ssid_esp32 = "ESP32-AP";
const char* password_esp32 = "morse123";

//Web Server
String ssid_WS;
String password_WS;
bool is_setup_done = false;
bool valid_ssid_received = false;
bool valid_password_received = false;
bool wifi_timeout = false;
DNSServer dnsServer;
AsyncWebServer server(80);
Preferences preferences;

const char index_html[] PROGMEM = R"=====(
  <!DOCTYPE html> <html>
    <head>
      <title>ESP32 Captive Portal</title>
      <style>
        b {color: red;}  
      </style>
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
    </head>
    <body>
      <h1>System status: <b>OFFLINE</b></h1>
      <p>Try adding a new Access Point</p>
      <form action="/get">
        <label for="ssid">SSID:</label><br>
        <input type="text" id="ssid" name="ssid"><br>
        <label for="password">Password:</label><br>
        <input type="text" id="password" name="password">
        <input type="submit" value="Connect">
      </form>
    </body>
  </html>
)=====";

//Global variables
bool isConnectedToWiFi = false;
bool botStarted = false;
bool activeServer = false;
char msg;

//Pin assignment
const int buzzer = 25;
const int extSwitch = 14;

//Morse
const char* const morse[39] = {".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", 
                            "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.", "-----", ".-.-.-", "--..--", 
                            "..--.."};
// Values in morse array are as follows:
// A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, 1, 2, 3, 4, 5, 6, 
// 7, 8, 9, 0, DOT, COMMA, Q-MARK
const char characters[39] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.', ',', '?'};

const char endChar = '/';

void buzzerTone(int freq, int playTime = DOT_TIME){
  tone(buzzer, freq);
  delay(playTime);
  noTone(buzzer);
  delay(DOT_TIME);
}

void playMorse(char code){
  if(code == '.'){
    buzzerTone(FREQUENCY);
  }
  else if(code == '-'){
    buzzerTone(FREQUENCY, DASH_TIME);
  }
}

void retrieveMorseCode(int characterIndex){
  const char* morseCode = morse[characterIndex];
  for(int k = 0; k < sizeof(morseCode); k++){
    playMorse(morseCode[k]);
  }
}

void textHandle(String text){
  for(int i = 0; i < text.length(); i++){
    if(text.charAt(i) != ' '){
      for(int j = 0; j < 39; j++){
        if(text.charAt(i) == characters[j]){ retrieveMorseCode(j); }
        delay(DOT_TIME * 3);
      }
    }
    else{ delay(DOT_TIME * 5); }
  }
}

void handleNewMessages(int numNewMessages){
  for(int i = 0; i < numNewMessages; i++){
    String chatID = String(bot.messages[i].chat_id);
    if(chatID != CHAT_ID){
      bot.sendMessage(chatID, "Unauthorized user", "");
      continue;
    }

    String text = bot.messages[i].text;
    String fromName = bot.messages[i].from_name;

    if(text == "/start" && !botStarted){
      String welcome = "Welcome.\n";
      welcome += "Type the message you want to send in Morse code. The system can handle no more than 80 characters so try to keep it short and simple.";
      bot.sendMessage(chatID, welcome, "");
      botStarted = true;
      Serial.println("App connection OK. Waiting for messages...");
    }
    else{

    }

    if(text == "/mail"){
      String info = "Setup an E-mail to send alerts. Please use the following format: +user@domain.com";
      bot.sendMessage(chatID, info, "");
    }

    else if(text == "/currentmail"){
      if(recipientMail){
        bot.sendMessage(chatID, recipientMail, "");
      }
      else{
        String noMailMsg = "No mail found. Use the command /mail to set a mail to receive alerts";
        bot.sendMessage(chatID, noMailMsg, "");
      }
    }

    if(text.startsWith("+")){
      recipientMail = text.substring(1);
      String mailSetup;
      if(!recipientMail){
        mailSetup += "Something went wrong. Try Again.";
      }
      else{
        mailSetup += "Mail set!";
      }
      bot.sendMessage(chatID, mailSetup, "");
    }

    else if(botStarted && !(text.startsWith("/") || text.startsWith("+"))){
      text.toUpperCase();
      if(text.length() > CHAR_LIMIT){
        bot.sendMessage(chatID, "Message too long. Try again.", "");
      }
      else{
        Serial.println(text);
        for(int pos = 0; pos < text.length() + 1; pos++){
          if(pos != text.length()){
            const char character = text.charAt(pos);
            radio.write(&character, sizeof(character));
            delay(10);
          }
          else{
            radio.write(&endChar, sizeof(endChar));
            delay(10);
          } 
        }
        //textHandle(text);
      }
    }
  }
}

class CaptiveRequestHandler : public AsyncWebHandler {
  public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request) {
      //request->addInterestingHeader("ANY");
      return true;
    }

    void handleRequest(AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", index_html);
    }
};

void setupServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
    Serial.println("Client Connected");
  });

  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String inputMessage;
    String inputParam;

    if (request->hasParam("ssid")) {
      inputMessage = request->getParam("ssid")->value();
      inputParam = "ssid";
      ssid_WS = inputMessage;
      Serial.println(inputMessage);
      valid_ssid_received = true;
    }

    if (request->hasParam("password")) {
      inputMessage = request->getParam("password")->value();
      inputParam = "password";
      password_WS = inputMessage;
      Serial.println(inputMessage);
      valid_password_received = true;
    }
    request->send(200, "text/html", "The values entered by you have been successfully sent to the device. It will now attempt WiFi connection");
  });
}

void WiFiSoftAPSetup()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_esp32,password_esp32, 6, 0, 1);

  // Disable AMPDU RX on the ESP32 WiFi to fix a bug on Android
	//esp_wifi_stop();
	//esp_wifi_deinit();
	//wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
	//my_config.ampdu_rx_enable = false;
	//esp_wifi_init(&my_config);
	//esp_wifi_start();
	//vTaskDelay(100 / portTICK_PERIOD_MS);  // Add a small delay
}

void StartCaptivePortal() {
  Serial.println("Setting up AP Mode");
  WiFiSoftAPSetup();
  Serial.println("Setting up Async WebServer");
  setupServer();
  Serial.println("Starting DNS Server");
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP
  server.begin();
  dnsServer.processNextRequest();
}

void WiFiStationSetup(String rec_ssid, String rec_password)
{
  wifi_timeout = false;
  WiFi.mode(WIFI_STA);
  char ssid_arr[20];
  char password_arr[20];
  rec_ssid.toCharArray(ssid_arr, rec_ssid.length() + 1);
  rec_password.toCharArray(password_arr, rec_password.length() + 1);
  Serial.print("Received SSID: "); Serial.println(ssid_arr); Serial.print("And password: "); Serial.println(password_arr);
  WiFi.begin(ssid_arr, password_arr);

  uint32_t t1 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    Serial.print(".");
    if (millis() - t1 > 50000) //50 seconds elapsed connecting to WiFi
    {
      Serial.println();
      Serial.println("Timeout connecting to WiFi. The SSID and Password seem incorrect.");
      valid_ssid_received = false;
      valid_password_received = false;
      is_setup_done = false;
      preferences.putBool("is_setup_done", is_setup_done);

      StartCaptivePortal();
      wifi_timeout = true;
      break;
    }
  }
  if (!wifi_timeout)
  {
    is_setup_done = true;
    Serial.println("");  Serial.print("WiFi connected to: "); Serial.println(rec_ssid);
    Serial.print("IP address: ");  Serial.println(WiFi.localIP());
    preferences.putBool("is_setup_done", is_setup_done);
    preferences.putString("rec_ssid", rec_ssid);
    preferences.putString("rec_password", rec_password);
  }
}

void STAConnectedToAP(WiFiEvent_t wifiEvent, WiFiEventInfo_t wifiInfo){
  isConnectedToWiFi = true;
}

void STAConnectionLost(WiFiEvent_t wifiEvent, WiFiEventInfo_t wifiInfo){
  //send mode change STA -> AP to app
  isConnectedToWiFi = false;
  buzzerTone(FREQUENCY, 500);
  Serial.println("Disconnected from WiFi. Generating AP...");
  
  WiFi.disconnect();   //added to start with the wifi off, avoid crashing
  WiFi.mode(WIFI_OFF); //added to start with the wifi off, avoid crashing

  StartCaptivePortal();
  while (!is_setup_done){
    dnsServer.processNextRequest();
    delay(10);
    if (valid_ssid_received && valid_password_received)
    {
      Serial.println("Attempting WiFi Connection!");
      WiFiStationSetup(ssid_WS, password_WS);
    }
  }
}

void startSystem(){
    WiFi.begin(ssid, password);
    Serial.println("\nConnecting");
    int timeoutCounter = 0;
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

    while(WiFi.status() != WL_CONNECTED){
      Serial.print(".");
      delay(200);
      timeoutCounter++;
      if(timeoutCounter >= CONNECTION_TIMEOUT * 10){
        StartCaptivePortal();
      }
    }
    Serial.println("\nConnected to the WiFi network");
    Serial.print("Local ESP32 IP: ");
    Serial.println(WiFi.localIP());

    //Start RF communication - Transmitter
    radio.begin();
    radio.openWritingPipe(address);
    radio.setPALevel(RF24_PA_MIN);
    radio.stopListening();
}

void IRAM_ATTR isr(){
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_14,1); //1 = High, 0 = Low
  Serial.println("Entering deep sleep...");
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  preferences.begin("my-pref", false);

  pinMode(buzzer, OUTPUT);
  pinMode(extSwitch,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(extSwitch),isr,FALLING);
  
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting");
  int timeoutCounter = 0;
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(200);
    timeoutCounter++;
    if(timeoutCounter >= CONNECTION_TIMEOUT * 10){
      ESP.restart();
    }
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());

  WiFi.onEvent(STAConnectedToAP, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(STAConnectionLost, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  //Start RF communication - Transmitter
  radio.begin();
  radio.openWritingPipe(address);
  radio.setPALevel(RF24_PA_MIN);
  radio.stopListening();
}

void loop() {
  if((millis() > lastTimeBotRan + botRequestDelay)){
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while(numNewMessages) {
    Serial.println("Got response");
    handleNewMessages(numNewMessages);
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
  lastTimeBotRan = millis();
  }
}
