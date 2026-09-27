#include "test_framework.h"
#include "../../main/gps.h"
#include "../../main/geohash.h"
#include <math.h>
#include <string.h>

#define EPS 1e-4

/* Standard NMEA 0183 sentences (checksums computed over the body). */
static const char *GGA_FIX =
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
static const char *GGA_SOUTH_WEST =
    "$GPGGA,001234,3352.000,S,15112.000,W,1,06,1.2,20.0,M,10.0,M,,*7E";
static const char *GGA_NOFIX =
    "$GPGGA,120000,,,,,0,00,,,M,,M,,*65";
static const char *RMC_FIX =
    "$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*74";
static const char *RMC_VOID =
    "$GPRMC,123519,V,,,,,,,230394,,*33";
static const char *RMC_MELBOURNE =
    "$GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62";
static const char *GSA_IGNORED =
    "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39";

static void test_checksum(void)
{
    printf("\n=== NMEA checksum ===\n");
    ASSERT_EQ_INT(1, gps_nmea_checksum_ok(GGA_FIX), "valid GGA checksum *47");
    ASSERT_EQ_INT(1, gps_nmea_checksum_ok(RMC_FIX), "valid RMC checksum *74");
    ASSERT_EQ_INT(1, gps_nmea_checksum_ok(GSA_IGNORED), "valid GSA checksum *39");

    ASSERT_EQ_INT(0, gps_nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00"),
                  "corrupted checksum rejected");
    ASSERT_EQ_INT(0, gps_nmea_checksum_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
                  "missing checksum rejected");
    ASSERT_EQ_INT(0, gps_nmea_checksum_ok("GPGGA,123519*47"), "no leading '$' rejected");
    ASSERT_EQ_INT(0, gps_nmea_checksum_ok("$*47"), "empty body rejected");
    ASSERT_EQ_INT(0, gps_nmea_checksum_ok("$GP*ZZ"), "non-hex checksum rejected");
    ASSERT_EQ_INT(0, gps_nmea_checksum_ok(NULL), "NULL rejected");
}

static void test_parse_gga(void)
{
    printf("\n=== parse GGA ===\n");
    gps_fix_t fix;
    memset(&fix, 0, sizeof(fix));

    ASSERT(gps_parse_sentence(GGA_FIX, &fix), "GGA parses");
    ASSERT(fabs(fix.latitude - 48.1173) < EPS, "latitude 4807.038N -> +48.1173");
    ASSERT(fabs(fix.longitude - 11.5167) < EPS, "longitude 01131.000E -> +11.5167");
    ASSERT(fix.have_coords, "coords flagged present");
    ASSERT(fix.valid, "quality 1 -> valid fix");
    ASSERT_EQ_INT(1, fix.fix_quality, "fix quality 1");
    ASSERT_EQ_INT(8, fix.satellites, "8 satellites");
    ASSERT(fabs(fix.hdop - 0.9) < EPS, "hdop 0.9");
    ASSERT(fabs(fix.altitude_m - 545.4) < EPS, "altitude 545.4 m");
    ASSERT_EQ_STR("123519", fix.utc_time, "utc time copied");
}

static void test_parse_hemispheres(void)
{
    printf("\n=== parse hemispheres ===\n");
    gps_fix_t fix;
    memset(&fix, 0, sizeof(fix));

    ASSERT(gps_parse_sentence(GGA_SOUTH_WEST, &fix), "S/W GGA parses");
    ASSERT(fix.latitude < 0.0, "southern latitude negative");
    ASSERT(fabs(fix.latitude - (-33.8667)) < EPS, "3352.000S -> -33.8667");
    ASSERT(fix.longitude < 0.0, "western longitude negative");
    ASSERT(fabs(fix.longitude - (-151.2)) < EPS, "15112.000W -> -151.2");
    ASSERT_EQ_INT(6, fix.satellites, "6 satellites");

    memset(&fix, 0, sizeof(fix));
    ASSERT(gps_parse_sentence(RMC_MELBOURNE, &fix), "Melbourne RMC parses");
    ASSERT(fabs(fix.latitude - (-37.86083)) < EPS, "3751.65S -> -37.86083");
    ASSERT(fabs(fix.longitude - 145.12267) < EPS, "14507.36E -> 145.12267");
    ASSERT(fabs(fix.speed_knots - 0.0) < EPS, "0.0 kn");
    ASSERT(fabs(fix.course_deg - 360.0) < EPS, "360.0 deg course");
    ASSERT_EQ_STR("130998", fix.utc_date, "date copied");
}

static void test_parse_no_fix(void)
{
    printf("\n=== parse no-fix sentences ===\n");
    gps_fix_t fix;
    memset(&fix, 0, sizeof(fix));

    ASSERT(gps_parse_sentence(GGA_NOFIX, &fix), "no-fix GGA recognised");
    ASSERT(!fix.valid, "quality 0 -> invalid");
    ASSERT(!fix.have_coords, "no coords parsed");
    ASSERT_EQ_INT(0, fix.fix_quality, "quality 0");
    ASSERT_EQ_INT(0, fix.satellites, "0 satellites");

    memset(&fix, 0, sizeof(fix));
    ASSERT(gps_parse_sentence(RMC_VOID, &fix), "void RMC recognised");
    ASSERT(!fix.valid, "status V -> invalid");
    ASSERT(!fix.have_coords, "no coords in void RMC");
    ASSERT_EQ_STR("230394", fix.utc_date, "date still copied from void RMC");
}

