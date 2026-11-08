#include <stdio.h>
#include <string.h>

#include "display.h"
#include "comm.h"
#include "pindefine.h"
#include "esp_log.h"

static const char *TAG = "epd";

#define PSR 0x00
#define PWR 0x01
#define POF 0x02
#define PON 0x04
#define BTST_N 0x05
#define BTST_P 0x06
#define DTM 0x10
#define DRF 0x12
#define CDI 0x50
#define TCON 0x60
#define TRES 0x61
#define AN_TM 0x74
#define AGID 0x86
#define BUCK_BOOST_VDDN 0xB0
#define TFT_VCOM_POWER 0xB1
#define EN_BUF 0xB6
#define BOOST_VDDP_EN 0xB7
#define CCSET 0xE0
#define PWS 0xE3
#define CMD66 0xF0

#define EPD_IMAGE_DATA_BUFFER 8192
#define BUSY_TIMEOUT_MS 45000

static unsigned char epdImageDataBuffer[EPD_IMAGE_DATA_BUFFER];
static const unsigned char spiCsPin[2] = {SPI_CS0, SPI_CS1};

static const unsigned char PSR_V[2] = {0xDF, 0x69};
static const unsigned char PWR_V[6] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static const unsigned char POF_V[1] = {0x00};
static const unsigned char DRF_V[1] = {0x01};
static const unsigned char CDI_V[1] = {0xF7};
static const unsigned char TCON_V[2] = {0x03, 0x03};
static const unsigned char TRES_V[4] = {0x04, 0xB0, 0x03, 0x20};
static const unsigned char CMD66_V[6] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
static const unsigned char EN_BUF_V[1] = {0x07};
static const unsigned char CCSET_V[1] = {0x01};
static const unsigned char PWS_V[1] = {0x22};
static const unsigned char AN_TM_V[9] = {0xC0, 0x1E, 0x1E, 0xCE, 0xCE, 0xCE, 0x15, 0x15, 0x55};
static const unsigned char AGID_V[1] = {0x10};
static const unsigned char BTST_P_V[2] = {0xE8, 0x28};
static const unsigned char BOOST_VDDP_EN_V[1] = {0x01};
static const unsigned char BTST_N_V[2] = {0xE8, 0x28};
static const unsigned char BUCK_BOOST_VDDN_V[1] = {0x01};
static const unsigned char TFT_VCOM_POWER_V[1] = {0x02};

static void setPinCs(unsigned char csNumber, unsigned int setLevel)
{
    setGpioLevel(spiCsPin[csNumber], setLevel);
}

static void checkBusyHigh(void)
{
    int waited = 0;
    while (!getGpioLevel(EPD_BUSY)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
        if (waited >= BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "BUSY stayed low for %d ms", waited);
            return;
        }
    }
}

static void writeEpd(unsigned char epdCommand, const unsigned char *epdData, unsigned int epdDataLength)
{
    spiTransmit(epdCommand, epdData, epdDataLength);
}

static void writeEpdCommand(unsigned char epdCommand)
{
    spiTransmitCommand(epdCommand);
}

static void writeEpdData(unsigned char *epdData, unsigned int epdDataLength)
{
    spiTransmitData(epdData, epdDataLength);
}

void setPinCsAll(unsigned int setLevel)
{
    for (unsigned char i = 0; i < 2; i++) {
        setGpioLevel(spiCsPin[i], setLevel);
    }
}

void epdHardwareReset(void)
{
    setGpioLevel(EPD_RST, GPIO_LOW);
    delayms(20);
    setGpioLevel(EPD_RST, GPIO_HIGH);
    delayms(20);
}

