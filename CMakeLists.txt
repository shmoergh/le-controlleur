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

project(le_controlleur C CXX ASM)

# Reserve top-of-flash sectors for Brain SDK storage before pico_sdk_init().
include(brain-sdk/cmake/brain-storage-reserve-flash.cmake)
brain_storage_configure_flash_reservation()

option(LE_CONTROLLEUR_DEBUG_LOG "Enable debug logging" ON)
set(LE_CONTROLLEUR_DEBUG_LEVEL 1 CACHE STRING "Debug verbosity: 0=error,1=info,2=trace")
set(LE_CONTROLLEUR_VERSION "0.1.0-dev" CACHE STRING "Firmware version string")

execute_process(
	COMMAND git -C ${CMAKE_CURRENT_LIST_DIR} rev-parse --short HEAD
	OUTPUT_VARIABLE LE_CONTROLLEUR_GIT_HASH
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)

if(NOT LE_CONTROLLEUR_GIT_HASH)
	set(LE_CONTROLLEUR_GIT_HASH "unknown")
endif()

# Initialize the Pico SDK
pico_sdk_init()

# Add brain-sdk which includes brain libraries
add_subdirectory(brain-sdk)

add_executable(le_controlleur ${FILES})

target_link_libraries(le_controlleur
	pico_stdlib
	brain-common
	brain-utils
	brain-ui
	brain-io)

if(LE_CONTROLLEUR_DEBUG_LOG)
	target_compile_definitions(le_controlleur PRIVATE LE_CONTROLLEUR_DEBUG_LOG=1)
else()
	target_compile_definitions(le_controlleur PRIVATE LE_CONTROLLEUR_DEBUG_LOG=0)
endif()

target_compile_definitions(le_controlleur PRIVATE
	LE_CONTROLLEUR_DEBUG_LEVEL=${LE_CONTROLLEUR_DEBUG_LEVEL}
	LE_CONTROLLEUR_VERSION="${LE_CONTROLLEUR_VERSION}"
	LE_CONTROLLEUR_GIT_HASH="${LE_CONTROLLEUR_GIT_HASH}"
)

pico_enable_stdio_usb(le_controlleur 1)
pico_enable_stdio_uart(le_controlleur 1)
pico_add_extra_outputs(le_controlleur)
