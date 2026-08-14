
@echo Build Start!!!!!!!!!!!!!!!!!!!!
@set GCC12_BIN=E:\E_paper_tool\MounRiver\MounRiver_Studio\toolchain\RISC-V Embedded GCC12\bin
@if exist "%GCC12_BIN%\riscv-none-elf-gcc.exe" @set PATH=%GCC12_BIN%;%PATH%
@if "%1"=="" (
	@set VER_TMP=100
) else (
	@set VER_TMP=%1
)
for %%i in ("%cd%\..\..") do set "PARENT_DIR=%%~fi"

@echo current dir:%cd%
@echo PARENT_DIR:%PARENT_DIR%

set "WORK_PATH=%PARENT_DIR%"
::#########DEFINE#########
@set VER=%VER_TMP%
@set FACTORY=ENABLE_SOFTWARE_TO_TDX
@set CUSTOMER=1
@set EPAPER_TYPE=ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4
@set FLASH_MODE=USB_EXTERN_FLASH_SAVE
@set USER_CFLAGS=-DVER=%VER% -D%FACTORY% -DINK_SCREEN_CUSTOMER=%CUSTOMER% -D%EPAPER_TYPE% -D%FLASH_MODE% -DINK_DEVICE_CHIP=INK_CHIP_CH585
::########################

::#########USER mkdir dir and file#########
@set U_FACTORY=TDX
@set U_CUSTOMER=TDX
@set U_EPAPER_TYPE=DKE_SSD_272X792C4
::########################

@set DEST_DIR_NAME=%U_FACTORY%_%U_EPAPER_TYPE%
@set OUT_DIR=%CD%\OUTPUT
@set DEST_DIR=%OUT_DIR%\%DEST_DIR_NAME%
@set SOURCE=%CD%\obj\BackupUpgrade_OTA.hex
@set DEST_FILE=%DEST_DIR%\%DEST_DIR_NAME%.hex

@echo /************************************************************/
@echo Build Boe 800x480 6color Project
@echo VER:			%VER_TMP%
@echo FACROTY:		%U_FACTORY%
@echo CUSTOMER:		%U_CUSTOMER%
@echo EPAPER_TYPE:		%U_EPAPER_TYPE%
@echo OUT_FILE:		%DEST_FILE%
@echo /************************************************************/

@make -C obj all
@if %ERRORLEVEL% neq 0 (
    @goto :error
)

@goto :success

:error
@echo Build Failure!!!!!!!!!!!!!!!!!!!!
@exit /b 1
:success
@echo Build Success, Need Copy File!!!!!!!!!!!!!!!!!!!!

@if not exist "%OUT_DIR%" (
    @md "%OUT_DIR%" 2>nul
    @if %ERRORLEVEL% neq 0 (
        @echo error: cannot mkdir "%OUT_DIR%"
        @exit /b 1
    )
    @echo create dir: "%OUT_DIR%"
)

:: 创建目标目录（如果不存在）
@if not exist "%DEST_DIR%" (
    @md "%DEST_DIR%" 2>nul
    @if %ERRORLEVEL% neq 0 (
        @echo error: cannot mkdir "%DEST_DIR%"
        @exit /b 1
    )
    @echo create dir: "%DEST_DIR%"
)

:: 拷贝文件并重命名
@copy /y "%SOURCE%" "%DEST_FILE%" >nul

@if %ERRORLEVEL% equ 0 (
@		 echo Build Complete!!!!!!!!!!!!!!!!!!!!
@    exit /b 0
) else ( 
@    echo failure: copy failure	
@    exit /b 1
)
