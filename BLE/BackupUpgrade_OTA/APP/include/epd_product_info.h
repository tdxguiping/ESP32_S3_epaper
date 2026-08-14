#ifndef EPD_PRODUCT_INFO_H
#define EPD_PRODUCT_INFO_H

/*
 * Product identity is selected by build scripts with one screen macro and one
 * vendor macro. Vendor ids are local to each screen model; do not treat the
 * same vendor name as a global fixed id across all screens.
 */

#if defined(EPD_SCREEN_13D3_1200X1600_C6) && defined(EPD_SCREEN_7D09_1200X1600_C6)
#error "Only one EPD_SCREEN_* macro can be defined"
#endif

#if !defined(EPD_SCREEN_13D3_1200X1600_C6) && !defined(EPD_SCREEN_7D09_1200X1600_C6)
#error "Please define one EPD_SCREEN_* macro"
#endif

#if defined(EPD_VENDOR_XT) && defined(EPD_VENDOR_DKE)
#error "Only one EPD_VENDOR_* macro can be defined"
#endif

#ifdef EPD_SCREEN_13D3_1200X1600_C6

#define EPD_PRODUCT_SCREEN_TYPE_CHAR 'd'
#define EPD_PRODUCT_DISPLAY_MAX_BUF  ((1200UL * 1600UL) / 2UL)
#define EPD_PRODUCT_DISPLAY_TIME     65UL

/*
 * 13.3 inch 1200x1600 6 color
 *
 * XT T133A01:
 *   vendor id: 0x00
 *   file suffix: XT00
 *   IC: NT61522/PVT61522 x2 + EK73601 x1
 *
 * DKE DEPG1330RTE133F5HP:
 *   vendor id: 0x01
 *   file suffix: DKE01
 *   IC: NT61522 x2 + EK73601BA x1
 */
#ifdef EPD_VENDOR_XT
#define EPD_PRODUCT_VENDOR_ID 0x00U
#elif defined(EPD_VENDOR_DKE)
#define EPD_PRODUCT_VENDOR_ID 0x01U
#else
#error "Unsupported vendor for EPD_SCREEN_13D3_1200X1600_C6"
#endif

#endif

#ifdef EPD_SCREEN_7D09_1200X1600_C6

#define EPD_PRODUCT_SCREEN_TYPE_CHAR 'e'
#define EPD_PRODUCT_DISPLAY_MAX_BUF  ((1200UL * 1600UL) / 2UL)
#define EPD_PRODUCT_DISPLAY_TIME     65UL

/*
 * 7.09 inch 1200x1600 6 color
 *
 * BOE/XT 24116-01390:
 *   vendor id: 0x00
 *   file suffix: XT00
 *   IC: TBD
 */
#ifdef EPD_VENDOR_XT
#define EPD_PRODUCT_VENDOR_ID 0x00U
#else
#error "Unsupported vendor for EPD_SCREEN_7D09_1200X1600_C6"
#endif

#endif

#if EPD_PRODUCT_VENDOR_ID > 0x0FU
#error "EPD_PRODUCT_VENDOR_ID must be 0x00..0x0F"
#endif

#define EPD_PRODUCT_BOARD_INFO_BASE 0x40U
#define EPD_PRODUCT_BOARD_INFO      (EPD_PRODUCT_BOARD_INFO_BASE | (EPD_PRODUCT_VENDOR_ID & 0x0FU))

#endif
