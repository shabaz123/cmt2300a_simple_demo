#include "radio.h"
#include "freq_ctrl.h"
#include "cmt2300a.h"
#include "cmt2300a_params.h"
#include "cmt2300a_defs.h"
#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"

/* Operating state */
static double freq_mhz = 433.920;
static double level_dbm = 20.0;
static radio_mode_t current_mode = RADIO_MODE_FSK;
static bool ready = false;
static bool tx_on = false;
static bool switch_configured = false;
static const char *error_text = "Not initialised";

const char *radio_error(void) { return error_text; }
bool radio_ready(void) { return ready; }
double radio_get_freq(void) { return freq_ctrl_get_freq_mhz(); }
double radio_get_level(void) { return level_dbm; }
radio_mode_t radio_get_mode(void) { return current_mode; }
const char *radio_get_mode_str(void)
{
    return (current_mode == RADIO_MODE_FSK) ? "FSK (Continuous Wave)" : "OOK (Direct Carrier)";
}
bool radio_get_tx(void) { return tx_on; }
bool radio_switch_configured(void) { return switch_configured; }

/* Verify only the two RF-switch selection bits; other bits serve unrelated functions. */
static bool verify_switch_mode(void)
{
    const uint8_t mask = CMT2300A_MASK_RF_SWT1_EN | CMT2300A_MASK_RF_SWT2_EN;
    switch_configured = (CMT2300A_ReadReg(CMT2300A_CUS_INT1_CTL) & mask)
                        == CMT2300A_MASK_RF_SWT1_EN;
    if (!switch_configured) error_text = "RF switch configuration failed";
    return switch_configured;
}

const char *radio_get_switch(void)
{
    if (!ready) {
        error_text = "Radio not initialised";
        return NULL;
    }
    if (!verify_switch_mode()) return NULL;
    switch (CMT2300A_GetChipStatus()) {
        case CMT2300A_STA_TX: return "TX";
        case CMT2300A_STA_RX: return "RX";
        default: return "IDLE";
    }
}

/* Frequency band validation across CMT2300A synthesizer coverage:
 * Band 5: 126.0 - 170.0 MHz (DIVIDER = 12)
 * Band 4 & 3: 189.0 - 340.0 MHz (DIVIDER = 8 and 6)
 * Band 2: 379.0 - 510.0 MHz (DIVIDER = 4)
 * Band 1: 758.0 - 1020.0 MHz (DIVIDER = 2) */
static bool valid_freq(double f)
{
    return isfinite(f) && (
        (f >= 126.0 && f <= 170.0) ||
        (f >= 189.0 && f <= 340.0) ||
        (f >= 379.0 && f <= 510.0) ||
        (f >= 758.0 && f <= 1020.0)
    );
}

/* Synthesizer PLL register calculation per AN199 */
static bool apply_freq(double f_mhz)
{
    uint32_t divider;
    uint8_t divx_code;

    if (f_mhz >= 758.0 && f_mhz <= 1020.0) {
        divider = 2;
        divx_code = 0; /* 0b000 */
    } else if (f_mhz >= 379.0 && f_mhz <= 510.0) {
        divider = 4;
        divx_code = 1; /* 0b001 */
    } else if (f_mhz >= 252.0 && f_mhz <= 340.0) {
        divider = 6;
        divx_code = 5; /* 0b101 */
    } else if (f_mhz >= 189.0 && f_mhz < 252.0) {
        divider = 8;
        divx_code = 2; /* 0b010 */
    } else if (f_mhz >= 126.0 && f_mhz <= 170.0) {
        divider = 12;
        divx_code = 3; /* 0b011 */
    } else {
        error_text = "Frequency must be in: 126-170, 189-340, 379-510, 758-1020 MHz";
        return false;
    }

    double f_vco = f_mhz * (double)divider;
    uint8_t vco_bank = (f_vco < 1680.0) ? 6 : 1; /* 0b110 or 0b001 */
    uint8_t paldo_sel = (f_mhz >= 500.0) ? 0x80 : 0x00;

    double nk = (f_mhz * (double)divider) / 26.0;
    uint32_t n = (uint32_t)floor(nk);
    double frac = nk - (double)n;
    uint32_t k = (uint32_t)round(frac * 1048576.0); /* 2^20 = 1048576 */
    if (k >= 1048576) {
        n += 1;
        k -= 1048576;
    }

    /* Program RX Frequency registers 0x18..0x1B */
    CMT2300A_WriteReg(CMT2300A_CUS_RF1, (uint8_t)(n & 0xFF));
    CMT2300A_WriteReg(CMT2300A_CUS_RF2, (uint8_t)(k & 0xFF));
    CMT2300A_WriteReg(CMT2300A_CUS_RF3, (uint8_t)((k >> 8) & 0xFF));
    uint8_t rf4 = paldo_sel | (uint8_t)(divx_code << 4) | (uint8_t)((k >> 16) & 0x0F);
    CMT2300A_WriteReg(CMT2300A_CUS_RF4, rf4);

    /* Program TX Frequency registers 0x1C..0x1F */
    CMT2300A_WriteReg(CMT2300A_CUS_RF5, (uint8_t)(n & 0xFF));
    CMT2300A_WriteReg(CMT2300A_CUS_RF6, (uint8_t)(k & 0xFF));
    CMT2300A_WriteReg(CMT2300A_CUS_RF7, (uint8_t)((k >> 8) & 0xFF));
    uint8_t old_rf8 = CMT2300A_ReadReg(CMT2300A_CUS_RF8);
    uint8_t rf8 = (uint8_t)(old_rf8 & 0x80) | (uint8_t)(vco_bank << 4) | (uint8_t)((k >> 16) & 0x0F);
    CMT2300A_WriteReg(CMT2300A_CUS_RF8, rf8);

    freq_mhz = f_mhz;
    return true;
}

