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

//access point the config page is served on. Anyone who knows the AP password can change the board's
//wifi credentials and target date, so change it before flashing (WPA2 needs 8 to 63 characters)
const char* AP_SSID = "DATE_TIMER";
const char* AP_PASSWORD = "password";

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
//written after the values on every save. Blank eeprom (all 0xFF) or data from an older layout has no marker
//and is treated as "not configured"; change the marker value whenever the layout changes
const int CONFIG_MARKER_ADDR = EVENT_TIME_ADDR + EVENT_TIME_SIZE;
const byte CONFIG_MARKER = 0xA1;

time_t currentTime;

//local timezone as a POSIX TZ string (DST-aware), used to decide when a new day starts.
//Central European Time; e.g. "EET-2EEST,M3.5.0/3,M10.5.0/4" for Eastern Europe or "GMT0BST,M3.5.0/1,M10.5.0" for the UK
const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

//when the target event takes place (it can be programmed when connecting to the board via wifi).
//A 32-bit time would stop at 2038-01-19; both cores use a 64-bit time_t
static_assert(sizeof(time_t) >= 8, "time_t must be 64-bit to hold dates past 2038");
time_t eventTime = 1632528000;
//latest accepted target date (9999-12-31 23:59:59 UTC), so gmtime_r() always gets a representable date
const time_t MAX_EVENT_TIME = 253402300799LL;
//number of days remaining until the target event
long int daysRemaining = 0;
//false until daysRemaining has been computed in the current countdown (reset on every connect)
bool daysRemainingValid = false;
//millis() of the last daysRemaining update, and the interval between updates
unsigned long daysRemainingUpdatedAt = 0;
const unsigned long DAYS_UPDATE_INTERVAL = 10000;
//time(nullptr) stays near 1970 until NTP has synced; any later time means the clock is set (2020-01-01)
const time_t TIME_SYNCED_AFTER = 1577836800;

//internal state
int state = 0;

//how often the wifi connection is checked while counting down, and when it was last checked
const unsigned long CONNECTION_CHECK_INTERVAL = 30000;
unsigned long connectionCheckedAt = 0;

//set by the /save handler; loop() connects to wifi once the http response had time to reach the browser
bool connectRequested = false;
unsigned long connectRequestedAt = 0;
//delay in ms between saving and switching off the access point to connect to wifi
const unsigned long CONNECT_REQUEST_DELAY = 1000;




void setup() {
  //set up serial 
  Serial.begin(115200);
  delay(10);
  // the wifi core's own debug output is as noisy as the sketch's, so it follows the same flag
#if DEBUG
  Serial.setDebugOutput(true);
#endif

  //initialize webserver
  initAccessPoint();
  initWebServer();
  
  //configure time
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  //Initialize EEPROM and read the saved data from eeprom
  EEPROM.begin(EEPROM_SIZE);
  delay(10);
  //without the marker the defaults are kept, so the credentials stay at their default values and the board never auto-connects
  if (EEPROM.read(CONFIG_MARKER_ADDR) == CONFIG_MARKER) {
    ssid = readFromEeprom(SSID_ADDR, SSID_SIZE);
    PRINT("EEPROM ssid value: ");
    PRINTLN(ssid);

    //the password is never printed, not even in debug builds
    password = readFromEeprom(PASSWORD_ADDR, PASSWORD_SIZE);

    char buf[EVENT_TIME_SIZE];
    char *eptr;
    readFromEeprom(EVENT_TIME_ADDR, EVENT_TIME_SIZE).toCharArray(buf, EVENT_TIME_SIZE);
    eventTime = strtoll(buf , &eptr, 10);

    PRINTLN("EEPROM eventvalue value: ");
    PRINTLN(eventTime);
  }
  else {
    PRINTLN("EEPROM holds no saved configuration");
  }
  
  // initialize and display start text on led matrix
  P.begin();
  P.setInvert(false);
  P.displayText("", PA_CENTER, SPEED_TIME, PAUSE_TIME, effect, effect);

}

