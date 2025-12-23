@echo off
setlocal

:: SSH Key Setup for LSB Launcher
:: This script sets up "password-less" access by deploying an SSH Key to the VPS.
:: You will need to enter your VPS password ONE LAST TIME.

set "VPS_IP="
set "VPS_USER="

echo ============================================================
echo       LSB Launcher - SSH Key Setup
echo ============================================================
echo.
echo This script will generate an SSH Key (if none exists) and
echo upload it to your VPS (%VPS_IP%).
echo.
echo [IMPORTANT] You will be asked for the VPS password one last time.
echo After this, you won't need to type it for uploads.
echo.
pause

:: 1. Check/Generate Key
if not exist "%USERPROFILE%\.ssh\id_rsa" (
    echo [KEY] Generating new SSH Key...
    if not exist "%USERPROFILE%\.ssh" mkdir "%USERPROFILE%\.ssh"
    ssh-keygen -t rsa -b 4096 -f "%USERPROFILE%\.ssh\id_rsa" -N ""
    echo [OK] Key generated.
) else (
    echo [KEY] Existing SSH Key found.
)

:: 2. Get Public Key Content
set "PUB_KEY_FILE=%USERPROFILE%\.ssh\id_rsa.pub"
set /p PUB_KEY=<"%PUB_KEY_FILE%"

:: 3. Deploy to VPS
echo.
echo [DEPLOY] Connecting to VPS to install key...
echo Please enter the VPS password below:
echo.

ssh -o StrictHostKeyChecking=no %VPS_USER%@%VPS_IP% "mkdir -p ~/.ssh && chmod 700 ~/.ssh && echo %PUB_KEY% >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] SSH Key installed!
    echo You can now use upload-release.bat without a password.
) else (
    echo.
    echo [ERROR] Failed to install key. please try again.
)

pause
