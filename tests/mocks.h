// Host-side mocks of the Arduino/ESP8266 APIs used by aion.ino, so the sketch compiles and runs
// on macOS/Linux. Each mock records what the sketch did and lets the test script the hardware's answers.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <time.h>

// ---------- Arduino basics ----------

typedef uint8_t byte;

// NodeMCU pin names
#define D8 15

// Arduino-like String backed by std::string (only what the sketch uses)
class String {
public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    explicit String(char c) : s_(1, c) {}
    String(int n) : s_(std::to_string(n)) {}
    String(unsigned int n) : s_(std::to_string(n)) {}
    String(long n) : s_(std::to_string(n)) {}
    String(unsigned long n) : s_(std::to_string(n)) {}
    String(long long n) : s_(std::to_string(n)) {}

    unsigned int length() const { return s_.size(); }
    char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    const char* c_str() const { return s_.c_str(); }
    void toCharArray(char* buf, unsigned int size) const {
        if (size == 0) return;
        size_t n = s_.size() < size - 1 ? s_.size() : size - 1;
        memcpy(buf, s_.data(), n);
        buf[n] = '\0';
    }

    friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
    friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
    friend String operator+(const char* a, const String& b) { return String(a + b.s_); }
    friend String operator+(const String& a, char c) { return String(a.s_ + c); }
    friend bool operator==(const String& a, const String& b) { return a.s_ == b.s_; }
    friend bool operator!=(const String& a, const String& b) { return a.s_ != b.s_; }

private:
    std::string s_;
};

// controllable clock: tests set g_millis, and delay() advances it
extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
inline void delay(unsigned long ms) { g_millis += ms; }

struct SerialClass {
    void begin(unsigned long) {}
    void setDebugOutput(bool) {}
    template <typename T> void print(const T&) {}
    template <typename T> void println(const T&) {}
};
extern SerialClass Serial;

// ---------- time ----------

// time(nullptr) returns g_now (0 = NTP not synced yet). The macro is defined after <time.h> is included,
// so only the sketch and the tests see it
extern time_t g_now;
inline time_t mock_time(time_t* t) { if (t) *t = g_now; return g_now; }
#define time(t) mock_time(t)

// sets the process timezone, so localtime_r() applies the sketch's TZ rules (DST included) like the ESP core does
inline void configTzTime(const char* tz, const char*, const char* = nullptr, const char* = nullptr) {
    setenv("TZ", tz, 1);
    tzset();
}

// ---------- EEPROM ----------

// 512 bytes, blank (0xFF) like erased flash; counts commits
struct EEPROMClass {
    uint8_t data[512];
    int commits = 0;
    void begin(size_t) {}
    uint8_t read(int addr) const { return data[addr]; }
    void write(int addr, uint8_t value) { data[addr] = value; }
    bool commit() { commits++; return true; }
    void erase() { memset(data, 0xFF, sizeof(data)); commits = 0; }
};
extern EEPROMClass EEPROM;

// ---------- WiFi ----------

enum WiFiMode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };
enum wl_status_t { WL_IDLE_STATUS, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };

struct IPAddress {
    uint8_t a, b, c, d;
    IPAddress(uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0) : a(a), b(b), c(c), d(d) {}
};

// begin() switches status() to beginResult; the test picks the result before the sketch connects
struct WiFiClass {
    WiFiMode_t lastMode = WIFI_OFF;
    wl_status_t currentStatus = WL_DISCONNECTED;
    wl_status_t beginResult = WL_CONNECTED;
    int begins = 0, reconnects = 0, softAPs = 0, disconnects = 0;
    std::string beganSsid, beganPassword;

    void mode(WiFiMode_t m) { lastMode = m; }
    void begin(const char* ssid, const char* password) {
        begins++;
        beganSsid = ssid;
        beganPassword = password;
        currentStatus = beginResult;
    }
    wl_status_t status() const { return currentStatus; }
    void disconnect() { disconnects++; currentStatus = WL_DISCONNECTED; }
    void reconnect() { reconnects++; }
    void setAutoReconnect(bool) {}
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
    bool softAP(const char*, const char*) { softAPs++; return true; }
    IPAddress softAPIP() const { return IPAddress(192, 168, 4, 4); }
    IPAddress localIP() const { return IPAddress(); }
    void reset() { *this = WiFiClass(); }
};
extern WiFiClass WiFi;

// ---------- web server ----------

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };

// arg() reads from the args map the test fills; send() records the last reply
class ESP8266WebServer {
public:
    std::map<std::string, std::string> args;
    int sentStatus = 0;
    std::string sentBody;

    ESP8266WebServer(int) {}
    void on(const char*, void (*)()) {}
    void on(const char*, HTTPMethod, void (*)()) {}
    void begin() {}
    void handleClient() {}
    String arg(const char* name) const {
        auto it = args.find(name);
        return it == args.end() ? String() : String(it->second);
    }
    void send(int status, const char*, const String& body) { sentStatus = status; sentBody = body.c_str(); }
    void reset() { args.clear(); sentStatus = 0; sentBody.clear(); }
};

// ---------- LED matrix ----------

class MD_MAX72XX {
public:
    enum moduleType_t { PAROLA_HW, GENERIC_HW, ICSTATION_HW, FC16_HW };
};

enum textPosition_t { PA_LEFT, PA_CENTER, PA_RIGHT };
enum textEffect_t { PA_NO_EFFECT, PA_PRINT, PA_SCROLL_UP, PA_SCROLL_DOWN, PA_SCROLL_LEFT, PA_SCROLL_RIGHT };

// displayAnimate() returns animationDone (set by the test); records the text buffer and effects loaded
class MD_Parola {
public:
    bool animationDone = true;
    const char* textBuffer = nullptr;
    textEffect_t effectIn = PA_NO_EFFECT, effectOut = PA_NO_EFFECT;
    uint16_t pause = 0;
    int resets = 0;

    MD_Parola(MD_MAX72XX::moduleType_t, int, int) {}
    bool begin() { return true; }
    void setInvert(bool) {}
    void displayText(const char* text, textPosition_t, uint16_t, uint16_t pause, textEffect_t in, textEffect_t out) {
        textBuffer = text; this->pause = pause; effectIn = in; effectOut = out;
    }
    bool displayAnimate() { return animationDone; }
    void setTextBuffer(const char* text) { textBuffer = text; }
    void setTextEffect(textEffect_t in, textEffect_t out) { effectIn = in; effectOut = out; }
    void setPause(uint16_t ms) { pause = ms; }
    void displayReset() { resets++; }
    // the text currently shown ("" before anything was loaded)
    std::string shown() const { return textBuffer ? textBuffer : ""; }
};
