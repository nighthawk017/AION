// ESP32 builds are only used for the Wokwi simulator (Wokwi has no ESP8266); the real board is an ESP8266 NodeMCU
#if defined(ESP32)
#define WOKWI_SIMULATION 1
#include <WiFi.h>
#include <WebServer.h>
typedef WebServer ESP8266WebServer;
#else
#define WOKWI_SIMULATION 0
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#endif
#include <time.h>
#include <EEPROM.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>


// Define the hardware type and number of devices we have in the chain and the hardware interface
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

// Define pins for the led matrix
#if WOKWI_SIMULATION
// ESP32 VSPI default pins
#define CLK_PIN   18
#define DATA_PIN  23
#define CS_PIN    5
#else
#define CLK_PIN   D5
#define DATA_PIN  D7
#define CS_PIN    D8
#endif


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

//eeprom layout: each value is stored as a null-terminated string in a fixed slot, so a slot
//holds at most SIZE - 1 characters. The sizes follow the WiFi limits (SSID 32 bytes, WPA2 password 63 characters)
const int EEPROM_SIZE = 512;
const int SSID_ADDR = 0;
const int SSID_SIZE = 33;
const int PASSWORD_ADDR = SSID_ADDR + SSID_SIZE;
const int PASSWORD_SIZE = 64;
const int EVENT_TIME_ADDR = PASSWORD_ADDR + PASSWORD_SIZE;
const int EVENT_TIME_SIZE = 32;

time_t currentTime;

//when the target event takes place (it can be programmed when connecting to the board via wifi)
long int eventTime = 1632528000;
//number of days remaining until the target event
long int daysRemaining = 0;

//internal state
int state = 0;

//set by the /save handler; loop() connects to wifi once the http response had time to reach the browser
bool connectRequested = false;
unsigned long connectRequestedAt = 0;
//delay in ms between saving and switching off the access point to connect to wifi
const unsigned long CONNECT_REQUEST_DELAY = 1000;




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
  EEPROM.begin(EEPROM_SIZE);
  delay(10);
  ssid = readFromEeprom(SSID_ADDR, SSID_SIZE);
  PRINT("EEPROM ssid value: ");
  PRINTLN(ssid);

  password = readFromEeprom(PASSWORD_ADDR, PASSWORD_SIZE);
  PRINT("EEPROM password value: ");
  PRINTLN(password);

  char buf[EVENT_TIME_SIZE];
  char *eptr;
  readFromEeprom(EVENT_TIME_ADDR, EVENT_TIME_SIZE).toCharArray(buf, EVENT_TIME_SIZE);
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
    // state 2: the last connection attempt failed and the access point was restarted
    if (state == 2) {
      displayText("Incorrect wifi credentials. Connect to DATE TIMER WiFi access point, then access 192.168.4.4 to resubmit.");
    }
    else {
      displayText("Connect to DATE TIMER WiFi access point, then access 192.168.4.4 in your browser.");
    }
    if (connectRequested) {
      if (numberOfMilis - connectRequestedAt > CONNECT_REQUEST_DELAY) {
        connectRequested = false;
        // connectToWifi() sets the state to 1 on success and 2 on failure
        connectToWifi();
      }
    }
    else if (state == 0 && ((numberOfMilis) > STARTUP_TIMEOUT)) {
      PRINTLN(ssid);
      PRINTLN(password);
      if(ssid != SSID_DEFAULT_VALUE || password != PASSWORD_DEFAULT_VALUE ) {
        // connectToWifi() sets the state to 1 on success and 2 on failure
        if(connectToWifi() == 0) {
          PRINTLN("Automatic wifi connection successful.");
        }
        else {
          PRINTLN("Automatic wifi connection unsuccessful.");
        }
      }
      //TODO implement if ssid or password are changed then connect to wifi and change state to 1 (maybe if wifi connection fails retry in another 3 minutes)
    }
  }
}


//************************ WebServer ************************

#if WOKWI_SIMULATION
// Wokwi only offers its open "Wokwi-GUEST" network, so the simulator joins it as a station instead of
// running an access point. wokwi.toml forwards localhost:8180 to the web server on port 80.
const char* WOKWI_SSID = "Wokwi-GUEST";
const int WOKWI_CHANNEL = 6;

void initAccessPoint() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WOKWI_SSID, "", WOKWI_CHANNEL);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  PRINT("Wokwi IP address: ");
  PRINTLN(WiFi.localIP());
}
#else
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
#endif

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
          var url = '/save?ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password);\
          var targetDate = document.getElementById('target-date').valueAsNumber;\
          if (!isNaN(targetDate)) url += '&eventTime=' + encodeURIComponent(targetDate / 1000);\
          fetch(url) \
            .then(response => response.text().then(text => alert(text))) \
            .catch(() => alert(\"failed\")); \
        };\
      </script>\
    </body>\
  </html>");

  server.send ( 200, "text/html", htmlResponse );
}

