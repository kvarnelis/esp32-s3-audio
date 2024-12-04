#ifndef WIFI_AUDIO_H
#define WIFI_AUDIO_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Display setup
extern Arduino_DataBus *bus;
extern Arduino_GFX *gfx;

// Use RGB565 color definitions from Arduino_GFX.h
#define BLACK RGB565_BLACK
#define WHITE RGB565_WHITE
#define RED   RGB565_RED
#define GREEN RGB565_GREEN
#define BLUE  RGB565_BLUE

#endif // WIFI_AUDIO_H