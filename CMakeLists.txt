cmake_minimum_required(VERSION 3.16)
set(EXCLUDE_COMPONENTS "mqtt")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(etensor)

spiffs_create_partition_image(storage spiffs_image FLASH_IN_PROJECT)