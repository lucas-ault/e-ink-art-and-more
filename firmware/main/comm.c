#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "comm.h"
#include "pindefine.h"

#define SPI SPI3_HOST
#define SPI_MAX_BUFFER_SIZE 32768

static spi_device_handle_t spi;

esp_err_t initialSpi(void)
{
    spi_bus_config_t spiBusConfig = {
        .data0_io_num = SPI_Data0,
        .data1_io_num = SPI_Data1,
        .sclk_io_num = SPI_CLK,
        .data2_io_num = -1,
        .data3_io_num = -1,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .max_transfer_sz = SPI_MAX_BUFFER_SIZE,
    };

    spi_device_interface_config_t spiDeviceConfig = {
        .command_bits = 8,
        .clock_speed_hz = SPI_MASTER_FREQ_10M,
        .duty_cycle_pos = 128,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 7,
        .cs_ena_posttrans = 3,
    };

    esp_err_t status = spi_bus_initialize(SPI, &spiBusConfig, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(status);
    status = spi_bus_add_device(SPI, &spiDeviceConfig, &spi);
    ESP_ERROR_CHECK(status);
    return status;
}

void initialGpio(void)
{
    gpio_config_t ioConfig = {
        .pin_bit_mask = ((1ULL << EPD_RST) | (1ULL << SPI_CS0) | (1ULL << SPI_CS1) | (1ULL << LOAD_SW)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&ioConfig));

    ioConfig.pin_bit_mask = (1ULL << EPD_BUSY);
    ioConfig.intr_type = GPIO_INTR_DISABLE;
    ioConfig.mode = GPIO_MODE_INPUT;
    ioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&ioConfig));
}

void delayms(unsigned int delayTime)
{
    vTaskDelay(pdMS_TO_TICKS(delayTime));
}

esp_err_t spiTransmitCommand(unsigned char commandBuf)
{
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.cmd = commandBuf;
    trans.length = 0;
    trans.tx_buffer = NULL;
    esp_err_t status = spi_device_transmit(spi, &trans);
    assert(status == ESP_OK);
    return status;
}

esp_err_t spiTransmit(unsigned char commandBuf, const unsigned char *dataBuffer, unsigned int dataLength)
{
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.cmd = commandBuf;
    trans.length = dataLength * 8;
    trans.tx_buffer = dataBuffer;
    trans.rx_buffer = NULL;
    return spi_device_transmit(spi, &trans);
}

esp_err_t spiTransmitData(unsigned char *dataBuffer, unsigned long dataLength)
{
    esp_err_t status = ESP_OK;

    while (dataLength >= SPI_MAX_BUFFER_SIZE) {
        spi_transaction_ext_t trans_ext;
        memset(&trans_ext, 0, sizeof(trans_ext));
        trans_ext.command_bits = 0;
        trans_ext.base.length = SPI_MAX_BUFFER_SIZE * 8;
        trans_ext.base.tx_buffer = dataBuffer;
        trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;
        status = spi_device_transmit(spi, &trans_ext.base);
        dataLength -= SPI_MAX_BUFFER_SIZE;
        dataBuffer += SPI_MAX_BUFFER_SIZE;
    }

    if (dataLength > 0) {
        spi_transaction_ext_t trans_ext;
        memset(&trans_ext, 0, sizeof(trans_ext));
        trans_ext.command_bits = 0;
        trans_ext.base.length = dataLength * 8;
        trans_ext.base.tx_buffer = dataBuffer;
        trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;
        status = spi_device_transmit(spi, &trans_ext.base);
    }

    return status;
}

void setGpioLevel(unsigned char pinNumber, unsigned char voltageLevel)
{
    gpio_set_level(pinNumber, voltageLevel);
}

unsigned char getGpioLevel(unsigned char pinNumber)
{
    return gpio_get_level(pinNumber);
}
