#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "USB.h"
#include "USBCDC.h"
#include "usb_descriptors.h"

#define TFT_BL 38
#define SAMPLE_RATE 48000

Arduino_DataBus *bus = new Arduino_ESP32LCD8(
    7,  /* DC */
    6,  /* CS */
    8,  /* WR */
    9,  /* RD */
    39, /* D0 */
    40, /* D1 */
    41, /* D2 */
    42, /* D3 */
    45, /* D4 */
    46, /* D5 */
    47, /* D6 */
    48  /* D7 */
);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus,
    5,   /* RST */
    0,   /* rotation */
    true,/* IPS */
    170, /* width */
    320, /* height */
    35,  /* col offset 1 */
    0,   /* row offset 1 */
    35,  /* col offset 2 */
    0    /* row offset 2 */
);

static uint16_t currentVolume = 0;
static uint8_t currentMute = 0;

// Audio feedback data
static uint32_t current_sample_rate = SAMPLE_RATE;

// USB Event callback declaration
static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == ARDUINO_USB_EVENTS) {
        arduino_usb_event_data_t * data = (arduino_usb_event_data_t*)event_data;
        switch (event_id) {
            case ARDUINO_USB_STARTED_EVENT:
                Serial.println("USB STARTED");
                break;
            case ARDUINO_USB_STOPPED_EVENT:
                Serial.println("USB STOPPED");
                break;
            case ARDUINO_USB_SUSPEND_EVENT:
                Serial.println("USB SUSPENDED");
                break;
            case ARDUINO_USB_RESUME_EVENT:
                Serial.println("USB RESUMED");
                break;
            default:
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting...");
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    gfx->begin();
    gfx->fillScreen(BLACK);
    
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 10);
    gfx->println("USB Audio Test");

    // Initialize USB with debug info
    Serial.println("Initializing USB...");
    
    USB.VID(_VID);
    USB.PID(_PID);
    USB.manufacturerName("Espressif");
    USB.productName("ESP32-S3 Audio");
    USB.serialNumber("123456");
    USB.onEvent(usbEventCallback);
    
    if (!USB.begin()) {
        Serial.println("USB initialization failed!");
        gfx->setTextColor(RED);
        gfx->setCursor(10, 50);
        gfx->println("USB Init Failed!");
    } else {
        Serial.println("USB initialized successfully!");
        gfx->setTextColor(GREEN);
        gfx->setCursor(10, 50);
        gfx->println("USB Ready");
    }
}

void loop() {
    static unsigned long lastTick = 0;
    static int16_t x = 85;
    static int16_t y = 160;
    static int8_t dx = 1;
    static int8_t dy = 1;

    if (millis() - lastTick > 50) {
        lastTick = millis();
        
        gfx->fillCircle(x, y, 5, BLACK);
        
        x += dx;
        y += dy;
        
        if (x <= 5 || x >= 165) dx = -dx;
        if (y <= 5 || y >= 315) dy = -dy;
        
        gfx->fillCircle(x, y, 5, RED);
    }
}