#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "WiFi.h"
#include "esp_wifi.h"

// WiFi packet structures
typedef struct {
    unsigned frame_ctrl:16;
    unsigned duration_id:16;
    uint8_t addr1[6]; // receiver address
    uint8_t addr2[6]; // sender address
    uint8_t addr3[6]; // filtering address
    unsigned sequence_ctrl:16;
    uint8_t addr4[6]; // optional
} wifi_ieee80211_mac_hdr_t;

typedef struct {
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0]; // flexible array member
} wifi_ieee80211_packet_t;

// All defines together
#define VERSION "v1.0.6"
#define SPLASH_DURATION 3000 

#define GFX_BL 38
#define POWER_PIN 15
#define DISPLAY_BUTTON 14
#define POWER_BUTTON 0
#define DISPLAY_BACKLIGHT 38
#define MAX_NETWORKS 50
#define DISPLAY_MODE_NORMAL 0
#define DISPLAY_MODE_BATTERY 1
#define DISPLAY_MODE_UNIQUE 2
#define DISPLAY_MODE_MAC 3
#define DISPLAY_MODE_PACKETS 4
#define MAX_PACKETS 120  // 2 minutes of packet counts
#define DISPLAY_MODE_SIGNAL 5
#define RSSI_SAMPLES 100
#define MAX_MAC_ADDRESSES 100

// Battery related defines
#define BATT_LOW_VOLTAGE 3.2
#define BATT_CRITICAL_VOLTAGE 3.0
#define BATT_CHECK_INTERVAL 10000
#define BATT_WARNING_DURATION 3000
#define VOLTAGE_SAMPLES 10

// Forward declarations
void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
void goToSleep();

// Display setup
Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
    7, 6, 8, 9, 39, 40, 41, 42, 45, 46, 47, 48);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 5, 1, true, 170, 320, 35, 0, 35, 0);

// Struct definitions
struct NetworkProbe {
    char ssid[33];
    unsigned long timestamp;
};

struct UniqueNetwork {
    char ssid[33];
    unsigned long firstSeen;
    bool active;
};

struct MacInfo {
    uint8_t mac[6];
    unsigned long firstSeen;
    unsigned long lastSeen;
    bool active;
};

// All global variables together
NetworkProbe recentProbes[MAX_NETWORKS];
int probeCount = 0;
int probeIndex = 0;
UniqueNetwork uniqueNetworks[MAX_NETWORKS];
int uniqueCount = 0;
bool displayOn = true;
uint8_t displayMode = DISPLAY_MODE_NORMAL;
static int currentChannel = 1;
uint16_t packetsPerSecond[MAX_PACKETS];  // Store packets/sec
uint8_t packetIndex = 0;
uint16_t currentPacketCount = 0;


// Battery related globals
float lastVoltage = 0.0;
unsigned long lastBatteryCheck = 0;
bool lowBatteryWarningShown = false;

// MAC tracking globals
MacInfo uniqueMacs[MAX_MAC_ADDRESSES];
int macCount = 0;
int8_t rssiValues[RSSI_SAMPLES] = {0};
int rssiIndex = 0;

// Scrolling related globals
unsigned long lastScrollTime = 0;
int ghostNetworksScroll = 0;
int uniqueNetworksScroll = 0;

// Battery related functions
float getBatteryVoltage() {
    int32_t raw = 0;
    for(int i = 0; i < VOLTAGE_SAMPLES; i++) {
        raw += analogReadMilliVolts(4);
        delay(1);
    }
    raw /= VOLTAGE_SAMPLES;
    return (raw * 2.0) / 1000.0;
}

void showBatteryWarning() {
    gfx->fillScreen(RED);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    
    gfx->setCursor(10, 40);
    gfx->println("LOW BATTERY!");
    
    gfx->setCursor(10, 80);
    gfx->print(lastVoltage, 2);
    gfx->println("V");
    
    gfx->setCursor(10, 120);
    gfx->println("Please charge soon!");
    
    delay(BATT_WARNING_DURATION);
}

