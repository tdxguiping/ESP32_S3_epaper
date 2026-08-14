#ifndef EPD_DRIVER_DEBUG_H
#define EPD_DRIVER_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * INCLUDES
 */
/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
// Device information service
/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

/*********************************************************************
 * Profile Attributes - variables
 */

/*********************************************************************
 * Profile Attributes - Table
 */
/*********************************************************************
 * LOCAL FUNCTIONS
 */
#ifdef EPD_DISPLAY_TEST_ENABLE
extern void EPD_Driver_Display_WithOut_Flash_Debug(void);
extern void	EPD_Driver_Display_From_Flash_Debug(void);
#endif
#ifdef EPD_BWSOLID_IMG_CYCLE_TEST_ENABLE
extern void EPD_BWSolidImg_CycleTest(void);
#endif
/*********************************************************************
 * PROFILE CALLBACKS
 */
 /*********************************************************************
*********************************************************************/
#endif