/* Configure PA power output according to AN183 and datasheet tables */
static void apply_power(double dbm)
{
    uint8_t cmt4 = CMT2300A_ReadReg(CMT2300A_CUS_CMT4);
    if (dbm >= 17.0) {
        /* +20 dBm: pan_sel=1 in 0x03, high driver bias, max IDAC */
        cmt4 |= CMT2300A_MASK_PAN_SEL;
        CMT2300A_WriteReg(CMT2300A_CUS_CMT4, cmt4);
        CMT2300A_WriteReg(CMT2300A_CUS_TX5, 0x42);
        CMT2300A_WriteReg(CMT2300A_CUS_TX8, 0x7F); /* POWER_CODE = 127 */
        CMT2300A_WriteReg(CMT2300A_CUS_TX9, 0x0A); /* PCODE_ADJ = 10 */
        CMT2300A_WriteReg(CMT2300A_CUS_TX10, 0x3F); /* IDAC_CODE = 63 (max) */
        level_dbm = 20.0;
    } else if (dbm >= 0.0) {
        /* +13 dBm: pan_sel=0 in 0x03, reference +13 dBm profile */
        cmt4 &= ~CMT2300A_MASK_PAN_SEL;
        CMT2300A_WriteReg(CMT2300A_CUS_CMT4, cmt4);
        CMT2300A_WriteReg(CMT2300A_CUS_TX5, 0x42);
        CMT2300A_WriteReg(CMT2300A_CUS_TX8, 0x37); /* POWER_CODE = 55 */
        CMT2300A_WriteReg(CMT2300A_CUS_TX9, 0x0A);
        CMT2300A_WriteReg(CMT2300A_CUS_TX10, 0x3F);
        level_dbm = 13.0;
    } else {
        /* -10 dBm: pan_sel=0 in 0x03, RFPDK -10 dBm profile */
        cmt4 &= ~CMT2300A_MASK_PAN_SEL;
        CMT2300A_WriteReg(CMT2300A_CUS_CMT4, cmt4);
        CMT2300A_WriteReg(CMT2300A_CUS_TX5, 0x2E);
        CMT2300A_WriteReg(CMT2300A_CUS_TX8, 0x0D); /* POWER_CODE = 13 */
        CMT2300A_WriteReg(CMT2300A_CUS_TX9, 0x01);
        CMT2300A_WriteReg(CMT2300A_CUS_TX10, 0x16);
        level_dbm = -10.0;
    }
}

