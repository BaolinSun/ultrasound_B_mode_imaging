#include "fake_control_sdk.h"
#include <stdio.h>
#define ESP_LOGW(tag, ...) do { (void)(tag); printf(__VA_ARGS__); putchar('\n'); } while(0)
