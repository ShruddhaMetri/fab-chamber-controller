# chamber_model.py
# Renode "plant model": the physics of the CVD chamber's temperature.
#
# There is no real chamber, so this script plays its part. Every time the
# firmware starts an ADC scan (every 100 ms), Renode calls chamber_step():
#   1. read the heater power the firmware is commanding (TIM3 PWM duty)
#   2. advance the chamber temperature by 100 ms using a first-order model
#   3. feed the new temperature into the ADC, like a real thermocouple would
# The firmware is not changed at all: it sees an ADC that reports temperature,
# exactly as on real hardware. This is hardware-in-the-loop simulation.
#
# First-order thermal model (one heat capacity, heat loss to the room):
#   dT/dt = ( K * heater%  -  (T - T_AMBIENT) ) / TAU
#     K   : how hot full power can get the chamber above ambient, per percent
#     TAU : time constant: how slowly the chamber responds

import sys

T_AMBIENT = 25.0      # room temperature, C
K         = 6.0       # C per % of heater power  -> 100 % settles at 625 C
TAU       = 30.0      # seconds
DT        = 0.1       # seconds between ADC scans

TIM3_ARR  = 0x4000042C   # PWM period register
TIM3_CCR1 = 0x40000434   # heater duty register (channel 1)
SWSTART   = 1 << 30      # bit in ADC1_CR2 meaning "start a scan"
ADC_TEMP_CHANNEL = 14

def chamber_step(bus, mon, value):
    if not (value & SWSTART):
        return                                    # not a scan start: ignore
    state = sys.__dict__.setdefault('chamber_state', {'T': T_AMBIENT})

    period = bus.ReadDoubleWord(TIM3_ARR) + 1
    duty   = bus.ReadDoubleWord(TIM3_CCR1)
    heater_pct = 100.0 * min(duty, period) / period if period > 1 else 0.0

    T = state['T']
    T += DT / TAU * (K * heater_pct - (T - T_AMBIENT))
    state['T'] = T

    raw = int(round(T * 4095.0 / 800.0))          # 0-800 C maps to 0-4095 counts
    raw = max(0, min(4095, raw))
    mon.Parse("sysbus.adc1 FeedSample %d %d -1" % (raw, ADC_TEMP_CHANNEL))

sys.chamber_step = chamber_step