void handleBattery() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastBatteryCheck >= BATT_CHECK_INTERVAL) {
        lastVoltage = getBatteryVoltage();
        lastBatteryCheck = currentTime;
        
        if (lastVoltage <= BATT_CRITICAL_VOLTAGE) {
            gfx->fillScreen(RED);
            gfx->setTextSize(2);
            gfx->setTextColor(WHITE);
            gfx->setCursor(10, 40);
            gfx->println("CRITICAL BATTERY!");
            gfx->setCursor(10, 80);
            gfx->print(lastVoltage, 2);
            gfx->println("V");
            gfx->setCursor(10, 120);
            gfx->println("Shutting down...");
            delay(2000);
            
            digitalWrite(DISPLAY_BACKLIGHT, LOW);
            digitalWrite(POWER_PIN, LOW);
            goToSleep();
        }
        else if (lastVoltage <= BATT_LOW_VOLTAGE && !lowBatteryWarningShown) {
            showBatteryWarning();
            lowBatteryWarningShown = true;
        }
        else if (lastVoltage > BATT_LOW_VOLTAGE + 0.1) {
            lowBatteryWarningShown = false;
        }
    }
}

// MAC address helper functions
bool isMacEqual(const uint8_t* mac1, const uint8_t* mac2) {
    return memcmp(mac1, mac2, 6) == 0;
}

void trackMacAddress(const uint8_t* mac) {
    // Skip broadcast MACs
    if (mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF && 
        mac[3] == 0xFF && mac[4] == 0xFF && mac[5] == 0xFF) {
        return;
    }
    
    unsigned long currentTime = millis();
    for(int i = 0; i < macCount; i++) {
        if(isMacEqual(mac, uniqueMacs[i].mac)) {
            uniqueMacs[i].lastSeen = currentTime;
            uniqueMacs[i].active = true;
            return;
        }
    }
    
    if(macCount < MAX_MAC_ADDRESSES) {
        memcpy(uniqueMacs[macCount].mac, mac, 6);
        uniqueMacs[macCount].firstSeen = currentTime;
        uniqueMacs[macCount].lastSeen = currentTime;
        uniqueMacs[macCount].active = true;
        macCount++;
    }
}
// Existing functions
void displaySplash() {
    gfx->fillScreen(BLACK);
    
    gfx->setTextSize(3);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, 40);
    gfx->println("Ghost Networks");
    
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 100);
    gfx->print("Version: ");
    gfx->println(VERSION);
    
    gfx->setCursor(10, 130);
    gfx->print("Built: ");
    gfx->print(__DATE__);
    gfx->print(" ");
    gfx->println(__TIME__);
    
    delay(SPLASH_DURATION);
}

bool isNetworkUnique(const char* ssid) {
    // Ignore empty SSIDs
    if (strlen(ssid) == 0) {
        return false;
    }
    
    unsigned long currentTime = millis();
    
    for (int i = 0; i < uniqueCount; i++) {
        // If network exists and was seen recently, update it
        if (strcmp(uniqueNetworks[i].ssid, ssid) == 0) {
            if (currentTime - uniqueNetworks[i].firstSeen <= 120000) {
                uniqueNetworks[i].active = true;
                return false;
            }
            // If network exists but is old, reuse its slot
            uniqueNetworks[i].firstSeen = currentTime;
            uniqueNetworks[i].active = true;
            return false;
        }
    }
    
    // Cleanup: Remove old networks if array is full
    if (uniqueCount >= MAX_NETWORKS) {
        int oldestIdx = 0;
        unsigned long oldestTime = currentTime;
        
        // Find oldest network
        for (int i = 0; i < uniqueCount; i++) {
            if (uniqueNetworks[i].firstSeen < oldestTime) {
                oldestTime = uniqueNetworks[i].firstSeen;
                oldestIdx = oldestIdx;
            }
        }
        
        // Remove oldest by shifting array
        for (int i = oldestIdx; i < uniqueCount - 1; i++) {
            memcpy(&uniqueNetworks[i], &uniqueNetworks[i + 1], sizeof(UniqueNetwork));
        }
        uniqueCount--;
    }
    
    return true;
}

void addUniqueNetwork(const char* ssid) {
    Serial.print("addUniqueNetwork called with SSID: ");
    Serial.println(ssid);
    if (uniqueCount < MAX_NETWORKS && isNetworkUnique(ssid)) {
        strncpy(uniqueNetworks[uniqueCount].ssid, ssid, 32);
        uniqueNetworks[uniqueCount].ssid[32] = '\0';
        uniqueNetworks[uniqueCount].firstSeen = millis();
        uniqueNetworks[uniqueCount].active = true;
        uniqueCount++;
        Serial.print("Successfully added network. uniqueCount: ");
        Serial.println(uniqueCount);
    }
}


void promiscuousCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    currentPacketCount++; 
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
    
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t*)buf;
    
    // Add RSSI tracking
    rssiValues[rssiIndex] = pkt->rx_ctrl.rssi;
    rssiIndex = (rssiIndex + 1) % RSSI_SAMPLES;
    
    uint8_t *payload = pkt->payload;
    Serial.printf("Packet type: %02x\n", payload[0]);
    
    wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)payload;
    wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;

    trackMacAddress(hdr->addr1);
    trackMacAddress(hdr->addr2);
    Serial.print("Processing packet type: 0x");
    Serial.println(payload[0], HEX);
    if (((payload[0] & 0xFC) == 0x40) || (payload[0] == 0x80)) {
        uint8_t SSID_length;
        uint8_t *ssid_start;
        
        if ((payload[0] & 0xFC) == 0x40) {
            SSID_length = payload[25];
            ssid_start = &payload[26];
        } else {
            SSID_length = payload[37];
            ssid_start = &payload[38];
        }

        if (SSID_length > 0 && SSID_length < 33) {
            char SSID[33] = { 0 };
            memcpy(SSID, ssid_start, SSID_length);
            SSID[SSID_length] = '\0';
            Serial.print("Found SSID: ");
Serial.print(SSID);
Serial.print(" Length: ");
Serial.print(SSID_length);
Serial.print(" Type: ");
Serial.println((payload[0] & 0xFC) == 0x40 ? "Probe" : "Beacon");
Serial.println("Calling addUniqueNetwork");
addUniqueNetwork(SSID);
            
            bool exists = false;
            for(int i = 0; i < probeCount; i++) {
                if(strcmp(recentProbes[i].ssid, SSID) == 0) {
                    recentProbes[i].timestamp = millis();
                    exists = true;
                    break;
                }
            }
            
            if(!exists) {
                strncpy(recentProbes[probeIndex].ssid, SSID, 32);
                recentProbes[probeIndex].timestamp = millis();
                probeIndex = (probeIndex + 1) % MAX_NETWORKS;
                if (probeCount < MAX_NETWORKS) probeCount++;
                Serial.printf("New network found: %s\n", SSID);
            }
        }
    }
}

void goToSleep() {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)POWER_BUTTON, 0);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting initialization...");

    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, HIGH);
    delay(100);
    
    pinMode(DISPLAY_BACKLIGHT, OUTPUT);
    digitalWrite(DISPLAY_BACKLIGHT, HIGH);
    
    gfx->begin();
    gfx->setRotation(1);
    
    displaySplash();
    
    pinMode(4, INPUT);
    pinMode(DISPLAY_BUTTON, INPUT);
    pinMode(POWER_BUTTON, INPUT);
    
    Serial.println("Ghost Networks Detector v" VERSION " Starting...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&promiscuousCallback);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    
    // Get initial battery reading
    lastVoltage = getBatteryVoltage();
}

void drawTruncatedText(const char* text, int x, int y, int maxWidth) {
    char truncated[41] = {0};  // Max 40 chars + null terminator
    strncpy(truncated, text, 40);
    truncated[40] = '\0';
    
    // Add "..." if text was truncated
    if (strlen(text) > 40) {
        truncated[37] = '.';
        truncated[38] = '.';
        truncated[39] = '.';
    }
    
    gfx->setCursor(x, y);
    gfx->println(truncated);
}

