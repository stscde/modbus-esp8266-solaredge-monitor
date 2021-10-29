#include <Adafruit_GFX.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

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
    Solar1
};

// at boot time no screen is shown
CurrentScreen lastScreen = None;

void printStateScreen(char *line1, char *line2, char *line3 = nullptr, char *line4 = nullptr);
void printWifiState();
void wifiConnected();
void configSaved();
void printUsage();
float round(float f);
void handleClick();

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
                printUsage();
                lastDisplayUpdateTime = millis();
            }

            if (DISPLAY_OFF_AFTER_MINS != 0 && (int)millis() > displayOnSince + DISPLAY_OFF_AFTER_MINS * 60 *1000) {
                display.ssd1306_command(SSD1306_DISPLAYOFF);
                displayOn = false;
            }
            else if (!displayOn) {
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

    displayOnSince = millis(); // activate Display if off
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
    dtostrf(batterySOE, 2, 0, batterySOEFmt);
    String batteryLevelOfEnergy(batterySOEFmt);
    String line4 = "B: " + batteryLevelOfEnergy + "% " + batteryPower + "kW";

    printStateScreen(&line1[0], &line2[0], &line3[0], &line4[0]);
}

float round(float f) {
    int sign = f < 0 ? -1 : 1;
    f = f * sign;
    f += 0.005;
    return f * sign;
}