# R0 hardware test — single-C5 TX/RX overlap

R0 answers one question:

> Does the ESP32-C5 expose useful receive-domain Q4/I4 information while its own Wi-Fi transmitter is active?

It does **not** attempt ranging or 3D reconstruction yet.

## Hardware

Current pin mapping and MODEM_DIAG routing target the **Seeed Studio XIAO ESP32-C5**, matching the mapping physically validated in C5VRX.

No external RF module is required.

## What the firmware records

Every boot creates two bounded records in the `c5xcap` flash partition:

1. **BASE** — receive-domain Q4/I4 with no intentional local TX burst.
2. **TX** — the same capture while C5X queues broadcast 802.11 Action frames.

Each record contains:

- 40 MS/s packed Q4/I4 samples (32 KiB / ~819 us by default)
- before/after modem dump registers
- capture status
- hash and simple signal statistics
- TX queue success counts

The experiment uses Wi-Fi channel 36 / 5 GHz, a fixed legacy 802.11a 6 Mbps OFDM raw-TX rate, and deliberately low TX power.

## Build / flash

ESP-IDF v6.0.2 is the CI reference.

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

The firmware waits three seconds and then runs once.

## Read the capture partition

After the firmware prints `R0 COMPLETE`, exit the monitor and read the raw partition:

```bash
parttool.py --port <PORT> read_partition \
  --partition-name c5xcap \
  --output c5xcap.bin
```

Then:

```bash
python tools/analyze_capture.py c5xcap.bin
```

## Physical experiment series

Do not judge the idea from one capture.

Save a separate partition image for each scene:

```text
empty-room.bin
plate-0.25m.bin
plate-0.50m.bin
plate-1.00m.bin
plate-2.00m.bin
person-near.bin
person-far.bin
```

For the first run, use a large metal plate or baking tray because it is a strong 5 GHz reflector.

Keep all of these fixed between runs:

- C5X position
- orientation
- antenna environment
- nearby people
- USB cable routing
- channel / firmware
- power supply

Only move the reflector.

Then analyze all runs:

```bash
python tools/analyze_capture.py \
  empty-room.bin \
  plate-0.25m.bin \
  plate-0.50m.bin \
  plate-1.00m.bin
```

## What counts as progress

### Good result

- BASE capture fills normally.
- TX frames actually queue.
- TX capture still contains changing Q4/I4 rather than only a fixed/sentinel pattern.
- Repeating the same scene gives similar statistics.
- Moving the reflector changes the TX-domain data reproducibly.

That would justify deeper work on self-interference cancellation and coherent phase analysis.

### Useful negative result

If TX causes:

- the MODEM_DIAG clock to stop,
- PARLIO to time out,
- all samples to clamp,
- or the dump engine to transition immediately,

that is still valuable.

The header stores the relevant register state specifically so we can identify *where* the path fails.

## Do not claim yet

R0 cannot prove:

- monostatic radar operation
- target range
- coherent TX/RX phase
- 3D imaging
- through-wall detection
- human detection

Those require later milestones.
