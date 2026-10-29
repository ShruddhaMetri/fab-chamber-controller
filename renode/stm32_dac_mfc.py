# stm32_dac_mfc.py
# Renode peripheral model: the STM32F4 DAC (registers at 0x40007400),
# plus two simulated mass flow controllers (MFCs).
#
# Renode has no STM32 DAC model, so this script provides one:
#   - firmware writes a 12-bit value to DHR12R1 / DHR12R2 (the DAC inputs)
#   - the model copies it to DOR1 / DOR2 (the DAC outputs), as the real DAC does
#   - each MFC "follows" its setpoint: the model feeds the same value into the
#     ADC flow-readback channel (MFC1 -> ADC channel 12, MFC2 -> channel 13)
# To simulate a blocked or failed MFC, override the readback by hand, e.g.
#   sysbus.adc1 FeedSample 0 12 -1

CR, DHR12R1, DHR12R2, DHR12RD, DOR1, DOR2 = 0x00, 0x08, 0x14, 0x20, 0x2C, 0x30
EN1, EN2 = 1 << 0, 1 << 16

if request.IsInit:
    regs = {}

elif request.IsRead:
    request.Value = regs.get(request.Offset, 0)

elif request.IsWrite:
    off = request.Offset
    val = request.Value
    regs[off] = val

    def output(dor, value, adc_channel, enable_bit):
        value = value & 0xFFF
        if not (regs.get(CR, 0) & enable_bit):
            value = 0                        # channel disabled: no output
        regs[dor] = value
        monitor.Parse("sysbus.adc1 FeedSample %d %d -1" % (value, adc_channel))

    if off == DHR12R1:
        output(DOR1, val, 12, EN1)
    elif off == DHR12R2:
        output(DOR2, val, 13, EN2)
    elif off == DHR12RD:                     # both channels in one write
        output(DOR1, val, 12, EN1)
        output(DOR2, val >> 16, 13, EN2)
