# chamber_model.py
# Renode "plant model": the physics of the CVD chamber.
#
# There is no real chamber, so this script plays its part. Every time the
# firmware starts an ADC scan (every 100 ms), Renode calls chamber_step():
#   1. read what the firmware is commanding: heater power and throttle-valve
#      opening (TIM3 PWM duties) and the two gas flows (DAC outputs)
#   2. advance the chamber temperature and pressure by 100 ms
#   3. feed the new values into the ADC, like real sensors would
# The firmware is not changed at all: it sees an ADC that reports temperature
# and pressure, exactly as on real hardware. This is hardware-in-the-loop
# simulation.
#
# Temperature (Step 10) - one heat capacity, heat loss to the room:
#   dT/dt = ( K * heater%  -  (T - T_AMBIENT) ) / TAU
#
# Pressure (Step 12) - a vacuum chamber with a pump behind a throttle valve:
#   gas flowing in raises the pressure, the pump removes gas faster the more
#   the valve is open, and a tiny leak always lets some air in:
#   dP/dt = ( A * total_flow  -  valve * (P - P_BASE) ) / TAU_P  +  leak
#   Steady state: P = P_BASE + A * flow / valve   (close the valve -> P rises)
#
# To simulate a vacuum leak (the recipe's leak check should then fail), type
# in the Renode monitor:     python "import sys; sys.chamber_leak(10)"
# and to repair it:          python "import sys; sys.chamber_leak(0.2)"
# (the number is the leak rate in milli-Torr per second)

import sys

T_AMBIENT = 25.0      # room temperature, C
K         = 6.0       # C per % of heater power  -> 100 % settles at 625 C
TAU       = 30.0      # seconds
DT        = 0.1       # seconds between ADC scans

P_START   = 760.0     # Torr: the chamber starts vented to atmosphere
P_BASE    = 0.005     # Torr: best vacuum the pump can reach
A         = 0.0013    # Torr per sccm with the valve fully open
TAU_P     = 2.0       # seconds, with the valve fully open
LEAK      = 0.2       # mTorr per second: a normal, tight chamber

TIM3_ARR  = 0x4000042C   # PWM period register
TIM3_CCR1 = 0x40000434   # heater duty (channel 1)
TIM3_CCR2 = 0x40000438   # throttle-valve duty (channel 2)
DAC_DOR1  = 0x4000742C   # MFC1 (SiH4) setpoint output, 0-4095 = 0-100 sccm
DAC_DOR2  = 0x40007430   # MFC2 (N2O)  setpoint output, 0-4095 = 0-1000 sccm
SWSTART   = 1 << 30      # bit in ADC1_CR2 meaning "start a scan"
ADC_TEMP_CHANNEL     = 14
ADC_PRESSURE_CHANNEL = 15

def _state():
    return sys.__dict__.setdefault('chamber_state',
                                   {'T': T_AMBIENT, 'P': P_START, 'leak': LEAK})

def chamber_leak(mtorr_per_s):
    _state()['leak'] = float(mtorr_per_s)

def chamber_step(bus, mon, value):
    if not (value & SWSTART):
        return                                    # not a scan start: ignore
    s = _state()

    period = bus.ReadDoubleWord(TIM3_ARR) + 1
    if period <= 1:
        return                                    # PWM not set up yet
    heater = min(bus.ReadDoubleWord(TIM3_CCR1), period) / float(period)   # 0..1
    valve  = min(bus.ReadDoubleWord(TIM3_CCR2), period) / float(period)   # 0..1
    flow   = (bus.ReadDoubleWord(DAC_DOR1) & 0xFFF) * 100.0 / 4095.0 \
           + (bus.ReadDoubleWord(DAC_DOR2) & 0xFFF) * 1000.0 / 4095.0   # sccm

    # Temperature
    T = s['T']
    T += DT / TAU * (K * heater * 100.0 - (T - T_AMBIENT))
    s['T'] = T

    # Pressure
    P = s['P']
    P += DT * ((A * flow - valve * (P - P_BASE)) / TAU_P + s['leak'] / 1000.0)
    P = max(P_BASE, min(P_START, P))
    s['P'] = P

    raw_t = int(round(T * 4095.0 / 800.0))        # 0-800 C   -> 0-4095 counts
    raw_p = int(round(P * 4095.0 / 10.0))         # 0-10 Torr -> 0-4095 counts
    mon.Parse("sysbus.adc1 FeedSample %d %d -1" % (max(0, min(4095, raw_t)), ADC_TEMP_CHANNEL))
    mon.Parse("sysbus.adc1 FeedSample %d %d -1" % (max(0, min(4095, raw_p)), ADC_PRESSURE_CHANNEL))

sys.chamber_step = chamber_step
sys.chamber_leak = chamber_leak
