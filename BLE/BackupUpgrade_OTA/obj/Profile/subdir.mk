################################################################################
# MRS Version: 1.9.1
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Profile/OTAprofile.c \
../Profile/aes.c \
../Profile/aes_util.c \
../Profile/base64.c \
../Profile/boardcast_data_op.c \
../Profile/boeinfoservice.c \
../Profile/xtinfoservice.c \
../Profile/tdxinfoservice.c \
../Profile/gattprofile.c \
../Profile/rledecode.c \
../Profile/util.c

OBJS += \
./Profile/OTAprofile.o \
./Profile/aes.o \
./Profile/aes_util.o \
./Profile/base64.o \
./Profile/boardcast_data_op.o \
./Profile/boeinfoservice.o \
./Profile/xtinfoservice.o \
./Profile/tdxinfoservice.o \
./Profile/rledecode.o \
./Profile/util.o \
./Profile/gattprofile.o

C_DEPS += \
./Profile/OTAprofile.d \
./Profile/aes.d \
./Profile/aes_util.d \
./Profile/base64.d \
./Profile/boardcast_data_op.d \
./Profile/boeinfoservice.d \
./Profile/xtinfoservice.d \
./Profile/tdxinfoservice.d \
./Profile/rledecode.d \
./Profile/util.d \
./Profile/gattprofile.d


# Each subdirectory must supply rules for building sources it contributes
Profile/%.o: ../Profile/%.c
	@	@	riscv-none-elf-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common  -g -DHAL_SLEEP=TRUE -DCLK_OSC32K=1 -DDEBUG=Debug_UART0 $(USER_CFLAGS) -DBLE_BUFF_MAX_LEN=251 -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Startup" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\APP\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Profile\include" -I"$(WORK_PATH)\SRC\StdPeriphDriver\inc" -I"$(WORK_PATH)\BLE\HAL\include" -I"$(WORK_PATH)\BLE\BackupUpgrade_OTA\Ld" -I"$(WORK_PATH)\BLE\LIB" -I"$(WORK_PATH)\SRC\RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