/* Configure modulation mode (FSK or OOK) */
static void apply_modulation(radio_mode_t mode)
{
    uint8_t tx1 = CMT2300A_ReadReg(CMT2300A_CUS_TX1);
    /* Crucial: Bit 2 of CUS_TX1 (0x55) MUST be 1 for direct GPIO DIN */
    tx1 |= CMT2300A_MASK_TX_DIN_SOURCE;

    if (mode == RADIO_MODE_FSK) {
        /* FSK: Bit 0 = 0 */
        tx1 &= ~CMT2300A_MASK_TX_MOD_TYPE;
        CMT2300A_WriteReg(CMT2300A_CUS_TX1, tx1);
        CMT2300A_WriteReg(CMT2300A_CUS_TX2, 0x26);
        CMT2300A_WriteReg(CMT2300A_CUS_TX3, 0x03);
        CMT2300A_ConfigRegBank(CMT2300A_DATA_RATE_BANK_ADDR, g_cmt2300aDataRateBank_FSK, CMT2300A_DATA_RATE_BANK_SIZE);
        current_mode = RADIO_MODE_FSK;
    } else {
        /* OOK: Bit 0 = 1 */
        tx1 |= CMT2300A_MASK_TX_MOD_TYPE;
        CMT2300A_WriteReg(CMT2300A_CUS_TX1, tx1);
        CMT2300A_WriteReg(CMT2300A_CUS_TX2, 0x9A);
        CMT2300A_WriteReg(CMT2300A_CUS_TX3, 0x0C);
        CMT2300A_ConfigRegBank(CMT2300A_DATA_RATE_BANK_ADDR, g_cmt2300aDataRateBank, CMT2300A_DATA_RATE_BANK_SIZE);
        current_mode = RADIO_MODE_OOK;
    }
}

bool radio_apply_synth_freq(double f_mhz)
{
    if (!ready) {
        return apply_freq(f_mhz);
    }

    bool resume_tx = tx_on;
    if (resume_tx) {
        /* Safely step to standby before updating PLL registers */
        gpio_put(IO3_GPIO, 0);
        CMT2300A_GoStby();
    }

    if (!apply_freq(f_mhz)) {
        if (resume_tx) radio_set_tx(true);
        return false;
    }

    if (resume_tx) {
        /* Re-enter TX on new frequency with automated PLL re-calibration */
        if (!radio_set_tx(true)) {
            return false;
        }
    } else {
        CMT2300A_GoStby();
    }

    return true;
}

bool radio_set_freq(double mhz)
{
    return freq_ctrl_set_freq_mhz(mhz);
}

bool radio_set_level(double dbm)
{
    if (!isfinite(dbm) || dbm < -20.0 || dbm > 20.0) {
        error_text = "Requested level outside -20 to +20 dBm";
        return false;
    }

    bool resume_tx = tx_on;
    if (resume_tx) {
        gpio_put(IO3_GPIO, 0);
        CMT2300A_GoStby();
    }

    if (fabs(dbm - 20.0) < 1.0) {
        apply_power(20.0);
    } else if (fabs(dbm - 13.0) < 1.0) {
        apply_power(13.0);
    } else if (fabs(dbm + 10.0) < 1.0) {
        apply_power(-10.0);
    } else {
        error_text = "Supported levels are 20 (or 20.0), 13 (or 13.0), -10 (or -10.0) dBm";
        if (resume_tx) radio_set_tx(true);
        return false;
    }

    if (resume_tx) {
        if (!radio_set_tx(true)) return false;
    } else {
        CMT2300A_GoStby();
    }

    return true;
}

bool radio_set_mode(radio_mode_t mode)
{
    bool resume_tx = tx_on;
    if (resume_tx) {
        gpio_put(IO3_GPIO, 0);
        CMT2300A_GoStby();
    }

    apply_modulation(mode);

    if (resume_tx) {
        if (!radio_set_tx(true)) return false;
    } else {
        CMT2300A_GoStby();
    }

    return true;
}

