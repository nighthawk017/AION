// Host tests for aion.ino: the sketch is compiled against mocks.h (tests/stubs stand in for the Arduino headers)
#include "mocks.h"

// The Arduino IDE generates prototypes for the sketch's functions; a plain C++ compiler needs them up front
void initAccessPoint();
void initWebServer();
void handleRoot();
void handleSave();
void checkConnection(unsigned long now);
int connectToWifi();
void saveConfig();
void writeToEeprom(int startingAddress, int size, String value);
String readFromEeprom(int startingAddress, int size);
void displayNumber(long int number);
void displayText(const char* text);
long int daysFromCivil(int year, int month, int day);
void updateRemainingDays();

#include "../aion.ino"

// Definitions for the externs declared in mocks.h
unsigned long g_millis = 0;
time_t g_now = 0;
SerialClass Serial;
EEPROMClass EEPROM;
WiFiClass WiFi;

// ---------- minimal test framework ----------
static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT_EQ(got, expected) \
    do { \
        long long _g = (long long)(got), _e = (long long)(expected); \
        if (_g != _e) { \
            printf("\n  FAIL %s:%d  expected %lld, got %lld", __FILE__, __LINE__, _e, _g); \
            g_tests_failed++; \
        } \
    } while(0)
#define ASSERT_STR_EQ(got, expected) \
    do { \
        std::string _g = (got), _e = (expected); \
        if (_g != _e) { \
            printf("\n  FAIL %s:%d  expected \"%s\", got \"%s\"", __FILE__, __LINE__, _e.c_str(), _g.c_str()); \
            g_tests_failed++; \
        } \
    } while(0)
#define ASSERT_CONTAINS(got, part) \
    do { \
        std::string _g = (got), _p = (part); \
        if (_g.find(_p) == std::string::npos) { \
            printf("\n  FAIL %s:%d  \"%s\" does not contain \"%s\"", __FILE__, __LINE__, _g.c_str(), _p.c_str()); \
            g_tests_failed++; \
        } \
    } while(0)
#define ASSERT_TRUE(x)  ASSERT_EQ(!!(x), 1)
#define ASSERT_FALSE(x) ASSERT_EQ(!!(x), 0)

#define RUN(name) \
    do { \
        g_tests_run++; \
        printf("  %-55s", #name); \
        int _before = g_tests_failed; \
        resetState(); \
        name(); \
        printf("%s\n", g_tests_failed == _before ? "OK" : ""); \
    } while(0)

// puts the sketch and the mocks back to a freshly flashed board with blank EEPROM
static void resetState() {
    g_millis = 0;
    g_now = 0;
    EEPROM.erase();
    WiFi.reset();
    server.reset();
    P.animationDone = true;
    P.textBuffer = nullptr;
    P.resets = 0;

    ssid = SSID_DEFAULT_VALUE;
    password = PASSWORD_DEFAULT_VALUE;
    eventTime = 1632528000;
    state = CONFIG;
    connectRequested = false;
    connectRequestedAt = 0;
    connectionCheckedAt = 0;
    daysRemaining = 0;
    daysRemainingValid = false;
    daysRemainingUpdatedAt = 0;
    numberDisplayed = false;
    memset(screenText, 0, sizeof(screenText));
    configTzTime(TIMEZONE, "pool.ntp.org");
}

// ---------- harness ----------

static void test_setup_starts_access_point_in_config_mode() {
    setup();
    ASSERT_EQ(state, CONFIG);
    ASSERT_EQ(WiFi.lastMode, WIFI_AP);
    ASSERT_EQ(WiFi.softAPs, 1);
}

static void test_root_page_is_served() {
    handleRoot();
    ASSERT_EQ(server.sentStatus, 200);
    ASSERT_CONTAINS(server.sentBody, "fetch('/save', {method: 'POST'");
}

int main() {
    printf("harness\n");
    RUN(test_setup_starts_access_point_in_config_mode);
    RUN(test_root_page_is_served);

    printf("\n%d tests, %d failures\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
