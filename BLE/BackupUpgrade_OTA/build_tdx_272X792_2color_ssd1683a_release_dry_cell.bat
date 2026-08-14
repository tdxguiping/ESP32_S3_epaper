@echo off

@echo Build Start!!!!!!!!!!!!!!!!!!!!
@set GCC12_BIN=E:\E_paper_tool\MounRiver\MounRiver_Studio\toolchain\RISC-V Embedded GCC12\bin
@if exist "%GCC12_BIN%\riscv-none-elf-gcc.exe" @set PATH=%GCC12_BIN%;%PATH%
@if "%1"=="" (
	@echo ver is null
	@exit /b 1
) else (
	@set VER_TMP=%1
)

:: 定义需要编译的所有 EPD_SCREEN_TYPE（用空格分隔）
set "EPD_SCREEN_TYPE=EPD_SCREEN_TYPE_A EPD_SCREEN_TYPE_T"

@for %%i in ("%cd%\..\..") do set "PARENT_DIR=%%~fi"

@echo current dir:%cd%
@echo PARENT_DIR:%PARENT_DIR%

@set "WORK_PATH=%PARENT_DIR%"
::#########DEFINE#########
@set VER=%VER_TMP%
@set FACTORY=ENABLE_SOFTWARE_TO_TDX
@set CUSTOMER=1
@set EPAPER_TYPE=ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2
@set BATTERY=HARDWAR_DRY_CELL 
@set FLASH_MODE=USB_EXTERN_FLASH_SAVE
::########################

::#########USER mkdir dir and file#########
@set U_FACTORY=TDX
@set U_CUSTOMER=TDX
@set U_EPAPER_TYPE_DIR=AVD_SSD1683A_272X792C2_DRY_CELL
::########################

:: 使用WMIC获取日期（格式：YYYYMMDD）
for /f "skip=1" %%a in ('wmic os get LocalDateTime') do (
    set "DATETIME=%%a"
    goto :continue
)
:continue
@set "YYYY=%DATETIME:~0,4%"
@set "MM=%DATETIME:~4,2%"
@set "DD=%DATETIME:~6,2%"

:: 启用延迟扩展（需要在循环前开启）
setlocal enabledelayedexpansion

:: 循环遍历每个 EPAPER_TYPE 并编译
for %%t in (%EPD_SCREEN_TYPE%) do (
    @set "EPD_SCREEN_TYPE_TMP=%%t"
	if "!EPD_SCREEN_TYPE_TMP!"=="EPD_SCREEN_TYPE_A" (
        @set U_EPAPER_TYPE=AVD_SSD1683A_272X792C2_A_DRY_CELL
    ) else (
        @set U_EPAPER_TYPE=AVD_SSD1683A_272X792C2_T_DRY_CELL
    )    
	@set DEST_DIR_NAME=!U_FACTORY!_!U_EPAPER_TYPE_DIR!_V!VER!_!YYYY!!MM!!DD!
	@set DEST_FILE_NAME=!U_FACTORY!_!U_EPAPER_TYPE!_V!VER!_!YYYY!!MM!!DD!
	@set OUT_DIR=!CD!\OUTPUT
	@set DEST_DIR=!OUT_DIR!\!DEST_DIR_NAME!
	@set SOURCE=!CD!\obj\BackupUpgrade_OTA.hex
	@set DEST_FILE=!DEST_DIR!\!DEST_FILE_NAME!.hex
	@set USER_CFLAGS=-DVER=!VER! -D!FACTORY! -DINK_SCREEN_CUSTOMER=!CUSTOMER! -D!EPAPER_TYPE! -D!FLASH_MODE! -D!EPD_SCREEN_TYPE_TMP! -D!BATTERY! -DINK_DEVICE_CHIP=INK_CHIP_CH585
	
	@echo /************************************************************/
	@echo Build TDX 272x792 2color SSD1683A Project
	@echo VER:			!VER_TMP!
	@echo FACROTY:		!U_FACTORY!
	@echo CUSTOMER:		!U_CUSTOMER!
	@echo EPAPER_TYPE:		!U_EPAPER_TYPE!
	@echo EPD_SCREEN_TYPE_TMP:    !EPD_SCREEN_TYPE_TMP!
	@echo OUT_FILE:		!DEST_FILE!
	@echo /************************************************************/

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
	@make -C obj clean
)

endlocal