bool radio_init(void)
{
    gpio_put(IO3_GPIO, 0);
    ready = false;
    tx_on = false;
    switch_configured = false;

    CMT2300A_Init();
    if (!CMT2300A_IsExist()) {
        error_text = "CMT2300A SPI identification failed";
        return false;
    }

    CMT2300A_ConfigRegBank(CMT2300A_CMT_BANK_ADDR, g_cmt2300aCmtBank, CMT2300A_CMT_BANK_SIZE);
    CMT2300A_ConfigRegBank(CMT2300A_SYSTEM_BANK_ADDR, g_cmt2300aSystemBank, CMT2300A_SYSTEM_BANK_SIZE);
    CMT2300A_ConfigRegBank(CMT2300A_FREQUENCY_BANK_ADDR, g_cmt2300aFrequencyBank, CMT2300A_FREQUENCY_BANK_SIZE);
    CMT2300A_ConfigRegBank(CMT2300A_DATA_RATE_BANK_ADDR, g_cmt2300aDataRateBank, CMT2300A_DATA_RATE_BANK_SIZE);
    CMT2300A_ConfigRegBank(CMT2300A_BASEBAND_BANK_ADDR, g_cmt2300aBasebandBank, CMT2300A_BASEBAND_BANK_SIZE);
    CMT2300A_ConfigRegBank(CMT2300A_TX_BANK_ADDR, g_cmt2300aTxBank, CMT2300A_TX_BANK_SIZE);

    if (!CMT2300A_GoStby()) {
        error_text = "Radio failed to enter standby";
        return false;
    }

    /* RF_SWT1: GPIO1 = RX_ACTIVE, GPIO2 = TX_ACTIVE.
       AS179 SPDT Switch on board:
       GPIO1=0/GPIO2=1 routes PA to Antenna.
       GPIO1=1/GPIO2=0 routes LNA to Antenna. */
    CMT2300A_EnableAntennaSwitch(0);
    if (!verify_switch_mode()) return false;

    /* Configure Direct Mode on Baseband */
    uint8_t pkt1 = CMT2300A_ReadReg(CMT2300A_CUS_PKT1);
    pkt1 = (uint8_t)((pkt1 & ~CMT2300A_MASK_DATA_MODE) | CMT2300A_DATA_MODE_DIRECT);
    CMT2300A_WriteReg(CMT2300A_CUS_PKT1, pkt1);

    /* Configure GPIO3 as DIN */
    uint8_t io = CMT2300A_ReadReg(CMT2300A_CUS_IO_SEL);
    io = (uint8_t)((io & ~CMT2300A_MASK_GPIO3_SEL) | CMT2300A_GPIO3_SEL_DIN);
    CMT2300A_ConfigGpio(io);
    CMT2300A_ConfigTxDin(CMT2300A_TX_DIN_SEL_GPIO3);
    CMT2300A_EnableTxDinInvert(false);
    CMT2300A_EnableTxDin(true);

    /* Initialize frequency controller & program initial synthesizer */
    freq_ctrl_init();
    apply_freq(freq_ctrl_get_synth_mhz());

    /* Apply user preferences: +20 dBm output power, FSK CW mode */
    apply_power(20.0);
    apply_modulation(RADIO_MODE_FSK);

    /* Verify critical register configurations */
    if ((CMT2300A_ReadReg(CMT2300A_CUS_FIFO_CTL) &
         (CMT2300A_MASK_TX_DIN_EN | CMT2300A_MASK_TX_DIN_SEL)) !=
        (CMT2300A_MASK_TX_DIN_EN | CMT2300A_TX_DIN_SEL_GPIO3)) {
        error_text = "GPIO3 DIN configuration readback failed";
        return false;
    }
    if (!(CMT2300A_ReadReg(CMT2300A_CUS_TX1) & CMT2300A_MASK_TX_DIN_SOURCE)) {
        error_text = "TX_DIN_SOURCE bit in CUS_TX1 readback failed";
        return false;
    }

    if (!CMT2300A_GoStby()) {
        error_text = "Failed to return to standby after setup";
        return false;
    }

    ready = true;
    error_text = "OK";
    return true;
}

