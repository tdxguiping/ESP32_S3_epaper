################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/driver/epd/SPD1657_400_600_color6.c \
../APP/driver/epd/M009FT_1024_600_color6.c \
../APP/driver/epd/JD79665_800_480_color4.c \
../APP/driver/epd/JD79668A_400_300_color4.c \
../APP/driver/epd/JD79686BB_800_480_color3.c \
../APP/driver/epd/SSD1677_960_640_color3.c \
../APP/driver/epd/SSD1683_272_792_color3.c \
../APP/driver/epd/SSD1863_400_300_color3.c \
../APP/driver/epd/UC8279_800_480_color3.c \
../APP/driver/epd/UC8276_400_300_color3.c \
../APP/driver/epd/SSD2683_400_300_color4.c \
../APP/driver/epd/UC8179_800_480_color2.c \
../APP/driver/epd/SSD1683A_272_792_color2.c \
../APP/driver/epd/JD79686BB_1360_480_color3.c \
../APP/driver/epd/JD79686AB_1360_480_color3.c \
../APP/driver/epd/JD79665_960_640_color4.c \
../APP/driver/epd/JD79665AA_1360_480_color4.c \
../APP/driver/epd/JD79665AA_1280_600_color4.c \
../APP/driver/epd/JD79686AC_1360_480_color3.c \
../APP/driver/epd/SSD2683ZA_272_792_color4.c \
../APP/driver/epd/UC8179_800_480_color3.c

OBJS += \
./APP/driver/epd/SPD1657_400_600_color6.o \
./APP/driver/epd/M009FT_1024_600_color6.o \
./APP/driver/epd/JD79665_800_480_color4.o \
./APP/driver/epd/JD79668A_400_300_color4.o \
./APP/driver/epd/JD79686BB_800_480_color3.o \
./APP/driver/epd/SSD1677_960_640_color3.o \
./APP/driver/epd/SSD1683_272_792_color3.o \
./APP/driver/epd/SSD1863_400_300_color3.o \
./APP/driver/epd/UC8279_800_480_color3.o \
./APP/driver/epd/UC8276_400_300_color3.o \
./APP/driver/epd/SSD2683_400_300_color4.o \
./APP/driver/epd/UC8179_800_480_color2.o \
./APP/driver/epd/SSD1683A_272_792_color2.o \
./APP/driver/epd/JD79686AB_1360_480_color3.o \
./APP/driver/epd/JD79686BB_1360_480_color3.o \
./APP/driver/epd/JD79665_960_640_color4.o \
./APP/driver/epd/JD79665AA_1360_480_color4.o \
./APP/driver/epd/JD79665AA_1280_600_color4.o \
./APP/driver/epd/JD79686AC_1360_480_color3.o \
./APP/driver/epd/SSD2683ZA_272_792_color4.o \
./APP/driver/epd/UC8179_800_480_color3.o

C_DEPS += \
./APP/driver/epd/SPD1657_400_600_color6.d \
./APP/driver/epd/M009FT_1024_600_color6.d \
./APP/driver/epd/JD79665_800_480_color4.d \
./APP/driver/epd/JD79668A_400_300_color4.d \
./APP/driver/epd/JD79686BB_800_480_color3.d \
./APP/driver/epd/SSD1677_960_640_color3.d \
./APP/driver/epd/SSD1683_272_792_color3.d \
./APP/driver/epd/SSD1863_400_300_color3.d \
./APP/driver/epd/UC8279_800_480_color3.d \
./APP/driver/epd/UC8276_400_300_color3.d \
./APP/driver/epd/SSD2683_400_300_color4.d \
./APP/driver/epd/UC8179_800_480_color2.d \
./APP/driver/epd/SSD1683A_272_792_color2.d \
./APP/driver/epd/JD79686AB_1360_480_color3.d \
./APP/driver/epd/JD79686BB_1360_480_color3.d \
./APP/driver/epd/JD79665_960_640_color4.d \
./APP/driver/epd/JD79665AA_1360_480_color4.d \
./APP/driver/epd/JD79665AA_1280_600_color4.d \
./APP/driver/epd/JD79686AC_1360_480_color3.d \
./APP/driver/epd/SSD2683ZA_272_792_color4.d \
./APP/driver/epd/UC8179_800_480_color3.d


# Each subdirectory must supply rules for building sources it contributes
APP/driver/epd/%.o: ../APP/driver/epd/%.c
	@	@	riscv-none-elf-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common  -g -DHAL_SLEEP=TRUE -DCLK_OSC32K=1 -DDEBUG=Debug_UART0 $(USER_CFLAGS) -DBLE_BUFF_MAX_LEN=251 -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Startup" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\APP\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Profile\include" -I"$(WORK_PATH)\SRC\StdPeriphDriver\inc" -I"$(WORK_PATH)\BLE\HAL\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Ld" -I"$(WORK_PATH)\BLE\LIB" -I"$(WORK_PATH)\SRC\RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

