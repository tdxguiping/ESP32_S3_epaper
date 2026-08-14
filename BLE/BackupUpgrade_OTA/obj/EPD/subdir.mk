################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../EPD/JD79665_800_480_color4.c \
../EPD/JD79668A_400_300_color4.c \
../EPD/JD79686BB_800_480_color3.c \
../EPD/SSD1677_960_640_color3.c \
../EPD/SSD1683_272_792_color3.c \
../EPD/SSD1863_400_300_color3.c 

OBJS += \
./EPD/JD79665_800_480_color4.o \
./EPD/JD79668A_400_300_color4.o \
./EPD/JD79686BB_800_480_color3.o \
./EPD/SSD1677_960_640_color3.o \
./EPD/SSD1683_272_792_color3.o \
./EPD/SSD1863_400_300_color3.o 

C_DEPS += \
./EPD/JD79665_800_480_color4.d \
./EPD/JD79668A_400_300_color4.d \
./EPD/JD79686BB_800_480_color3.d \
./EPD/SSD1677_960_640_color3.d \
./EPD/SSD1683_272_792_color3.d \
./EPD/SSD1863_400_300_color3.d 


# Each subdirectory must supply rules for building sources it contributes
EPD/%.o: ../EPD/%.c
	@	@	riscv-none-elf-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common  -g -DHAL_SLEEP=TRUE -DCLK_OSC32K=1 -DDEBUG=Debug_UART0 $(USER_CFLAGS) -DBLE_BUFF_MAX_LEN=251 -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Startup" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\APP\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Profile\include" -I"$(WORK_PATH)\SRC\StdPeriphDriver\inc" -I"$(WORK_PATH)\BLE\HAL\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Ld" -I"$(WORK_PATH)\BLE\LIB" -I"$(WORK_PATH)\SRC\RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

