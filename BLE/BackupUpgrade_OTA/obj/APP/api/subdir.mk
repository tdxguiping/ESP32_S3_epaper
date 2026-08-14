################################################################################
# MRS Version: 1.9.1
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/api/Display_EPD_W21_spi.c \
../APP/api/display_api.c \
../APP/api/flash_api.c \
../APP/api/presave_api.c \
../APP/api/adc_api.c \
../APP/api/ch583_secure.c 

OBJS += \
./APP/api/Display_EPD_W21_spi.o \
./APP/api/display_api.o \
./APP/api/flash_api.o \
./APP/api/presave_api.o \
./APP/api/adc_api.o \
./APP/api/ch583_secure.o

C_DEPS += \
./APP/api/Display_EPD_W21_spi.d \
./APP/api/display_api.d \
./APP/api/flash_api.d \
./APP/api/presave_api.d \
./APP/api/adc_api.d \
./APP/api/ch583_secure.d


# Each subdirectory must supply rules for building sources it contributes
APP/api/%.o: ../APP/api/%.c
	@	@	riscv-none-elf-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common  -g -DHAL_SLEEP=TRUE -DCLK_OSC32K=1 -DDEBUG=Debug_UART0 $(USER_CFLAGS) -DBLE_BUFF_MAX_LEN=251 -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Startup" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\APP\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Profile\include" -I"$(WORK_PATH)\SRC\StdPeriphDriver\inc" -I"$(WORK_PATH)\BLE\HAL\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Ld" -I"$(WORK_PATH)\BLE\LIB" -I"$(WORK_PATH)\SRC\RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

