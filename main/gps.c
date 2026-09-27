#include "gps.h"
#include "geohash.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TEST_HOST
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "gps";
#endif

#define GPS_LINE_BUF   160
#define GPS_MAX_FIELDS 24

static gps_fix_t s_fix;

#ifdef TEST_HOST
/* Deterministic clock for host unit tests. */
static int64_t s_test_now_ms = 0;
void gps_test_advance_ms(int64_t ms) { s_test_now_ms += ms; }
static int64_t gps_now_ms(void) { return s_test_now_ms; }
#else
static int64_t gps_now_ms(void) { return esp_timer_get_time() / 1000; }
#endif

static bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int gps_nmea_checksum_ok(const char *sentence)
{
    if (!sentence || sentence[0] != '$') return 0;

    const char *star = strchr(sentence, '*');
    if (!star || star == sentence + 1) return 0;
    if (!is_hex_digit(star[1]) || !is_hex_digit(star[2])) return 0;

    char hex[3] = { star[1], star[2], '\0' };
    unsigned int want = (unsigned int)strtoul(hex, NULL, 16);

    unsigned int sum = 0;
    for (const char *p = sentence + 1; p < star; p++) {
        sum ^= (unsigned char)*p;
    }
    return ((sum & 0xFF) == (want & 0xFF)) ? 1 : 0;
}

