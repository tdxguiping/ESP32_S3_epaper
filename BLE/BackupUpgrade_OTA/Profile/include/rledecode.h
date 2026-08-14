
#ifndef RLEDECODE_H
#define RLEDECODE_H

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
typedef void (*RleDataCallback_t)(unsigned char *, int);
typedef int (*RleImgDataCallback_t)(unsigned char);

extern int rle_decrypt(unsigned char *input, int length, RleImgDataCallback_t rCb);
extern int rle_reverse_decrypt(unsigned char *input, int length,RleDataCallback_t rCb);
extern void rle_un_decrypt() ;
//extern int set_displayCb(DisplayDataCallback_t rCb);
/*********************************************************************
 * PROFILE CALLBACKS
 */
 /*********************************************************************
*********************************************************************/
#endif
