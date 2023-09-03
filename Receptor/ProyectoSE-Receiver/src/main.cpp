#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <RF24.h>
#include <nRF24L01.h>

#define FREQUENCY 4*432 //in  Hz
#define DOT_TIME 100 //ms
#define DASH_TIME 300 //ms
//LCD Display instance
LiquidCrystal_I2C lcd(0x27, 20, 4);

//RF chip instance
RF24 radio(4, 5);
const byte address[6] = "00001";

//Morse
const char* const morse[39] = {".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", 
                            "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.", "-----", ".-.-.-", "--..--", 
                            "..--.."};
// Values in morse array are as follows:
// A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, 1, 2, 3, 4, 5, 6, 
// 7, 8, 9, 0, DOT, COMMA, Q-MARK
const char characters[39] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.', ',', '?'};

//Pin assignment
const int buzzer = 14;

bool msgReceived = false;
char text[80];
int charCounter = 0;

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
  Serial.println(morseCode);
  for(int k = 0; k < strlen(morseCode); k++){
    playMorse(morseCode[k]);
  }
}

void textHandle(char text){
  if(text != ' '){
    for(int j = 0; j < 39; j++){
      if(text == characters[j]){ retrieveMorseCode(j); }
    }
    delay(DOT_TIME * 3);
  }
  else{ delay(DOT_TIME * 5); }
}

void setup() {
  Serial.begin(115200);

  pinMode(buzzer, OUTPUT);
  //Start LCD
  lcd.init();
  lcd.clear();
  lcd.backlight();
  lcd.setCursor(2, 0);
  lcd.print("Bienvenido");
  delay(3000);
  lcd.clear();
  lcd.print("Sin mensajes");   
  Serial.println("Esperando mensaje...");

  //Start RF communication - Receiver
  radio.begin();
  radio.openReadingPipe(0, address);
  radio.setPALevel(RF24_PA_MIN);
  radio.startListening();
  
}

void loop() {
  while(!msgReceived){
    if(radio.available()){
      char character = 0;
      radio.read(&character, sizeof(character));
      if(character == '/'){
        msgReceived = true;
        Serial.println("Got message.");
      }
      else{
        text[charCounter] = character;
        charCounter++;
      }
    }
  }
  
  if(msgReceived){
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("Mensaje entrante");
    delay(1000);
    
    lcd.clear();
    Serial.println(text);
    int row = 0;
    int col = 0;
    for(int curChar = 0; curChar <= charCounter; curChar++){
      if(col < 20){
        lcd.setCursor(col, row);
        lcd.print(text[curChar]);
        textHandle(text[curChar]);
        col++;
      }
      else{
        row++;
        col = 0;
      }
    }
    msgReceived = false;
    charCounter = 0;
    delay(1000);
    lcd.clear();
    lcd.print("Sin mensajes"); 
  }

}

