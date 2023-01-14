#include <ESP8266WiFi.h>
#include <time.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>


// Define the hardware type and number of devices we have in the chain and the hardware interface
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

// Define pins for the led matrix
#define CLK_PIN   D5
#define DATA_PIN  D7
#define CS_PIN    D8


// Hardware SPI connection
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary output pins
// MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// speed with which the text is scrolling on the led matrix (text scrolls every SPEED_TIME ms)
#define SPEED_TIME  50
// pause time in ms for led matrix
#define PAUSE_TIME  3000

// Turn on debug statements to the serial output (set it to 1 to turn on debug, and set it to 0 to turn of debug)
#define  DEBUG  0

#if  DEBUG
#define PRINT(x) Serial.print(x)
#define PRINTLN(x) Serial.println(x)
#else
#define PRINT(x)
#define PRINTLN(x)
#endif


// text effect used for led matrix text (currently using scroll left effect)
textEffect_t  effect = PA_SCROLL_LEFT;

//variable containing the text displayed on the led matrix. It is initialez with the text desplayed before esp is set up
char screenText[128] = "Connect DATE TIMER WiFi access point, then access 192.168.4.4 in your browser.";

//default wifi credentials
String SSID_DEFAULT_VALUE = "default-wifi-network";
String PASSWORD_DEFAULT_VALUE = "default-wifi-password";

//variables holding the wifi credentials
String password = PASSWORD_DEFAULT_VALUE; 
String ssid = SSID_DEFAULT_VALUE; 

//server port
ESP8266WebServer server (80);

// 
char htmlResponse[3000];

//timeout time before the board tries to automatically connect to wifi (using the data saved in eeprom)
const unsigned long STARTUP_TIMEOUT = 180000;

//field length in bytes for eeprom data
const int MAX_FIELD_LENGTH = 32;

time_t currentTime;

//when the target event takes place (it can be programmed when connecting to the board via wifi)
long int eventTime = 1632528000;
//number of days remaining until the target event
long int daysRemaining = 0;

//internal state 
int state = 0;




void setup() {
  //set up serial 
  Serial.begin(115200);
  delay(10);
  Serial.setDebugOutput(true);

  //initialize webserver
  initAccessPoint();
  initWebServer();
  
  //configure time
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");

  //Initialize EEPROM and read the saved data from eeprom
  EEPROM.begin(512);  
  delay(10);
  ssid = readFromEeprom(0);
  PRINT("EEPROM ssid value: ");
  PRINTLN(ssid);

  password = readFromEeprom(1);
  PRINT("EEPROM password value: ");
  PRINTLN(password);

  char buf[32];
  char *eptr;
  readFromEeprom(2).toCharArray(buf, 32);
  eventTime = strtol(buf , &eptr, 10);
  
  PRINTLN("EEPROM eventvalue value: ");
  PRINTLN(eventTime);
  
  // initialize and display start text on led matrix
  P.begin();
  P.setInvert(false);
  P.displayText("", PA_CENTER, SPEED_TIME, PAUSE_TIME, effect, effect);

}

void loop() {
  unsigned long numberOfMilis = millis();
  
  if (state == 1) {
    if ((numberOfMilis % 10000) < 5) {
      updateRemainingDays();      
      PRINTLN("Updated remaining days");
    }
    displayNumber(daysRemaining);    
  }

  else {
    server.handleClient();
    displayText("Connect to DATE TIMER WiFi access point, then access 192.168.4.4 in your browser.");
    if (state == 0 && ((numberOfMilis) > STARTUP_TIMEOUT)) {
      PRINTLN(ssid);
      PRINTLN(password);
      if(ssid != SSID_DEFAULT_VALUE || password != PASSWORD_DEFAULT_VALUE ) {
        if(connectToWifi() == 0) {
          PRINTLN("Automatic wifi connection successful.");
          state = 1;
        }
          
        else {
          state = 2;
          PRINTLN("Automatic wifi connection unsuccessful.");
        }         
      }
      //TODO implement if ssid or password are changed then connect to wifi and change state to 1 (maybe if wifi connection fails retry in another 3 minutes)
    }
  }
}


//************************ WebServer ************************

//Initializes the board as access point
void initAccessPoint() {
  PRINTLN("\nSetting AP (Access Point)…");
  WiFi.mode(WIFI_AP);
  IPAddress local_ip(192, 168, 4, 4);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  // Remove the password parameter, if you want the AP (Access Point) to be open
  WiFi.softAP("DATE_TIMER", "password"); //TODO  extract variables outside
  delay(100);

  IPAddress IP = WiFi.softAPIP();
  PRINT("AP IP address: ");
  PRINTLN(IP);

  // Print ESP8266 Local IP Address
  PRINTLN(WiFi.softAPIP());
}

//initializez webserver, sets the handlers & starts the web server
void initWebServer() {
  server.on ( "/", handleRoot );
  server.on ("/save", handleSave);

  server.begin();
  PRINTLN( "HTTP server started" );
}

