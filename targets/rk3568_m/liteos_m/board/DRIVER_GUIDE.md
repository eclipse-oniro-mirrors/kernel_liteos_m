# RK3568 AMP slave-side CAN / NIC drivers - usage guide

## 1. what the slave drivers are

`can/can_rk3568.c` and `net/net_dwmac.c` are protocol-agnostic frame-level
data planes running on the AMP slave core (CPU3, liteOS-M, aArch64 EL1):

- **CAN** (`can.h`): classic CAN 2.0B + optional CAN FD, standard and
  extended ids, data and remote frames, acceptance filters, IRQ / POLL /
  HYBRID receive modes.
- **NIC** (`net.h`): raw ethernet frame send/receive over GMAC1 (RGMII),
  promiscuous or MAC-filtered receive, IRQ / POLL / HYBRID modes, PHY link
  query (`NetGetLinkInfo` reads the PHY over MDIO on demand).

both drivers touch ONLY their controller's registers plus the slave's own
RAM. they have **no runtime handshake with the linux master** - no shared
control blocks, no publish/subscribe state machine. the hardware
bring-up is done by the master-side kernel modules before the slave
starts, and that ordering is a **deployment constraint** (section 2),
not something the drivers check at run time.

## 2. DEPLOYMENT CONSTRAINT: load the master .ko first

the slave cores run with the peripherals handed over by the linux
master. the hand-over work (clocks, resets, pinmux, PHY) lives in the
master-side kernel modules from `device/board/hihope/rk3568_amp`:

```
# on the linux master (A55 core 0..2), BEFORE the slave runs CAN/net:
insmod mcs_km.ko        # lifecycle: loads/starts/stops the slave image
insmod mcs_can.ko       # CAN hand-over: retargets cLK_CANx to 100MHz and
                        #   holds it on, pulses the CRU resets, applies
                        #   the can1m1 pinmux, hands the SPI over
insmod mcs_net.ko       # GMAC1 hand-over: holds the clocks on, releases
                        #   the CRU reset, applies the RGMII pinmux + GRF
                        #   delays, brings the PHY up (aneg or forced)
```

why this is not optional:

- **CAN**: with `mcs_can.ko` not loaded the port clocks are gated, and
  the FIRST CAN register access from the slave **stalls the AXI bus and
  hangs the slave core**. there is no safe software probe, so the driver
  does not try - it simply assumes the constraint is met.
- **NIC**: same clock story for GMAC1, plus `mcs_net.ko` must **stay
  loaded**: its 1-second link work keeps the RGMII `mac_speed` clock
  tracking the negotiated rate (10 -> 2.5MHz, 100 -> 25MHz, 1000 ->
  125MHz). unloading the ko mid-run leaves a stale interface rate and
  silently breaks the mismatched direction(s).
- the interrupt routing is NOT part of the constraint: the slave owns
  its sPIs end to end (`ArchHwiCreate` programs the GICD per-core
  route/enable at `CanStart`/`NetStart` time; the master never touches
  the slave's interrupt lines).

ordering summary: `mcs_km.ko` (slave image load) is normally started at
boot; `mcs_can.ko` / `mcs_net.ko` must be loaded before the slave-side
demo/user task calls `CanInit` / `NetInit`. if the slave image is
(re)started after the modules, that is also fine - the order that
matters is "ko loaded before the slave touches the peripheral".

## 3. slave-side API (contracts)

the full contracts live in `can.h` and `net.h` (comments are the
specification). shape:

| CAN (can.h)            | NIC (net.h)            | purpose |
|------------------------|------------------------|---------|
| `CanInit(cfg, &dev)`   | `NetInit(cfg, &dev)`   | configure the port (mode, timing / link policy, filters) |
| `CanStart(dev)`        | `NetStart(dev)`        | join the bus / start the engines (registers the IRQ in IRQ/HYBRID modes) |
| `CanSend(dev, f, tmo)` | `NetSend(dev, f, len)` | transmit one frame (CAN: synchronous, single-frame fifo) |
| `CanRecv(dev, f, tmo)` | `NetRecv(dev, buf, cap, &len)` | receive one frame (POLL reaps the hardware) |
| `CanStop(dev, tmo)`    | `NetStop(dev, tmo)`    | leave the bus / quiesce the DMA, deregister the IRQ |
| `CanGetStats(dev, &st)`| `NetGetStats(dev, &st)`| counters (tx/rx/ errors, linkChanges, irq) |
| `canGetBusInfo(dev, &bi)` | `NetGetLinkInfo(dev, &li)` | bus state / PHY link (speed, duplex, up/down) |

notes:

- **bit timing** (`canConfig`): `bitrate` style lets the driver solve
  the segments against the profile baud clock (`cANx_BAUDCLK_HZ`,
  100MHz); `useRawTiming` writes raw `brp/sjw/ts1/ts2` values.
  800kbps is unreachable at a 100MHz bit clock (prescaler granularity).
- **link** (`NetGetLinkInfo`): reads BMSR/BMCR/LPA/STAT1000 from the PHY
  over MDIO on every call (BMSR is read twice - latched-low). the
  master's 1s link work reads the same PHY concurrently; both sides use
  the GO/BUSY handshake so accesses serialize, and a torn sample
  self-heals on the next call.
- all entry points are non-blocking-safe from one task; IRQ modes fill
  a lock-free SPSC ready queue drained by `recv`.

## 4. demos

`can_test.c` / `net_test.c` are self-contained bring-up suites (loopback
phases for CAN; forced-100M poll-mode smoke for the NIC). they are
compiled in when the kconfig switches are set
(`kernel/liteos_m/targets/rk3568_m/liteos_m/kconfig`):

```
LOSCFG_MCS_CAN=y     # CAN1 data plane + can_test demo
LOSCFG_MCS_NET=y     # GMAC1 data plane + net_test demo
LOSCFG_MCS_RPC=y     # mica rpc + socket proxy demo (netproxy_test.c)
```

expected CAN demo output (CAN1, loopback):

```
[can] [lb-irq-500k] up
[can] [lb-irq-500k] stop: 0 (sent 20, echoed 20, errors 0)
...
[can] all phases done
```

## 5. porting / adding ports

- **another CAN controller**: new `canN_profile.h` (base, INTID, baud
  clock, fifo depth, default bitrate) + a `g_ports[]` entry in
  `can_rk3568.c` + a `CAN_PORT_*_RK3568` selector in `can.h` + the
  master-side dts node / ko support.
- **another GMAC**: new `gmacN_profile.h` (base, PHY address, INTID,
  DMA pool window - the pool must be covered by an NC mapping in
  `arch/arm64/los_mmu.c`) + the port plumbing in `net_dwmac.c`.
- **another soC (e.g. rk3588)**: the contracts (`can.h` / `net.h`) and
  the call sequence stay identical; the register-level driver files and
  profiles are per-soC.
