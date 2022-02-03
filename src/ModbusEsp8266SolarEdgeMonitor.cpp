#include <Adafruit_GFX.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Fonts/FreeMono9pt7b.h>

#define IOTWEBCONF_PASSWORD_LEN 65
#include <IotWebConf.h>
#include <IotWebConfUsing.h>  // This loads aliases for easier class names.
#include <ModbusSolarEdge.h>
#include <OneButton.h>
#include <SPI.h>
#include <Wire.h>

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library.
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET 0         // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char *VERSION = "2.2.0";

// Modifying the config version will probably cause a loss of the existig configuration.
// Be careful!
const char *CONFIG_VERSION = "1.0.2";

const char *WIFI_AP_SSID = "SolarEdgeMonitor";
const char *WIFI_AP_DEFAULT_PASSWORD = "";

// -- Method declarations.
void handleRoot();

DNSServer dnsServer;
WebServer server(80);

boolean needReset = false;
boolean connected = false;

// last known WiFi network state
iotwebconf::NetworkState lastNetWorkState = iotwebconf::NetworkState::OffLine;

// which screen is currently shown
enum CurrentScreen {
    None,
    WifiState,
    Solar1,
    Solar2
};

// at boot time no screen is shown
CurrentScreen lastScreen = None;

void printStateScreen(char *line1, char *line2, char *line3 = nullptr, char *line4 = nullptr);
void printStateScreen2(char *sunPowerStr, char *line2, char *line3 = nullptr, char *line4 = nullptr, char *batteryLevelOfEnergy = nullptr, float batteryLevelOfEnergyPct = 0.0f, float sunPowerPowerKw = 0.0f, float meterPowerKw = 0.0f, float houseUsagePowerKw = 0.0f, float batteryPowerKw = 0.0f, float i_ac_power_norm = 0.0f);
void printWifiState();
void wifiConnected();
void configSaved();
void printUsage();
float round(float f);
void handleClick();
void handleDoubleClick();

IotWebConf iotWebConf(WIFI_AP_SSID, &dnsServer, &server, WIFI_AP_DEFAULT_PASSWORD, CONFIG_VERSION);

// Parameter group for inverter parameter
IotWebConfParameterGroup groupModbus = IotWebConfParameterGroup("groupModbus", "Modbus Settings");

// Parameter for inverter IP address
char inverterIpAddressParamValue[15] = {""};
IotWebConfTextParameter inverterIpAddressParam = IotWebConfTextParameter("Inverter IP", "inverterIpParam", inverterIpAddressParamValue, 15, "192.168.0.1");

// Parameter for Modbus TCP port
char inverterPortParamValue[32];
IotWebConfNumberParameter inverterPortParam = IotWebConfNumberParameter("Inverter Port", "inverterPort", inverterPortParamValue, 32, "1502", "1..65535", "min='1' max='65535' step='1'");

// IP of the SolarEdge inverter
IPAddress remote;

// Modbus port of the SolarEdge inverter
int port = -1;

// ModbusIP object
ModbusIP mb;

// Modbus SolarEdge helper
ModbusSolarEdge mbse;

// Time since display is on
long displayOnSince;

// Turn display off after this time in minutes to reduce OLED wearing, 0 = always on
const int DISPLAY_OFF_AFTER_MINS = 15;

// Time since last display update
long lastDisplayUpdateTime = 0;

const int DISPLAY_UPDATE_INTERVAL_SECS = 5;

boolean displayOn = true;

OneButton btn = OneButton(
    D5,     // Input pin for the button
    false,  // Button is active LOW
    false   // Enable internal pull-up resistor
);

