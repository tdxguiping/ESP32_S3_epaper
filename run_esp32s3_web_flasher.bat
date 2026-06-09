@echo off
setlocal

REM ============================================================
REM TDX ESP32-S3 Web Flasher launcher - final version v20
REM Put this BAT file in the parent directory of:
REM   bootloader.bin
REM   partition-table.bin
REM   ota_data_initial.bin
REM   file_server.bin
REM   tdx_esp32s3_web_flasher_file_input_v20\
REM ============================================================

cd /d "%~dp0"

set "WEB_DIR=tdx_esp32s3_web_flasher_file_input_v20"
set "URL=http://localhost:8000/%WEB_DIR%/index.html"

if not exist "%WEB_DIR%\index.html" (
    echo [ERROR] Cannot find %WEB_DIR%\index.html
    echo.
    echo Please put this BAT file in the parent directory, for example:
    echo.
    echo   fuse_flash\
    echo   ^|-- run_esp32s3_web_flasher.bat
    echo   ^|-- bootloader.bin
    echo   ^|-- partition-table.bin
    echo   ^|-- ota_data_initial.bin
    echo   ^|-- file_server.bin
    echo   ^|-- %WEB_DIR%\
    echo       ^|-- index.html
    echo.
    pause
    exit /b 1
)

for %%F in (bootloader.bin partition-table.bin ota_data_initial.bin file_server.bin) do (
    if not exist "%%F" (
        echo [WARN] %%F not found in %CD%
        echo       The web page can still run, but automatic loading may fail.
    )
)

echo ============================================================
echo TDX ESP32-S3 Web Flasher - final version v20
echo ============================================================
echo.
echo Current directory: %CD%
echo Web page: %URL%
echo.
echo Do not close this window while flashing.
echo Press Ctrl+C in this window to stop the HTTP server.
echo.

python --version >nul 2>&1
if errorlevel 1 (
    py --version >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Python was not found.
        echo Please install Python or add it to PATH.
        pause
        exit /b 1
    ) else (
        set "PYTHON_CMD=py"
    )
) else (
    set "PYTHON_CMD=python"
)

start "" "%URL%"

%PYTHON_CMD% -m http.server 8000

endlocal
