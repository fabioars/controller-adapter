# Wraps libxsm3 (fetched into third_party/ by scripts/setup.sh) as an ESP-IDF component.
# libxsm3 is LGPL-2.1: it stays a separate library with its own license.
set(XSM3_DIR ${CMAKE_CURRENT_LIST_DIR}/../../third_party/libxsm3)
if(NOT EXISTS ${XSM3_DIR}/xsm3.c)
    message(FATAL_ERROR "libxsm3 not found. Run ./scripts/setup.sh first.")
endif()

idf_component_register(
    SRCS ${XSM3_DIR}/xsm3.c ${XSM3_DIR}/usbdsec.c ${XSM3_DIR}/excrypt_des.c
         ${XSM3_DIR}/excrypt_parve.c ${XSM3_DIR}/excrypt_sha.c
    INCLUDE_DIRS ${XSM3_DIR})
target_compile_definitions(${COMPONENT_LIB} PRIVATE XSM3_NO_DEBUGGING)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-value -Wno-parentheses)
