#ifndef DISPLAY_H
#define DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#define BLACK 0x00
#define WHITE 0x11
#define YELLOW 0x22
#define RED 0x33
#define BLUE 0x55
#define GREEN 0x66

#define EPD_WIDTH 1200
#define EPD_HEIGHT 1600
#define EPD_ROW_BYTES (EPD_WIDTH / 2)
#define EPD_HALF_BYTES (EPD_ROW_BYTES / 2)
#define EPD_FRAME_BYTES (EPD_ROW_BYTES * EPD_HEIGHT)

void epdHardwareReset(void);
void setPinCsAll(unsigned int setLevel);
void initEPD(void);
void epdDisplayColor(unsigned char colorSelect);
void epdDisplayFrame(const unsigned char *frame);

#endif
