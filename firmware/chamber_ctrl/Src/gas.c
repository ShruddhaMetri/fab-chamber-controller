/*
 * gas.c
 * Turns "50 sccm of silane" into a DAC voltage, reads back what the MFC
 * really delivers through the ADC, and trips a fault if they disagree.
 */
#include "gas.h"
#include "dac.h"
#include "adc.h"
#include "safety.h"
#include "app_config.h"
#include "irq.h"

typedef struct {
    const char *name;
    uint32_t    full_scale;     /* sccm at 3.3 V                     */
    uint32_t    dac_ch;         /* which DAC output sets it          */
    uint32_t    adc_idx;        /* which ADC reading reports it      */
    uint32_t    setpoint;       /* sccm requested                    */
    uint32_t    measured;       /* sccm reported by the MFC          */
    uint32_t    bad_count;      /* readings in a row outside tolerance */
} mfc_t;

/* One table entry per MFC: adding a third gas is one more line */
static mfc_t mfcs[2] = {
    { "SiH4", 100U,  DAC_CH1, ADC_IDX_FLOW1, 0U, 0U, 0U },
    { "N2O",  1000U, DAC_CH2, ADC_IDX_FLOW2, 0U, 0U, 0U },
};

/* Integer conversions, rounded to the nearest whole number:
 * adding half the divisor before dividing turns "always round down"
 * into "round to nearest" (e.g. 2047.5 -> 2048, not 2047). */
static uint32_t sccm_to_counts(uint32_t sccm, uint32_t full_scale)
{
    return ((sccm * DAC_MAX_COUNT) + (full_scale / 2U)) / full_scale;
}

static uint32_t counts_to_sccm(uint32_t counts, uint32_t full_scale)
{
    return ((counts * full_scale) + (ADC_MAX_COUNT / 2U)) / ADC_MAX_COUNT;
}

static mfc_t *get(uint32_t mfc)
{
    if (mfc == GAS_MFC1) { return &mfcs[0]; }
    if (mfc == GAS_MFC2) { return &mfcs[1]; }
    return 0;                                  /* invalid number */
}

void gas_init(void)
{
    dac_init();                                /* both setpoints start at 0 V */
}

int gas_set_sccm(uint32_t mfc, uint32_t sccm)
{
    mfc_t *m = get(mfc);
    if ((m == 0) || (sccm > m->full_scale)) {
        return 0;
    }
    /* Critical section: check and write together (see heater_set_percent) */
    irq_disable();
    if (safety_is_faulted() && (sccm > 0U)) {
        irq_enable();
        return 0;                              /* no gas while a fault is latched */
    }
    m->setpoint  = sccm;
    m->bad_count = 0U;                         /* give the MFC time to respond */
    dac_write(m->dac_ch, sccm_to_counts(sccm, m->full_scale));
    irq_enable();
    return 1;
}

void gas_poll(void)
{
    uint32_t i;

    /* During a fault the safety ISR has already set both DAC outputs to 0 V.
     * Bring the setpoints in line with that here, in the main loop, and skip
     * the flow comparison (the fault is already latched). */
    if (safety_is_faulted()) {
        for (i = 0U; i < 2U; i++) {
            mfcs[i].setpoint  = 0U;
            mfcs[i].bad_count = 0U;
            mfcs[i].measured  = counts_to_sccm(adc_get_raw(mfcs[i].adc_idx), mfcs[i].full_scale);
        }
        return;
    }

    for (i = 0U; i < 2U; i++) {
        mfc_t   *m   = &mfcs[i];
        uint32_t tol = (m->full_scale * GASFLOW_TOLERANCE_PCT) / 100U;
        uint32_t diff;

        m->measured = counts_to_sccm(adc_get_raw(m->adc_idx), m->full_scale);
        diff = (m->measured > m->setpoint) ? (m->measured - m->setpoint)
                                           : (m->setpoint - m->measured);

        /* Out of tolerance for 2 s in a row = blocked, leaking or failed MFC */
        if (diff > tol) {
            if (m->bad_count < GASFLOW_SAMPLES) {
                m->bad_count++;
            }
            if (m->bad_count == GASFLOW_SAMPLES) {
                safety_trip_from_task(FAULT_GASFLOW);
            }
        } else {
            m->bad_count = 0U;
        }
    }
}

uint32_t gas_setpoint_sccm(uint32_t mfc)   { mfc_t *m = get(mfc); return m ? m->setpoint   : 0U; }
uint32_t gas_measured_sccm(uint32_t mfc)   { mfc_t *m = get(mfc); return m ? m->measured   : 0U; }
uint32_t gas_full_scale_sccm(uint32_t mfc) { mfc_t *m = get(mfc); return m ? m->full_scale : 0U; }
const char *gas_name(uint32_t mfc)         { mfc_t *m = get(mfc); return m ? m->name       : "?"; }

int gas_flow_fault_active(void)
{
    uint32_t i;
    for (i = 0U; i < 2U; i++) {
        uint32_t tol  = (mfcs[i].full_scale * GASFLOW_TOLERANCE_PCT) / 100U;
        uint32_t diff = (mfcs[i].measured > mfcs[i].setpoint)
                      ? (mfcs[i].measured - mfcs[i].setpoint)
                      : (mfcs[i].setpoint - mfcs[i].measured);
        if (diff > tol) {
            return 1;
        }
    }
    return 0;
}

/* Called from the safety ISR: hardware register writes only.
 * The setpoint variables are updated later by gas_poll() in the main loop. */
void gas_all_off(void)
{
    dac_write(DAC_CH1, 0U);
    dac_write(DAC_CH2, 0U);
}
