cmake_minimum_required(VERSION 3.16)

# Detect target chip to select the correct pre-compiled SDK path
if(CONFIG_IDF_TARGET_ESP32C3)
    set(CHIP_DIR "esp32c3")
elseif(CONFIG_IDF_TARGET_ESP32S2)
    set(CHIP_DIR "esp32s2")
elseif(CONFIG_IDF_TARGET_ESP32S3)
    set(CHIP_DIR "esp32s3")
else()
    set(CHIP_DIR "esp32")
endif()

# Convert Windows backslashes to forward slashes to prevent CMake escape errors
file(TO_CMAKE_PATH "$ENV{USERPROFILE}" USER_PROFILE_CMAKE)

# Path to the pre-compiled Arduino SDK for the target chip
set(ARDUINO_SDK_ROOT "${USER_PROFILE_CMAKE}/.platformio/packages/framework-arduinoespressif32/tools/sdk/${CHIP_DIR}")

# Register as an IDF component, providing the include path for esp_littlefs.h
idf_component_register(
    SRCS "esp_littlefs_proxy.c"
    INCLUDE_DIRS "${ARDUINO_SDK_ROOT}/include/esp_littlefs/include"
)

# Link against the pre-compiled esp_littlefs library
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-L${ARDUINO_SDK_ROOT}/lib"
)
target_link_libraries(${COMPONENT_LIB} PUBLIC esp_littlefs)
