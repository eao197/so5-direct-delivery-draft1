cmake_minimum_required(VERSION 3.14)

project(so5extra_master LANGUAGES CXX)

option(DEMO_SANITIZE "Sanitizer to use (thread|address) [default: OFF]" OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_CXX_STANDARD)
	message(STATUS "C++20 will be used by default")
	set(CMAKE_CXX_STANDARD 20)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CXX_EXTENSIONS NO)
else()
	message(STATUS "C++ standard is set to C++${CMAKE_CXX_STANDARD}")
endif()

set(DEMO_SUPPORTED_SANITIZERS "OFF" "thread" "address")
if(NOT (DEMO_SANITIZE IN_LIST DEMO_SUPPORTED_SANITIZERS))
	message(FATAL_ERROR "Unsupported sanitizer: ${DEMO_SANITIZE}")
endif()
if(NOT (DEMO_SANITIZE STREQUAL "OFF"))
	message(STATUS "Sanitizer to be used: ${DEMO_SANITIZE}")
endif()

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

set(SOBJECTIZER_BUILD_SHARED OFF CACHE INTERNAL "")
set(SOBJECTIZER_BUILD_STATIC ON CACHE INTERNAL "")
set(SOBJECTIZER_INSTALL ON CACHE INTERNAL "")
set(BUILD_ALL OFF CACHE INTERNAL "")
set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(BUILD_TESTS OFF CACHE INTERNAL "")
set(SOBJECTIZER_SANITIZE ${DEMO_SANITIZE} CACHE INTERNAL "")

FetchContent_Declare(sobjectizer
	GIT_REPOSITORY https://github.com/stiffstream/sobjectizer
	GIT_TAG v5.8.6
	SOURCE_SUBDIR dev
	OVERRIDE_FIND_PACKAGE
	LOG_DOWNLOAD TRUE
	LOG_CONFIGURE TRUE
	LOG_BUILD TRUE
	LOG_INSTALL TRUE
)

FetchContent_MakeAvailable(sobjectizer)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	add_compile_options(
		-Wall
		-Wextra
		-Wpedantic
		-Wno-c++98-compat
		-Wno-c++98-compat-pedantic
		-Wno-padded
		-Wno-missing-noreturn
		-Wno-documentation-unknown-command
		-Wno-documentation-deprecated-sync
		-Wno-documentation
		-Wno-weak-vtables
		-Wno-missing-prototypes
		-Wno-missing-variable-declarations
		-Wno-exit-time-destructors
		-Wno-global-constructors
	)
	if(DEMO_SANITIZE STREQUAL "thread")
		add_compile_options(-fsanitize=thread)
		add_link_options(-fsanitize=thread)
	elseif(DEMO_SANITIZE STREQUAL "address")
		add_compile_options(-fsanitize=address)
		add_link_options(-fsanitize=address)
	endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	add_compile_options(
		-Wextra
		-Wpedantic
		-Wall
		-Wno-unused-function
	)
	if(DEMO_SANITIZE STREQUAL "thread")
		add_compile_options(-fsanitize=thread)
		add_link_options(-fsanitize=thread)
	elseif(DEMO_SANITIZE STREQUAL "address")
		add_compile_options(-fsanitize=address)
		add_link_options(-fsanitize=address)
	endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	add_compile_options(
		-W3
	)
	add_compile_definitions(
		_WIN32_WINNT=0x0A00
	)
endif()

add_subdirectory(agent_ring)

