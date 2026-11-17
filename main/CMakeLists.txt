idf_component_register(
    SRCS "main.c" "adapter_config.c" "pad_state.c" "bt_status.c" "xbox_bt.c"
         "usb_out.c" "ds3_report.c" "ds3_usb.c" "x360_report.c" "x360_usb.c"
         "web_config.c" "status_led.c"
    INCLUDE_DIRS "." "tusb"
    EMBED_TXTFILES "web/index.html"
    REQUIRES bluepad32 btstack esp_hw_support esp_timer nvs_flash esp_wifi esp_netif esp_event esp_http_server
             usb espressif__tinyusb xsm3)

# Give TinyUSB our tusb_config.h (main/tusb/).
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tusb")
