/*
 * test_main.c
 * Host (PC) unit tests for the chamber controller firmware.
 * Build and run:   make -C tests
 */
#include <string.h>
#include "test.h"
#include "fakes.h"
#include "crc16.h"
#include "crc32.h"
#include "temp_ctrl.h"
#include "recipe.h"

int g_tests_run      = 0;
int g_tests_failed   = 0;
int g_current_failed = 0;

/* ======================= CRC ======================= */
static const uint8_t CHECK_STRING[] = "123456789";   /* the standard test input */

static void test_crc16_check_value(void)
{
    /* CRC-16/CCITT-FALSE is defined to give 0x29B1 for "123456789" */
    CHECK_EQ(crc16(CHECK_STRING, 9U), 0x29B1);
}

static void test_crc32_check_value(void)
{
    CHECK_EQ(crc32_update(0U, CHECK_STRING, 9U), 0xCBF43926UL);
}

static void test_crc32_in_pieces(void)
{
    /* Feeding the data in two parts must give the same result (the firmware
     * update relies on this when data arrives frame by frame) */
    uint32_t c = crc32_update(0U, CHECK_STRING, 4U);
    c = crc32_update(c, CHECK_STRING + 4, 5U);
    CHECK_EQ(c, 0xCBF43926UL);
}

static void test_crc32_detects_single_bit_error(void)
{
    uint8_t buf[9];
    memcpy(buf, CHECK_STRING, 9U);
    buf[4] ^= 0x01U;
    CHECK(crc32_update(0U, buf, 9U) != 0xCBF43926UL);
}

/* ======================= PID temperature control ======================= */
/* Runs the real temp_ctrl.c against the simulated chamber, 10 cycles per second */
typedef struct {
    double max_temp;
    double settle_s;      /* last time it was outside +/-1 C of the setpoint */
} run_result_t;

static run_result_t simulate(double seconds, double setpoint)
{
    run_result_t r = { 0.0, 0.0 };
    int i, n = (int)(seconds * 10.0);
    for (i = 0; i < n; i++) {
        tc_poll();                    /* the firmware decides the heater power */
        fake_plant_step();            /* the "chamber" reacts                  */
        if (fake_temp_c > r.max_temp) { r.max_temp = fake_temp_c; }
        if (fake_temp_c < setpoint - 1.0 || fake_temp_c > setpoint + 1.0) {
            r.settle_s = (i + 1) / 10.0;
        }
    }
    return r;
}

static void pid_setup(void)
{
    fake_plant_reset();
    tc_init();
    tc_set_gains(2000U, 200U, 2000U);          /* the tuned values */
}

static void test_pid_reaches_400C(void)
{
    run_result_t r;
    pid_setup();
    CHECK(tc_set_setpoint_c(400U));
    r = simulate(180.0, 400.0);
    CHECK_RANGE(fake_temp_c, 399.0, 401.0);           /* on target            */
    CHECK_RANGE(r.max_temp, 400.0, 405.0);            /* overshoot under 5 C  */
    CHECK_RANGE(r.settle_s, 0.0, 90.0);               /* settled in time      */
    CHECK_RANGE(fake_heater_permille, 615, 635);      /* (400-25)/6 = 62.5 %  */
}

static void test_pid_p_only_leaves_offset(void)
{
    /* Without the integral term a steady error remains: textbook behaviour.
     * Theory: heater% = 2 * error and T - 25 = 6 * heater%  ->  T = 371.2 C */
    pid_setup();
    tc_set_gains(2000U, 0U, 0U);
    CHECK(tc_set_setpoint_c(400U));
    (void)simulate(240.0, 400.0);
    CHECK_RANGE(fake_temp_c, 369.0, 373.0);
}

static void test_pid_antiwindup_during_heatup(void)
{
    /* Heater saturated at 100 %: the integral must not keep growing */
    int32_t p, i, d, out;
    pid_setup();
    CHECK(tc_set_setpoint_c(400U));
    (void)simulate(10.0, 400.0);
    tc_get_terms(&p, &i, &d, &out);
    CHECK_EQ(out, 1000);                               /* still saturated      */
    CHECK_RANGE(i, 0, 50);                             /* integral held back   */
}

static void test_pid_fault_stops_auto(void)
{
    pid_setup();
    CHECK(tc_set_setpoint_c(400U));
    (void)simulate(30.0, 400.0);
    fake_fault = 1;                                    /* e.g. e-stop pressed  */
    fake_heater_permille = 0U;                         /* the ISR forced it off */
    tc_poll();
    CHECK_EQ(tc_is_auto(), 0);                         /* PID gave up control  */
    (void)simulate(5.0, 400.0);
    CHECK_EQ(fake_heater_permille, 0);                 /* and never re-heated  */
    CHECK_EQ(tc_set_setpoint_c(300U), 0);              /* restart refused      */
}

