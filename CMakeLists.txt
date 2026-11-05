cmake_minimum_required(VERSION 3.28)
project(ZolikaPatch LANGUAGES CXX)

# A Win32 ASI: 32-bit, MSVC, static CRT, fixed image base, windowed subsystem.
if(NOT MSVC)
  message(FATAL_ERROR "The ASI is built with MSVC; configure with a Visual Studio generator.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The ASI is 32-bit (PE32); configure with -A Win32.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release, Debug, and Trace: the shipped binary (silent, like the original) and
# the same binary with the parity trace switched on. Trace compiles like Release.
if(CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Trace" CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_TRACE "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_TRACE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_TRACE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

set(ZP_NAME "ZolikaPatch")
set(ZP_VERSION_MAJOR 7)
set(ZP_VERSION_MINOR 65)
set(ZP_VERSION_BUILD 0)
set(ZP_VERSION_REVISION 0)

find_package(Git QUIET)
set(ZP_GIT_SHA "")
if(Git_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE ZP_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
set(ZP_PRODUCT_VERSION "${ZP_VERSION_MAJOR}.${ZP_VERSION_MINOR}")
if(ZP_GIT_SHA)
  string(APPEND ZP_PRODUCT_VERSION "-${ZP_GIT_SHA}")
endif()

set(ZP_VERSION_DEFINITIONS
  "rsc_CompanyName=\"${ZP_NAME}\""
  "rsc_FileDescription=\"${ZP_NAME}\""
  "rsc_InternalName=\"${ZP_NAME}\""
  "rsc_OriginalFilename=\"${ZP_NAME}.asi\""
  "rsc_ProductName=\"${ZP_NAME}\""
  "rsc_LegalCopyright=\"GPL-3.0\""
  "rsc_FileVersion_MAJOR=${ZP_VERSION_MAJOR}"
  "rsc_FileVersion_MINOR=${ZP_VERSION_MINOR}"
  "rsc_FileVersion_BUILD=${ZP_VERSION_BUILD}"
  "rsc_FileVersion_REVISION=${ZP_VERSION_REVISION}"
  "rsc_FileVersion=\"${ZP_VERSION_MAJOR}.${ZP_VERSION_MINOR}.${ZP_VERSION_BUILD}\""
  "rsc_ProductVersion=\"${ZP_PRODUCT_VERSION}\""
  "rsc_GitSHA1=\"${ZP_GIT_SHA}\"")

add_library(zp_modules STATIC
  external/hooking/Hooking.Patterns.cpp)
set_target_properties(zp_modules PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_EXTENSIONS OFF)
target_sources(zp_modules PUBLIC
  FILE_SET CXX_MODULES FILES
    source/common.ixx
    source/bytes.ixx
    source/pattern.ixx
    source/bodies.ixx
    source/patch.ixx
    source/config.ixx
    source/game.ixx
    source/options.ixx
    source/loader.ixx
    source/stability.ixx
    source/vehicles.ixx
    source/useless_checks.ixx
    source/graphics.ixx)
target_include_directories(zp_modules PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/external/injector/include
  ${CMAKE_CURRENT_SOURCE_DIR}/external/hooking)
target_compile_features(zp_modules PUBLIC cxx_std_23)
target_compile_definitions(zp_modules PUBLIC
  UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS
  ${ZP_VERSION_DEFINITIONS})
target_compile_options(zp_modules PUBLIC /utf-8 /Zc:__cplusplus /W3)
target_link_libraries(zp_modules PUBLIC version shlwapi)

add_library(${ZP_NAME} SHARED
  source/dllmain.cpp
  source/resources/version.rc)
set_target_properties(${ZP_NAME} PROPERTIES
  CXX_SCAN_FOR_MODULES ON
  PREFIX ""
  SUFFIX ".asi")
target_link_libraries(${ZP_NAME} PRIVATE zp_modules)
# A linker map is a development aid: the transcribed bodies are proved against
# the original ASI at the addresses it records, so the map is what gives a body
# its candidate VA. It is not shipped and changes no byte of the image.
option(ZP_LINK_MAP "Emit a linker map for the ASI" ON)
target_link_options(${ZP_NAME} PRIVATE
  /MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO)
if(ZP_LINK_MAP)
  target_link_options(${ZP_NAME} PRIVATE /MAP)
endif()
# The trace build writes the diagnostic log the parity tools read; the shipped
# ASI is silent, exactly like the original.
option(ZP_TRACE "Write ZolikaPatch.trace.log" OFF)
if(ZP_TRACE)
  target_compile_definitions(${ZP_NAME} PRIVATE ZP_TRACE)
endif()
target_compile_definitions(${ZP_NAME} PRIVATE $<$<CONFIG:Trace>:ZP_TRACE>)

enable_testing()

add_executable(zp_tests tests/test_main.cpp)
set_target_properties(zp_tests PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_tests PRIVATE zp_modules)
add_dependencies(zp_tests ${ZP_NAME})
add_test(NAME zp_tests COMMAND zp_tests $<TARGET_FILE:${ZP_NAME}>)
set_tests_properties(zp_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(zp_dump tests/dump_main.cpp)
set_target_properties(zp_dump PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_dump PRIVATE zp_modules)
add_dependencies(zp_dump ${ZP_NAME})

# The empty baseline: with no arm installed the pipeline must change nothing on
# every supported image. Every later arm is diffed against that.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV.exe)
  add_test(NAME zp_dump_empty
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_empty_dump.py
            $<TARGET_FILE:zp_dump> ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs)
endif()

add_executable(zp_pe_check tests/pe_check.cpp)
add_dependencies(zp_pe_check ${ZP_NAME})
add_test(NAME zp_pe_check COMMAND zp_pe_check $<TARGET_FILE:${ZP_NAME}>)
if(Python3_Interpreter_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off)
  add_test(NAME zp_body_proof_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_bodies.py
            --selftest --original ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off)
  # The transcribed bodies, one manifest row each: instruction-for-instruction
  # against the original ASI, with absolute operands compared as slots.
  add_test(NAME zp_body_proof
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_bodies.py
            --original ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off
            --candidate $<TARGET_FILE:${ZP_NAME}>
            --bodies ${CMAKE_CURRENT_SOURCE_DIR}/tests/bodies.tsv)
  set_tests_properties(zp_body_proof PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
