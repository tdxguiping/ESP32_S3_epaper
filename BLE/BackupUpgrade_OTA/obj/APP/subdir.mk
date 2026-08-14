################################################################################
# MRS Version: 1.9.1
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/app_nfc.c \
../APP/app_uart.c \
../APP/central.c \
../APP/epd_product_info.c \
../APP/nfc/ISO14443-3A.c \
../APP/nfc/wch_nfca_picc_bsp.c \
../APP/peripheral.c \
../APP/peripheral_main.c \
../APP/uart1_wifi_passthrough.c \
../APP/wifi_time_wake.c

OBJS += \
./APP/app_nfc.o \
./APP/app_uart.o \
./APP/central.o \
./APP/epd_product_info.o \
./APP/nfc/ISO14443-3A.o \
./APP/nfc/wch_nfca_picc_bsp.o \
./APP/peripheral.o \
./APP/peripheral_main.o \
./APP/uart1_wifi_passthrough.o \
./APP/wifi_time_wake.o

C_DEPS += \
./APP/app_nfc.d \
./APP/app_uart.d \
./APP/central.d \
./APP/epd_product_info.d \
./APP/nfc/ISO14443-3A.d \
./APP/nfc/wch_nfca_picc_bsp.d \
./APP/peripheral.d \
./APP/peripheral_main.d \
./APP/uart1_wifi_passthrough.d \
./APP/wifi_time_wake.d


# Each subdirectory must supply rules for building sources it contributes
APP/%.o: ../APP/%.c
	@	@	riscv-none-elf-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common  -g -DHAL_SLEEP=TRUE -DCLK_OSC32K=1 -DDEBUG=Debug_UART0 $(USER_CFLAGS) -DIS014443A_FAST_CL=0 -DBLE_BUFF_MAX_LEN=251 -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Startup" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\APP\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Profile\include" -I"$(WORK_PATH)\SRC\StdPeriphDriver\inc" -I"$(WORK_PATH)\BLE\HAL\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Ld" -I"$(WORK_PATH)\BLE\LIB" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\NFCA_LIB" -I"$(WORK_PATH)\SRC\RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