static void test_pid_setpoint_limits(void)
{
    pid_setup();
    CHECK_EQ(tc_set_setpoint_c(481U), 0);              /* above 480 C: refused */
    CHECK_EQ(tc_is_auto(), 0);
    CHECK_EQ(tc_set_setpoint_c(480U), 1);
    CHECK_EQ(tc_is_auto(), 1);
    CHECK_EQ(tc_set_setpoint_c(0U), 1);                /* 0 = back to MANUAL   */
    CHECK_EQ(tc_is_auto(), 0);
    CHECK_EQ(fake_heater_permille, 0);
}

static void test_pid_extreme_gains_stay_in_range(void)
{
    /* Largest allowed gains: the 64-bit maths must not overflow and flip sign */
    int i;
    pid_setup();
    tc_set_gains(TC_GAIN_MAX, TC_GAIN_MAX, TC_GAIN_MAX);
    CHECK(tc_set_setpoint_c(480U));
    for (i = 0; i < 600; i++) {
        tc_poll();
        fake_plant_step();
        CHECK(fake_heater_permille <= 1000U);
    }
}

/* ======================= Recipes in EEPROM ======================= */
static recipe_t make_recipe(void)
{
    recipe_t r;
    memset(&r, 0, sizeof(r));
    strcpy(r.name, "OXIDE");
    r.temp_c = 400U;  r.pressure_mtorr = 2000U;
    r.gas1_sccm = 50U; r.gas2_sccm = 400U; r.time_s = 120U;
    return r;
}

static void eeprom_blank(void)
{
    memset(fake_eeprom, 0xFF, sizeof(fake_eeprom));    /* erased EEPROM reads 0xFF */
    fake_eeprom_fail = 0;
    fake_eeprom_corrupt_write = 0;
}

static void test_recipe_save_and_load(void)
{
    recipe_t in = make_recipe(), out;
    eeprom_blank();
    CHECK_EQ(recipe_save(1U, &in), RECIPE_OK);
    CHECK_EQ(recipe_load(1U, &out), RECIPE_OK);
    CHECK(strcmp(out.name, "OXIDE") == 0);
    CHECK_EQ(out.temp_c, 400);
    CHECK_EQ(out.time_s, 120);
}

static void test_recipe_empty_slot(void)
{
    recipe_t out;
    eeprom_blank();
    CHECK_EQ(recipe_load(2U, &out), RECIPE_EMPTY);
}

static void test_recipe_detects_corruption(void)
{
    recipe_t in = make_recipe(), out;
    eeprom_blank();
    CHECK_EQ(recipe_save(0U, &in), RECIPE_OK);
    fake_eeprom[0U * 32U + 16U] ^= 0x01U;              /* one bit flips in storage */
    CHECK_EQ(recipe_load(0U, &out), RECIPE_CORRUPT);
}

static void test_recipe_verify_catches_bad_write(void)
{
    recipe_t in = make_recipe();
    eeprom_blank();
    fake_eeprom_corrupt_write = 1;                     /* the chip lies about the write */
    CHECK_EQ(recipe_save(0U, &in), RECIPE_VERIFY_FAIL);
}

static void test_recipe_io_error_and_range(void)
{
    recipe_t in = make_recipe(), out;
    eeprom_blank();
    fake_eeprom_fail = 1;                              /* chip not answering */
    CHECK_EQ(recipe_load(0U, &out), RECIPE_IO_ERROR);
    fake_eeprom_fail = 0;
    CHECK_EQ(recipe_save(RECIPE_SLOTS, &in), RECIPE_IO_ERROR);   /* slot 4 does not exist */
}

static void test_recipe_erase(void)
{
    recipe_t in = make_recipe(), out;
    eeprom_blank();
    CHECK_EQ(recipe_save(3U, &in), RECIPE_OK);
    CHECK_EQ(recipe_erase(3U), RECIPE_OK);
    CHECK_EQ(recipe_load(3U, &out), RECIPE_EMPTY);
}

/* ======================= Runner ======================= */
int main(void)
{
    printf("CRC\n");
    RUN_TEST(test_crc16_check_value);
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_crc32_in_pieces);
    RUN_TEST(test_crc32_detects_single_bit_error);

    printf("PID temperature control (against a simulated chamber)\n");
    RUN_TEST(test_pid_reaches_400C);
    RUN_TEST(test_pid_p_only_leaves_offset);
    RUN_TEST(test_pid_antiwindup_during_heatup);
    RUN_TEST(test_pid_fault_stops_auto);
    RUN_TEST(test_pid_setpoint_limits);
    RUN_TEST(test_pid_extreme_gains_stay_in_range);

    printf("Recipes in EEPROM\n");
    RUN_TEST(test_recipe_save_and_load);
    RUN_TEST(test_recipe_empty_slot);
    RUN_TEST(test_recipe_detects_corruption);
    RUN_TEST(test_recipe_verify_catches_bad_write);
    RUN_TEST(test_recipe_io_error_and_range);
    RUN_TEST(test_recipe_erase);

    printf("\n%d tests, %d failed\n", g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