// '211105_solaredige_mon_screen', 128x64px
const unsigned char img_background[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x4f, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x44, 0x91, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x99, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x82, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x49, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x11, 0xc4, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x12, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x30, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x24, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x48, 0x09, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x48, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x91, 0x44, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x20, 0x82, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x07, 0xd0, 0x05, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x02, 0x40, 0x01, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x48, 0x09, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x43, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x30, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x11, 0xc4, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x82, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x44, 0x91, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x7f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0xa8, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0xa3, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x73, 0xf9, 0xc0, 0x00, 0x00, 0x00, 0x00,
    0x07, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x5f, 0x1f, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x52, 0xa9, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0xa8, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x01, 0x00, 0x07, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0c, 0xa6, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x12, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x14, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x14, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x28, 0x02, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char img_arr_right_3x7[] PROGMEM = {
    0x00, 0x80, 0xc0, 0xe0, 0xc0, 0x80, 0x00};

// '210511_pfeil_3x7_hoch', 7x3px
const unsigned char img_arr_up_7x3[] PROGMEM = {
    0x10, 0x38, 0x7c};

// '210511_pfeil_3x7_runter', 7x3px
const unsigned char img_arr_down_7x3[] PROGMEM = {
    0x7c, 0x38, 0x10};

void setup() {
    Serial.begin(115200);  // Start the Serial communication to send messages to the computer
    delay(10);
    Serial.println("Starting up...");

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Address 0x3D for 128x64
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ;  // Don't proceed, loop forever
    }
    // display is built in upside down
    display.setRotation(2);
    display.clearDisplay();

    displayOnSince = millis();

    // -- Initializing the configuration.
    groupModbus.addItem(&inverterIpAddressParam);
    groupModbus.addItem(&inverterPortParam);
    iotWebConf.addParameterGroup(&groupModbus);

    iotWebConf.setWifiConnectionCallback(&wifiConnected);
    iotWebConf.setConfigSavedCallback(&configSaved);
    iotWebConf.setStatusPin(LED_BUILTIN);
    iotWebConf.init();

    // -- Set up required URL handlers on the web server.
    server.on("/", [] { iotWebConf.handleConfig(); });
    server.onNotFound([]() { iotWebConf.handleNotFound(); });

    mb.client();

    btn.attachClick(handleClick);
    btn.attachDoubleClick(handleDoubleClick);

    Serial.println("setup done");
}

void loop() {
    if (needReset) {
        // config changes require reset
        Serial.println("restart in 1 sec");
        delay(1000);
        ESP.restart();
    }

    if (!connected) {
        printWifiState();
    }

    if (connected) {
        if (!mb.isConnected(remote)) {
            Serial.print("Inverter IP address: ");
            Serial.println(inverterIpAddressParamValue);
            Serial.print("Inverter TCP port: ");
            Serial.println(inverterPortParamValue);

            String line1 = "Init Modbus client";
            String ipStr(inverterIpAddressParamValue);
            String line2 = "IP: " + ipStr;
            String portStr(inverterPortParamValue);
            String line3 = "Port: " + portStr;
            String line4 = "";

            port = atoi(inverterPortParamValue);

            boolean valid = remote.fromString(inverterIpAddressParamValue);
            if (!valid) {
                line4 = "> IP is invalid";
            }
            printStateScreen(&line1[0], &line2[0], &line3[0], &line4[0]);
            delay(2000);

            if (valid) {
                boolean connected = mb.connect(remote, port);
                line4 = connected ? "> Modbus connected" : "> Modbus conn. failed";
                printStateScreen(&line1[0], &line2[0], &line3[0], &line4[0]);
                delay(2000);
            }
        } else {
            if ((int)millis() > lastDisplayUpdateTime + DISPLAY_UPDATE_INTERVAL_SECS * 1000 && displayOn) {
                // init to Solar1
                lastScreen = lastScreen == None || lastScreen == WifiState ? Solar2 : lastScreen;

                printUsage();

                lastDisplayUpdateTime = millis();
            }

            if (DISPLAY_OFF_AFTER_MINS != 0 && (int)millis() > displayOnSince + DISPLAY_OFF_AFTER_MINS * 60 * 1000) {
                display.ssd1306_command(SSD1306_DISPLAYOFF);
                displayOn = false;
            } else if (!displayOn) {
                display.ssd1306_command(SSD1306_DISPLAYON);
                displayOn = true;
            }
        }
    }

    iotWebConf.doLoop();
    btn.tick();
}

void handleClick() {
    Serial.println("Clicked!");
    displayOnSince = millis();  // activate Display if off
}

void handleDoubleClick() {
    Serial.println("Double Clicked!");
    displayOnSince = millis();  // activate Display if off

    if (lastScreen == Solar1) {
        lastScreen = Solar2;
    } else if (lastScreen == Solar2) {
        lastScreen = Solar1;
    }

    lastDisplayUpdateTime = 0;
}

void configSaved() {
    Serial.println("config saved");
    needReset = true;
}

void wifiConnected() {
    connected = true;
    Serial.println("wifi connected");
}