void initEPD(void)
{
    epdHardwareReset();
    checkBusyHigh();

    setPinCs(0, GPIO_LOW);
    writeEpd(AN_TM, AN_TM_V, sizeof(AN_TM_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(CMD66, CMD66_V, sizeof(CMD66_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(PSR, PSR_V, sizeof(PSR_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(CDI, CDI_V, sizeof(CDI_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(TCON, TCON_V, sizeof(TCON_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(AGID, AGID_V, sizeof(AGID_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(PWS, PWS_V, sizeof(PWS_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(CCSET, CCSET_V, sizeof(CCSET_V));
    setPinCsAll(GPIO_HIGH);

    setPinCsAll(GPIO_LOW);
    writeEpd(TRES, TRES_V, sizeof(TRES_V));
    setPinCsAll(GPIO_HIGH);

    setPinCs(0, GPIO_LOW);
    writeEpd(PWR, PWR_V, sizeof(PWR_V));
    setPinCsAll(GPIO_HIGH);

    setPinCs(0, GPIO_LOW);
    writeEpd(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
    setPinCsAll(GPIO_HIGH);

    setPinCs(0, GPIO_LOW);
    writeEpd(BTST_P, BTST_P_V, sizeof(BTST_P_V));
    setPinCsAll(GPIO_HIGH);

    setPinCs(0, GPIO_LOW);
    writeEpd(BOOST_VDDP_EN, BOOST_VDDP_EN_V, sizeof(BOOST_VDDP_EN_V));
    setPinCsAll(GPIO_HIGH);

    setPinCs(0, GPIO_LOW);
    writeEpd(BTST_N, BTST_N_V, sizeof(BTST_N_V));
    setPinCsAll(GPIO_HIGH);

    setPinCs(0, GPIO_LOW);
    writeEpd(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V, sizeof(BUCK_BOOST_VDDN_V));
    setPinCsAll(GPIO_HIGH);

    setPinCs(0, GPIO_LOW);
    writeEpd(TFT_VCOM_POWER, TFT_VCOM_POWER_V, sizeof(TFT_VCOM_POWER_V));
    setPinCsAll(GPIO_HIGH);

    ESP_LOGI(TAG, "init done");
}

static void epdDisplay(void)
{
    ESP_LOGI(TAG, "PON");
    setPinCsAll(GPIO_LOW);
    writeEpdCommand(PON);
    checkBusyHigh();
    setPinCsAll(GPIO_HIGH);

    ESP_LOGI(TAG, "DRF");
    setPinCsAll(GPIO_LOW);
    delayms(30);
    writeEpd(DRF, DRF_V, sizeof(DRF_V));
    checkBusyHigh();
    setPinCsAll(GPIO_HIGH);

    ESP_LOGI(TAG, "POF");
    setPinCsAll(GPIO_LOW);
    writeEpd(POF, POF_V, sizeof(POF_V));
    checkBusyHigh();
    setPinCsAll(GPIO_HIGH);
    ESP_LOGI(TAG, "refresh finished");
}

void epdDisplayColor(unsigned char colorSelect)
{
    const unsigned long halfFrame = (unsigned long)EPD_HALF_BYTES * EPD_HEIGHT;
    memset(epdImageDataBuffer, colorSelect, EPD_IMAGE_DATA_BUFFER);

    setPinCsAll(GPIO_LOW);
    writeEpdCommand(DTM);
    for (unsigned long i = 0; i < halfFrame / EPD_IMAGE_DATA_BUFFER; i++) {
        writeEpdData(epdImageDataBuffer, EPD_IMAGE_DATA_BUFFER);
    }
    writeEpdData(epdImageDataBuffer, halfFrame % EPD_IMAGE_DATA_BUFFER);
    setPinCsAll(GPIO_HIGH);

    epdDisplay();
}

void epdDisplayFrame(const unsigned char *frame)
{
    setPinCsAll(GPIO_HIGH);
    setPinCs(0, GPIO_LOW);
    writeEpdCommand(DTM);
    for (int i = 0; i < EPD_HEIGHT; i++) {
        writeEpdData((unsigned char *)frame + (i * EPD_ROW_BYTES), EPD_HALF_BYTES);
    }
    setPinCsAll(GPIO_HIGH);

    setPinCs(1, GPIO_LOW);
    writeEpdCommand(DTM);
    for (int i = 0; i < EPD_HEIGHT; i++) {
        writeEpdData((unsigned char *)frame + (i * EPD_ROW_BYTES) + EPD_HALF_BYTES, EPD_HALF_BYTES);
    }
    setPinCsAll(GPIO_HIGH);

    epdDisplay();
}
