#include "tusb.h"

#define _PID 0x4002
#define _VID 0x303A

const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB            = 0x0200,
    .bDeviceClass      = TUSB_CLASS_MISC,
    .bDeviceSubClass   = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol   = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0   = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor          = _VID,
    .idProduct         = _PID,
    .bcdDevice         = 0x0100,
    .iManufacturer     = 0x01,
    .iProduct          = 0x02,
    .iSerialNumber     = 0x03,
    .bNumConfigurations = 0x01
};

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(0, 0, 0x01, 0x81, 64)
};

char const* string_desc_arr [] = {
    (const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
    "Espressif",                   // 1: Manufacturer
    "ESP32-S3 Audio",             // 2: Product
    "123456",                     // 3: Serials
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc_str[32];
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) return NULL;
        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;
        for(uint8_t i=0; i<chr_count; i++) {
            desc_str[1+i] = str[i];
        }
    }
    desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);
    return desc_str;
}