// The handler used to process the form input proided by the user and save the data to eeprom.
// It only requests the wifi connection: loop() connects after the response is sent, because
// connecting shuts down the access point the browser is using
void handleSave() {
  // validate everything before saving anything, so a bad request leaves the stored values unchanged
  // (an empty date picker used to send "NaN", which was stored and parsed as 1970)
  long int newEventTime = 0;
  String eventTimeArg = server.arg("eventTime");
  if (eventTimeArg != "") {
    char *eptr;
    newEventTime = strtol(eventTimeArg.c_str(), &eptr, 10);
    if (*eptr != '\0' || newEventTime <= 0) {
      server.send(400, "text/plain", "Invalid target date, nothing was saved.");
      return;
    }
  }
  //values that do not fit their eeprom slot used to be skipped silently while the page reported success
  if (server.arg("ssid").length() >= SSID_SIZE) {
    server.send(400, "text/plain", "The WiFi name is too long (max " + String(SSID_SIZE - 1) + " bytes), nothing was saved.");
    return;
  }
  if (server.arg("password").length() >= PASSWORD_SIZE) {
    server.send(400, "text/plain", "The WiFi password is too long (max " + String(PASSWORD_SIZE - 1) + " characters), nothing was saved.");
    return;
  }

  if (server.arg("ssid") != "") {
    PRINTLN("ssid: " + server.arg("ssid"));
    writeToEeprom(SSID_ADDR, SSID_SIZE, server.arg("ssid"));
    ssid = server.arg("ssid");
  }
  if (server.arg("password") != "") {
    PRINTLN("password: " + server.arg("password"));
    password = server.arg("password");
    writeToEeprom(PASSWORD_ADDR, PASSWORD_SIZE, server.arg("password"));
  }
  if (server.arg("eventTime") != "") {
    PRINTLN("eventTime: " + server.arg("eventTime"));
    writeToEeprom(EVENT_TIME_ADDR, EVENT_TIME_SIZE, server.arg("eventTime"));
    eventTime = newEventTime;
  }
  server.send(200, "text/plain", "Saved. AION is now connecting to the WiFi network and this access point will close. "
    "On success the LED matrix shows the days remaining. On failure it shows an error and the DATE_TIMER access point comes back so you can resubmit.");
  connectRequested = true;
  connectRequestedAt = millis();
}

void checkConnection() {
  //TODO check if connection to wifi is established, if not, return to AP state to update logic
}

//connects to wifi and changes the internal state to 1 (display counter to target date).
//On failure it restarts the access point so the config page is reachable again, and changes the state to 2
int connectToWifi() {
  int retriesCount = 0;
  WiFi.mode(WIFI_STA);
  PRINTLN("\nConnecting to WiFi");
  PRINTLN(ssid);
  PRINTLN(password);
#if WOKWI_SIMULATION
  // The submitted credentials can't work in the simulator; stay on the Wokwi network
  WiFi.begin(WOKWI_SSID, "", WOKWI_CHANNEL);
#else
  WiFi.begin(ssid.c_str(), password.c_str());
#endif
  while (WiFi.status() != WL_CONNECTED) {
    if (retriesCount > 100) {
      PRINTLN("Check wifi credentials");
      // stop the station from retrying in the background, then bring the access point back
      WiFi.disconnect();
      initAccessPoint();
      state = 2;
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
#if WOKWI_SIMULATION
  WiFi.mode(WIFI_OFF);
#else
  WiFi.forceSleepBegin();                  // turn off ESP8266 RF
#endif
  delay(100);
}

//************************ END  WebServer ************************

//************************ EEPROM ************************

//writes value as a null-terminated string to the slot at startingAddress.
//Values that do not fit the slot (size - 1 characters) are not written; handleSave() rejects them first
void writeToEeprom(int startingAddress, int size, String value) {
  if (value.length() < size) {
    int i;
    for (i = 0; i < value.length(); i++) {
      EEPROM.write(startingAddress + i, value.charAt(i));
    }
    EEPROM.write(startingAddress + i, '\0');    
    EEPROM.commit();
  }
}


//reads the null-terminated string stored in the slot at startingAddress
String readFromEeprom(int startingAddress, int size) {
  String result = "";
  for (int i = 0; i < size; i++) {
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

void displayText(const char* text) {
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
