file(GLOB FILES src/*.cpp src/*.h)

cmake_minimum_required(VERSION 3.22)

# Default board/platform. Can be overridden at configure time with:
#   -DPICO_BOARD=<board>
#   -DPICO_PLATFORM=<platform>
if(NOT DEFINED PICO_BOARD)
	set(PICO_BOARD pico2)
endif()

if(NOT DEFINED PICO_PLATFORM)
	set(PICO_PLATFORM rp2350-arm-s)
endif()

# Include Pico SDK (from brain-sdk subdirectory)
include(brain-sdk/pico_sdk_import.cmake)

project(brain-basic-midi2cv C CXX ASM)

# Initialize the Pico SDK
pico_sdk_init()

# Add brain-sdk which includes brain libraries
add_subdirectory(brain-sdk)

add_executable(brain-basic-midi2cv ${FILES})

target_link_libraries(brain-basic-midi2cv
	pico_stdlib
	brain-common
	brain-utils
	brain-ui
	brain-io)

pico_enable_stdio_usb(brain-basic-midi2cv 1)
pico_enable_stdio_uart(brain-basic-midi2cv 1)
pico_add_extra_outputs(brain-basic-midi2cv)