/* Split a comma-separated NMEA body in place.  Returns the number of fields. */
static int split_fields(char *buf, char **fields, int max_fields)
{
    int n = 0;
    fields[n++] = buf;
    for (char *p = buf; *p && n < max_fields; p++) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

/* Parse a NMEA ddmm.mmmm / dddmm.mmmm coordinate field.  deg_digits is 2 for
 * latitude and 3 for longitude (derived from the hemisphere letter). */
static bool parse_coordinate(const char *ddmm, char hemi, double *out)
{
    if (!ddmm || !out) return false;
    bool is_lat = (hemi == 'N' || hemi == 'S');
    bool is_lon = (hemi == 'E' || hemi == 'W');
    if (!is_lat && !is_lon) return false;

    const char *dot = strchr(ddmm, '.');
    if (!dot) return false;

    int deg_digits = is_lat ? 2 : 3;
    /* Spec form is exactly ddmm.mmmm / dddmm.mmmm. */
    if ((dot - ddmm) != deg_digits + 2) return false;

    char deg_buf[8];
    memcpy(deg_buf, ddmm, (size_t)deg_digits);
    deg_buf[deg_digits] = '\0';
    int deg = atoi(deg_buf);

    double minutes = strtod(dot - 2, NULL);
    if (minutes < 0.0 || minutes >= 60.0) return false;

    double value = (double)deg + minutes / 60.0;
    if (is_lat && (value < 0.0 || value > 90.0)) return false;
    if (is_lon && (value < 0.0 || value > 180.0)) return false;
    if (hemi == 'S' || hemi == 'W') value = -value;

    *out = value;
    return true;
}

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool parse_gga(char **f, int n, gps_fix_t *fix)
{
    if (n < 10) return false;

    bool coords_ok = false;
    if (f[2][0] && f[4][0]) {
        double lat = 0.0, lon = 0.0;
        if (!parse_coordinate(f[2], f[3][0], &lat)) return false;
        if (!parse_coordinate(f[4], f[5][0], &lon)) return false;
        fix->latitude = lat;
        fix->longitude = lon;
        fix->have_coords = true;
        coords_ok = true;
    }

    fix->fix_quality = atoi(f[6]);
    fix->satellites = atoi(f[7]);
    fix->hdop = f[8][0] ? strtod(f[8], NULL) : 0.0;
    fix->altitude_m = f[9][0] ? strtod(f[9], NULL) : 0.0;
    copy_field(fix->utc_time, sizeof(fix->utc_time), f[1]);
    fix->valid = (fix->fix_quality > 0) && coords_ok;
    return true;
}

static bool parse_rmc(char **f, int n, gps_fix_t *fix)
{
    if (n < 10) return false;

    bool coords_ok = false;
    if (f[3][0] && f[5][0]) {
        double lat = 0.0, lon = 0.0;
        if (!parse_coordinate(f[3], f[4][0], &lat)) return false;
        if (!parse_coordinate(f[5], f[6][0], &lon)) return false;
        fix->latitude = lat;
        fix->longitude = lon;
        fix->have_coords = true;
        coords_ok = true;
    }

    bool active = (f[2][0] == 'A');
    fix->valid = active && coords_ok;
    fix->speed_knots = f[7][0] ? strtod(f[7], NULL) : 0.0;
    fix->course_deg = f[8][0] ? strtod(f[8], NULL) : 0.0;
    copy_field(fix->utc_time, sizeof(fix->utc_time), f[1]);
    copy_field(fix->utc_date, sizeof(fix->utc_date), f[9]);
    return true;
}

bool gps_parse_sentence(const char *sentence, gps_fix_t *fix)
{
    if (!sentence || !fix) return false;
    if (!gps_nmea_checksum_ok(sentence)) return false;

    const char *star = strchr(sentence, '*');
    size_t len = (size_t)(star - sentence);
    if (len < 8 || len >= GPS_LINE_BUF) return false;

    char buf[GPS_LINE_BUF];
    memcpy(buf, sentence, len);
    buf[len] = '\0';

    char *fields[GPS_MAX_FIELDS];
    int n = split_fields(buf, fields, GPS_MAX_FIELDS);
    if (n < 2) return false;

    /* field[0] is e.g. "$GPGGA" / "$GNRMC" / "$GLGGA". */
    const char *tag = fields[0];
    size_t tl = strlen(tag);
    if (tl < 4) return false;
    const char *type = tag + tl - 3;

    if (strcmp(type, "GGA") == 0) return parse_gga(fields, n, fix);
    if (strcmp(type, "RMC") == 0) return parse_rmc(fields, n, fix);
    return false; /* GSA / GSV / VTG / TXT / ... are ignored */
}

bool gps_feed_line(const char *line)
{
    if (!line) return false;

    while (*line == ' ' || *line == '\t') line++;
    if (line[0] != '$') return false;

    gps_fix_t next = s_fix;
    if (!gps_parse_sentence(line, &next)) return false;

    next.updated_ms = gps_now_ms();
    if (next.have_coords) {
        geohash_encode(next.latitude, next.longitude, GPS_GEOHASH_PRECISION, next.geohash);
    } else {
        next.geohash[0] = '\0';
    }

    s_fix = next;
    return true;
}

const gps_fix_t *gps_get_fix(void)
{
    return &s_fix;
}

void gps_reset_state(void)
{
    memset(&s_fix, 0, sizeof(s_fix));
}

#ifndef TEST_HOST
static void gps_uart_task(void *arg)
{
    int uart_num = (int)(intptr_t)arg;
    char line[GPS_LINE_BUF];
    size_t pos = 0;
    uint8_t byte;

    while (1) {
        int n = uart_read_bytes(uart_num, &byte, 1, pdMS_TO_TICKS(1000));
        if (n <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (pos > 0) {
                line[pos] = '\0';
                gps_feed_line(line);
                pos = 0;
            }
            continue;
        }
        if (pos < sizeof(line) - 1) {
            line[pos++] = (char)byte;
        } else {
            pos = 0; /* overlong line: resynchronise on the next newline */
        }
    }
}

esp_err_t gps_start(int uart_num, int tx_pin, int rx_pin, int baud)
{
    const uart_config_t uart_cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(uart_num, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install(%d) failed: %s", uart_num, esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(uart_num, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config(%d) failed: %s", uart_num, esp_err_to_name(err));
        return err;
    }
    err = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin(%d) failed: %s", uart_num, esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(gps_uart_task, "gps_uart", 4096, (void *)(intptr_t)uart_num, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create gps_uart task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "NMEA reader started on UART%d (tx=%d rx=%d baud=%d)",
             uart_num, tx_pin, rx_pin, baud);
    return ESP_OK;
}
#else
esp_err_t gps_start(int uart_num, int tx_pin, int rx_pin, int baud)
{
    (void)uart_num;
    (void)tx_pin;
    (void)rx_pin;
    (void)baud;
    return ESP_OK;
}
#endif
