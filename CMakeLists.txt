cmake_minimum_required(VERSION 3.21)

project(SimpleWindowsPosixSemaphore
	VERSION 2.0.0
	DESCRIPTION "Simple POSIX semaphore replacement for Windows"
	HOMEPAGE_URL "https://github.com/DanielTillett/Simple-Windows-Posix-Semaphore"
	LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SWPS_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(SWPS_BUILD_EXAMPLES "Build the example program" ${PROJECT_IS_TOP_LEVEL})

if(NOT WIN32)
	# Other platforms already have POSIX semaphores. Provide the same target name
	# so cross-platform projects can link it unconditionally.
	find_package(Threads REQUIRED)
	add_library(posix_semaphore INTERFACE)
	add_library(SimpleWindowsPosixSemaphore::semaphore ALIAS posix_semaphore)
	target_link_libraries(posix_semaphore INTERFACE Threads::Threads)
	return()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Installed headers live in a subdirectory so they never shadow another
# semaphore.h (for example MinGW-w64's winpthreads).
set(SWPS_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/SimpleWindowsPosixSemaphore")

add_library(posix_semaphore semaphore.c semaphore.h)
add_library(SimpleWindowsPosixSemaphore::semaphore ALIAS posix_semaphore)
target_include_directories(posix_semaphore PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<INSTALL_INTERFACE:${SWPS_INSTALL_INCLUDEDIR}>)
set_target_properties(posix_semaphore PROPERTIES
	EXPORT_NAME semaphore
	WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(PROJECT_IS_TOP_LEVEL)
	if(MSVC)
		set(SWPS_WARNINGS /W4)
	else()
		set(SWPS_WARNINGS -Wall -Wextra -Wpedantic)
	endif()
	target_compile_options(posix_semaphore PRIVATE ${SWPS_WARNINGS})
endif()

if(SWPS_BUILD_EXAMPLES)
	add_executable(semaphore_example examples/example.c)
	target_link_libraries(semaphore_example PRIVATE posix_semaphore)
endif()

if(SWPS_BUILD_TESTS)
	enable_testing()
	add_executable(test_semaphore tests/test_semaphore.c)
	target_link_libraries(test_semaphore PRIVATE posix_semaphore)
	target_compile_options(test_semaphore PRIVATE ${SWPS_WARNINGS})
	add_test(NAME semaphore COMMAND test_semaphore)
	set_tests_properties(semaphore PROPERTIES TIMEOUT 120)
endif()

install(TARGETS posix_semaphore
	EXPORT SimpleWindowsPosixSemaphoreTargets
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES semaphore.h DESTINATION ${SWPS_INSTALL_INCLUDEDIR})
install(EXPORT SimpleWindowsPosixSemaphoreTargets
	NAMESPACE SimpleWindowsPosixSemaphore::
	FILE SimpleWindowsPosixSemaphoreConfig.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SimpleWindowsPosixSemaphore)
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/SimpleWindowsPosixSemaphoreConfigVersion.cmake"
	COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SimpleWindowsPosixSemaphoreConfigVersion.cmake"
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SimpleWindowsPosixSemaphore)
