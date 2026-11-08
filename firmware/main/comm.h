#ifndef COMM_H
#define COMM_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t initialSpi(void);
void initialGpio(void);
void delayms(unsigned int delayTime);
esp_err_t spiTransmitCommand(unsigned char commandBuf);
esp_err_t spiTransmitData(unsigned char *dataBuffer, unsigned long dataLength);
esp_err_t spiTransmit(unsigned char commandBuf, const unsigned char *dataBuffer, unsigned int dataLength);
void setGpioLevel(unsigned char pinNumber, unsigned char voltageLevel);
unsigned char getGpioLevel(unsigned char pinNumber);

#endif
