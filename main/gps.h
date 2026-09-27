#ifndef GPS_H
#define GPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Geohash precision used for the location payload (matches the geohash.c
 * implementation already present in main/). 9 chars ~= 4.8 m x 4.8 m. */
#define GPS_GEOHASH_PRECISION 9
#define GPS_GEOHASH_BUF       16

/* Defaults for the GY-GPS6MV2 (u-blox NEO-6M) module in the Car TollGate
 * inventory.  The module speaks NMEA 0183 over UART at 9600 baud, 8N1.
 * Pins follow the carrier-board plan (docs/PCB-DESIGN-PLAN.md): MCU TX -> GPIO8
 * (GPS RXD), MCU RX -> GPIO9 (GPS TXD).  Only used when gps_enabled is set in
 * config.json. */
#define GPS_DEFAULT_UART   1
#define GPS_DEFAULT_TX_PIN 8
#define GPS_DEFAULT_RX_PIN 9
#define GPS_DEFAULT_BAUD   9600

typedef struct {
    bool valid;           /* fix usable: GGA quality > 0 / RMC status 'A'      */
    bool have_coords;     /* latitude/longitude were parsed at least once      */
    double latitude;      /* decimal degrees, +N / -S                          */
    double longitude;     /* decimal degrees, +E / -W                          */
    double altitude_m;    /* MSL altitude in metres (GGA)                      */
    double hdop;          /* horizontal dilution of precision (GGA)            */
    double speed_knots;   /* speed over ground in knots (RMC)                  */
    double course_deg;    /* course over ground in degrees true (RMC)          */
    int satellites;       /* satellites used in the fix (GGA)                  */
    int fix_quality;      /* 0 none, 1 GPS, 2 DGPS (GGA)                       */
    char utc_time[16];    /* hhmmss.ss as reported (GGA/RMC)                   */
    char utc_date[8];     /* ddmmyy as reported (RMC)                          */
    int64_t updated_ms;   /* uptime ms when this fix was last updated          */
    char geohash[GPS_GEOHASH_BUF]; /* geohash of lat/lon, "" until first fix    */
} gps_fix_t;

/* Validate the NMEA 0183 XOR checksum of a sentence.  Returns 1 when the
 * sentence starts with '$', carries a '*' and the 2-hex-digit checksum
 * matches, 0 otherwise. */
int gps_nmea_checksum_ok(const char *sentence);

/* Parse a single NMEA sentence into *fix (only the fields the sentence
 * carries are touched).  Returns true when the sentence was a recognised,
 * checksum-valid GGA/RMC sentence. */
bool gps_parse_sentence(const char *sentence, gps_fix_t *fix);

/* Feed one raw line (as read from the module's UART, CR/LF tolerated) into the
 * module-wide fix state.  Returns true when the line was a recognised, valid
 * sentence and the state was updated. */
bool gps_feed_line(const char *line);

/* Current fix snapshot (never NULL).  Cleared by gps_reset_state(). */
const gps_fix_t *gps_get_fix(void);

/* Reset the module-wide fix state (used by tests and on config reload). */
void gps_reset_state(void);

/* Start the UART reader task feeding gps_feed_line().  On host builds
 * (TEST_HOST) this is a no-op returning ESP_OK so unit tests can link gps.c. */
esp_err_t gps_start(int uart_num, int tx_pin, int rx_pin, int baud);

#ifdef TEST_HOST
/* Advance the deterministic clock used by host unit tests. */
void gps_test_advance_ms(int64_t ms);
#endif

#endif
