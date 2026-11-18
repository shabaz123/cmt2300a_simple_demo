#include "menu.h"
#include "radio.h"
#include "freq_ctrl.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#define LINE_MAX_LEN 120
#define HISTORY_SIZE 12
static char line[LINE_MAX_LEN + 1];
static char history[HISTORY_SIZE][LINE_MAX_LEN + 1];
static char saved[LINE_MAX_LEN + 1];
static unsigned used, head, length, cursor;
static int browsing = -1;
static unsigned char esc_state;

static void redraw(void)
{
    printf("\r\033[2K> %s", line);
    if (length > cursor) printf("\033[%uD", length - cursor);
    fflush(stdout);
}
static void set_line(const char *s)
{
    snprintf(line, sizeof line, "%s", s);
    length = cursor = (unsigned)strlen(line);
    redraw();
}
static void navigate(int dir)
{
    if (!used) return;
    if (dir < 0) {
        if (browsing < 0) { strcpy(saved, line); browsing = 0; }
        else if ((unsigned)(browsing + 1) < used) ++browsing;
    } else {
        if (browsing < 0) return;
        --browsing;
    }
    if (browsing < 0) set_line(saved);
    else set_line(history[(head + HISTORY_SIZE - 1 - (unsigned)browsing) % HISTORY_SIZE]);
}
static int equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return !*a && !*b;
}
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}
static bool parse_number(const char *s, double *out)
{
    char *end;
    *out = strtod(s, &end);
    return end != s && *trim(end) == '\0' && isfinite(*out);
}
static void get_param(const char *name)
{
    if (equal_ci(name, "freq")) {
        printf("freq=%.6f MHz\r\n", freq_ctrl_get_freq_mhz());
    }
    else if (equal_ci(name, "offset")) {
        printf("offset=%.6f MHz\r\n", freq_ctrl_get_offset_mhz());
    }
    else if (equal_ci(name, "outfreq")) {
        printf("outfreq=%.6f MHz\r\n", freq_ctrl_get_outfreq_mhz());
    }
    else if (equal_ci(name, "cal")) {
        printf("cal=%.6f ppm (%s)\r\n", freq_ctrl_get_cal_ppm(), freq_ctrl_get_cal_state_str());
    }
    else if (equal_ci(name, "calstate")) {
        printf("calstate=%s\r\n", freq_ctrl_get_cal_state_str());
    }
    else if (equal_ci(name, "step")) {
        printf("step=%lld Hz\r\n", (long long)freq_ctrl_get_step_hz());
    }
    else if (equal_ci(name, "level")) {
        printf("level=%.1f dBm\r\n", radio_get_level());
    }
    else if (equal_ci(name, "mode")) {
        printf("mode=%s\r\n", radio_get_mode_str());
    }
    else if (equal_ci(name, "tx")) {
        printf("tx=%d\r\n", radio_get_tx() ? 1 : 0);
    }
    else if (equal_ci(name, "switch")) {
        const char *route = radio_get_switch();
        if (route) printf("switch=%s\r\n", route);
        else printf("ERROR: %s\r\n", radio_error());
    }
    else {
        printf("Unknown parameter '%s'\r\n", name);
    }
}

