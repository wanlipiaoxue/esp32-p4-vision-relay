# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(basic_uvc_stream)

# Defer the pinned Hosted transport constructor until the optional network task.
idf_component_get_property(preview_hosted_lib espressif__esp_hosted COMPONENT_LIB)
get_target_property(preview_hosted_sources ${preview_hosted_lib} SOURCES)
list(LENGTH preview_hosted_sources preview_hosted_before)
list(FILTER preview_hosted_sources EXCLUDE REGEX "port_esp_hosted_host_init[.]c$")
list(LENGTH preview_hosted_sources preview_hosted_after)
if(NOT preview_hosted_after LESS preview_hosted_before)
    message(FATAL_ERROR "Pinned Hosted initialization source not found; review deferred initialization")
endif()
set_property(TARGET ${preview_hosted_lib} PROPERTY SOURCES "${preview_hosted_sources}")
