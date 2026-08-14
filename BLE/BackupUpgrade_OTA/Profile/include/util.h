#ifndef UTIL_H
#define UTIL_H
 
char hexToChar(int hex);
void convert_number(int num, char *char1, char *char2);
unsigned char getAdcAndWorkMode(unsigned char adc, unsigned char workmode);
void reverseMac(uint8_t Mac[6]);
uint8_t Read_ChipID_R8(void);
void reverseData(uint8_t *data, uint8_t len);

#endif //UTIL_H


