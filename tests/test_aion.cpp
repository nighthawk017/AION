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

// Unix time of the given UTC date and time
static time_t utc(int year, int month, int day, int hour = 0, int minute = 0) {
    return (time_t)daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60;
}

// days remaining at "now" for a target date (stored, like the page sends it, as UTC midnight)
static long daysUntil(time_t now, int year, int month, int day) {
    g_now = now;
    eventTime = utc(year, month, day);
    updateRemainingDays();
    return daysRemaining;
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

// ---------- EEPROM storage ----------

static void test_eeprom_slots_round_trip() {
    writeToEeprom(SSID_ADDR, SSID_SIZE, "home-network");
    writeToEeprom(PASSWORD_ADDR, PASSWORD_SIZE, "s3cret pass");
    writeToEeprom(EVENT_TIME_ADDR, EVENT_TIME_SIZE, "1767225600");
    String readSsid = readFromEeprom(SSID_ADDR, SSID_SIZE);
    // the length catches a NUL terminator kept in the String
    ASSERT_EQ(readSsid.length(), 12);
    ASSERT_STR_EQ(readSsid.c_str(), "home-network");
    ASSERT_STR_EQ(readFromEeprom(PASSWORD_ADDR, PASSWORD_SIZE).c_str(), "s3cret pass");
    ASSERT_STR_EQ(readFromEeprom(EVENT_TIME_ADDR, EVENT_TIME_SIZE).c_str(), "1767225600");
}

static void test_eeprom_value_filling_the_slot_round_trips() {
    std::string longest(PASSWORD_SIZE - 1, 'p');
    writeToEeprom(PASSWORD_ADDR, PASSWORD_SIZE, longest.c_str());
    ASSERT_STR_EQ(readFromEeprom(PASSWORD_ADDR, PASSWORD_SIZE).c_str(), longest);
}

static void test_eeprom_read_matches_default_credentials() {
    writeToEeprom(SSID_ADDR, SSID_SIZE, SSID_DEFAULT_VALUE);
    ASSERT_TRUE(readFromEeprom(SSID_ADDR, SSID_SIZE) == SSID_DEFAULT_VALUE);
}

static void test_eeprom_too_long_value_is_not_written() {
    writeToEeprom(SSID_ADDR, SSID_SIZE, "old-ssid");
    writeToEeprom(PASSWORD_ADDR, PASSWORD_SIZE, "neighbour");
    std::string tooLong(SSID_SIZE, 's');
    writeToEeprom(SSID_ADDR, SSID_SIZE, tooLong.c_str());
    ASSERT_STR_EQ(readFromEeprom(SSID_ADDR, SSID_SIZE).c_str(), "old-ssid");
    ASSERT_STR_EQ(readFromEeprom(PASSWORD_ADDR, PASSWORD_SIZE).c_str(), "neighbour");
}

static void test_eeprom_save_config_commits_once_and_sets_marker() {
    ssid = "home";
    password = "pass";
    eventTime = 1767225600;
    saveConfig();
    ASSERT_EQ(EEPROM.commits, 1);
    ASSERT_EQ(EEPROM.read(CONFIG_MARKER_ADDR), CONFIG_MARKER);
}

static void test_eeprom_saved_config_is_loaded_on_boot() {
    ssid = "home";
    password = "pass";
    eventTime = 1767225600;
    saveConfig();
    ssid = SSID_DEFAULT_VALUE;
    password = PASSWORD_DEFAULT_VALUE;
    eventTime = 0;
    setup();
    ASSERT_STR_EQ(ssid.c_str(), "home");
    ASSERT_STR_EQ(password.c_str(), "pass");
    ASSERT_EQ(eventTime, 1767225600);
}

static void test_eeprom_blank_reads_as_not_configured() {
    // erased flash is all 0xFF, which used to be read back as 33/64 bytes of 0xFF credentials
    setup();
    ASSERT_TRUE(ssid == SSID_DEFAULT_VALUE);
    ASSERT_TRUE(password == PASSWORD_DEFAULT_VALUE);
    ASSERT_EQ(eventTime, 1632528000);
}

static void test_eeprom_data_without_marker_is_ignored() {
    // a layout from before the marker existed
    writeToEeprom(SSID_ADDR, SSID_SIZE, "old-layout");
    setup();
    ASSERT_TRUE(ssid == SSID_DEFAULT_VALUE);
}

// ---------- day calculation (timezone CET/CEST) ----------

static void test_days_from_civil_known_dates() {
    ASSERT_EQ(daysFromCivil(1970, 1, 1), 0);
    ASSERT_EQ(daysFromCivil(2000, 3, 1), 11017);
    ASSERT_EQ(daysFromCivil(2038, 1, 19), 24855);
    ASSERT_EQ(daysFromCivil(1969, 12, 31), -1);
}

static void test_days_tomorrow_today_yesterday() {
    time_t now = utc(2026, 6, 15, 10);
    ASSERT_EQ(daysUntil(now, 2026, 6, 16), 1);
    ASSERT_EQ(daysUntil(now, 2026, 6, 15), 0);
    ASSERT_EQ(daysUntil(now, 2026, 6, 14), -1);
}

static void test_days_100_days_ahead() {
    ASSERT_EQ(daysUntil(utc(2026, 6, 15, 10), 2026, 9, 23), 100);
}

static void test_days_past_2038() {
    ASSERT_EQ(daysUntil(utc(2026, 6, 15, 10), 2040, 1, 1), 4948);
}

static void test_days_change_at_local_midnight_not_utc() {
    // 23:30 CEST on Jul 10 is still Jul 10, 00:30 CEST on Jul 11 is already Jul 11 (both are Jul 10 in UTC)
    ASSERT_EQ(daysUntil(utc(2026, 7, 10, 21, 30), 2026, 7, 11), 1);
    ASSERT_EQ(daysUntil(utc(2026, 7, 10, 22, 30), 2026, 7, 11), 0);
    // winter: CET is UTC+1
    ASSERT_EQ(daysUntil(utc(2026, 1, 10, 23, 30), 2026, 1, 11), 0);
}

static void test_days_across_spring_dst_change() {
    // DST starts on 2026-03-29, a 23-hour day
    ASSERT_EQ(daysUntil(utc(2026, 3, 28, 23, 30), 2026, 3, 30), 1);
    ASSERT_EQ(daysUntil(utc(2026, 3, 29, 22, 30), 2026, 3, 30), 0);
    ASSERT_EQ(daysUntil(utc(2026, 3, 27, 12), 2026, 4, 3), 7);
}

static void test_days_across_autumn_dst_change() {
    // DST ends on 2026-10-25, a 25-hour day
    ASSERT_EQ(daysUntil(utc(2026, 10, 24, 22, 30), 2026, 10, 26), 1);
    ASSERT_EQ(daysUntil(utc(2026, 10, 25, 23, 30), 2026, 10, 26), 0);
    ASSERT_EQ(daysUntil(utc(2026, 10, 23, 12), 2026, 10, 30), 7);
}

static void test_days_syncing_until_ntp_sets_the_clock() {
    state = COUNTDOWN;
    WiFi.currentStatus = WL_CONNECTED;
    eventTime = utc(2026, 6, 16);
    g_now = 0;
    loop();
    ASSERT_STR_EQ(P.shown(), "Syncing time...");
    ASSERT_FALSE(daysRemainingValid);
    // NTP synced
    g_now = utc(2026, 6, 15, 10);
    loop();
    ASSERT_STR_EQ(P.shown(), "1");
    ASSERT_EQ(P.effectIn, PA_PRINT);
}

static void test_days_recomputed_every_update_interval() {
    state = COUNTDOWN;
    WiFi.currentStatus = WL_CONNECTED;
    eventTime = utc(2026, 6, 16);
    g_now = utc(2026, 6, 15, 21, 59);
    loop();
    ASSERT_STR_EQ(P.shown(), "1");
    // local midnight passes, but the count only updates after DAYS_UPDATE_INTERVAL
    g_now = utc(2026, 6, 15, 22, 0);
    g_millis += DAYS_UPDATE_INTERVAL - 1;
    loop();
    ASSERT_STR_EQ(P.shown(), "1");
    g_millis += 1;
    loop();
    ASSERT_STR_EQ(P.shown(), "0");
}

// ---------- /save handler ----------

static void test_save_stores_and_applies_all_fields() {
    server.args = {{"ssid", "home"}, {"password", "pass1234"}, {"eventTime", "1767225600"}};
    handleSave();
    ASSERT_EQ(server.sentStatus, 200);
    ASSERT_STR_EQ(ssid.c_str(), "home");
    ASSERT_STR_EQ(password.c_str(), "pass1234");
    ASSERT_EQ(eventTime, 1767225600);
    ASSERT_STR_EQ(readFromEeprom(SSID_ADDR, SSID_SIZE).c_str(), "home");
    ASSERT_STR_EQ(readFromEeprom(PASSWORD_ADDR, PASSWORD_SIZE).c_str(), "pass1234");
    ASSERT_STR_EQ(readFromEeprom(EVENT_TIME_ADDR, EVENT_TIME_SIZE).c_str(), "1767225600");
    ASSERT_EQ(EEPROM.read(CONFIG_MARKER_ADDR), CONFIG_MARKER);
}

static void test_save_empty_fields_keep_current_values() {
    ssid = "home";
    password = "pass1234";
    eventTime = 1767225600;
    server.args = {{"ssid", ""}, {"password", ""}, {"eventTime", ""}};
    handleSave();
    ASSERT_EQ(server.sentStatus, 200);
    ASSERT_STR_EQ(ssid.c_str(), "home");
    ASSERT_STR_EQ(password.c_str(), "pass1234");
    ASSERT_EQ(eventTime, 1767225600);
}

static void test_save_missing_event_time_keeps_stored_date() {
    eventTime = 1767225600;
    server.args = {{"ssid", "home"}, {"password", "pass1234"}};
    handleSave();
    ASSERT_EQ(server.sentStatus, 200);
    ASSERT_EQ(eventTime, 1767225600);
}

static void test_save_nan_event_time_is_rejected() {
    // an empty date picker used to send "NaN", stored as 1970
    eventTime = 1767225600;
    server.args = {{"ssid", "home"}, {"eventTime", "NaN"}};
    handleSave();
    ASSERT_EQ(server.sentStatus, 400);
    ASSERT_EQ(eventTime, 1767225600);
    ASSERT_TRUE(ssid == SSID_DEFAULT_VALUE);
    ASSERT_EQ(EEPROM.commits, 0);
    ASSERT_FALSE(connectRequested);
}

static void test_save_out_of_range_event_time_is_rejected() {
    const char* invalid[] = {"0", "-86400", "253402300800", "1767225600abc", "1.5"};
    for (const char* value : invalid) {
        server.args = {{"eventTime", value}};
        handleSave();
        ASSERT_EQ(server.sentStatus, 400);
    }
    ASSERT_EQ(EEPROM.commits, 0);
}

static void test_save_special_character_password_stored_unchanged() {
    // the page urlencodes the body; the server hands the decoded value to the handler
    const char* special = "p&ss=w0rd+%20 #?\"'<>";
    server.args = {{"ssid", "home"}, {"password", special}};
    handleSave();
    ASSERT_EQ(server.sentStatus, 200);
    ASSERT_STR_EQ(password.c_str(), special);
    ASSERT_STR_EQ(readFromEeprom(PASSWORD_ADDR, PASSWORD_SIZE).c_str(), special);
}

static void test_save_page_urlencodes_the_form() {
    // building the body by string concatenation broke passwords containing & or =
    handleRoot();
    ASSERT_CONTAINS(server.sentBody, "new URLSearchParams(");
    ASSERT_CONTAINS(server.sentBody, "body: body");
}

static void test_save_too_long_values_are_rejected() {
    server.args = {{"ssid", std::string(SSID_SIZE, 's')}, {"password", "pass1234"}};
    handleSave();
    ASSERT_EQ(server.sentStatus, 400);
    ASSERT_CONTAINS(server.sentBody, "max 32 bytes");

    server.args = {{"ssid", "home"}, {"password", std::string(PASSWORD_SIZE, 'p')}};
    handleSave();
    ASSERT_EQ(server.sentStatus, 400);
    ASSERT_CONTAINS(server.sentBody, "max 63 characters");

    ASSERT_TRUE(ssid == SSID_DEFAULT_VALUE);
    ASSERT_TRUE(password == PASSWORD_DEFAULT_VALUE);
    ASSERT_EQ(EEPROM.commits, 0);
}

static void test_save_does_not_block_on_wifi() {
    g_millis = 5000;
    server.args = {{"ssid", "home"}, {"password", "pass1234"}};
    handleSave();
    // replies right away and leaves the connecting to loop()
    ASSERT_EQ(server.sentStatus, 200);
    ASSERT_EQ(g_millis, 5000);
    ASSERT_EQ(WiFi.begins, 0);
    ASSERT_EQ(state, CONFIG);
    ASSERT_TRUE(connectRequested);
}

// ---------- state machine ----------

// a board that was configured before and has just restarted
static void bootWithSavedConfig() {
    ssid = "home";
    password = "pass1234";
    eventTime = utc(2026, 6, 25);
    saveConfig();
    ssid = SSID_DEFAULT_VALUE;
    password = PASSWORD_DEFAULT_VALUE;
    setup();
}

static void test_state_no_config_stays_in_config_mode() {
    setup();
    g_millis = STARTUP_TIMEOUT + 1;
    loop();
    ASSERT_EQ(state, CONFIG);
    ASSERT_EQ(WiFi.begins, 0);
    ASSERT_CONTAINS(P.shown(), "Connect to DATE TIMER");
}

static void test_state_saved_config_waits_for_startup_timeout() {
    bootWithSavedConfig();
    g_millis = STARTUP_TIMEOUT;
    loop();
    ASSERT_EQ(state, CONFIG);
    ASSERT_EQ(WiFi.begins, 0);
}

static void test_state_saved_config_connects_and_counts_down() {
    bootWithSavedConfig();
    g_now = utc(2026, 6, 15, 10);
    g_millis = STARTUP_TIMEOUT + 1;
    loop();
    ASSERT_EQ(state, COUNTDOWN);
    ASSERT_EQ(WiFi.lastMode, WIFI_STA);
    ASSERT_STR_EQ(WiFi.beganSsid, "home");
    ASSERT_STR_EQ(WiFi.beganPassword, "pass1234");
    loop();
    ASSERT_STR_EQ(P.shown(), "10");
}

static void test_state_connect_failure_restarts_access_point() {
    bootWithSavedConfig();
    WiFi.beginResult = WL_DISCONNECTED;
    g_millis = STARTUP_TIMEOUT + 1;
    loop();
    ASSERT_EQ(state, CONNECT_FAILED);
    ASSERT_EQ(WiFi.disconnects, 1);
    ASSERT_EQ(WiFi.lastMode, WIFI_AP);
    ASSERT_EQ(WiFi.softAPs, 2);
    loop();
    ASSERT_CONTAINS(P.shown(), "Incorrect wifi credentials");
    // no automatic retry
    g_millis += STARTUP_TIMEOUT;
    loop();
    ASSERT_EQ(WiFi.begins, 1);
}

static void test_state_save_connects_after_the_reply_was_sent() {
    setup();
    g_millis = 1000;
    server.args = {{"ssid", "home"}, {"password", "pass1234"}};
    handleSave();
    g_millis += CONNECT_REQUEST_DELAY;
    loop();
    ASSERT_EQ(WiFi.begins, 0);
    g_millis += 1;
    loop();
    ASSERT_EQ(WiFi.begins, 1);
    ASSERT_EQ(state, COUNTDOWN);
    ASSERT_FALSE(connectRequested);
}

static void test_state_resubmit_after_failure() {
    setup();
    WiFi.beginResult = WL_DISCONNECTED;
    server.args = {{"ssid", "home"}, {"password", "wrong"}};
    handleSave();
    g_millis += CONNECT_REQUEST_DELAY + 1;
    loop();
    ASSERT_EQ(state, CONNECT_FAILED);

    WiFi.beginResult = WL_CONNECTED;
    server.args = {{"password", "pass1234"}};
    handleSave();
    g_millis += CONNECT_REQUEST_DELAY + 1;
    loop();
    ASSERT_EQ(state, COUNTDOWN);
    ASSERT_STR_EQ(WiFi.beganSsid, "home");
    ASSERT_STR_EQ(WiFi.beganPassword, "pass1234");
}

static void test_state_countdown_reconnects_when_wifi_drops() {
    bootWithSavedConfig();
    g_now = utc(2026, 6, 15, 10);
    g_millis = STARTUP_TIMEOUT + 1;
    loop();
    ASSERT_EQ(state, COUNTDOWN);
    WiFi.currentStatus = WL_DISCONNECTED;
    g_millis += CONNECTION_CHECK_INTERVAL - 1;
    loop();
    ASSERT_EQ(WiFi.reconnects, 0);
    g_millis += 1;
    loop();
    ASSERT_EQ(WiFi.reconnects, 1);
    // the countdown keeps running from the system clock meanwhile
    ASSERT_EQ(state, COUNTDOWN);
    ASSERT_STR_EQ(P.shown(), "10");
}

int main() {
    printf("harness\n");
    RUN(test_setup_starts_access_point_in_config_mode);
    RUN(test_root_page_is_served);

    printf("EEPROM storage\n");
    RUN(test_eeprom_slots_round_trip);
    RUN(test_eeprom_value_filling_the_slot_round_trips);
    RUN(test_eeprom_read_matches_default_credentials);
    RUN(test_eeprom_too_long_value_is_not_written);
    RUN(test_eeprom_save_config_commits_once_and_sets_marker);
    RUN(test_eeprom_saved_config_is_loaded_on_boot);
    RUN(test_eeprom_blank_reads_as_not_configured);
    RUN(test_eeprom_data_without_marker_is_ignored);

    printf("day calculation\n");
    RUN(test_days_from_civil_known_dates);
    RUN(test_days_tomorrow_today_yesterday);
    RUN(test_days_100_days_ahead);
    RUN(test_days_past_2038);
    RUN(test_days_change_at_local_midnight_not_utc);
    RUN(test_days_across_spring_dst_change);
    RUN(test_days_across_autumn_dst_change);
    RUN(test_days_syncing_until_ntp_sets_the_clock);
    RUN(test_days_recomputed_every_update_interval);

    printf("/save handler\n");
    RUN(test_save_stores_and_applies_all_fields);
    RUN(test_save_empty_fields_keep_current_values);
    RUN(test_save_missing_event_time_keeps_stored_date);
    RUN(test_save_nan_event_time_is_rejected);
    RUN(test_save_out_of_range_event_time_is_rejected);
    RUN(test_save_special_character_password_stored_unchanged);
    RUN(test_save_page_urlencodes_the_form);
    RUN(test_save_too_long_values_are_rejected);
    RUN(test_save_does_not_block_on_wifi);

    printf("state machine\n");
    RUN(test_state_no_config_stays_in_config_mode);
    RUN(test_state_saved_config_waits_for_startup_timeout);
    RUN(test_state_saved_config_connects_and_counts_down);
    RUN(test_state_connect_failure_restarts_access_point);
    RUN(test_state_save_connects_after_the_reply_was_sent);
    RUN(test_state_resubmit_after_failure);
    RUN(test_state_countdown_reconnects_when_wifi_drops);

    printf("\n%d tests, %d failures\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