bool radio_set_tx(bool on)
{
    if (!on) {
        gpio_put(IO3_GPIO, 0);
        if (ready && !CMT2300A_GoStby()) {
            error_text = "Failed to enter standby";
            return false;
        }
        tx_on = false;
        return true;
    }

    if (!ready || !verify_switch_mode()) {
        error_text = "Radio/switch not initialised"; return false;
    }

    /* Ensure direct-mode, DIN enable, and TX_DIN_SOURCE have not been lost */
    uint8_t pkt1 = CMT2300A_ReadReg(CMT2300A_CUS_PKT1);
    if ((pkt1 & CMT2300A_MASK_DATA_MODE) != CMT2300A_DATA_MODE_DIRECT) {
        pkt1 = (uint8_t)((pkt1 & ~CMT2300A_MASK_DATA_MODE) | CMT2300A_DATA_MODE_DIRECT);
        CMT2300A_WriteReg(CMT2300A_CUS_PKT1, pkt1);
    }
    uint8_t fifo_ctl = CMT2300A_ReadReg(CMT2300A_CUS_FIFO_CTL);
    if ((fifo_ctl & (CMT2300A_MASK_TX_DIN_EN | CMT2300A_MASK_TX_DIN_SEL)) !=
        (CMT2300A_MASK_TX_DIN_EN | CMT2300A_TX_DIN_SEL_GPIO3)) {
        fifo_ctl = (uint8_t)((fifo_ctl & ~CMT2300A_MASK_TX_DIN_SEL) |
                             CMT2300A_MASK_TX_DIN_EN | CMT2300A_TX_DIN_SEL_GPIO3);
        CMT2300A_WriteReg(CMT2300A_CUS_FIFO_CTL, fifo_ctl);
    }
    uint8_t tx1 = CMT2300A_ReadReg(CMT2300A_CUS_TX1);
    if (!(tx1 & CMT2300A_MASK_TX_DIN_SOURCE)) {
        tx1 |= CMT2300A_MASK_TX_DIN_SOURCE;
        CMT2300A_WriteReg(CMT2300A_CUS_TX1, tx1);
    }

    gpio_put(IO3_GPIO, 0);
    if (!CMT2300A_GoStby()) { error_text = "Cannot enter standby"; return false; }

    if (current_mode == RADIO_MODE_FSK) {
        /* Set static high mark on GPIO3 then enter TX */
        gpio_put(IO3_GPIO, 1);
        if (!CMT2300A_GoTx() || CMT2300A_GetChipStatus() != CMT2300A_STA_TX) {
            gpio_put(IO3_GPIO, 0);
            CMT2300A_GoStby();
            error_text = "Failed to enter TX";
            return false;
        }
    } else {
        /* In OOK mode: enter TX then key the carrier high */
        if (!CMT2300A_GoTx() || CMT2300A_GetChipStatus() != CMT2300A_STA_TX) {
            gpio_put(IO3_GPIO, 0);
            CMT2300A_GoStby();
            error_text = "Failed to enter TX";
            return false;
        }
        gpio_put(IO3_GPIO, 1);
    }

    tx_on = true;
    return true;
}

bool radio_test_switch(bool tx)
{
    if (!ready || !verify_switch_mode()) {
        error_text = "Radio or RF switch not initialised";
        return false;
    }
    gpio_put(IO3_GPIO, 0);
    if (!CMT2300A_GoStby()) {
        error_text = "Failed to enter standby before switch test";
        return false;
    }
    bool ok = tx ? CMT2300A_GoTx() : CMT2300A_GoRx();
    if (!ok || CMT2300A_GetChipStatus() != (tx ? CMT2300A_STA_TX : CMT2300A_STA_RX)) {
        CMT2300A_GoStby();
        error_text = tx ? "Could not enter TX test state" : "Could not enter RX test state";
        return false;
    }
    return true;
}

bool radio_end_switch_test(void)
{
    gpio_put(IO3_GPIO, 0);
    bool ok = CMT2300A_GoStby();
    if (!ok) error_text = "Could not return to standby after test";
    return ok;
}

