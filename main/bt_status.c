#include "bt_status.h"

#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bt_status_t s_status;

void bt_status_set(const bt_status_t* s) {
    taskENTER_CRITICAL(&s_lock);
    s_status = *s;
    taskEXIT_CRITICAL(&s_lock);
}

bt_status_t bt_status_get(void) {
    taskENTER_CRITICAL(&s_lock);
    bt_status_t s = s_status;
    taskEXIT_CRITICAL(&s_lock);
    return s;
}
