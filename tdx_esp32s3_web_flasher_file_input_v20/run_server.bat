@echo off
set "APP_DIR=%~dp0"
for %%I in ("%APP_DIR:~0,-1%") do set "APP_NAME=%%~nxI"
cd /d "%APP_DIR%.."
echo Open this URL in Chrome or Edge:
echo http://localhost:8000/%APP_NAME%/index.html
python -m http.server 8000
pause