static void test_parse_rmc_fields(void)
{
    printf("\n=== parse RMC fields ===\n");
    gps_fix_t fix;
    memset(&fix, 0, sizeof(fix));

    ASSERT(gps_parse_sentence(RMC_FIX, &fix), "RMC parses");
    ASSERT(fix.valid, "status A -> valid");
    ASSERT(fabs(fix.speed_knots - 22.4) < EPS, "speed 22.4 kn");
    ASSERT(fabs(fix.course_deg - 84.4) < EPS, "course 84.4 deg");
    ASSERT_EQ_STR("123519", fix.utc_time, "time copied");
}

static void test_unknown_sentence(void)
{
    printf("\n=== unknown / malformed sentences ===\n");
    gps_fix_t fix;
    memset(&fix, 0, sizeof(fix));

    ASSERT(!gps_parse_sentence(GSA_IGNORED, &fix), "GSA ignored (not GGA/RMC)");
    ASSERT(!gps_parse_sentence("$GPGGA,1,2,3*00", &fix), "bad checksum not parsed");
    ASSERT(!gps_parse_sentence("not a sentence", &fix), "garbage not parsed");
    ASSERT(!gps_parse_sentence(NULL, &fix), "NULL not parsed");
}

static void test_feed_state(void)
{
    printf("\n=== feed state machine ===\n");
    gps_reset_state();

    gps_fix_t zero;
    memset(&zero, 0, sizeof(zero));
    const gps_fix_t *fix = gps_get_fix();
    ASSERT(fix != NULL, "gps_get_fix never NULL");
    ASSERT(memcmp(fix, &zero, sizeof(zero)) == 0, "state starts zeroed");

    gps_test_advance_ms(1000);
    ASSERT(gps_feed_line(GGA_FIX), "feed GGA accepted");
    fix = gps_get_fix();
    ASSERT(fix->valid, "fix valid after GGA");
    ASSERT_EQ_UINT64(1000, fix->updated_ms, "updated_ms stamped from uptime");
    ASSERT_EQ_STR("u281wp6pt", fix->geohash, "geohash computed at precision 9");

    char ref[GPS_GEOHASH_BUF];
    geohash_encode(fix->latitude, fix->longitude, GPS_GEOHASH_PRECISION, ref);
    ASSERT_EQ_STR(ref, fix->geohash, "geohash matches geohash.c directly");

    /* An RMC must merge into the existing state: GGA altitude survives, RMC
     * speed/date are added. */
    gps_test_advance_ms(500);
    ASSERT(gps_feed_line(RMC_FIX), "feed RMC accepted");
    fix = gps_get_fix();
    ASSERT(fabs(fix->altitude_m - 545.4) < EPS, "altitude retained across sentences");
    ASSERT(fabs(fix->speed_knots - 22.4) < EPS, "speed added by RMC");
    ASSERT_EQ_STR("230394", fix->utc_date, "date added by RMC");
    ASSERT_EQ_UINT64(1500, fix->updated_ms, "updated_ms refreshed by RMC");

    /* CR/LF toleration, as read from the UART. */
    gps_reset_state();
    gps_test_advance_ms(2000);
    ASSERT(gps_feed_line("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"),
           "line with CR/LF accepted");
    ASSERT(gps_get_fix()->valid, "fix valid after CR/LF line");

    /* A corrupt sentence must not disturb the existing fix. */
    int64_t before = gps_get_fix()->updated_ms;
    ASSERT(!gps_feed_line("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00"),
           "corrupt line rejected");
    ASSERT_EQ_UINT64((unsigned long long)before, gps_get_fix()->updated_ms,
                     "state untouched by rejected line");

    /* A no-fix sentence invalidates the fix but keeps the geohash of the last
     * known position cleared (no valid coords in the sentence). */
    gps_reset_state();
    ASSERT(gps_feed_line(GGA_NOFIX), "no-fix GGA accepted");
    fix = gps_get_fix();
    ASSERT(!fix->valid, "no-fix GGA leaves fix invalid");
    ASSERT_EQ_STR("", fix->geohash, "no geohash without coords");

    /* Ignored sentence types are rejected without touching state. */
    gps_reset_state();
    ASSERT(!gps_feed_line(GSA_IGNORED), "GSA not fed into state");
}

static void test_reset(void)
{
    printf("\n=== reset ===\n");
    gps_reset_state();
    ASSERT(gps_feed_line(GGA_FIX), "fix installed");
    ASSERT(gps_get_fix()->valid, "valid before reset");
    gps_reset_state();
    ASSERT(!gps_get_fix()->valid, "invalid after reset");
    ASSERT(!gps_get_fix()->have_coords, "coords cleared after reset");
}

int main(void)
{
    printf("=== test_gps ===\n");
    test_checksum();
    test_parse_gga();
    test_parse_hemispheres();
    test_parse_no_fix();
    test_parse_rmc_fields();
    test_unknown_sentence();
    test_feed_state();
    test_reset();
    TEST_SUMMARY();
}
