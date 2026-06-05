#pragma once

// Shared implementation for the 7.9-inch and 13.3-inch 1600x1200 panels.

const unsigned char PSR_V[2] = {
	0xDF, 0x69
};
const unsigned char PWR_V[6] = {
	0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38
};
const unsigned char POF_V[1] = {
	0x00
};
const unsigned char DRF_V[1] = {
	0x01
};
const unsigned char CDI_V[1] = {
	0x37
};

const unsigned char TRES_V[4] = {
	0x04, 0xB0, 0x03, 0x20
};
const unsigned char AMV_V[2] = {
	0x01, 0x00
};

//调用当前温度
const unsigned char CCSET_V_CUR[1] = {
	0x01
};
//锁定温度
const unsigned char CCSET_V_LOCK[1] = {
	0x03
};

const unsigned char PWS_V[1] = {
	0x22
};

const unsigned char DCDC_V[3] = {
	0x44, 0x54 ,0x00
};

const unsigned char BTST_P_V[2] = {
	0xE0, 0x20
};
const unsigned char BTST_N_V[2] = {
	0xE0, 0x20
};
const unsigned char Sleep_V[1] = {
	0xa5
};



#define PSR             0x00
#define PWR             0x01
#define POF             0x02
#define POFS            0x03
#define PON             0x04
#define BTST_N          0x05
#define BTST_P          0x06
#define DTM             0x10
#define DRF             0x12
#define PLL             0x30
#define TSC             0x40
#define CDI             0x50
#define TCON            0x60
#define TRES            0x61
#define PTLW            0x83
#define AN_TM           0x74
#define AGID            0x86
#define CMDA4           0xA4
#define DCDC            0xA5
#define BUCK_BOOST_VDDN 0xB0
#define TFT_VCOM_POWER  0xB1
#define EN_BUF          0xB6
#define BOOST_VDDP_EN   0xB7
#define CCSET           0xE0
#define PWS             0xE3
#define CMD66           0xF0
