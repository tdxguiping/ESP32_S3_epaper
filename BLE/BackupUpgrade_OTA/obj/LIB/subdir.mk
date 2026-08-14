################################################################################
# MRS Version: 1.9.2
# CH585 BLE interrupt scheduler
################################################################################

# Add inputs and outputs from these tool invocations to the build variables
S_UPPER_SRCS += \
$(WORK_PATH)/BLE/LIB/ble_task_scheduler.S

OBJS += \
./LIB/ble_task_scheduler.o

S_UPPER_DEPS += \
./LIB/ble_task_scheduler.d


# Each subdirectory must supply rules for building sources it contributes
LIB/ble_task_scheduler.o: $(WORK_PATH)/BLE/LIB/ble_task_scheduler.S
	@	@	riscv-none-elf-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common  -g -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

