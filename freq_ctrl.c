#include "freq_ctrl.h"
#include "radio.h"
#include <stdio.h>
#include <math.h>

/* Internal frequency state stored in int64_t (Hz) */
static int64_t     g_freq_hz    = 433920000LL;  /* Default: 433.920 MHz */
static int64_t     g_offset_hz  = 0LL;          /* Default: 0 Hz */
static int64_t     g_step_hz    = 25LL;         /* Default: 25 Hz */
static double      g_cal_ppm    = 0.0;          /* Default: 0.0 ppm */
static cal_state_t g_cal_state  = CAL_STATE_UNCAL;
static const char *g_error_text = "OK";

/* Frequency bands with 100 kHz safety margin inside hardware limits:
 * Band 0: 126.1 - 169.9 MHz (Nominal 127 - 169 MHz)
 * Band 1: 189.1 - 339.9 MHz (Nominal 190 - 339 MHz)
 * Band 2: 379.1 - 509.9 MHz (Nominal 380 - 509 MHz)
 * Band 3: 758.1 - 1019.9 MHz (Nominal 759 - 1019 MHz) */
typedef struct {
    int64_t min_hz;
    int64_t max_hz;
} freq_band_t;

static const freq_band_t BANDS[] = {
    { 126100000LL,  169900000LL },
    { 189100000LL,  339900000LL },
    { 379100000LL,  509900000LL },
    { 758100000LL, 1019900000LL }
};
#define NUM_BANDS (sizeof(BANDS) / sizeof(BANDS[0]))

static int find_band(int64_t hz)
{
    for (size_t i = 0; i < NUM_BANDS; i++) {
        if (hz >= BANDS[i].min_hz && hz <= BANDS[i].max_hz) {
            return (int)i;
        }
    }
    return -1;
}

static bool is_outfreq_valid(int64_t outfreq_hz)
{
    return find_band(outfreq_hz) >= 0;
}

int64_t freq_ctrl_get_freq_hz(void)    { return g_freq_hz; }
double  freq_ctrl_get_freq_mhz(void)   { return (double)g_freq_hz / 1e6; }

int64_t freq_ctrl_get_offset_hz(void)  { return g_offset_hz; }
double  freq_ctrl_get_offset_mhz(void) { return (double)g_offset_hz / 1e6; }

int64_t freq_ctrl_get_outfreq_hz(void) { return g_freq_hz + g_offset_hz; }
double  freq_ctrl_get_outfreq_mhz(void)
{
    return (double)(g_freq_hz + g_offset_hz) / 1e6;
}

int64_t freq_ctrl_get_step_hz(void)    { return g_step_hz; }
double  freq_ctrl_get_cal_ppm(void)    { return g_cal_ppm; }
cal_state_t freq_ctrl_get_cal_state(void) { return g_cal_state; }

const char* freq_ctrl_get_cal_state_str(void)
{
    return (g_cal_state == CAL_STATE_CAL) ? "CAL" : "UNCAL";
}

const char* freq_ctrl_error(void) { return g_error_text; }

int64_t freq_ctrl_get_synth_hz(void)
{
    int64_t outfreq = freq_ctrl_get_outfreq_hz();
    double corrected = (double)outfreq * (1.0 + g_cal_ppm * 1e-6);
    return (int64_t)llround(corrected);
}

double freq_ctrl_get_synth_mhz(void)
{
    return (double)freq_ctrl_get_synth_hz() / 1e6;
}

static bool update_hardware_synth(void)
{
    double synth_mhz = freq_ctrl_get_synth_mhz();
    if (!radio_apply_synth_freq(synth_mhz)) {
        g_error_text = radio_error();
        return false;
    }
    g_error_text = "OK";
    return true;
}

void freq_ctrl_init(void)
{
    g_freq_hz   = 433920000LL;
    g_offset_hz = 0LL;
    g_step_hz   = 25LL;
    g_cal_ppm   = 0.0;
    g_cal_state = CAL_STATE_UNCAL;
    g_error_text = "OK";
}

bool freq_ctrl_set_freq_hz(int64_t hz)
{
    int64_t new_outfreq = hz + g_offset_hz;
    if (!is_outfreq_valid(new_outfreq)) {
        g_error_text = "Effective output frequency out of range (supported bands: 127-169, 190-339, 380-509, 759-1019 MHz)";
        return false;
    }
    g_freq_hz = hz;
    return update_hardware_synth();
}

bool freq_ctrl_set_freq_mhz(double mhz)
{
    if (!isfinite(mhz)) {
        g_error_text = "Invalid frequency value";
        return false;
    }
    int64_t hz = (int64_t)llround(mhz * 1e6);
    return freq_ctrl_set_freq_hz(hz);
}

