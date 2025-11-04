@echo off
echo ========================================
echo    Installing MQTT Broker on Windows
echo ========================================
echo.

echo Step 1: Downloading Mosquitto MQTT Broker...
echo Please download from: https://mosquitto.org/download/
echo.
echo Step 2: After installation, run these commands:
echo.
echo   mosquitto -v
echo.
echo Step 3: Test MQTT connection:
echo   mosquitto_pub -h localhost -t "test/topic" -m "Hello MQTT"
echo   mosquitto_sub -h localhost -t "test/topic"
echo.
echo Alternative: Use Docker (if you have Docker installed):
echo   docker run -it -p 1883:1883 -p 9001:9001 eclipse-mosquitto
echo.
echo ========================================
echo    Manual Installation Steps:
echo ========================================
echo.
echo 1. Go to: https://mosquitto.org/download/
echo 2. Download "mosquitto-2.0.18-install-windows-x64.exe"
echo 3. Run the installer as Administrator
echo 4. Install to default location (usually C:\Program Files\mosquitto)
echo 5. Add C:\Program Files\mosquitto to your PATH environment variable
echo 6. Open Command Prompt as Administrator and run:
echo    net start mosquitto
echo.
echo ========================================
echo    Quick Test (after installation):
echo ========================================
echo.
echo Open two Command Prompt windows:
echo.
echo Window 1 (Publisher):
echo   mosquitto_pub -h localhost -t "fire/command" -m "test message"
echo.
echo Window 2 (Subscriber):
echo   mosquitto_sub -h localhost -t "fire/command"
echo.
echo If you see the message in Window 2, MQTT is working!
echo.
pause
