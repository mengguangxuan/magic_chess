@echo off
setlocal
set "ESP_ROOT=C:\Users\mengguangxuan\AppData\Local\Arduino15\packages\esp32"
set "ESP_CORE=%ESP_ROOT%\hardware\esp32\3.3.10"
set "ESP_SDK=%ESP_ROOT%\tools\esp32s3-libs\3.3.10"
set "ESP_BIN=%ESP_ROOT%\tools\esp-x32\2601\bin"
set "BUILD_DIR=%TEMP%\recognization_esp32s3_build"
set "ENCODER_SRC=%USERPROFILE%\Documents\Arduino\libraries\ESP32Encoder\src"
set "BLE_SRC=%ESP_CORE%\libraries\BLE\src"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%ENCODER_SRC%\ESP32Encoder.h" set "ENCODER_SRC=%BUILD_DIR%\ESP32Encoder-5.0.0\src"
if not exist "%ENCODER_SRC%\ESP32Encoder.h" tar -xf "%LOCALAPPDATA%\Arduino15\staging\libraries\ESP32Encoder-5.0.0.zip" -C "%BUILD_DIR%"
if not exist "%ENCODER_SRC%\ESP32Encoder.h" (
  echo ESP32Encoder library not found
  exit /b 1
)

set "COMMON=-DF_CPU=240000000L -DARDUINO=10819 -DARDUINO_ESP32S3_DEV -DARDUINO_ARCH_ESP32 -DARDUINO_BOARD=\"ESP32S3_DEV\" -DARDUINO_VARIANT=\"esp32s3\" -DARDUINO_PARTITION_default -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0 @%ESP_SDK%\flags\defines -I%~dp0.. -I%~dp0..\src\tscp -I%ENCODER_SRC% -I%BLE_SRC% -I%ESP_CORE%\cores\esp32 -I%ESP_CORE%\variants\esp32s3 -iprefix %ESP_SDK%\include\ @%ESP_SDK%\flags\includes -I%ESP_SDK%\qio_qspi\include"
set "CXX=%ESP_BIN%\xtensa-esp32s3-elf-g++.exe"
set "CC=%ESP_BIN%\xtensa-esp32s3-elf-gcc.exe"

"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\receive.cpp" -o "%BUILD_DIR%\esp_receive.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\bluetooth_transport.cpp" -o "%BUILD_DIR%\esp_bluetooth_transport.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\board.cpp" -o "%BUILD_DIR%\esp_board.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\engine_adapter.cpp" -o "%BUILD_DIR%\esp_engine_adapter.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\move_quality.cpp" -o "%BUILD_DIR%\esp_move_quality.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\physical_board_adapter.cpp" -o "%BUILD_DIR%\esp_physical_board_adapter.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\chessBoard.cpp" -o "%BUILD_DIR%\esp_chess_board.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%ENCODER_SRC%\ESP32Encoder.cpp" -o "%BUILD_DIR%\esp_encoder.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%ENCODER_SRC%\InterruptEncoder.cpp" -o "%BUILD_DIR%\esp_interrupt_encoder.o"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -x c++ -c @"%ESP_SDK%\flags\cpp_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\recognization.ino" -o "%BUILD_DIR%\esp_sketch.o"
if errorlevel 1 exit /b %errorlevel%

"%CC%" -c @"%ESP_SDK%\flags\c_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\src\tscp\board.c" -o "%BUILD_DIR%\esp_tscp_board.o"
if errorlevel 1 exit /b %errorlevel%
"%CC%" -c @"%ESP_SDK%\flags\c_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\src\tscp\data.c" -o "%BUILD_DIR%\esp_tscp_data.o"
if errorlevel 1 exit /b %errorlevel%
"%CC%" -c @"%ESP_SDK%\flags\c_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\src\tscp\eval.c" -o "%BUILD_DIR%\esp_tscp_eval.o"
if errorlevel 1 exit /b %errorlevel%
"%CC%" -c @"%ESP_SDK%\flags\c_flags" -Wall -Os -Werror=return-type %COMMON% "%~dp0..\src\tscp\search.c" -o "%BUILD_DIR%\esp_tscp_search.o"
if errorlevel 1 exit /b %errorlevel%

echo ESP32-S3 source compilation passed
