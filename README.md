# C5X

**Experimental low-cost RF spatial sensing with the ESP32-C5.**

C5X explores whether inexpensive ESP32-C5 hardware can be used to build a coarse real-time 3D representation of a room using radio signals instead of cameras.

The idea is simple:

> transmit RF → measure how the environment changes the signal → combine measurements from multiple viewpoints → reconstruct a probabilistic 3D model.

Rather than trying to create photographic images, C5X aims to produce a **coarse RF point cloud / voxel map** showing likely structure, free space and moving or human-like targets.

> [!IMPORTANT]
> C5X is an early research project. It is **not** a proven through-wall imaging system, life-safety device, or replacement for professional radar, LiDAR, thermal imaging, or search-and-rescue equipment.

---

## Goal

The long-term goal is a cheap portable scanner that can create a simplified live 3D representation such as:

- probable room structure
- large static obstacles
- free / occupied regions
- moving targets
- possible human or animal presence
- target movement over time

The result could be rendered on a phone, tablet or computer as a point cloud, voxel grid or simplified mesh.

Possible future use cases include robotics, room occupancy sensing, research, indoor mapping and experimental search-and-rescue sensing.

---

## Why ESP32-C5?

The ESP32-C5 is interesting for RF sensing because it combines low cost with:

- 2.4 GHz and 5 GHz Wi-Fi
- Wi-Fi Channel State Information (CSI)
- access to per-subcarrier complex measurements
- configurable Wi-Fi bandwidth and channels
- enough processing power for local filtering and acquisition
- Wi-Fi / BLE connectivity
- a large existing ESP-IDF ecosystem

C5X initially focuses on the **official CSI path** rather than relying on undocumented RF behaviour.

---

## Concept

A single RF link tells us only a limited amount about the environment.

C5X instead aims to combine many measurements across:

- multiple ESP32-C5 nodes
- multiple transmitter / receiver geometries
- multiple Wi-Fi channels
- amplitude and phase
- time
- motion / Doppler-like changes
- scanner movement
- background measurements

Conceptually:

```text
          C5 node
             \
              \
 C5 node ---> ROOM / TARGET ---> C5 node
              /
             /
          C5 node

              |
              v

     CSI amplitude + phase
              |
              v
      calibration/filtering
              |
              v
       multi-link fusion
              |
              v
      3D probability volume
              |
              v
    point cloud / voxel model
              |
              v
        phone / computer
```

The displayed points are **not literal LiDAR returns**. They represent locations where the reconstruction model estimates a higher probability of structure or a target being present.

---

## Proposed architecture

### Sensor nodes

ESP32-C5 nodes capture RF measurements and perform lightweight preprocessing.

Possible measurements include:

- CSI I/Q values
- amplitude
- phase
- RSSI
- channel / bandwidth
- timestamps
- node identity
- packet sequence
- calibration state

### Coordinator

A coordinator collects synchronized measurements from the sensing nodes and forwards them to the host.

For early prototypes this may simply be another ESP or a computer.

### Host

The phone / computer performs the heavier processing:

```text
raw CSI
   ↓
sanity checks
   ↓
phase / amplitude calibration
   ↓
static background estimation
   ↓
temporal filtering
   ↓
multi-node fusion
   ↓
3D voxel probability map
   ↓
tracking / classification
   ↓
point cloud or mesh rendering
```

Keeping the reconstruction on the host lets the sensing hardware remain cheap.

---

## Static vs dynamic sensing

A useful part of the project is separating persistent RF structure from changing RF structure.

### Static layer

Slowly changing features may contain information about:

- walls
- large obstacles
- openings
- strong reflectors
- general room geometry

### Dynamic layer

Short-term changes may contain information about:

- people
- animals
- moving objects
- direction of motion
- small periodic movement

A basic model can be thought of as:

```text
live RF environment - learned background = dynamic RF component
```

This is intentionally simplified; real indoor RF propagation contains significant multipath and ambiguity.

---

## Point cloud vs voxels