static void execute(char *command)
{
    char *p = trim(command);
    if (!*p) return;

    /* Handle single character step shortcuts entered with Enter */
    if (strcmp(p, "]") == 0) {
        if (!freq_ctrl_step(true)) {
            printf("ERROR: %s\r\n", freq_ctrl_error());
        } else {
            printf("freq=%.6f MHz (outfreq=%.6f MHz)\r\n", freq_ctrl_get_freq_mhz(), freq_ctrl_get_outfreq_mhz());
        }
        return;
    }
    if (strcmp(p, "[") == 0) {
        if (!freq_ctrl_step(false)) {
            printf("ERROR: %s\r\n", freq_ctrl_error());
        } else {
            printf("freq=%.6f MHz (outfreq=%.6f MHz)\r\n", freq_ctrl_get_freq_mhz(), freq_ctrl_get_outfreq_mhz());
        }
        return;
    }

    char *space = p;
    while (*space && !isspace((unsigned char)*space)) ++space;
    if (*space) *space++ = '\0';
    char *args = trim(space);

    if (equal_ci(p, "help") || equal_ci(p, "?")) {
        printf("Commands (case insensitive):\r\n"
               "  SET freq=<mhz>            Set requested frequency in MHz (e.g. 433.920)\r\n"
               "  GET freq                  Read requested frequency\r\n"
               "  SET offset=<mhz>          Set additive frequency offset in MHz (e.g. -10.7)\r\n"
               "  GET offset                Read frequency offset\r\n"
               "  GET outfreq               Read effective target frequency (freq + offset)\r\n"
               "  SET cal=<ppm>             Set manual calibration in ppm (+/-50 ppm)\r\n"
               "  GET cal                   Read calibration in ppm\r\n"
               "  SET measfreq=<mhz>        Auto-calibrate from spectrum analyzer measured frequency\r\n"
               "  GET calstate              Read calibration state (UNCAL or CAL)\r\n"
               "  SET step=<hz>             Set fine-tune step in Hz (default: 25)\r\n"
               "  GET step                  Read fine-tune step in Hz\r\n"
               "  SET level=20|13|-10       Output power in dBm (default: +20 dBm)\r\n"
               "  GET level                 Read output power level\r\n"
               "  SET mode=fsk|ook          Modulation mode (default: FSK continuous wave)\r\n"
               "  GET mode                  Read modulation mode\r\n"
               "  SET tx=0|1                Disable/enable transmission\r\n"
               "  GET tx                    Read transmission state\r\n"
               "  GET all                   Show all settings\r\n"
               "  TEST switch=rx|tx         Exercise RF switch for 5 seconds\r\n"
               "  TEST regs                 Dump CMT2300A registers\r\n"
               "  ]                         Fine-tune: increment freq by step (+step)\r\n"
               "  [                         Fine-tune: decrement freq by step (-step)\r\n"
               "  HELP                      This help\r\n"
               "Editing: Left/Right, Up/Down history, Home/End, Delete, Backspace.\r\n"
               "NOTE: Bands supported: 127-169, 190-339, 380-509, 759-1019 MHz.\r\n");
    } else if (equal_ci(p, "test")) {
        if (equal_ci(args, "regs")) {
            radio_dump_regs();
            return;
        }
        bool tx;
        if (equal_ci(args, "switch=rx")) tx = false;
        else if (equal_ci(args, "switch=tx")) tx = true;
        else { printf("Usage: TEST switch=rx|tx OR TEST regs\r\n"); return; }
        if (tx) printf("WARNING: TX test may emit RF. Use a 50-ohm load.\r\n");
        if (!radio_test_switch(tx)) {
            printf("ERROR: %s\r\n", radio_error());
            return;
        }
        printf("switch=%s (test active for 5 seconds)\r\n", tx ? "TX" : "RX");
        fflush(stdout);
        sleep_ms(5000);
        if (!radio_end_switch_test()) printf("ERROR: %s\r\n", radio_error());
        else printf("switch=IDLE (test complete)\r\n");
    } else if (equal_ci(p, "get")) {
        if (equal_ci(args, "all")) {
            get_param("freq");
            get_param("offset");
            get_param("outfreq");
            get_param("cal");
            get_param("calstate");
            get_param("step");
            get_param("level");
            get_param("mode");
            get_param("tx");
            get_param("switch");
        }
        else if (*args) get_param(args);
        else printf("Usage: GET freq|offset|outfreq|cal|calstate|step|level|mode|tx|switch|all\r\n");
    } else if (equal_ci(p, "set")) {
        char *eq = strchr(args, '=');
        if (!eq) { printf("Usage: SET name=value\r\n"); return; }
        *eq = '\0';
        char *name = trim(args), *value = trim(eq + 1);
        bool ok = false;
        const char *err = NULL;

        if (equal_ci(name, "mode")) {
            if (equal_ci(value, "ook")) ok = radio_set_mode(RADIO_MODE_OOK);
            else if (equal_ci(value, "fsk")) ok = radio_set_mode(RADIO_MODE_FSK);
            else { printf("mode must be 'fsk' or 'ook'\r\n"); return; }
            if (!ok) err = radio_error();
        } else {
            double number;
            if (!parse_number(value, &number)) { printf("Invalid numeric value\r\n"); return; }

            if (equal_ci(name, "freq")) {
                ok = freq_ctrl_set_freq_mhz(number);
                if (!ok) err = freq_ctrl_error();
            }
            else if (equal_ci(name, "offset")) {
                ok = freq_ctrl_set_offset_mhz(number);
                if (!ok) err = freq_ctrl_error();
            }
            else if (equal_ci(name, "cal")) {
                ok = freq_ctrl_set_cal_ppm(number);
                if (!ok) err = freq_ctrl_error();
            }
            else if (equal_ci(name, "measfreq")) {
                ok = freq_ctrl_set_measfreq_mhz(number);
                if (!ok) err = freq_ctrl_error();
            }
            else if (equal_ci(name, "step")) {
                ok = freq_ctrl_set_step_hz((int64_t)llround(number));
                if (!ok) err = freq_ctrl_error();
            }
            else if (equal_ci(name, "level")) {
                ok = radio_set_level(number);
                if (!ok) err = radio_error();
            }
            else if (equal_ci(name, "tx")) {
                if (strcmp(value, "0") && strcmp(value, "1")) {
                    printf("tx must be 0 or 1\r\n"); return;
                }
                ok = radio_set_tx(number == 1.0);
                if (!ok) err = radio_error();
            }
            else { printf("Unknown parameter '%s'\r\n", name); return; }
        }

        if (!ok) {
            printf("ERROR: %s\r\n", err ? err : "Operation failed");
        } else {
            printf("OK\r\n");
            if (equal_ci(name, "measfreq")) {
                get_param("cal");
                get_param("calstate");
            } else {
                get_param(name);
            }
        }
    } else printf("Unknown command '%s' (try HELP)\r\n", p);
}

