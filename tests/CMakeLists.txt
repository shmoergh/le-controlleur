cmake_minimum_required(VERSION 3.16)

project(le_controlleur_settings_storage_tests CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(add_settings_storage_test target_name source_file)
	add_executable(
		${target_name}
		${source_file}
		${CMAKE_CURRENT_LIST_DIR}/../src/settings-storage.cpp
		${CMAKE_CURRENT_LIST_DIR}/mocks/storage-mock.cpp
	)
	target_include_directories(
		${target_name}
		PRIVATE
			${CMAKE_CURRENT_LIST_DIR}/../src
			${CMAKE_CURRENT_LIST_DIR}/mocks
	)
	target_compile_definitions(${target_name} PRIVATE LE_CONTROLLEUR_DEBUG_LOG=0)
	add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_settings_storage_test(
	settings_storage_defaults
	${CMAKE_CURRENT_LIST_DIR}/test_settings_storage_defaults.cpp
)
add_settings_storage_test(
	settings_storage_loads_v1
	${CMAKE_CURRENT_LIST_DIR}/test_settings_storage_loads_v1.cpp
)
add_settings_storage_test(
	settings_storage_loads_v2
	${CMAKE_CURRENT_LIST_DIR}/test_settings_storage_loads_v2.cpp
)
add_settings_storage_test(
	settings_storage_loads_v4
	${CMAKE_CURRENT_LIST_DIR}/test_settings_storage_loads_v4.cpp
)
add_settings_storage_test(
	settings_storage_deferred_commit
	${CMAKE_CURRENT_LIST_DIR}/test_settings_storage_deferred_commit.cpp
)
add_settings_storage_test(
	settings_storage_validation
	${CMAKE_CURRENT_LIST_DIR}/test_settings_storage_validation.cpp
)
