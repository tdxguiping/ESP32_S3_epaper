#include "epd_type_1600_1200_common.h"
#include "display_bsp.h"

void ePaperPort::NT61522_Sleep() {
    isEPDInit = false;
    setPinCsAll(0);
    spiTransmit(0x07, NT61522_Sleep_V(), 1);
    setPinCsAll(1);

    
}

void ePaperPort::NT61522_DisplayImage(const uint8_t *imageData, size_t imageSize) {
    uint32_t i;

    if (imageData == nullptr || imageSize == 0) {
        ESP_LOGE(TAG, "NT61522 display image invalid data");
        return;
    }

    if (imageSize != 960000) {
        ESP_LOGW(TAG, "NT61522_DisplayImage unexpected imageSize=%u, expect=960000", (unsigned int)imageSize);
    }

    isEPDInit = false;
    EPD_Init();
    NT61522_Init_display();
    //EPD_Select_Master();
    //spiTransmitCommand(R10_DTM);
    uint32_t offset;

#if 1
    offset=0;
    for (i = 0; i < 1600; ++i) {
        spiTransmitData(imageData + offset, 300);
        //memset(Data_Buffer, 0x11,300);        
        //spiTransmitData(Data_Buffer, 300);
        offset += 300;
    }

    EPD_Select_Slave();
    spiTransmitCommand(R10_DTM);
    for (i = 0; i < 1600; ++i) {
        spiTransmitData(imageData + offset, 300);
        //memset(Data_Buffer, 0x33,300);        
        //spiTransmitData(Data_Buffer, 300);
        offset += 300;
    }
#endif

    // memset(Data_Buffer, 0x11,75);        
    // memset(Data_Buffer+75, 0x22,75);                
    // memset(Data_Buffer+75+75, 0x33,75);                
    // memset(Data_Buffer+75+75+75, 0x55,75);                


    // setPinCs(TARGET_MASTER, 0);
    // spiTransmitCommand(R10_DTM);
    // ESP_LOGI(TAG, "NT61522 master transmit start");
    // offset=0;
    // j=0;
    // for (size_t i = 0; i < 1600; ++i) {
    //      spiTransmitData(imageData + offset, 300);
    //     offset += 300;
    // }
    // setPinCs(TARGET_MASTER, 1);
    // delayms(30);
    // ESP_LOGI(TAG, "NT61522 master transmit done, offset=%u", (unsigned int)offset);

    // setPinCs(TARGET_SLAVE, 0);
    // spiTransmitCommand(R10_DTM);
    // ESP_LOGI(TAG, "NT61522 slave transmit start");
    // for (size_t i = 0; i < 1600; ++i) {
    //     spiTransmitData(imageData + offset, 300);
    //     offset += 300;
    // }
    // setPinCs(TARGET_SLAVE, 1);
    // delayms(30);
    // ESP_LOGI(TAG, "NT61522 slave transmit done, offset=%u", (unsigned int)offset);

    setPinCsAll(1);   
    NT61522_Display();

}

const uint8_t* ePaperPort::NT61522_PSR_V() {
    static const uint8_t v[] = {0xDF, 0x6B};
    return v;
}

const uint8_t* ePaperPort::NT61522_PWR_V() {
    static const uint8_t v[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
    return v;
}

const uint8_t* ePaperPort::NT61522_POF_V() {
    static const uint8_t v[] = {0x01};
    return v;
}

const uint8_t* ePaperPort::NT61522_DRF_V() {
    static const uint8_t v[] = {0x00};
    return v;
}

const uint8_t* ePaperPort::NT61522_CDI_V() {
    static const uint8_t v[] = {0x37};
    return v;
}

const uint8_t* ePaperPort::NT61522_TRES_V() {
    static const uint8_t v[] = {0x04, 0xB0, 0x03, 0x20};
    return v;
}

const uint8_t* ePaperPort::NT61522_AMV_V() {
    static const uint8_t v[] = {0x01, 0x00};
    return v;
}

const uint8_t* ePaperPort::NT61522_CCSET_V_CUR() {
    static const uint8_t v[] = {0x01};
    return v;
}

const uint8_t* ePaperPort::NT61522_CCSET_V_LOCK() {
    static const uint8_t v[] = {0x03};
    return v;
}

const uint8_t* ePaperPort::NT61522_PWS_V() {
    static const uint8_t v[] = {0x22};
    return v;
}

const uint8_t* ePaperPort::NT61522_DCDC_V() {
    static const uint8_t v[] = {0x44, 0x54, 0x00};
    return v;
}

const uint8_t* ePaperPort::NT61522_BTST_P_V() {
    static const uint8_t v[] = {0xE0, 0x20};
    return v;
}

const uint8_t* ePaperPort::NT61522_BTST_N_V() {
    static const uint8_t v[] = {0xE0, 0x20};
    return v;
}

const uint8_t* ePaperPort::NT61522_Sleep_V() {
    static const uint8_t v[] = {0xA5};
    return v;
}
