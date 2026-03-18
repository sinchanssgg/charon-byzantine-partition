# Charon: Byzantine Partition Detection Under Churn

A C++ implementation of the **Charon** algorithm from the paper:

> *"Charon: Byzantine Partition Detection Under Churn"* 

Charon is the first strictly-stabilizing Byzantine fault-tolerant algorithm for network partition detection under continuous churn. It enables correct nodes to determine whether the network remains sufficiently connected to resist partitioning despite up to `t` Byzantine nodes and up to `c` nodes joining or leaving each round.

---

## Overview

Distributed systems operating over dynamic networks face three fundamental challenges that Charon addresses simultaneously:

- **Phantom connectivity** — stale evidence about departed nodes falsely suggesting communication paths still exist
- **Arbitrary initial state** — nodes may start from corrupted state due to transient faults, crashes, or adversarial interference
- **Permanent Byzantine presence** — Byzantine nodes continuously inject misleading information

Charon solves these using three mechanisms:

1. **Presence beacons** — signed per-round liveness broadcasts that automatically evict departed nodes from connectivity records
2. **Edge tokens** — certified link records with expiring freshness certificates, discarded when endpoints depart, eliminating phantom connectivity
3. **Anchor-based adaptive flooding** — relay bounded by locally observed stable membership, requiring no global knowledge of `n`

### Guarantees

- Achieves `(t+c, t)`-strict stabilization
- Every `(t+c)`-safe node converges to correct output within `O(t + c + δmax)` rounds from any initial state
- Freshness horizon: `F = 4(t+c) + 2 + δmax`
- Stabilization window: `W = 4(t+c) + 2`
- Accuracy threshold: `κ(G*) > 2(t+c)`

---

## Key Concepts

| Term | Meaning |
|---|---|
| `t` | Maximum number of Byzantine nodes per round |
| `c` | Maximum churn (join/leave events) per round |
| `δmax` | Upper bound on stable subgraph diameter |
| `F` | Freshness horizon — how old info can be before expiry |
| `W` | Stabilization window — rounds to recover from corrupt state |
| `κ(G*)` | Vertex connectivity of the stable subgraph |
| `ρ = t+c` | Containment radius — Byzantine influence radius |
| Safe node | A correct node more than `ρ` hops from any Byzantine node |
| Lost node | A correct node within `ρ` hops of a Byzantine node |
| Beacon | Signed per-round liveness broadcast `β_i^r = (i, r, σ_i(i‖r))` |
| Token | Certified edge record `τ = (u, v, π_{u,v}, β_u, β_v, d, S)` |

---

## Project Structure
```
charon-byzantine-partition/
├── CMakeLists.txt                  # Top-level build system
├── README.md                       # This file
├── src/
│   ├── charon/
│   │   ├── messages.h              # Beacon and Token structs
│   │   ├── node.h                  # CharonNode class declaration
│   │   ├── node.cpp                # Core algorithm implementation
│   │   ├── serialization.h         # Wire format for OMNeT++ messages
│   │   ├── CharonNodeModule.h      # OMNeT++ module declaration
│   │   └── CharonNodeModule.cpp    # OMNeT++ module implementation
│   ├── crypto/
│   │   ├── crypto.h                # ECDSA signing interface
│   │   └── crypto.cpp              # OpenSSL ECDSA implementation
│   └── topology/
│       ├── RippleLoader.h          # Ripple topology loader declaration
│       ├── RippleLoader.cpp        # CSV loader, churn, NED generator
│       └── generate_ripple_ned.cpp # Standalone NED generator tool
├── simulations/
│   ├── data/                       # Topology CSV files go here
│   └── scenarios/
│       ├── CharonNetwork.ned       # OMNeT++ network description
│       └── omnetpp.ini             # Simulation configurations
├── docker/
│   ├── node/
│   │   └── Dockerfile              # Container image for simulation
│   └── docker-compose.yml          # Multi-scenario orchestration
├── tests/                          # Unit tests
└── scripts/                        # Helper scripts
```

---

### Installing dependencies on Ubuntu/Debian
```bash
# Compiler and build tools
sudo apt-get update
sudo apt-get install -y \
    build-essential g++ cmake \
    libssl-dev \
    git \
    wget bison flex perl python3 \
    qtbase5-dev libqt5opengl5-dev \
    libxml2-dev zlib1g-dev

# Docker
sudo apt-get install -y docker.io docker-compose
sudo usermod -aG docker $USER
newgrp docker
```

