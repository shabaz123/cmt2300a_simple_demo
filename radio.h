#ifndef RADIO_H
#define RADIO_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RADIO_MODE_OOK = 0,
    RADIO_MODE_FSK = 1
} radio_mode_t;

bool radio_init(void);
bool radio_test_switch(bool tx);
bool radio_end_switch_test(void);
bool radio_apply_synth_freq(double f_mhz);
bool radio_set_freq(double mhz);
bool radio_set_level(double dbm);
bool radio_set_mode(radio_mode_t mode);
bool radio_set_tx(bool on);
double radio_get_freq(void);
double radio_get_level(void);
radio_mode_t radio_get_mode(void);
const char *radio_get_mode_str(void);
bool radio_get_tx(void);
bool radio_ready(void);
bool radio_switch_configured(void);
const char *radio_get_switch(void);
const char *radio_error(void);
void radio_dump_regs(void);

#ifdef __cplusplus
}
#endif
#endif
