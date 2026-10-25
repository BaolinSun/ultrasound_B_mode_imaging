#ifndef SYSTEM_CTRL_TEMP_H
#define SYSTEM_CTRL_TEMP_H
#include <stdint.h>
#define POWER_TEMP_ADDR 0x48U
#define AFE_TEMP_ADDR 0x49U
#define TEMP_DATA_REGISTER 0x00U
#define CONF_REGISTER 0x01U
#define TH_REGISTER 0x02U
#define TL_REGISTER 0x03U
int temp_read_checked(uint32_t address,int32_t *millidegrees);
float read_temp(uint32_t address); /* NAN on failure, not a fabricated 0 C. */
uint32_t temp_init(void);
void temperature_service_init(void);
void temperature_service_poll(void); /* Foreground only; never call in ISR. */
uint32_t temperature_service_remaining_ms(void); /* zero if due/unavailable */
uint32_t temperature_service_sequence(void); /* Read-only wake/report generation. */
#endif
