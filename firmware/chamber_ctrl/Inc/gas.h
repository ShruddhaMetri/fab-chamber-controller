/*
 * gas.h
 * Process-gas control through two mass flow controllers (MFCs).
 *   MFC 1 : SiH4 (silane),        0-100  sccm, setpoint DAC ch1, readback ADC ch12
 *   MFC 2 : N2O  (nitrous oxide), 0-1000 sccm, setpoint DAC ch2, readback ADC ch13
 * (sccm = standard cubic centimetres per minute, the usual unit of gas flow)
 * Each MFC takes a 0-3.3 V setpoint and reports its real flow as 0-3.3 V.
 */
#ifndef GAS_H
#define GAS_H

#include <stdint.h>

#define GAS_MFC1   1U
#define GAS_MFC2   2U

void     gas_init(void);
void     gas_poll(void);                         /* every SENSOR_PERIOD_MS */

/* Returns 1 if applied, 0 if refused (out of range, or a fault is latched) */
int      gas_set_sccm(uint32_t mfc, uint32_t sccm);

uint32_t gas_setpoint_sccm(uint32_t mfc);
uint32_t gas_measured_sccm(uint32_t mfc);
uint32_t gas_full_scale_sccm(uint32_t mfc);
const char *gas_name(uint32_t mfc);

int      gas_flow_fault_active(void);  /* 1 while any MFC is far from its setpoint */
void     gas_all_off(void);            /* both setpoints to 0. ISR-safe.          */

#endif /* GAS_H */
