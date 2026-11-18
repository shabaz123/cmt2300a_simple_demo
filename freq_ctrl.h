#ifndef FREQ_CTRL_H
#define FREQ_CTRL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAL_STATE_UNCAL = 0,
    CAL_STATE_CAL   = 1
} cal_state_t;

/* NV storage structure ready for future Pico flash / EEPROM persistence */
typedef struct {
    uint32_t magic;         /* 0x434D5432 "CMT2" */
    uint32_t version;       /* 1 */
    int64_t  freq_hz;
    int64_t  offset_hz;
    int64_t  step_hz;
    double   cal_ppm;
    uint32_t cal_state;
    uint32_t crc32;
} freq_nv_config_t;

void        freq_ctrl_init(void);

/* Getters */
int64_t     freq_ctrl_get_freq_hz(void);
double      freq_ctrl_get_freq_mhz(void);
int64_t     freq_ctrl_get_offset_hz(void);
double      freq_ctrl_get_offset_mhz(void);
int64_t     freq_ctrl_get_outfreq_hz(void);
double      freq_ctrl_get_outfreq_mhz(void);
int64_t     freq_ctrl_get_step_hz(void);
double      freq_ctrl_get_cal_ppm(void);
cal_state_t freq_ctrl_get_cal_state(void);
const char* freq_ctrl_get_cal_state_str(void);
int64_t     freq_ctrl_get_synth_hz(void);
double      freq_ctrl_get_synth_mhz(void);
const char* freq_ctrl_error(void);

/* Setters */
bool freq_ctrl_set_freq_hz(int64_t hz);
bool freq_ctrl_set_freq_mhz(double mhz);

bool freq_ctrl_set_offset_hz(int64_t hz);
bool freq_ctrl_set_offset_mhz(double mhz);

bool freq_ctrl_set_step_hz(int64_t step_hz);

bool freq_ctrl_set_cal_ppm(double ppm);
bool freq_ctrl_set_measfreq_mhz(double meas_mhz);

/* Fine-tune step: true = increment (+step), false = decrement (-step) */
bool freq_ctrl_step(bool increment);

/* Flash / NV readiness */
void freq_ctrl_get_nv_config(freq_nv_config_t *cfg);
bool freq_ctrl_set_nv_config(const freq_nv_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* FREQ_CTRL_H */
