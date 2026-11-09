set(UPSTREAM "${CMAKE_CURRENT_LIST_DIR}/apriltag-b7c0ebe9aa20f82ec7a828579004f9e706bfecd9")
file(GLOB COMMON_SOURCES "${UPSTREAM}/common/*.c")
idf_component_register(
    SRCS "${UPSTREAM}/apriltag.c" "${UPSTREAM}/apriltag_quad_thresh.c"
         "${UPSTREAM}/tag36h11.c" ${COMMON_SOURCES}
    INCLUDE_DIRS "${UPSTREAM}" REQUIRES pthread esp_timer)
