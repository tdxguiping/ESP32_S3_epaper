@echo Build Start!!!!!!!!!!!!!!!!!!!!
@set GCC12_BIN=C:\MounRiver\MounRiver_Studio\toolchain\RISC-V Embedded GCC12\bin
@if exist "%GCC12_BIN%\riscv-none-elf-gcc.exe" @set PATH=%GCC12_BIN%;%PATH%
@if "%1"=="" (
	@echo ver is null
	@exit /b 1
) else (
	@set VER_TMP=%1
)


set "EPD_SCREEN_TYPE=EPD_SCREEN_TYPE_A EPD_SCREEN_TYPE_T"

@for %%i in ("%cd%\..\..") do set "PARENT_DIR=%%~fi"

@echo current dir:%cd%
@echo PARENT_DIR:%PARENT_DIR%

@set "WORK_PATH=%PARENT_DIR%"
::#########DEFINE#########
@set VER=%VER_TMP%
@set FACTORY=ENABLE_SOFTWARE_TO_TDX
@set CUSTOMER=1
@set EPAPER_TYPE=ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6
@set FLASH_MODE=USB_EXTERN_FLASH_SAVE
::########################

::#########USER mkdir dir and file#########
@set U_FACTORY=TDX
@set U_CUSTOMER=TDX
@set U_EPAPER_TYPE_DIR=YT_SPD_800X400C6
::########################


for /f "skip=1" %%a in ('wmic os get LocalDateTime') do (
    set "DATETIME=%%a"
    goto :continue
)
:continue
@set "YYYY=%DATETIME:~0,4%"
@set "MM=%DATETIME:~4,2%"
@set "DD=%DATETIME:~6,2%"


setlocal enabledelayedexpansion


for %%t in (%EPD_SCREEN_TYPE%) do (
    @set "EPD_SCREEN_TYPE_TMP=%%t"
	if "!EPD_SCREEN_TYPE_TMP!"=="EPD_SCREEN_TYPE_A" (
        @set U_EPAPER_TYPE=YT_SPD_800X400C6_A
    ) else (
        @set U_EPAPER_TYPE=YT_SPD_800X400C6_T
    )    
	@set DEST_DIR_NAME=!U_FACTORY!_!U_EPAPER_TYPE_DIR!_V!VER!_!YYYY!!MM!!DD!
	@set DEST_FILE_NAME=!U_FACTORY!_!U_EPAPER_TYPE!_V!VER!_!YYYY!!MM!!DD!
	@set OUT_DIR=!CD!\OUTPUT
	@set DEST_DIR=!OUT_DIR!\!DEST_DIR_NAME!
	@set SOURCE=!CD!\obj\BackupUpgrade_OTA.hex
	@set DEST_FILE=!DEST_DIR!\!DEST_FILE_NAME!.hex
	@set USER_CFLAGS=-DVER=!VER! -D!FACTORY! -DINK_SCREEN_CUSTOMER=!CUSTOMER! -D!EPAPER_TYPE! -D!FLASH_MODE! -D!EPD_SCREEN_TYPE_TMP! -DINK_DEVICE_CHIP=INK_CHIP_CH585 -DTDX_SMALL_STREAM_ENABLE=1 -DTDX_LARGE_RAW_COLOR_DISABLE=1 -DTDX_LARGE_RAW_DIRECT=1 -DTDX_DISABLE_EPD_DELAY=1 -DTDX_FLASH_POWERUP_DELAY_MS=10U -DTDX_FLASH_WIP_LEGACY_DELAY=0 -DIMG_PERF_ENABLE=0 -DAPP_FACTORY_UART0_ENABLE=0 -DAPP_FACTORY_POWER_HOLD_ENABLE=0 -DUART0_TRANSFER_LOG_ENABLE=0 -DUART0_BLE_LOG_ENABLE=0 -DUART0_IMAGE_LOG_ENABLE=0 -DUART0_BUSY_LOG_ENABLE=0 -DUART0_OTA_LOG_ENABLE=0 -DUART0_BOOT_LOG_ENABLE=1 -DUART0_FAULT_LOG_ENABLE=1
	
	@echo /************************************************************/
	@echo Build Boe 800x480 6color Project
	@echo VER:			!VER_TMP!
	@echo FACROTY:		!U_FACTORY!
	@echo CUSTOMER:		!U_CUSTOMER!
	@echo EPAPER_TYPE:		!U_EPAPER_TYPE!
	@echo EPD_SCREEN_TYPE_TMP:    !EPD_SCREEN_TYPE_TMP!
	@echo OUT_FILE:		!DEST_FILE!
	@echo /************************************************************/

	@rem Rebuild each panel with its own USER_CFLAGS, including after a failed build.
	@make -C obj clean
	@if !ERRORLEVEL! neq 0 (
		@echo Clean Failure!!!!!!!!!!!!!!!!!!!!
		endlocal
		@exit /b 1
	)

	@make -C obj all
	@if !ERRORLEVEL! neq 0 (
		@echo Build Failure!!!!!!!!!!!!!!!!!!!!
	    endlocal
        @exit /b 1
	)

	@echo Build Success, Need Copy File!!!!!!!!!!!!!!!!!!!!

	@if not exist "!OUT_DIR!" (
	    @md "!OUT_DIR%" 2>nul
		@if !ERRORLEVEL! neq 0 (
	        @echo error: cannot mkdir "!OUT_DIR!"
	        endlocal
	        @exit /b 1
	    )
	    @echo create dir: "%OUT_DIR%"
	)

	@if not exist "!DEST_DIR!" (
	    @md "!DEST_DIR!" 2>nul
		@if !ERRORLEVEL! neq 0 (
	        @echo error: cannot mkdir "!DEST_DIR!"
	        endlocal
	        @exit /b 1
	    )
	    @echo create dir: "%DEST_DIR%"
	)

	@copy /y "!SOURCE!" "!DEST_FILE!" >nul

	@if !ERRORLEVEL! equ 0 (
	@		 echo Build Complete!!!!!!!!!!!!!!!!!!!!
	) else ( 
	@    echo failure: copy failure	
	endlocal
	@    exit /b 1
	)
	@rem Keep the final build artifacts for size/map inspection.
)

endlocal