Internally, C5X will likely use a **voxel probability grid** rather than immediately trying to reconstruct surfaces.

Each voxel can store values such as:

```text
occupancy probability
static confidence
dynamic confidence
target confidence
measurement count
uncertainty
```

The UI can then render high-confidence voxels as points.

Later, a mesh may be estimated from sufficiently stable regions.

---

## Connectivity

The intended setup is:

- **Wi-Fi** for high-rate measurement transport
- **BLE** for discovery, setup and low-rate control
- phone / tablet / PC for reconstruction and visualization

A future node may use a dedicated communication MCU so the sensing C5 can spend more radio time on acquisition.

---

## Development roadmap

### Phase 0 — RF data capture

- [ ] Basic ESP32-C5 CSI receiver
- [ ] Stable timestamped binary capture format
- [ ] Live CSI visualizer
- [ ] Record datasets to disk
- [ ] Empty-room baseline measurements
- [ ] Repeatability tests

### Phase 1 — Presence and motion

- [ ] Static background subtraction
- [ ] Human presence experiments
- [ ] Motion detection
- [ ] Compare 2.4 GHz vs 5 GHz
- [ ] Compare HT20 vs HT40
- [ ] Test different channels and geometries

### Phase 2 — Multi-node sensing

- [ ] Synchronize multiple C5 receivers
- [ ] Combine multiple Tx/Rx links
- [ ] Measure phase stability
- [ ] Calibrate node-to-node offsets
- [ ] Build a 2D probability map

### Phase 3 — C5X 3D

- [ ] 3D voxel reconstruction
- [ ] Temporal accumulation
- [ ] Point-cloud renderer
- [ ] Scanner pose / movement integration
- [ ] Basic room-structure estimation
- [ ] Dynamic target tracking

### Phase 4 — Experimental classification

- [ ] Human-like target detection
- [ ] Animal / object experiments
- [ ] Stationary-target experiments
- [ ] Confidence / uncertainty model
- [ ] Optional ML-based reconstruction

---

## First hardware target

The initial setup will likely use **3–4 ESP32-C5 boards**:

```text
              TX
               ●
              / \
             /   \
            /     \
        target / room
          /           \
         ●             ●
       RX A           RX B
           \         /
            \       /
              ●
            RX C
```

The goal of the first prototype is **not** a complete room scan.

Success would already mean:

1. repeatable CSI capture
2. reliable background / motion separation
3. rough spatial localization from multiple RF links
4. a live visual representation of that estimate

---

## Important limitations

Indoor RF sensing is difficult.

C5X has to deal with:

- severe multipath
- phase offsets and drift
- limited instantaneous Wi-Fi bandwidth
- antenna pattern differences
- RF interference
- wall-material differences
- moving clutter
- clock synchronization
- ambiguous reflections
- position-dependent measurements

A visually clean 3D model does **not** automatically mean the reconstruction is physically correct.

For this reason, C5X should expose uncertainty rather than hiding it.

---

## Safety and privacy

C5X is intended as an experimental research platform.

Do not rely on it to determine whether a building, room or area is safe or unoccupied.

RF-based presence sensing can also have privacy implications because it may detect activity without an optical camera. Use the technology responsibly and comply with applicable laws and radio regulations.

---

## Project status

**Very early research / proof of concept.**

The first milestone is simply to collect clean, synchronized ESP32-C5 CSI data and determine how much useful spatial information can realistically be extracted from cheap hardware.

Expect experiments, dead ends and major architectural changes.

---

## Contributing

Ideas, experiments, measurements and pull requests are welcome.

Especially useful areas:

- ESP32-C5 CSI / PHY research
- synchronization
- RF calibration
- signal processing
- multi-static sensing
- tomography
- SLAM
- point-cloud / voxel reconstruction
- mobile visualization
- machine learning for RF sensing

If you have RF / radar / Wi-Fi sensing experience, open an issue and share what you know.

---

## Name

**C5X**

**C5** — ESP32-C5  
**X** — experimental sensing beyond normal Wi-Fi use

---

## License

A license has not been selected yet.
