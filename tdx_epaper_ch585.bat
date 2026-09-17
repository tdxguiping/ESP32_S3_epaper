@echo off
:: 切换到UTF-8编码（适用于Windows 10及以上系统）
chcp 65001 >nul

echo 当前操作：准备进入boe epaper目录

set target_dir=F:\project\CH585\project_3\BLE\BackupUpgrade_OTA
cd /d %target_dir%

if %errorlevel% equ 0 (
    echo 当前所在目录: %cd%
) else (
    echo 进入目录失败，请检查路径是否正确
)

echo.
echo 脚本执行完毕，现在可以手动输入命令...
echo.

:: 启动新的命令提示符会话，保持窗口打开并允许交互
cmd /k