void loop() {
  unsigned long numberOfMilis = millis();
  
  if (state == 1) {
    checkConnection(numberOfMilis);
    // before NTP has synced the computed days would be based on 1970
    if (time(nullptr) < TIME_SYNCED_AFTER) {
      displayText("Syncing time...");
    }
    else {
      // compute right away after connecting, then every DAYS_UPDATE_INTERVAL
      if (!daysRemainingValid || numberOfMilis - daysRemainingUpdatedAt >= DAYS_UPDATE_INTERVAL) {
        updateRemainingDays();
        daysRemainingValid = true;
        daysRemainingUpdatedAt = numberOfMilis;
        PRINTLN("Updated remaining days");
      }
      displayNumber(daysRemaining);
    }
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
  WiFi.softAP(AP_SSID, AP_PASSWORD);
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
  // POST keeps the credentials out of the URL (browser history, logs)
  server.on ("/save", HTTP_POST, handleSave);

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
            <input type='password' name='password' id='password' size=64>  \
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
          var body = new URLSearchParams({ssid: ssid, password: password});\
          var targetDate = document.getElementById('target-date').valueAsNumber;\
          if (!isNaN(targetDate)) body.append('eventTime', targetDate / 1000);\
          fetch('/save', {method: 'POST', body: body}) \
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
  time_t newEventTime = 0;
  String eventTimeArg = server.arg("eventTime");
  if (eventTimeArg != "") {
    char *eptr;
    newEventTime = strtoll(eventTimeArg.c_str(), &eptr, 10);
    if (*eptr != '\0' || newEventTime <= 0 || newEventTime > MAX_EVENT_TIME) {
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
    ssid = server.arg("ssid");
  }
  if (server.arg("password") != "") {
    password = server.arg("password");
  }
  if (server.arg("eventTime") != "") {
    PRINTLN("eventTime: " + server.arg("eventTime"));
    eventTime = newEventTime;
  }
  saveConfig();
  server.send(200, "text/plain", "Saved. AION is now connecting to the WiFi network and this access point will close. "
    "On success the LED matrix shows the days remaining. On failure it shows an error and the DATE_TIMER access point comes back so you can resubmit.");
  connectRequested = true;
  connectRequestedAt = millis();
}

//called from loop() in state 1: restarts the connection when it dropped (e.g. after a router restart).
//WiFi.reconnect() does not block, so the countdown keeps running from the system clock meanwhile,
//and SNTP resyncs on its own once the connection is back
void checkConnection(unsigned long now) {
  if (now - connectionCheckedAt < CONNECTION_CHECK_INTERVAL) {
    return;
  }
  connectionCheckedAt = now;
  if (WiFi.status() != WL_CONNECTED) {
    PRINTLN("WiFi connection lost, reconnecting");
    WiFi.reconnect();
  }
}

//connects to wifi and changes the internal state to 1 (display counter to target date).
//On failure it restarts the access point so the config page is reachable again, and changes the state to 2
int connectToWifi() {
  int retriesCount = 0;
  WiFi.mode(WIFI_STA);
  PRINTLN("\nConnecting to WiFi");
  PRINTLN(ssid);
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
  // checkConnection() takes over from here
  WiFi.setAutoReconnect(true);
  connectionCheckedAt = millis();
  state = 1;
  daysRemainingValid = false;
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

//writes every value, including the ones that were not changed, so all slots hold valid strings
//once the marker is set. The marker is written last so an interrupted first save leaves the board unconfigured
void saveConfig() {
  writeToEeprom(SSID_ADDR, SSID_SIZE, ssid);
  writeToEeprom(PASSWORD_ADDR, PASSWORD_SIZE, password);
  writeToEeprom(EVENT_TIME_ADDR, EVENT_TIME_SIZE, String(eventTime));
  EEPROM.write(CONFIG_MARKER_ADDR, CONFIG_MARKER);
  EEPROM.commit();
}

//writes value as a null-terminated string to the slot at startingAddress (call EEPROM.commit() afterwards).
//Values that do not fit the slot (size - 1 characters) are not written; handleSave() rejects them first
void writeToEeprom(int startingAddress, int size, String value) {
  if (value.length() < size) {
    int i;
    for (i = 0; i < value.length(); i++) {
      EEPROM.write(startingAddress + i, value.charAt(i));
    }
    EEPROM.write(startingAddress + i, '\0');
  }
}


//reads the null-terminated string stored in the slot at startingAddress
String readFromEeprom(int startingAddress, int size) {
  String result = "";
  for (int i = 0; i < size; i++) {
    char currentChar;
    currentChar = EEPROM.read(startingAddress + i);
    //stop before appending the terminator, otherwise the String keeps an embedded NUL and its
    //length and == comparisons (e.g. against the default credentials) are off
    if(currentChar == '\0') {
      break;
    }
    result = result + currentChar;
  }
  return result;
}

//************************ END  EEPROM ************************

//************************ LED Matrix ************************

//true while screenText holds the number last passed to displayNumber() (displayedNumber)
bool numberDisplayed = false;
long int displayedNumber = 0;

//shows the number without scrolling. It replaces the current content right away when the number changed
//or text is shown, instead of waiting for the current animation (or the 60 s pause) to finish
void displayNumber(long int number) {
  bool animationFinished = P.displayAnimate(); // animates and returns true when an animation is completed
  if (animationFinished || !numberDisplayed || number != displayedNumber)
  {
      numberDisplayed = true;
      displayedNumber = number;
      sprintf(screenText, "%ld", number); 
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
      numberDisplayed = false;
      sprintf(screenText, "%s", text); 
      // Set the display for the next string. 
      P.setTextBuffer(screenText);
      // When we have gone back to the first string, set a new exit effect
      // and when we have done all those set a new entry effect.
      P.setTextEffect(effect, effect);
      // displayNumber() sets a 60 s pause
      P.setPause(PAUSE_TIME);
      // Tell Parola we have a new animation
      P.displayReset();
  }  
  
}

//************************ END  LED Matrix ************************

//************************ Timer Logic ************************

//number of days from 1970-01-01 to the given calendar date (month 1-12), for any year
//(H. Hinnant's days_from_civil). Lets two dates be compared without time of day or timezone getting in the way
long int daysFromCivil(int year, int month, int day) {
  year -= month <= 2;
  long int era = (year >= 0 ? year : year - 399) / 400;
  long int yearOfEra = year - era * 400;
  long int dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  long int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + dayOfEra - 719468;
}

//days remaining = target date - today's local date, counted in calendar days:
//target tomorrow -> 1, today -> 0, yesterday -> -1. The count changes at local midnight
void updateRemainingDays() {
  currentTime = time(nullptr);
  struct tm now;
  localtime_r(&currentTime, &now);
  long int today = daysFromCivil(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
  // the date picker sends UTC midnight of the chosen date, so the target date is eventTime's UTC date
  struct tm event;
  gmtime_r(&eventTime, &event);
  long int eventDay = daysFromCivil(event.tm_year + 1900, event.tm_mon + 1, event.tm_mday);
  daysRemaining = eventDay - today;
  PRINTLN(ctime(&currentTime));
  PRINTLN(daysRemaining);
}

//************************ END  Timer Logic ************************
