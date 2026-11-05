# fab_host.py
# A simulated FAB HOST: the factory computer that supervises the tool over CAN.
#
# In a real fab, a host PC (or a PLC / gateway) sits on the CAN bus with the
# chamber controller. Here a Python script plays that part inside Renode:
#   - it listens to every frame the firmware transmits on CAN1 and decodes it
#   - it sends a heartbeat every second, so the controller knows it is alive
#   - it can send commands (run a recipe, abort, ping)
#
# Commands it adds to the Renode MONITOR (type them in the monitor window):
#   fabhost         last status reported by the chamber
#   fabevents       the last events (faults, process steps, command replies)
#   fabrun N        ask the chamber to run recipe N (1-4)
#   fababort        ask the chamber to abort the running recipe
#   fabping         check that the chamber answers
#   fablink 0       stop the heartbeat (simulates a broken cable / crashed host)
#   fablink 1       start it again
# (Not "host...": Renode already has a built-in element called "host".)
#
# Frame layout: see firmware/chamber_ctrl/Inc/canlink.h

import sys
import System
from Antmicro.Renode.Core import EmulationManager
from Antmicro.Renode.Core.CAN import CANMessageFrame

STEPS = ["IDLE", "PUMPDOWN", "LEAKCHECK", "HEAT", "STABILIZE", "GAS_ON",
         "DEPOSIT", "PURGE", "COOL", "DONE", "ABORTED"]
EVENTS  = {1: "BOOT", 2: "FAULT CHANGE", 3: "STEP CHANGE"}
CMDS    = {1: "RUN", 2: "ABORT", 3: "PING"}
RESULTS = {0: "OK", 1: "REFUSED", 2: "BAD RECIPE", 3: "UNKNOWN COMMAND"}

def _faults(b):
    names = []
    if b & 0x01: names.append("e-stop")
    if b & 0x02: names.append("door")
    if b & 0x04: names.append("over-temp")
    if b & 0x08: names.append("gas-flow")
    return ", ".join(names) if names else "none"

def _step(n):
    return STEPS[n] if n < len(STEPS) else "?"

def _u16(d, i):
    return d[i] | (d[i + 1] << 8)

class FabHost(object):
    def __init__(self, machine):
        self.machine   = machine
        self.can       = machine["sysbus.can1"]
        self.status    = None        # last 0x100 frame data
        self.process   = None        # last 0x101 frame data
        self.events    = []          # (time, text)
        self.frames    = 0
        self.heartbeat = True
        self.hb_count  = 0
        self.can.FrameSent += self.on_frame     # every frame the firmware transmits

    def now(self):
        try:
            return self.machine.LocalTimeSource.ElapsedVirtualTime.TotalSeconds
        except Exception:
            return 0.0

    def log(self, text):
        self.events.append((self.now(), text))
        self.events = self.events[-20:]
        print("[HOST %7.1f s] %s" % (self.now(), text))

    def send(self, can_id, data):
        frame = CANMessageFrame(System.UInt32(can_id),
                                System.Array[System.Byte]([System.Byte(b) for b in data]),
                                False, False, False, False)
        self.can.OnFrameReceived(frame)          # the frame appears on the controller's bus

    # ---- frames from the chamber ----
    def on_frame(self, f):
        d = list(f.Data)
        self.frames += 1
        if f.Id == 0x080:
            text = "EVENT %s: faults=%s, step=%s" % (EVENTS.get(d[0], "?"), _faults(d[1]), _step(d[2]))
            self.log(text)
        elif f.Id == 0x100:
            self.status = d
            # heartbeat every second = every 2nd status frame
            self.hb_count += 1
            if self.heartbeat and (self.hb_count % 2 == 0):
                self.send(0x210, [self.hb_count & 0xFF])
        elif f.Id == 0x101:
            self.process = d
        elif f.Id == 0x180:
            self.log("REPLY to %s: %s (arg %d)" % (CMDS.get(d[0], "?"), RESULTS.get(d[1], "?"), d[2]))

    # ---- what the monitor commands print ----
    def show(self):
        if self.status is None:
            print("No status received yet. Is the firmware running?")
            return
        s, p = self.status, self.process
        print("Frames received : %d" % self.frames)
        print("Faults          : %s" % _faults(s[0]))
        print("Process step    : %s" % _step(s[1]))
        print("Temperature     : %.1f C" % (_u16(s, 2) / 10.0))
        print("Pressure        : %.3f Torr" % (_u16(s, 4) / 1000.0))
        print("Heater / valve  : %d %% / %d %%" % (s[6], s[7]))
        if p is not None:
            print("SiH4 / N2O      : %d / %d sccm" % (_u16(p, 0), _u16(p, 2)))
            print("Deposit left    : %d s" % _u16(p, 4))
            print("Alive counter   : %d" % p[6])
            print("Controller sees host: %s" % ("UP" if p[7] else "DOWN"))
        print("Host heartbeat  : %s" % ("ON" if self.heartbeat else "OFF"))

    def show_events(self):
        if not self.events:
            print("No events yet.")
        for t, text in self.events:
            print("[%7.1f s] %s" % (t, text))

# Create the host for the first machine and remember it in 'sys', so that the
# monitor commands below (and a later 'include') can find it.
sys.fab_host = FabHost(list(EmulationManager.Instance.CurrentEmulation.Machines)[0])

# ---- Renode monitor commands: a function named mc_xxx becomes command "xxx" ----
def mc_fabhost():
    sys.fab_host.show()

def mc_fabevents():
    sys.fab_host.show_events()

def mc_fabrun(slot):
    sys.fab_host.send(0x200, [1, int(slot)])
    print("Sent RUN %d (0x200). Reply will appear in 'fabevents'." % int(slot))

def mc_fababort():
    sys.fab_host.send(0x200, [2, 0])
    print("Sent ABORT (0x200).")

def mc_fabping():
    sys.fab_host.send(0x200, [3, 0x5A])
    print("Sent PING (0x200).")

def mc_fablink(on):
    sys.fab_host.heartbeat = bool(int(on))
    print("Host heartbeat %s." % ("ON" if sys.fab_host.heartbeat else "OFF"))