//The webpage containing the form in which the wifi setting and the target date are set
void handleRoot() {
  snprintf ( htmlResponse, 3000, "<!DOCTYPE html>\
  <html lang=\"en\">\
    <head>\
      <meta charset=\"utf-8\">\
      <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
    </head>\
    <body>\
            <h1>ssid</h1>\
            <input type='text' name='ssid' id='ssid' size=64 autofocus>  \
            <h1>password</h1>\
            <input type='text' name='password' id='password' size=64 autofocus>  \
            <h1>Timer target date</h1>\
            <input type='date' id='target-date'> \
            <div>\
            <br><button onclick=\"handleSaveClick()\" id=\"save_button\">Save</button>\
            </div>\
      <script>\
        var ssid;\
        function handleSaveClick(){\
          ssid = document.getElementById('ssid').value;\
          var password = document.getElementById('password').value;\
          var eventTime = document.getElementById('target-date').valueAsNumber / 1000;\
          fetch('/save?ssid=' + ssid + '&password=' + password + '&eventTime=' + eventTime) \
            .then(response => { \
              if(response.status === 200) alert(\"success\"); \
              else alert(\"failed\");}); \
        };\
      </script>\
    </body>\
  </html>");

  server.send ( 200, "text/html", htmlResponse );
}

// The handler used to process the form input proided by the user and save the data to eeprom.
// After saving the data in eeprom, it tries to connect to wifi
void handleSave() {
  if (server.arg("ssid") != "") {
    PRINTLN("ssid: " + server.arg("ssid"));
    writeToEeprom(0, server.arg("ssid"));
    ssid = server.arg("ssid");
  }
  if (server.arg("password") != "") {
    PRINTLN("password: " + server.arg("password"));
    password = server.arg("password");
    writeToEeprom(MAX_FIELD_LENGTH, server.arg("password"));
  }
  if (server.arg("eventTime") != "") {
    PRINTLN("eventTime: " + server.arg("eventTime"));
    char buf[32];
    char *eptr;
    server.arg("eventTime").toCharArray(buf, 32);
    writeToEeprom(2*MAX_FIELD_LENGTH, server.arg("eventTime"));
    eventTime = strtol(buf , &eptr, 10);
  }
  server.send(200);
  delay(100);
  connectToWifi();
}

void checkConnection() {
  //TODO check if connection to wifi is established, if not, return to AP state to update logic
}

//connects to wifi and changes the internal state to 1 (display counter to target date)
int connectToWifi() {
  int retriesCount = 0;
  WiFi.mode(WIFI_STA);
  PRINTLN("\nConnecting to WiFi");
  PRINTLN(ssid);
  PRINTLN(password);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    if (retriesCount > 100) {
      PRINTLN("Check wifi credentials");
      displayText("Incorrect wifi credentials. Please resubmit wifi credentials.");
      return 1;
    }
    else {
      PRINT(".");
      delay(1000);
      retriesCount++;
    }    
  }
  state = 1;
  return 0;
}



void disconnectFromWifi() {
  WiFi.forceSleepBegin();                  // turn off ESP8266 RF
  delay(100);
}

//************************ END  WebServer ************************

//************************ EEPROM ************************

void writeToEeprom(int startingAddress, String value) {
  if (value.length() < MAX_FIELD_LENGTH) {
    int i;
    for (i = 0; i < value.length(); i++) {
      EEPROM.write(startingAddress + i, value.charAt(i));
    }
    EEPROM.write(startingAddress + i, '\0');    
    EEPROM.commit();
  }
}


String readFromEeprom(int fieldIndex) {
  int startingAddress = fieldIndex * MAX_FIELD_LENGTH;
  String result = "";
  for (int i = 0; i < MAX_FIELD_LENGTH; i++) {
    char currentChar;
    currentChar = EEPROM.read(startingAddress + i);
    result = result + currentChar;
    if(currentChar == '\0') {
      break;
    }
  }
  PRINTLN(result);
  return result;
}

//************************ END  EEPROM ************************

//************************ LED Matrix ************************

void displayNumber(int number) {
  if (P.displayAnimate()) // animates and returns true when an animation is completed
  {
      sprintf(screenText, "%d", number); 
      // Set the display for the next string. 
      P.setTextBuffer(screenText);
      // When we have gone back to the first string, set a new exit effect
      // and when we have done all those set a new entry effect.
      P.setTextEffect(PA_PRINT, PA_PRINT);
      P.setPause(60000);
      // Tell Parola we have a new animation
      P.displayReset();
  }  
  
}

void displayText(char* text) {
  if (P.displayAnimate()) // animates and returns true when an animation is completed
  {
      sprintf(screenText, "%s", text); 
      // Set the display for the next string. 
      P.setTextBuffer(screenText);
      // When we have gone back to the first string, set a new exit effect
      // and when we have done all those set a new entry effect.
      P.setTextEffect(effect, effect);
      // Tell Parola we have a new animation
      P.displayReset();
  }  
  
}

//************************ END  LED Matrix ************************

//************************ Timer Logic ************************

void updateRemainingDays() {
  currentTime = time(nullptr);
  long int timeRemaining = eventTime - currentTime;
  long int hoursRemaining = timeRemaining / 3600;
  daysRemaining = hoursRemaining / 24;
  PRINTLN(ctime(&currentTime));
  PRINTLN(daysRemaining);
}

//************************ END  Timer Logic ************************