void radio_dump_regs(void)
{
    printf("\r\n--- CMT2300A Register Dump ---\r\n");
    uint8_t cmt2 = CMT2300A_ReadReg(CMT2300A_CUS_CMT2);
    uint8_t cmt4 = CMT2300A_ReadReg(CMT2300A_CUS_CMT4);
    printf("Part ID (0x01):       0x%02X\r\n", cmt2);
    printf("CUS_CMT4 (0x03):      0x%02X (pan_sel=%u)\r\n", cmt4, (cmt4 & CMT2300A_MASK_PAN_SEL) ? 1 : 0);

    printf("Requested Freq:       %.6f MHz\r\n", freq_ctrl_get_freq_mhz());
    printf("Additive Offset:      %.6f MHz\r\n", freq_ctrl_get_offset_mhz());
    printf("Target OutFreq:       %.6f MHz\r\n", freq_ctrl_get_outfreq_mhz());
    printf("Calibration:          %.6f ppm (%s)\r\n", freq_ctrl_get_cal_ppm(), freq_ctrl_get_cal_state_str());
    printf("Synth Freq (Prog):    %.6f MHz\r\n", freq_ctrl_get_synth_mhz());
    printf("Step Size:            %lld Hz\r\n", (long long)freq_ctrl_get_step_hz());
    printf("RF1..RF8 (0x18..0x1F):");
    for (uint8_t addr = 0x18; addr <= 0x1F; addr++) {
        printf(" %02X", CMT2300A_ReadReg(addr));
    }
    printf("\r\n");

    uint8_t pkt1 = CMT2300A_ReadReg(CMT2300A_CUS_PKT1);
    const char *data_mode_str = "Unknown";
    switch (pkt1 & CMT2300A_MASK_DATA_MODE) {
        case CMT2300A_DATA_MODE_DIRECT: data_mode_str = "Direct"; break;
        case CMT2300A_DATA_MODE_PACKET: data_mode_str = "Packet"; break;
    }
    printf("CUS_PKT1 (0x38):      0x%02X (mode=%s)\r\n", pkt1, data_mode_str);

    uint8_t tx1 = CMT2300A_ReadReg(CMT2300A_CUS_TX1);
    printf("CUS_TX1 (0x55):       0x%02X (mod=%s, din_src=%s)\r\n",
           tx1,
           (tx1 & CMT2300A_MASK_TX_MOD_TYPE) ? "OOK" : "FSK",
           (tx1 & CMT2300A_MASK_TX_DIN_SOURCE) ? "GPIO DIN" : "TX FIFO");
    printf("CUS_TX2 (0x56):       0x%02X\r\n", CMT2300A_ReadReg(CMT2300A_CUS_TX2));
    printf("CUS_TX3 (0x57):       0x%02X\r\n", CMT2300A_ReadReg(CMT2300A_CUS_TX3));
    printf("CUS_TX5 (0x59):       0x%02X\r\n", CMT2300A_ReadReg(CMT2300A_CUS_TX5));

    uint8_t tx8 = CMT2300A_ReadReg(CMT2300A_CUS_TX8);
    uint8_t tx9 = CMT2300A_ReadReg(CMT2300A_CUS_TX9);
    uint8_t tx10 = CMT2300A_ReadReg(CMT2300A_CUS_TX10);
    printf("CUS_TX8 (0x5C):       0x%02X (power_code=%u)\r\n", tx8, tx8);
    printf("CUS_TX9 (0x5D):       0x%02X (pcode_adj=%u)\r\n", tx9, tx9 & CMT2300A_MASK_PCODE_ADJ);
    printf("CUS_TX10 (0x5E):      0x%02X (idac_code=%u)\r\n", tx10, tx10 & CMT2300A_MASK_IDAC_CODE);

    uint8_t mode_sta = CMT2300A_ReadReg(CMT2300A_CUS_MODE_STA);
    const char *sta_str = "Unknown";
    switch (mode_sta & CMT2300A_MASK_CHIP_MODE_STA) {
        case CMT2300A_STA_IDLE: sta_str = "IDLE"; break;
        case CMT2300A_STA_SLEEP: sta_str = "SLEEP"; break;
        case CMT2300A_STA_STBY: sta_str = "STBY"; break;
        case CMT2300A_STA_RFS: sta_str = "RFS"; break;
        case CMT2300A_STA_TFS: sta_str = "TFS"; break;
        case CMT2300A_STA_RX: sta_str = "RX"; break;
        case CMT2300A_STA_TX: sta_str = "TX"; break;
        case CMT2300A_STA_EEPROM: sta_str = "EEPROM"; break;
        case CMT2300A_STA_ERROR: sta_str = "ERROR"; break;
        case CMT2300A_STA_CAL: sta_str = "CAL"; break;
    }
    printf("CUS_MODE_STA (0x61):  0x%02X (state=%s)\r\n", mode_sta, sta_str);

    uint8_t io_sel = CMT2300A_ReadReg(CMT2300A_CUS_IO_SEL);
    printf("CUS_IO_SEL (0x65):    0x%02X (gpio3_sel=0x%02X)\r\n", io_sel, io_sel & CMT2300A_MASK_GPIO3_SEL);

    uint8_t int1_ctl = CMT2300A_ReadReg(CMT2300A_CUS_INT1_CTL);
    printf("CUS_INT1_CTL (0x66):  0x%02X (swt1_en=%u, swt2_en=%u)\r\n",
           int1_ctl,
           (int1_ctl & CMT2300A_MASK_RF_SWT1_EN) ? 1 : 0,
           (int1_ctl & CMT2300A_MASK_RF_SWT2_EN) ? 1 : 0);

    uint8_t fifo_ctl = CMT2300A_ReadReg(CMT2300A_CUS_FIFO_CTL);
    printf("CUS_FIFO_CTL (0x69):  0x%02X (din_en=%u, din_sel=0x%02X)\r\n",
           fifo_ctl,
           (fifo_ctl & CMT2300A_MASK_TX_DIN_EN) ? 1 : 0,
           fifo_ctl & CMT2300A_MASK_TX_DIN_SEL);
    printf("Pico GP6 (IO3/DIN):   %d\r\n", gpio_get(IO3_GPIO));
    printf("------------------------------\r\n");
}
