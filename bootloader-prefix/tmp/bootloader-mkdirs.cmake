# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/ESP/v5.4.1/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "C:/ESP/v5.4.1/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader"
  "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader-prefix"
  "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader-prefix/tmp"
  "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader-prefix/src/bootloader-stamp"
  "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader-prefix/src"
  "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/_work/_Projects/electro_devices/weatherStation/ESP32_internet_gate/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