void loop() {
     static unsigned long lastChannelSwitch = 0;
    if (millis() - lastChannelSwitch > 200) {
        currentChannel = (currentChannel % 13) + 1;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        Serial.printf("Switching to channel %d\n", currentChannel);
        lastChannelSwitch = millis();
    }
    static unsigned long lastDisplay = 0;

    // Handle buttons
    if (digitalRead(DISPLAY_BUTTON) == LOW) {
        if (displayOn) {
            uint8_t oldMode = displayMode;
            displayMode = (displayMode + 1) % 6;  // Changed from 5 to 6 to include the new SIGNAL mode
            
            // Reset packet counting when switching to or from packet mode
            if (oldMode == DISPLAY_MODE_PACKETS || displayMode == DISPLAY_MODE_PACKETS) {
                memset(packetsPerSecond, 0, sizeof(packetsPerSecond));
                packetIndex = 0;
                currentPacketCount = 0;
            }
        } else {
            displayOn = true;
        }
        digitalWrite(DISPLAY_BACKLIGHT, displayOn);
        delay(200);
    }

    if (digitalRead(POWER_BUTTON) == LOW) {
        gfx->fillScreen(BLACK);
        digitalWrite(DISPLAY_BACKLIGHT, LOW);
        digitalWrite(POWER_PIN, LOW);
        goToSleep();
    }

    // Check battery status
    handleBattery();

    // Update display
    if (millis() - lastDisplay >= 1000) {
        gfx->fillScreen(BLACK);

        switch(displayMode) {
            case DISPLAY_MODE_BATTERY: {
                lastVoltage = getBatteryVoltage();
                
                gfx->setTextSize(2);
                gfx->setTextColor(CYAN);
                gfx->setCursor(10, 20);
                gfx->println("Battery Status:");
                
                gfx->setTextSize(3);
                gfx->setCursor(10, 50);
                if (lastVoltage <= BATT_CRITICAL_VOLTAGE) {
                    gfx->setTextColor(RED);
                } else if (lastVoltage <= BATT_LOW_VOLTAGE) {
                    gfx->setTextColor(YELLOW);
                } else {
                    gfx->setTextColor(GREEN);
                }
                gfx->print(lastVoltage, 2);
                gfx->println("V");
                
                gfx->setTextSize(2);
                gfx->setCursor(10, 100);
                int percentage = (lastVoltage - BATT_CRITICAL_VOLTAGE) * 100 / 
                                (4.2 - BATT_CRITICAL_VOLTAGE);
                percentage = constrain(percentage, 0, 100);
                gfx->print("Approx: ");
                gfx->print(percentage);
                gfx->println("%");
                break;
            }
            
            case DISPLAY_MODE_UNIQUE: {
                gfx->setTextSize(2);  // Larger text for header
                gfx->setTextColor(CYAN);
                gfx->setCursor(10, 10);
                gfx->println("Unique Networks (2m):");
                
                gfx->setTextSize(1);  // Smaller text for network names
                int yPos = 35;  // Start networks list slightly higher
                int activeCount = 0;
                unsigned long currentTime = millis();
                
                // First count active networks
                for (int i = 0; i < uniqueCount; i++) {
                    if (currentTime - uniqueNetworks[i].firstSeen <= 120000) {
                        uniqueNetworks[i].active = true;
                        activeCount++;
                    } else {
                        uniqueNetworks[i].active = false;
                    }
                }

                // Auto-scroll every 2 seconds if there are more networks than can fit
                if (currentTime - lastScrollTime >= 2000 && activeCount > 8) {
                    uniqueNetworksScroll = (uniqueNetworksScroll + 1) % activeCount;
                    lastScrollTime = currentTime;
                }

                // Display networks starting from scroll position
                int displayed = 0;
                int skipped = 0;
                for (int i = 0; i < uniqueCount && displayed < 8; i++) {
                    if (!uniqueNetworks[i].active) continue;
                    
                    if (skipped < uniqueNetworksScroll) {
                        skipped++;
                        continue;
                    }

                    gfx->setTextColor(WHITE);
                    drawTruncatedText(uniqueNetworks[i].ssid, 10, yPos, 300);
                    yPos += 12;  // Reduced spacing between networks
                    displayed++;
                }
                
                gfx->setTextSize(2);  // Back to larger text for footer
                gfx->setTextColor(GREEN);
                gfx->setCursor(10, 150);
                gfx->print("Active: ");
                gfx->print(activeCount);
                gfx->setCursor(120, 150);
                gfx->printf("(%d/%d)", uniqueNetworksScroll + 1, activeCount);
                
                break;
            }
            
          case DISPLAY_MODE_MAC: {
    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, 10);
    gfx->println("Unique MACs:");
    
    unsigned long currentTime = millis();
    int activeCount1Min = 0;
    int activeCount2Min = 0;
    
    for(int i = 0; i < macCount; i++) {
        unsigned long age = currentTime - uniqueMacs[i].lastSeen;
        if(age <= 60000) activeCount1Min++;
        if(age <= 120000) activeCount2Min++;
    }
    
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 50);
    gfx->print("Last 1m: ");
    gfx->println(activeCount1Min);
    
    gfx->setCursor(10, 80);
    gfx->print("Last 2m: ");
    gfx->println(activeCount2Min);
    break;
}

case DISPLAY_MODE_PACKETS: {
    static unsigned long lastPacketCount = 0;
    if (millis() - lastPacketCount >= 1000) {
        packetsPerSecond[packetIndex] = currentPacketCount;
        packetIndex = (packetIndex + 1) % MAX_PACKETS;
        currentPacketCount = 0;
        lastPacketCount = millis();
        
        uint32_t totalPackets = 0;
        for(int i = 0; i < MAX_PACKETS; i++) {
            totalPackets += packetsPerSecond[i];
        }
        
        gfx->setTextSize(2);
        gfx->setTextColor(CYAN);
        gfx->setCursor(10, 10);
        gfx->println("Packets (2min):");
        gfx->setTextSize(3);
        gfx->setTextColor(WHITE);
        gfx->setCursor(10, 50);
        gfx->println(totalPackets);
    }
    break;
}

case DISPLAY_MODE_SIGNAL: {
    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, 10);
    gfx->println("Signal Strength:");
    
    // Calculate average RSSI
    int32_t rssiSum = 0;
    int32_t rssiMin = -30;
    int32_t rssiMax = -100;
    int validSamples = 0;
    
    for(int i = 0; i < RSSI_SAMPLES; i++) {
        if(rssiValues[i] != 0) {  // Only count valid readings
            rssiSum += rssiValues[i];
            rssiMin = min(rssiMin, (int32_t)rssiValues[i]);
            rssiMax = max(rssiMax, (int32_t)rssiValues[i]);
            validSamples++;
        }
    }
    
    int32_t rssiAvg = validSamples > 0 ? rssiSum / validSamples : 0;
    
    // Display average RSSI
    gfx->setTextSize(3);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 50);
    gfx->print("Avg: ");
    gfx->print(rssiAvg);
    gfx->println(" dBm");
    
    // Display range
    gfx->setTextSize(2);
    gfx->setCursor(10, 100);
    gfx->print("Range: ");
    gfx->print(rssiMin);
    gfx->print(" to ");
    gfx->print(rssiMax);
    gfx->println(" dBm");
    
    // Draw signal strength indicator
    int strength = map(rssiAvg, -100, -30, 0, 4);  // Convert RSSI to 0-4 bars
    for(int i = 0; i < 4; i++) {
        int barHeight = (i + 1) * 10;
        int x = 250 + (i * 15);
        int y = 140 - barHeight;
        if(i < strength) {
            gfx->fillRect(x, y, 10, barHeight, GREEN);
        } else {
            gfx->drawRect(x, y, 10, barHeight, DARKGREY);
        }
    }
    break;
}

            default: {  // DISPLAY_MODE_NORMAL
                gfx->setTextSize(2);  // Larger text for header
                gfx->setTextColor(CYAN);
                gfx->setCursor(10, 10);
                gfx->println("Ghost Networks:");
                
                gfx->setTextSize(1);  // Smaller text for network names
                int yPos = 35;
                int count = 0;
                int validNetworks = 0;
                unsigned long currentTime = millis();

                // First count valid networks
                for (int i = 0; i < probeCount; i++) {
                    unsigned long age = (currentTime - recentProbes[i].timestamp) / 1000;
                    if (age <= 30) validNetworks++;
                }

                // Auto-scroll every 2 seconds if there are more networks than can fit
                if (currentTime - lastScrollTime >= 2000 && validNetworks > 8) {
                    ghostNetworksScroll = (ghostNetworksScroll + 1) % validNetworks;
                    lastScrollTime = currentTime;
                }

                // Display networks starting from scroll position
                int displayed = 0;
                int skipped = 0;
                for (int i = 0; i < probeCount && displayed < 8; i++) {
                    unsigned long age = (currentTime - recentProbes[i].timestamp) / 1000;
                    if (age > 30) continue;
                    
                    if (skipped < ghostNetworksScroll) {
                        skipped++;
                        continue;
                    }

                    if (age < 5) gfx->setTextColor(WHITE);
                    else if (age < 15) gfx->setTextColor(YELLOW);
                    else gfx->setTextColor(RED);
                    
                    drawTruncatedText(recentProbes[i].ssid, 10, yPos, 300);
                    yPos += 12;  // Reduced spacing between networks
                    displayed++;
                }

                gfx->setTextSize(2);  // Back to larger text for footer
                gfx->setTextColor(GREEN);
                
                // Move channel display to the left
                gfx->setCursor(10, 150);
                gfx->print("Ch:");
                gfx->print(currentChannel);
                
                // Move scroll counter more to the right with proper spacing
                gfx->setCursor(160, 150);  // Increased from 120 to 160
                gfx->printf("(%d/%d)", ghostNetworksScroll + 1, validNetworks);

                break;
            }
        }
        lastDisplay = millis();
    }
}