void printWifiState() {
    if (iotWebConf.getState() != lastNetWorkState || lastScreen != WifiState) {
        String state = "";
        switch (iotWebConf.getState()) {
            case iotwebconf::NetworkState::ApMode:
                state = "Access Point Mode";
                break;
            case iotwebconf::NetworkState::Boot:
                state = "Booting";
                break;
            case iotwebconf::NetworkState::Connecting:
                state = "Connecting to WiFi";
                break;
            case iotwebconf::NetworkState::NotConfigured:
                state = "Not configured";
                break;
            case iotwebconf::NetworkState::OffLine:
                state = "Offline";
                break;
            case iotwebconf::NetworkState::OnLine:
                state = "Online";
                break;
        }

        String initString = "Init WiFi connection";
        printStateScreen(&initString[0], &state[0]);

        lastNetWorkState = iotWebConf.getState();
        lastScreen = WifiState;
    }
}

void printStateScreen(char *line1, char *line2, char *line3, char *line4) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, 2);
    display.println(line1);

    display.setCursor(2, 12);
    display.println(line2);

    if (line3 != nullptr) {
        display.setCursor(2, 22);
        display.println(line3);
    }

    if (line4 != nullptr) {
        display.setCursor(2, 32);
        display.println(line4);
    }

    display.display();
    delay(500);
}

void printStateScreen2(char *sunPowerStr, char *houseUsagePower, char *meterPower, char *batteryPower, char *batteryLevelOfEnergy, float batteryLevelOfEnergyPct, float sunPowerPowerKw, float meterPowerKw, float houseUsagePowerKw, float batteryPowerKw, float i_ac_power_norm) {
    display.clearDisplay();

    display.drawBitmap(0, 0, img_background, 128, 64, 1);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // #######
    // # Sun #
    // #######
    display.setCursor(33, 13);
    display.println(sunPowerStr);
    display.setCursor(33, 23);
    display.println("  kW");

    // #########
    // # House #
    // #########
    display.setCursor(98, 13);
    display.println(houseUsagePower);
    display.setCursor(98, 23);
    display.println("  kW");

    // print pv system to house power flow arrow
    if (i_ac_power_norm > 0) {
        display.fillRect(63, 13, 6, 9, SSD1306_BLACK);
        display.drawBitmap(62, 14, img_arr_right_3x7, 3, 7, 1);
    }

    // ###########
    // # Battery #
    // ###########

    float batteryPowerKwAbs = batteryPowerKw < 0 ? batteryPowerKw * -1 : batteryPowerKw;
    char batteryPowerFmt[4];
    dtostrf(batteryPowerKwAbs, 4, 2, batteryPowerFmt);
    String batteryPowerAbsStr(batteryPowerFmt);

    display.setCursor(33, 45);
    display.println(batteryPowerAbsStr);
    display.setCursor(33, 55);
    display.println("  kW");

    String batteryLevelOfEnergyFmt(batteryLevelOfEnergy);
    batteryLevelOfEnergyFmt += "%";

    display.setCursor(4, 57);
    display.println(batteryLevelOfEnergyFmt);

    // draw battery charge level
    const int BATT_X = 7;
    const int BATT_Y = 46;
    const int BATT_WIDTH = 17;
    const int BATT_HEIGHT = 5;

    float width = batteryLevelOfEnergyPct * BATT_WIDTH / 100.0f;
    display.fillRect(BATT_X, BATT_Y, (int)width, BATT_HEIGHT, SSD1306_WHITE);

    // print battery power flow arrow
    long batteryPowerWAbs = batteryPowerKw > 0 ? batteryPowerKw * 1000 :  batteryPowerKw * -1000;
    // Serial.print("battery power w: ");
    // Serial.println(batteryPowerWAbs);
    if (batteryPowerWAbs > 9) {
        display.fillRect(28, 29, 9, 6, SSD1306_BLACK);
        if (batteryPowerKw < 0) {
            display.drawBitmap(29, 31, img_arr_up_7x3, 7, 3, 1);
        } else {
            display.drawBitmap(29, 31, img_arr_down_7x3, 7, 3, 1);
        }
    }

    // #########
    // # Meter #
    // #########

    float meterPowerKwAbs = meterPowerKw < 0 ? meterPowerKw * -1 : meterPowerKw;
    char meterPowerFmt[4];
    dtostrf(meterPowerKwAbs, 4, 2, meterPowerFmt);
    String meterPowerAbsStr(meterPowerFmt);

    display.setCursor(98, 45);
    display.println(meterPowerAbsStr);
    display.setCursor(98, 55);
    display.println("  kW");

    long meterPowerWAbs = meterPowerKwAbs * 1000;
    // print meter power flow arrow
    if (meterPowerWAbs > 9) {
        // Serial.print("meter active w: ");
        // Serial.println(meterPowerWAbs);
        display.fillRect(95, 29, 9, 6, SSD1306_BLACK);
        if (meterPowerKw < 0) {
            display.drawBitmap(96, 31, img_arr_up_7x3, 7, 3, 1);
        } else {
            display.drawBitmap(96, 31, img_arr_down_7x3, 7, 3, 1);
        }
    }
    else {
        Serial.print("meter inactive w: ");
        Serial.println(meterPowerWAbs);
    }

    display.display();

    // reset font to default
    display.setFont();
    delay(500);
}

