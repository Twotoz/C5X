# C5X

**Can a single ESP32-C5 be turned into a self-contained 3D RF scanner?**

C5X is an experimental research project exploring whether the ESP32-C5 can be pushed far beyond normal Wi-Fi use and used as a low-cost software-defined RF imaging platform.

The long-term idea is intentionally ambitious:

> **one ESP32-C5 → RF measurements → self-localization → 3D voxel / point-cloud reconstruction**

No camera. No LiDAR module. No dedicated radar front-end.

The goal is not photographic imaging. C5X aims for a **coarse probabilistic 3D model** of nearby structure, reflectors and dynamic targets using radio-frequency measurements.

> [!IMPORTANT]
> C5X is very early research. It is **not** a proven through-wall imaging system, life-safety device, or replacement for professional radar, LiDAR, thermal imaging, or search-and-rescue equipment.

---

## Core idea

A conventional LiDAR directly measures optical reflections and converts them into XYZ points.

C5X explores a different route:

```text
RF probe
   ↓
environment modifies / reflects RF
   ↓
complex receive-domain measurements
   ↓
calibration + interference cancellation
   ↓
frequency-domain / spatial reconstruction
   ↓
3D probability volume
   ↓
voxel grid / point cloud
```

The displayed points would not be literal laser returns.

Instead, each point or voxel represents **RF evidence that something is likely present at that position**.

---

## The real target: single-C5 active scanning

The primary research goal is a **single ESP32-C5 operating as both the RF source and sensing platform**.

Conceptually:

```text
                  object / wall
                      █
                     ↙
 C5X  )))))))))))))))))
  ↑ TX                 ↘ reflection
  │                      ↘
  └──── receive I/Q ? ─────┘
```

If the C5 receive chain remains observable while the transmitter is active, the chip may be usable as a crude monostatic RF sensing platform.

That is the first major question C5X must answer.

### Why this is interesting

Normal ESP32 Wi-Fi sensing typically works with:

- another Wi-Fi transmitter
- CSI from received packets
- multiple nodes
- known APs

C5X wants to investigate something harder:

> Can the C5 create its **own RF illumination**, observe how the environment changes it, and reconstruct spatial information without requiring another radio?

---

## What we already know