static void enter_line(void)
{
    printf("\r\n");
    if (length) {
        if (!used || strcmp(line, history[(head + HISTORY_SIZE - 1) % HISTORY_SIZE])) {
            strcpy(history[head], line);
            head = (head + 1) % HISTORY_SIZE;
            if (used < HISTORY_SIZE) ++used;
        }
        execute(line);
    }
    length = cursor = 0;
    line[0] = '\0';
    browsing = -1;
    printf("> "); fflush(stdout);
}

static void process_byte(unsigned char c)
{
    if (esc_state == 1) {
        if (c == '[') { esc_state = 2; return; }
        if (c == 'O') { esc_state = 3; return; }
        esc_state = 0; return;
    }
    if (esc_state == 2 || esc_state == 3) {
        if (c >= '0' && c <= '9' && esc_state == 2) { esc_state = (unsigned char)(10 + c - '0'); return; }
        esc_state = 0;
        switch (c) {
            case 'A': navigate(-1); break;
            case 'B': navigate(1); break;
            case 'C': if (cursor < length) { ++cursor; redraw(); } break;
            case 'D': if (cursor) { --cursor; redraw(); } break;
            case 'H': cursor = 0; redraw(); break;
            case 'F': cursor = length; redraw(); break;
        }
        return;
    }
    if (esc_state >= 10) {
        unsigned code = esc_state - 10;
        esc_state = 0;
        if (c == '~') {
            if (code == 3 && cursor < length) {
                memmove(line + cursor, line + cursor + 1, length - cursor);
                --length; redraw();
            } else if (code == 1 || code == 7) { cursor = 0; redraw(); }
            else if (code == 4 || code == 8) { cursor = length; redraw(); }
        }
        return;
    }

    /* Single key shortcuts [ and ] when input buffer is empty */
    if (esc_state == 0 && length == 0) {
        if (c == ']') {
            if (!freq_ctrl_step(true)) {
                printf("\r\033[2K> ERROR: %s", freq_ctrl_error());
            } else {
                printf("\r\033[2K> freq=%.6f MHz (outfreq=%.6f MHz)", freq_ctrl_get_freq_mhz(), freq_ctrl_get_outfreq_mhz());
            }
            fflush(stdout);
            return;
        }
        if (c == '[') {
            if (!freq_ctrl_step(false)) {
                printf("\r\033[2K> ERROR: %s", freq_ctrl_error());
            } else {
                printf("\r\033[2K> freq=%.6f MHz (outfreq=%.6f MHz)", freq_ctrl_get_freq_mhz(), freq_ctrl_get_outfreq_mhz());
            }
            fflush(stdout);
            return;
        }
    }

    if (c == 27) { esc_state = 1; return; }
    if (c == '\r' || c == '\n') { enter_line(); return; }
    if (c == 8 || c == 127) {
        if (cursor) {
            memmove(line + cursor - 1, line + cursor, length - cursor + 1);
            --cursor; --length; redraw();
        }
        return;
    }
    if (c >= 32 && c < 127 && length < LINE_MAX_LEN) {
        memmove(line + cursor + 1, line + cursor, length - cursor + 1);
        line[cursor++] = (char)c;
        ++length;
        redraw();
    }
}

void menu_init(void)
{
    printf("\r\nCMT2300A Pico RF Generator\r\nType HELP for commands.\r\n> ");
    fflush(stdout);
}

void menu_poll(void)
{
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT)
        process_byte((unsigned char)c);
}