bool freq_ctrl_set_offset_hz(int64_t hz)
{
    int64_t new_outfreq = g_freq_hz + hz;
    if (!is_outfreq_valid(new_outfreq)) {
        g_error_text = "Offset causes output frequency to exceed supported bands";
        return false;
    }
    g_offset_hz = hz;
    return update_hardware_synth();
}

bool freq_ctrl_set_offset_mhz(double mhz)
{
    if (!isfinite(mhz)) {
        g_error_text = "Invalid offset value";
        return false;
    }
    int64_t hz = (int64_t)llround(mhz * 1e6);
    return freq_ctrl_set_offset_hz(hz);
}

bool freq_ctrl_set_step_hz(int64_t step_hz)
{
    if (step_hz <= 0LL || step_hz > 50000000LL) {
        g_error_text = "Step must be positive and <= 50 MHz";
        return false;
    }
    g_step_hz = step_hz;
    g_error_text = "OK";
    return true;
}

bool freq_ctrl_set_cal_ppm(double ppm)
{
    if (!isfinite(ppm) || ppm < -50.0 || ppm > 50.0) {
        g_error_text = "Calibration must be within +/-50 ppm";
        return false;
    }
    g_cal_ppm = ppm;
    g_cal_state = CAL_STATE_CAL;
    return update_hardware_synth();
}

bool freq_ctrl_set_measfreq_mhz(double meas_mhz)
{
    if (!isfinite(meas_mhz) || meas_mhz <= 0.0) {
        g_error_text = "Invalid measured frequency";
        return false;
    }
    int64_t meas_hz = (int64_t)llround(meas_mhz * 1e6);
    int64_t outfreq_hz = freq_ctrl_get_outfreq_hz();
    if (outfreq_hz <= 0) {
        g_error_text = "Target output frequency is invalid";
        return false;
    }

    /* Relative error of measured output versus target outfreq */
    double diff_hz = (double)(meas_hz - outfreq_hz);
    double ppm_err = (diff_hz / (double)outfreq_hz) * 1e6;
    double new_cal_ppm = g_cal_ppm - ppm_err;

    if (new_cal_ppm < -50.0 || new_cal_ppm > 50.0) {
        g_error_text = "Calculated calibration ignored (exceeds +/-50 ppm)";
        return false;
    }

    g_cal_ppm = new_cal_ppm;
    g_cal_state = CAL_STATE_CAL;
    return update_hardware_synth();
}

bool freq_ctrl_step(bool increment)
{
    int64_t current_outfreq = freq_ctrl_get_outfreq_hz();
    int band = find_band(current_outfreq);
    if (band < 0) {
        g_error_text = "Current frequency not in valid band";
        return false;
    }

    int64_t delta = increment ? g_step_hz : -g_step_hz;
    int64_t new_freq = g_freq_hz + delta;
    int64_t new_outfreq = new_freq + g_offset_hz;

    /* Enforce clamping within the current band (never jump across band boundaries) */
    if (new_outfreq < BANDS[band].min_hz || new_outfreq > BANDS[band].max_hz) {
        g_error_text = increment ? "Step exceeds upper band boundary" : "Step exceeds lower band boundary";
        return false;
    }

    g_freq_hz = new_freq;
    return update_hardware_synth();
}

void freq_ctrl_get_nv_config(freq_nv_config_t *cfg)
{
    if (!cfg) return;
    cfg->magic     = 0x434D5432; /* "CMT2" */
    cfg->version   = 1;
    cfg->freq_hz   = g_freq_hz;
    cfg->offset_hz = g_offset_hz;
    cfg->step_hz   = g_step_hz;
    cfg->cal_ppm   = g_cal_ppm;
    cfg->cal_state = (uint32_t)g_cal_state;
    cfg->crc32     = 0;
}

bool freq_ctrl_set_nv_config(const freq_nv_config_t *cfg)
{
    if (!cfg) return false;
    if (cfg->magic != 0x434D5432 || cfg->version != 1) {
        g_error_text = "Invalid NV configuration header";
        return false;
    }
    int64_t outfreq = cfg->freq_hz + cfg->offset_hz;
    if (!is_outfreq_valid(outfreq)) {
        g_error_text = "NV configuration frequency out of range";
        return false;
    }
    if (cfg->cal_ppm < -50.0 || cfg->cal_ppm > 50.0) {
        g_error_text = "NV configuration cal_ppm out of range";
        return false;
    }
    g_freq_hz   = cfg->freq_hz;
    g_offset_hz = cfg->offset_hz;
    g_step_hz   = cfg->step_hz;
    g_cal_ppm   = cfg->cal_ppm;
    g_cal_state = (cfg->cal_state == CAL_STATE_CAL) ? CAL_STATE_CAL : CAL_STATE_UNCAL;
    return update_hardware_synth();
}