### Installing OMNeT++
```bash
# Download OMNeT++ 6.x
wget https://github.com/omnetpp/omnetpp/releases/download/omnetpp-6.0.3/omnetpp-6.0.3-linux-x86_64.tgz
tar xfz omnetpp-6.0.3-linux-x86_64.tgz
cd omnetpp-6.0.3

# Configure and build (headless — no GUI required)
source setenv
./configure WITH_QTENV=no WITH_OSG=no
make -j$(nproc)

# Add to PATH permanently
echo 'export PATH="/opt/omnetpp-6.0.3/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

---

## Installation
```bash
# Clone the repository
git clone git@github.com:YOUR_USERNAME/charon-byzantine-partition.git
cd charon-byzantine-partition
```

---

## Building the Project
```bash
# Configure (Debug mode for development)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Configure (Release mode for simulation runs)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build -j$(nproc)
```

If OMNeT++ is not in the default location, pass the path explicitly:
```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DOMNETPP_ROOT=/path/to/your/omnetpp-6.0.3
```

This produces two executables:
```
build/charon_sim            # Main OMNeT++ simulation
build/generate_ripple_ned   # Ripple topology NED generator

### Available simulation configurations

| Config | Nodes | t | c | δmax | Description |
|---|---|---|---|---|---|
| `ER_baseline` | 100 | 5 | 3 | 6 | Small ER graph, quick test |
| `Ripple_subsampled` | 5000 | 50 | 5 | 8 | Ripple payment network |

Results are written to `simulations/results/`.

---

## Running with Docker

### Build and run a single scenario
```bash
docker compose -f docker/docker-compose.yml up --build charon-sim
```

### Run both scenarios in parallel
```bash
docker compose -f docker/docker-compose.yml up --build
```

### Run a specific scenario by name
```bash
SIM_CONFIG=Ripple_subsampled \
    docker compose -f docker/docker-compose.yml up --build charon-sim
```

## Topologies

### Erdős–Rényi (ER) synthetic graphs

Used for controlled experiments with known connectivity properties. Parameters are set in `omnetpp.ini`:
```ini
*.numNodes = 5000
*.t        = 5
*.c        = 3
*.deltaMax = 6
```

### Ripple payment network

A real-world financial network dataset with 67k nodes and 99k edges. Subsampled to 5000 nodes while preserving the original degree distribution.

To use your own topology, provide a CSV file with one edge per line:
```
node_u,node_v
1,2
1,3
2,4
...
```

Then run:
```bash
./build/generate_ripple_ned \
    your_topology.csv \
    simulations/scenarios/YourNetwork.ned \
    <maxNodes> <t> <c> <deltaMax>
```

---

## Byzantine Behaviors

Charon is evaluated against four adversary behaviors, selectable per node in `omnetpp.ini` via the `byzBehavior` parameter:

| ID | Name | Parameter Value | Description |
|---|---|---|---|
| B1 | Silent | `0` | Node transmits nothing — quickly expired from neighbors' beacon stores |
| B2 | Selective forwarding | `1` | Drops ~50% of relayed tokens, slowing convergence without triggering Accept rejection |
| B3 | Stale-beacon replay | `2` | Broadcasts fabricated old-round beacons trying to keep phantom nodes alive in peers' stores |
| B4 | Token flooding | `3` | Floods neighbors with syntactically valid but semantically invalid tokens to exhaust processing |

### Configuring Byzantine nodes in omnetpp.ini
```ini
# Mark specific nodes as Byzantine
*.node[0].byzantine    = true
*.node[0].byzBehavior  = 1    # B2: selective forwarding

*.node[1].byzantine    = true
*.node[1].byzBehavior  = 0    # B1: silent
```

### Adversary placement strategies

- **A1 (worst-case)** — Byzantine nodes placed near the minimum vertex cut of the stable subgraph, maximizing containment impact
- **A2 (degree-proportional)** — Nodes selected with probability proportional to degree, modelling realistic infiltration

The `generate_ripple_ned` tool uses A1 by default (places Byzantine nodes at the highest-degree nodes).

---

## Algorithm Parameters

### Key formulas
```
F = 4(t + c) + 2 + δmax     # Freshness horizon
W = 4(t + c) + 2             # Stabilization window
ρ = t + c                    # Containment radius
```

### Parameter selection guide

| Parameter | How to choose |
|---|---|
| `t` | Upper bound on Byzantine nodes you expect in your network |
| `c` | Maximum join/leave events you expect per round |
| `δmax` | Set to `n-1` for safety, or the 99th percentile of observed path lengths in normal operation |

### Effect on convergence

| Scenario | Convergence time |
|---|---|
| t=5, c=3, δmax=6 | W + δmax = 40 + 6 = 46 rounds |
| t=50, c=5, δmax=8 | W + δmax = 222 + 8 = 230 rounds |

If the actual diameter exceeds `δmax`, tokens may expire before reaching all nodes. This increases convergence delay but does **not** violate correctness — nodes simply output `Partitionable` conservatively until tokens arrive.

---

## License

The repo is private. Please do not share, all right reserved.