Work on [C5VRX](https://github.com/Twotoz/C5VRX) has shown that the ESP32-C5 receive path exposes much more useful low-level RF information than ordinary Wi-Fi APIs suggest.

On tested ESP32-C5 hardware, the modem diagnostic path has been used to expose live receive-domain I/Q information and feed it into a continuous hardware pipeline.

That makes C5X especially interesting because the project is not limited to normal packet-level CSI.

Possible sensing inputs include:

- receive-domain I/Q
- amplitude
- relative phase
- CSI
- RSSI
- channel / center frequency
- time
- scanner motion
- repeated observations across frequency

---

## First make-or-break experiment

Before building any 3D reconstruction, C5X needs to answer one question:

> **Can useful receive-domain RF information still be observed while the same ESP32-C5 is transmitting?**

The initial experiment is deliberately simple.

```text
1. capture RX I/Q with TX off
2. enable a controlled TX signal
3. capture RX I/Q again
4. place a strong reflector nearby
5. move the reflector to several known distances
6. check whether the complex measurements change reproducibly
```

Example test scene:

```text
 C5X                            metal plate
  ●  ))))))))))))))))))))))))      ███
                                   ↑
                              move 0.5 m
                              move 1.0 m
                              move 2.0 m
```

If reflector position produces repeatable information in the captured signal, C5X has the basis for active RF ranging experiments.

If the receive path is completely disconnected or saturated during TX, the active single-C5 architecture will need a different PHY-level approach.

---

## Self-interference is not only a problem

A single-radio transmitter/receiver will see a huge direct component from its own transmission.

Conceptually:

```text
RX = direct TX leakage + environment reflections + noise
```

C5X will investigate whether the direct component can become a useful **phase and amplitude reference**.

A baseline estimate can be learned:

```text
RX_live - estimated_direct_path = reflection residual
```

More advanced versions may use complex adaptive cancellation rather than simple subtraction.

Possible approaches:

- complex baseline subtraction
- adaptive LMS / NLMS cancellation
- slow direct-path tracking
- normalization against the direct leakage component
- static-clutter cancellation
- repeated coherent averaging

The important idea is that the strongest unwanted signal may also provide the reference required to stabilize measurements.

---

## Synthetic bandwidth

A single 20 or 40 MHz Wi-Fi channel provides poor classical radar range resolution.

C5X therefore aims to investigate **frequency stitching**.

Instead of treating each channel independently:

```text
f1 → complex response
f2 → complex response
f3 → complex response
...
fn → complex response
```

we attempt to build one synthetic frequency response:

```text
H(f)
```

If measurements across a sufficiently wide frequency span can be phase-aligned, an inverse transform can produce a delay-like profile:

```text
H(f)
 ↓
calibration
 ↓
frequency stitching
 ↓
IFFT / matched reconstruction
 ↓
range-like response
```

The challenge is maintaining or recovering relative phase when the RF synthesizer retunes.

A strong direct TX leakage path may be useful as a per-frequency phase reference.

---

## From range to 3D

Range alone is not enough for imaging.

C5X therefore uses another idea: **synthetic aperture sensing**.

Move the same C5 through space and take repeated measurements:

```text
●──●──●──●──●──●──●
p0 p1 p2 p3 p4 p5 p6
```

Each location becomes another virtual antenna position.

The resulting dataset becomes approximately:

```text
H(f, position, time)
```

For every candidate voxel in the scene, C5X can then ask:

> If a reflector existed here, would the measured phase / delay evolution across frequency and scanner positions agree with that hypothesis?

Measurements that align coherently increase the probability of that voxel.

Conceptually:

```text
frequency diversity
        +
scanner movement
        ↓
synthetic RF aperture
        ↓
backprojection / inverse reconstruction
        ↓
3D probability volume
        ↓
point cloud
```

---

## SelfLoc: RF-only motion estimation

The long-term goal is for C5X to estimate its own movement from RF instead of requiring an external positioning module.

This becomes an RF-SLAM-like problem:

```text
unknown scanner trajectory
          +
unknown environment
          ↓
find the combination that best explains
the measured RF data
```

A useful idea is **autofocus**.

If the assumed scanner trajectory is wrong, static reflectors blur in the reconstructed image.

C5X can iteratively adjust the estimated trajectory to maximize spatial coherence / image sharpness.

```text
initial trajectory guess
        ↓
reconstruct scene
        ↓
measure coherence / sharpness
        ↓
adjust trajectory
        ↓
repeat
```

Early experiments will still use known scanner positions because that lets us validate the RF physics before solving motion estimation at the same time.

---

## Two operating modes

### Active mode — primary research target

One C5 attempts to generate its own RF probe and observe environment-dependent receive-domain data.

```text
C5X TX
  ↓
room / object
  ↓
C5X RX
  ↓
RF reconstruction
```

This is the most experimental mode and the core C5X research challenge.

### Ambient mode — complementary mode

C5X can also use existing Wi-Fi transmissions as ambient RF illumination.

Possible sources include:

- access points
- routers
- phones
- laptops
- IoT devices
- other nearby Wi-Fi transmitters

Network credentials are not inherently required for passive RF observation.

Ambient mode may provide extra viewpoints and diversity even if active mode becomes the primary scanner.

---

## Reconstruction pipeline

A possible future pipeline:

```text
raw RX-domain I/Q / CSI
          ↓
sample validation
          ↓
DC / IQ correction
          ↓
direct-path estimation
          ↓
self-interference cancellation
          ↓
phase normalization
          ↓
multi-frequency response H(f)
          ↓
frequency stitching
          ↓
range / delay reconstruction
          ↓
synthetic-aperture accumulation
          ↓
trajectory refinement / autofocus
          ↓
3D voxel probability field
          ↓
static / dynamic separation
          ↓
point cloud / simplified mesh
```

The phone or computer can perform the heavy reconstruction while the ESP32-C5 focuses on RF acquisition.

---

## Point cloud vs voxels

C5X will likely reconstruct a **voxel probability grid** internally.

Each voxel may eventually contain values such as:

```text
occupancy probability
reflection strength
static confidence
dynamic confidence
measurement count
uncertainty
```

The viewer can render sufficiently confident voxels as a point cloud.

Later versions may estimate surfaces or simplified meshes from stable voxel clusters.

---

## Dynamic targets

Once a static scene model exists, temporal changes become especially interesting.

```text
live RF scene - static RF scene = dynamic component
```

Possible future experiments include:

- moving-person detection
- motion direction
- stationary-vs-moving separation
- possible human / animal classification
- periodic micro-motion research

These features are later-stage research. They should not be confused with reliable life detection.

---

## Development roadmap

### R0 — TX/RX overlap probe

- [ ] Port the proven receive-domain I/Q capture concepts to C5X
- [ ] Generate a controlled C5 RF transmission
- [ ] Capture RX-domain data during TX
- [ ] Check for saturation / RX disable behaviour
- [ ] Compare TX-off vs TX-on captures
- [ ] Move a strong reflector through known positions
- [ ] Determine whether the measurements change reproducibly

**Success criterion:** the environment measurably affects receive-domain data while C5X generates its own RF.

### R1 — self-interference cancellation

- [ ] Characterize the direct TX leakage component
- [ ] Build complex baseline subtraction
- [ ] Test adaptive cancellation
- [ ] Measure residual noise floor
- [ ] Determine usable dynamic range

### R2 — reflector sensing

- [ ] Detect a metal plate
- [ ] Detect large static objects
- [ ] Compare reflector distances
- [ ] Compare TX power levels
- [ ] Repeatability testing

### R3 — frequency sweep

- [ ] Capture complex response across multiple 5 GHz frequencies
- [ ] Characterize retune phase offsets
- [ ] Test direct-path phase normalization
- [ ] Build a stitched frequency response
- [ ] Measure effective synthetic bandwidth

### R4 — 1D range-like reconstruction

- [ ] Transform stitched frequency response into delay space
- [ ] Compare predicted vs measured reflector distance
- [ ] Characterize multipath
- [ ] Measure practical resolution
- [ ] Measure stability across repeated scans

### R5 — 2D synthetic aperture

- [ ] Move C5X along known positions
- [ ] Capture a frequency response at every position
- [ ] Implement backprojection
- [ ] Reconstruct one or more strong reflectors
- [ ] Test wall / plate imaging

### R6 — 3D C5X

- [ ] Multi-line synthetic aperture capture
- [ ] 3D voxel reconstruction
- [ ] Confidence / uncertainty model
- [ ] Point-cloud renderer
- [ ] Simplified surface reconstruction

### R7 — SelfLoc

- [ ] Estimate relative movement from RF
- [ ] Joint trajectory / scene optimization
- [ ] RF autofocus
- [ ] Reduce dependence on known scan positions

### R8 — dynamic scene sensing

- [ ] Static background model
- [ ] Moving target isolation
- [ ] Target tracking
- [ ] Experimental human / animal classification

### R9 — mobile viewer

- [ ] Wi-Fi data transport
- [ ] BLE discovery / setup
- [ ] Live point-cloud visualization
- [ ] Scan quality / confidence display
- [ ] Portable handheld workflow

---

## What success looks like

C5X does **not** need to produce a beautiful room scan on day one.

The project should progress through small physical proofs:

```text
RX data changes with reflector
          ↓
reflector can be separated from baseline
          ↓
distance affects reconstructed response
          ↓
multiple scanner positions sharpen localization
          ↓
2D reflector image
          ↓
3D voxel cloud
          ↓
self-localized scan
```

The first major win is simply:

> **Move a metal plate and prove that a single transmitting C5 can measure a repeatable environment-dependent complex RF change.**

Everything else builds from that.

---

## Important limitations

C5X is pushing hardware well outside its intended Wi-Fi use.

Major unknowns include:

- whether RX remains meaningfully observable during TX
- TX-to-RX isolation
- receiver saturation
- dynamic range after self-interference cancellation
- LO / PLL phase behaviour during retuning
- coherence across frequency steps
- antenna pattern and direct coupling
- multipath ambiguity
- effective usable RF bandwidth
- scan-position estimation
- regulatory limits on generated RF
- undocumented PHY behaviour

A visually convincing reconstruction does **not** prove physical accuracy.

C5X should preserve and display uncertainty rather than hiding it.

---

## Why C5X?

Dedicated radar and imaging hardware can solve many of these problems more directly.

C5X asks a different question:

> **How much spatial RF information can be extracted from one extremely cheap Wi-Fi SoC if we exploit the hardware aggressively enough?**

The ESP32-C5 is interesting because it combines:

- 2.4 GHz + 5 GHz RF
- low cost
- accessible Wi-Fi PHY features
- CSI
- programmable digital peripherals
- enough compute for acquisition and filtering
- Wi-Fi / BLE connectivity
- a large ESP-IDF ecosystem
- previous evidence that low-level receive-domain data can be exposed

The goal is not to beat dedicated radar hardware.

The goal is to discover how far the C5 can be pushed.

---

## Safety, privacy and RF regulations

C5X is an experimental research platform.

Do not rely on C5X to determine whether a room, building or area is safe, occupied or unoccupied.

RF sensing may reveal activity without optical imaging and therefore has privacy implications.

Active RF experiments must comply with applicable radio regulations, channel restrictions and transmit-power limits.

---

## Project status

**Very early feasibility research.**

The current priority is **not 3D rendering**.

The current priority is proving whether a single ESP32-C5 can generate an RF signal while exposing useful environment-dependent receive-domain information at the same time.

That experiment determines the direction of the entire project.

Expect failed experiments, PHY reverse-engineering and major architectural changes.

---

## Contributing

C5X is especially interested in contributors with experience in:

- ESP32-C5 PHY / RF internals
- software-defined radio
- radar
- RF self-interference cancellation
- Wi-Fi CSI
- DSP
- coherent frequency sweeps
- synthetic aperture radar
- RF tomography
- SLAM / autofocus
- inverse problems
- point-cloud / voxel reconstruction

If you have relevant measurements, PHY findings or hardware experiments, open an issue.

---

## Name

**C5X**

**C5** — ESP32-C5  
**X** — experimental sensing beyond normal Wi-Fi use

---

## Related project

[C5VRX](https://github.com/Twotoz/C5VRX) explores low-level ESP32-C5 RF receive processing for analog FPV video and provides useful prior work for C5X RF access experiments.

---

## License

A license has not been selected yet.