void printStateScreen3(char *line1, char *line2, char *line3, char *line4) {
    display.clearDisplay();

    display.setFont(&FreeMono9pt7b);

    const int LINE_SPACE = 17;
    int y = 10;

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, y);
    display.println(line1);

    y += LINE_SPACE;
    display.setCursor(2, y);
    display.println(line2);

    y += LINE_SPACE;
    if (line3 != nullptr) {
        display.setCursor(2, y);
        display.println(line3);
    }

    y += LINE_SPACE;
    if (line4 != nullptr) {
        display.setCursor(2, y);
        display.println(line4);
    }

    display.display();

    // reset font to default
    display.setFont();
    delay(500);
}

void printUsage() {
    int16_t i_ac_power = mbse.readHregInt(mb, remote, I_AC_POWER);
    int16_t i_ac_power_sf = mbse.readHregInt(mb, remote, I_AC_POWER_SF);
    int16_t i_ac_power_norm = mbse.norm(i_ac_power, i_ac_power_sf);

    int16_t m1_m_ac_power = mbse.readHregInt(mb, remote, M1_AC_POWER);
    int16_t m1_m_ac_power_sf = mbse.readHregInt(mb, remote, M1_AC_POWER_SF);
    int16_t m1_m_ac_power_norm = mbse.norm(m1_m_ac_power, m1_m_ac_power_sf);

    float b1_b_instantaneous_power = mbse.readHregFloat32(mb, remote, B1_INSTANTANEOUS_POWER);

    // (A) calculate sun power
    int a_sun_power = mbse.calculate_sun_power(i_ac_power_norm, b1_b_instantaneous_power);

    // (B) calculate power used by house
    int b_house_usage = mbse.calculate_house_usage(i_ac_power_norm, m1_m_ac_power_norm);

    // (C) grid input/consumption
    int c_meter_power = m1_m_ac_power_norm;

    // (D) battery charge/discharge
    int d_battery_power = b1_b_instantaneous_power;

    // battery level in percent
    float b1_b_state_of_energy = mbse.readHregFloat32(mb, remote, B1_STATE_OF_ENERGY_SOE);

    float sunPowerPowerKw = round(a_sun_power / 1000.0f);
    char sunPowerFmt[4];
    dtostrf(sunPowerPowerKw, 4, 2, sunPowerFmt);
    String sunPowerStr(sunPowerFmt);
    String line1 = "S: " + sunPowerStr + "kW";

    float houseUsagePowerKw = round(b_house_usage / 1000.0f);
    char houseUsagePowerFmt[4];
    dtostrf(houseUsagePowerKw, 4, 2, houseUsagePowerFmt);
    String houseUsagePower(houseUsagePowerFmt);
    String line2 = "H: " + houseUsagePower + "kW";

    float meterPowerKw = round(c_meter_power / 1000.0f);
    char meterPowerFmt[4];
    dtostrf(meterPowerKw, 4, 2, meterPowerFmt);
    String meterPower(meterPowerFmt);
    String line3 = "M: " + meterPower + "kW";

    float batteryPowerKw = round(d_battery_power / 1000.0f);
    char batteryPowerFmt[4];
    dtostrf(batteryPowerKw, 4, 2, batteryPowerFmt);
    String batteryPower(batteryPowerFmt);

    float batterySOE = b1_b_state_of_energy;
    char batterySOEFmt[3];
    dtostrf(batterySOE, 3, 0, batterySOEFmt);
    String batteryLevelOfEnergy(batterySOEFmt);
    String line4 = "B: " + batteryLevelOfEnergy + "% " + batteryPower + "kW";

    Serial.println(lastScreen);

    if (lastScreen == Solar1) {
        printStateScreen(&line1[0], &line2[0], &line3[0], &line4[0]);
    } else {
        printStateScreen2(&sunPowerStr[0], &houseUsagePower[0], &meterPower[0], &batteryPower[0], &batteryLevelOfEnergy[0], batterySOE, sunPowerPowerKw, meterPowerKw, houseUsagePowerKw, batteryPowerKw, i_ac_power_norm);
    }
}

float round(float f) {
    int sign = f < 0 ? -1 : 1;
    f = f * sign;
    f += 0.005;
    return f * sign;
}