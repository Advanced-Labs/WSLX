# WHP vs HCS, VMBus Internals, and Inter-VM Shared Memory on Hyper-V
**Project:** WSLX Research
**Status:** Research document
**Date:** 2026-02-07

---

## 1. Does Windows Home Have Access to WHP?

**Yes.** Windows Home has access to the Windows Hypervisor Platform (WHP) API.

The confusion stems from there being **four separate optional Windows features** that sound similar but do completely different things:

| Optional Feature | Windows Home | Windows Pro/Enterprise | What It Does |
|---|---|---|---|
| **Hyper-V** (Platform + Management Tools) | No | Yes | Full VM management: Hyper-V Manager, VMMS service, PowerShell cmdlets, snapshots, live migration |
| **Virtual Machine Platform** | Yes | Yes | Lightweight hypervisor partitioning. What WSL2 and WSA need. Exposes HCS API |
| **Windows Hypervisor Platform** | Yes | Yes | User-mode WHP API (`WinHvPlatform.dll`) for third-party hypervisors |
| **Windows Sandbox** | No | Yes | Disposable lightweight VM (requires full Hyper-V) |

The key insight: **all four features sit on top of the same underlying Windows hypervisor** (`hvix64.exe` / `hvax64.exe`). Enabling any of the bottom three activates the hypervisor. They differ only in *which management APIs* they expose.

### What third-party software uses WHP on Windows Home

- **VirtualBox 7.1+**: Uses WHPX backend for hardware acceleration
- **Android Emulator**: Google's official emulator uses WHPX (users have until end of 2026 to migrate)
- **QEMU**: Supports `-accel whpx` acceleration mode
- **Docker Desktop**: Uses WSL2 backend (Virtual Machine Platform), not WHP directly

### SDK details

WHP headers ship with the Windows SDK (10.0.17763.0+):

```
C:\Program Files (x86)\Windows Kits\10\Include\<version>\um\WinHvPlatform.h
C:\Program Files (x86)\Windows Kits\10\Include\<version>\um\WinHvEmulation.h
C:\Program Files (x86)\Windows Kits\10\Include\<version>\um\WinHvPlatformDefs.h
```

Link against `WinHvPlatform.lib` → loads `WinHvPlatform.dll`.

---

## 2. Could WSL Be Recoded Using WHP Instead of HCS?

### Short answer

Technically yes, but it would be a massive engineering regression. You'd be replacing a high-level orchestration API with a low-level CPU/memory management API and re-implementing everything HCS provides.

### What WHP gives you

WHP is a **bare-metal virtual CPU and memory API**. Its surface is:

```c
// Partition lifecycle
WHvCreatePartition() / WHvSetupPartition() / WHvDeletePartition()

// Memory management
WHvMapGpaRange()     // Map host virtual address → guest physical address
WHvUnmapGpaRange()

// Virtual CPU lifecycle
WHvCreateVirtualProcessor() / WHvDeleteVirtualProcessor()
WHvRunVirtualProcessor()    // Run vCPU until exit (MMIO, I/O port, HLT, etc.)
WHvGetVirtualProcessorRegisters() / WHvSetVirtualProcessorRegisters()

// Instruction emulation (separate DLL)
WHvEmulatorCreateEmulator()
WHvEmulatorTryMmioEmulation() / WHvEmulatorTryIoEmulation()
```

That's it. WHP gives you **a partition, some memory, and some vCPUs**. You get a `WHvRunVirtualProcessor` loop that exits on every I/O operation, and you must handle each exit yourself.

### What HCS gives you (and WHP doesn't)

| Capability | HCS | WHP |
|---|---|---|
| Create/destroy a VM | `HcsCreateComputeSystem()` with JSON config | Manual: create partition, map memory, create vCPUs, load kernel |
| Boot a Linux kernel | Built-in direct Linux boot | You must: load kernel ELF, set up page tables, configure boot params, jump to entry |
| Synthetic devices (storage, NIC) | Built-in via VMBus VSPs | You must implement device emulation from scratch |
| Virtual networking (NAT, DHCP, DNS) | HCN API (`HcnCreateNetwork`) | You must implement your own virtual switch |
| 9P/Plan9 file sharing | Built-in | You must implement a 9P server + transport |
| Dynamic memory (balloon) | Built-in | You must implement memory hot-add/remove |
| VM save/restore | `HcsSaveComputeSystem` / `HcsCreateComputeSystem` | You must serialize all state yourself |
| GPU-P passthrough | Built-in | Not available at this layer |
| hvsocket transport | Built-in | You must implement your own host↔guest channel |
| Console/serial | Built-in | You must emulate UART or virtio-console |

### What it would actually mean

Building WSL on WHP is essentially building **a custom hypervisor VMM** (like QEMU or OpenVMM). You'd need:

1. **A vCPU run loop** handling all VM exits (MMIO, PIO, MSR access, CPUID, interrupts)
2. **Device emulation** for at minimum: UART, virtio-blk/SCSI, virtio-net, interrupt controller (IOAPIC + LAPIC)
3. **A boot loader** to load the Linux kernel into guest memory
4. **A virtual switch** for networking
5. **A file-sharing protocol** implementation
6. **Memory management** including SLAT page table setup

This is precisely what QEMU does when it uses the WHPX accelerator — QEMU provides all the device emulation, WHP provides only the CPU virtualization.

### Is HCN a relevant question?

Yes. HCN (Host Compute Network) is the networking counterpart to HCS. If you switched to WHP, you'd lose HCN too, because HCN creates virtual switches and NAT rules that integrate with HCS compute systems. You'd need to implement networking from scratch — either by emulating an e1000/virtio-net and bridging to a TAP adapter, or by writing your own virtual switch driver.

---

## 3. Why Did Microsoft Choose HCS/HCN Over WHP for WSL2?

### The architectural reason

HCS and WHP exist at **completely different abstraction levels**. The choice wasn't "HCS or WHP" — it was "do we want to build a full VMM, or use the one we already have?"

```
Layer 4:  WSL2 (wslservice.exe)
Layer 3:  HCS (computecore.dll) + HCN (computenetwork.dll)     ← WSL2 uses this
Layer 2:  vmcompute.exe / vmwp.exe (VM Worker Process)
Layer 1:  WHP (WinHvPlatform.dll)                               ← Third parties use this
Layer 0:  Windows Hypervisor (hvix64.exe)                        ← Same for everyone
```

WSL2 calls HCS, which internally manages the VM worker process (`vmwp.exe`), which internally uses the hypervisor. WHP is a *parallel* entry point into Layer 0 for third parties who bring their own VMM.

Microsoft chose HCS because:

1. **It already existed.** HCS was built for Windows Containers (Docker on Windows). Adding a Linux VM was a configuration change, not a new stack.
2. **Device stack reuse.** VMBus synthetic devices (StorVSC, NetVSC), 9P file sharing, GPU-P — all require the existing Hyper-V device stack that ships with HCS. WHP exposes none of this.
3. **Boot time.** The HCS path boots a lightweight utility VM in ~1 second because the VM worker process and device stack are highly optimized. A WHP-based solution would need custom device emulation that couldn't match this.
4. **Maintenance.** HCS is maintained as part of Windows; device drivers, networking, and security updates flow automatically. A WHP-based VMM would be a parallel maintenance burden.

### Would a WHP-based WSL offer benefits?

**Potentially yes, and this is where it gets interesting.** A WHP-based approach would sacrifice convenience for *control*:

#### Benefit 1: Custom device model

With WHP, you control the entire device model. Instead of being locked to VMBus synthetic devices, you could:
- Implement **virtio devices** directly (virtio-blk, virtio-net, virtio-fs, virtio-pmem)
- Use **vhost-user** for user-space device backends
- Implement novel devices with no HCS/VMBus equivalent

This is exactly what **OpenVMM (hvlite)** does — it's Microsoft's own WHP-based VMM written in Rust. OpenVMM supports both VMBus synthetic devices AND virtio devices.

#### Benefit 2: Custom memory model

WHP's `WHvMapGpaRange()` gives direct control over the guest physical address space. You could:
- Map **shared memory regions** between host and guest without going through 9P or virtio-fs
- Implement **zero-copy I/O** by mapping host buffers directly into guest GPA space
- Create **memory-mapped IPC channels** with sub-microsecond latency

This is precisely what our **Portal** concept needs (see Section 3 of the Hyper-V Virtualization Primitives doc).

#### Benefit 3: Multi-VM architectures

HCS creates opaque compute systems — you can't easily set up shared resources between them. With WHP, you could:
- Create **multiple partitions** with overlapping GPA mappings (shared memory between VMs)
- Implement **custom inter-VM channels** without VMBus's parent-child constraint
- Build **distributed kernel architectures** across VMs

#### Benefit 4: Transparent device passthrough

WHP gives you the VM exit loop. You could implement **selective passthrough** where some devices are emulated, some are passed through via SR-IOV, and some use custom protocols — all in the same VM, with fine-grained control.

### The OpenVMM middle path

Microsoft has implicitly acknowledged these benefits by building **OpenVMM** — an open-source VMM that can use WHP as its backend. OpenVMM provides:
- Both VMBus and virtio device support
- Flexible device configuration
- Rust safety guarantees
- Cross-platform potential (WHP on Windows, KVM on Linux)

**For WSLX's purposes, the interesting path isn't "WHP instead of HCS" — it's "OpenVMM alongside or instead of HCS."** OpenVMM gives us the control benefits of WHP with a mature device stack already implemented.

---

## 4. Understanding VMBus (and How It Differs from hvsocket)

### The analogy

Think of it this way:
- **VMBus** is like a **PCI bus** — it's the infrastructure that enumerates and connects devices
- **hvsocket** is like a **TCP socket** — it's an application-level communication channel
- VMBus channels are like **DMA rings on a NIC** — kernel-mode, zero-copy, interrupt-driven
- hvsocket connections are like **TCP connections over that NIC** — user-mode, buffered, stream-oriented

### What VMBus actually is

VMBus is a **software-defined, ACPI-enumerated bus** that provides shared-memory ring buffer channels between the host (root partition) and guest (child partition). It is NOT a virtual PCI bus — it has its own enumeration mechanism via ACPI DSDT.

#### The hardware-level reality

At the lowest level, VMBus works through three hypervisor primitives:

1. **Shared memory pages (GPADL)**: Guest allocates memory, then tells the host the physical page frame numbers via a Guest Physical Address Descriptor List. The host maps these same physical pages into its own address space. Now both partitions can read/write the same memory.

2. **HvCallSignalEvent hypercall**: Guest → Host doorbell. "I put data in the ring buffer, come read it."

3. **Synthetic interrupts (SynIC)**: Host → Guest doorbell. "I put data in the ring buffer, come read it." Delivered via the Synthetic Interrupt Controller, which extends the guest's LAPIC with 16 additional interrupt lines (SINT0-SINT15).

That's the entire foundation. Everything else is protocol layered on top.

#### Channel lifecycle

```
1. INITIATE_CONTACT      Guest → Host    "I speak VMBus protocol version X"
2. VERSION_RESPONSE       Host → Guest    "OK, we'll use version X"
3. OFFER_CHANNEL          Host → Guest    "I have a SCSI controller, channel ID 7"
4. OFFER_CHANNEL          Host → Guest    "I have a NIC, channel ID 12"
   ... (one offer per synthetic device)
5. OPEN_CHANNEL           Guest → Host    "I accept channel 7. Here's my GPADL
                                           for the ring buffers (pages 0x1000-0x1FFF)"
6. GPADL_CREATED          Host → Guest    "I've mapped your ring buffer pages"
7. [Data transfer begins via ring buffers]
```

#### Ring buffer structure

Each channel has **two ring buffers** sharing a contiguous memory region:

```
┌────────────────────────────────────────────────────┐
│                  GPADL region                       │
├──────────────────────┬─────────────────────────────┤
│   Send ring (G→H)    │    Receive ring (H→G)       │
├──────────────────────┼─────────────────────────────┤
│ ┌──────────────────┐ │ ┌─────────────────────────┐ │
│ │ Header (4 KB)    │ │ │ Header (4 KB)           │ │
│ │  - write_index   │ │ │  - write_index          │ │
│ │  - read_index    │ │ │  - read_index           │ │
│ │  - pending_count │ │ │  - pending_count        │ │
│ │  - interrupt_mask│ │ │  - interrupt_mask       │ │
│ ├──────────────────┤ │ ├─────────────────────────┤ │
│ │ Data pages       │ │ │ Data pages              │ │
│ │ (variable size,  │ │ │ (variable size,         │ │
│ │  device-specific)│ │ │  device-specific)       │ │
│ └──────────────────┘ │ └─────────────────────────┘ │
└──────────────────────┴─────────────────────────────┘
```

The producer writes data and advances `write_index`. The consumer reads data and advances `read_index`. When the producer needs to wake the consumer, it fires a doorbell (hypercall or synthetic interrupt).

### VSP / VSC architecture

```
Root Partition (Windows Host)                Child Partition (Linux Guest)
┌──────────────────────────┐                ┌──────────────────────────┐
│  Storage VSP (storvsp)   │                │  Storage VSC (storvsc)   │
│  ┌────────────────────┐  │   Ring Buffer  │  ┌────────────────────┐  │
│  │ Physical disk I/O  │◄─┼───────────────►│  │ SCSI command issue │  │
│  └────────────────────┘  │                │  └────────────────────┘  │
│                          │                │           ▲              │
│  Network VSP (netvsp)    │                │  Network VSC (netvsc)    │
│  ┌────────────────────┐  │   Ring Buffer  │  ┌────────────────────┐  │
│  │ Virtual switch     │◄─┼───────────────►│  │ Packet send/recv   │  │
│  └────────────────────┘  │                │  └────────────────────┘  │
│                          │                │           ▲              │
│  ... more VSPs ...       │                │  ... more VSCs ...       │
│                          │                │           │              │
│  VMBus provider          │   Shared mem   │  VMBus driver            │
│  (vmbusr.sys)            │◄──────────────►│  (vmbus_drv.c)           │
└──────────────────────────┘   + hypercalls  └──────────────────────────┘
                               + SynIC
```

Key insight: **the VSP runs in the host**. The guest never talks to real hardware — it talks to the VSP, which talks to real hardware on the guest's behalf. This is why VMBus is fundamentally a parent-child mechanism.

### What synthetic devices use VMBus

| VSP (Host) | VSC (Guest Linux driver) | Channel Purpose |
|---|---|---|
| StorVSP | `storvsc` | Synthetic SCSI: disk I/O commands |
| NetVSP | `hv_netvsc` | Synthetic NIC: packet send/receive |
| — | `hv_balloon` | Dynamic memory: balloon inflate/deflate |
| — | `hv_kvp` | Key-Value Pair exchange (IP config, hostname) |
| — | `hv_utils` | Heartbeat, time sync, shutdown, file copy |
| — | `hv_sock` | Hyper-V sockets (AF_VSOCK transport) |
| Video VSP | `hyperv_fb` | Synthetic framebuffer |
| Keyboard VSP | `hyperv_keyboard` | Synthetic keyboard |

### VMBus vs hvsocket: when to use which

| | VMBus channel | hvsocket (AF_HYPERV / AF_VSOCK) |
|---|---|---|
| **Abstraction level** | Kernel device driver | User-space socket |
| **Programming model** | Ring buffer + interrupts | `connect()` / `send()` / `recv()` |
| **Development requirements** | Windows Driver Kit (WDK) | Standard socket API |
| **Use case** | Synthetic device implementation | Application-level IPC |
| **Performance** | Lower latency (no socket layer overhead) | Higher latency (socket buffer copies) |
| **Data format** | Raw ring buffer entries | Byte stream (SOCK_STREAM only) |
| **Direction** | Bidirectional within a channel | Bidirectional per connection |
| **Connection model** | Channel offer/accept at boot | `listen()` / `accept()` at runtime |
| **Who uses it** | `storvsc`, `netvsc`, `hv_balloon` | WSL init, Plan9 server, custom services |

**hvsocket is built on top of VMBus** — it's essentially "TCP over VMBus" without the IP stack. The `hv_sock.c` driver creates a VMBus channel under the hood but exposes it as a regular socket to user space.

### How VMBus could be used in theory

#### Custom VMBus channels

Microsoft provides the **VMBus Kernel Mode Client Library (KMCL)** via the WDK:

```c
#include <vmbuskernelmodeclientlibapi.h>

// Allocate a channel
VmbChannelAllocate(parentDevice, isServer, &channel);

// Create GPADL from buffer (share memory with other partition)
VmbChannelCreateGpadlFromBuffer(channel, flags, buffer, bufferSize, &gpadlHandle);

// Map GPADL on the server side
VmbChannelMapGpadl(channel, flags, gpadlHandle, &mappedBuffer);
```

This lets you create **custom synthetic devices** with arbitrary protocols. However, the KMCL only supports the parent-child model — you cannot create channels between two child partitions.

#### What if you need guest-to-guest?

VMBus cannot do this. Every channel has a VSP endpoint in the root partition. For guest-to-guest communication, your options are:
1. **Proxy through the host**: VSP in root accepts data from VM-A, forwards to VM-B
2. **Virtual networking**: Use the Hyper-V virtual switch (which itself uses VMBus + NetVSP)
3. **SR-IOV + RDMA**: Hardware-assisted, bypasses VMBus entirely (see Section 5)

### VMBus performance characteristics

| Metric | VMBus | Bare Metal | Overhead |
|---|---|---|---|
| Storage 4K random read (Q1/T1) | ~22K IOPS | ~113K IOPS | **~80%** |
| Storage sequential (high queue depth) | Near-native | Baseline | **<10%** |
| Network throughput | Near-native with multi-channel | Baseline | **<5%** |
| Latency floor | ~25-50 μs per I/O dispatch | <10 μs (NVMe) | ~3-5x |

The high small-I/O overhead is VMBus's biggest weakness. Each I/O traverses: guest ring buffer → hypercall doorbell → host VMBus → VSP driver → actual I/O → reverse path. For large sequential I/O with deep queues, the pipeline stays full and overhead is amortized.

---

## 5. Inter-VM Shared Memory and Distributed Kernel Architectures on Hyper-V

### The fundamental constraint

Hyper-V does **not** natively support direct shared memory between child partitions.

Unlike:
- **KVM**: ivshmem — a virtual PCI device backed by a shared memory region, allowing any two VMs to mmap the same physical pages
- **Xen**: Grant tables — a capability-based system where one domain grants another domain access to specific memory pages

Hyper-V's architecture enforces strict child partition isolation. Each child has its own GPA (Guest Physical Address) space managed via SLAT (Second Level Address Translation). There is no built-in mechanism to create overlapping GPA→SPA (System Physical Address) mappings between two children.

### Why this matters for distributed kernels

A distributed kernel across VMs needs tight coupling:
- **Shared page tables** or **distributed shared memory** for coherent address spaces
- **Sub-microsecond signaling** for synchronization primitives (locks, barriers)
- **Zero-copy data movement** for efficiency

On KVM, ivshmem gives you direct shared physical pages between VMs — VMs can literally `mmap()` the same memory region with no copies. On Hyper-V, you cannot do this between two child partitions.

### Available approaches, ranked by performance

#### Approach 1: SR-IOV + RDMA (Best available)

**How it works:**
- SR-IOV (Single Root I/O Virtualization) allows a physical NIC to present multiple virtual functions (VFs) directly to VMs, bypassing the virtual switch
- RDMA (Remote Direct Memory Access) enables one VM to read/write another VM's memory without involving the remote CPU
- Windows Server 2019+ supports Guest RDMA over SR-IOV

**Performance:**
- **Latency**: 1-10 μs round-trip
- **Bandwidth**: 90+ Gb/s for 32KB+ transfers
- **CPU overhead**: Near zero (DMA engine does the work)

**Architecture for a distributed kernel:**
```
VM-A (Kernel Node 0)              VM-B (Kernel Node 1)
┌────────────────────┐            ┌────────────────────┐
│ Distributed Memory │            │ Distributed Memory │
│ Manager            │            │ Manager            │
│   │                │            │   │                │
│   ▼                │            │   ▼                │
│ RDMA Verbs API     │            │ RDMA Verbs API     │
│   │                │            │   │                │
│   ▼                │            │   ▼                │
│ SR-IOV VF (direct) │◄══════════►│ SR-IOV VF (direct) │
└────────────────────┘  RDMA DMA  └────────────────────┘
           │                                │
           ▼                                ▼
    ┌──────────────────────────────────────────┐
    │        Physical RDMA NIC (PF)            │
    └──────────────────────────────────────────┘
```

**Key RDMA operations:**
- `RDMA Write`: VM-A writes directly into VM-B's registered memory region
- `RDMA Read`: VM-A reads from VM-B's registered memory region
- `RDMA Send/Recv`: Message passing with receive-side buffer

**Implementation:**
```c
// Register memory region for RDMA access
struct ibv_mr *mr = ibv_reg_mr(pd, buffer, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);

// RDMA Write: write directly into remote VM's memory
struct ibv_send_wr wr = {
    .opcode = IBV_WR_RDMA_WRITE,
    .wr.rdma.remote_addr = remote_mr->addr,
    .wr.rdma.rkey = remote_mr->rkey,
    .sg_list = &sge,
    .num_sge = 1,
};
ibv_post_send(qp, &wr, &bad_wr);
```

**Pros**: Closest to true shared memory semantics; hardware-accelerated; well-supported on Windows Server
**Cons**: Requires RDMA-capable hardware; not literally shared memory (explicit operations, not transparent page sharing); NIC dependency

#### Approach 2: Custom VMBus proxy via parent partition

**How it works:**
- Each child partition creates a VMBus channel to a custom VSP in the parent
- The parent VSP maps GPADL regions from both children
- The parent copies data between the two GPADLs (or, with careful implementation, could map overlapping host virtual pages)

**Performance:**
- **Latency**: 25-100 μs (two VMBus hops + parent copy)
- **Bandwidth**: Limited by GPADL size (~1280 MB max per channel)
- **CPU overhead**: Parent CPU involved in every transfer

**Architecture:**
```
VM-A (Child)             Root Partition              VM-B (Child)
┌──────────┐         ┌──────────────────┐         ┌──────────┐
│ VSC-A    │◄───────►│ Proxy VSP        │◄───────►│ VSC-B    │
│ (ring A) │ VMBus-A │  maps GPADL-A    │ VMBus-B │ (ring B) │
│          │         │  maps GPADL-B    │         │          │
│          │         │  copies A↔B      │         │          │
└──────────┘         └──────────────────┘         └──────────┘
```

**Pros**: Uses native Hyper-V mechanisms; no special hardware; works on any SKU
**Cons**: Parent partition bottleneck; not zero-copy between children; complex driver development

#### Approach 3: Hypercall-based message passing

**How it works:**
- Use `HvCallPostMessage` (0x005C) for parameterized messages between partitions
- Use `HvCallSignalEvent` (0x005D) for lightweight doorbell notifications
- Parent partition routes messages between children

**Performance:**
- **Signal (HvCallSignalEvent)**: 5-20 μs (very lightweight — just sets a bit in the SIEF page)
- **Message (HvCallPostMessage)**: 50-200 μs (requires buffer allocation, queuing, SynIC delivery)
- **Message size**: Max 240 bytes payload per message

**TLFS inter-partition communication model:**
```
Connections and Ports:

Port (owned by partition)           Connection (cross-partition)
┌─────────────┐                     ┌────────────────────┐
│ Port ID     │◄────────────────────│ Connection ID      │
│ Port Type:  │                     │ Source Partition    │
│  - Message  │                     │ Target Port ID     │
│  - Event    │                     └────────────────────┘
│ Target VPI  │
└─────────────┘

Message flow: Source partition → HvCallPostMessage(ConnectionId, ...) → Hypervisor queues →
              Target partition SINT fires → SynIC delivers to target VP message buffer
```

**SynIC (Synthetic Interrupt Controller) per vCPU:**
- 16 SINT sources (SINT0-SINT15)
- Each SINT has: a message slot (256 bytes) + event flags (2048 bits)
- Messages queued by hypervisor, delivered on next guest entry
- Events are edge-triggered bit-sets, polled by guest

**Pros**: Lightweight signaling; native to hypervisor; no device driver needed for events
**Cons**: Messages still routed through parent; tiny payload size; not zero-copy; not shared memory

#### Approach 4: WHP-based custom hypervisor (the nuclear option)

**How it works:**
Build a custom VMM using the WHP API that creates multiple partitions with **intentionally overlapping GPA→HVA mappings**:

```c
// Allocate shared memory on host
void *shared_region = VirtualAlloc(NULL, SHARED_SIZE, MEM_COMMIT, PAGE_READWRITE);

// Map into VM-A's GPA space at address 0x8000_0000
WHvMapGpaRange(partitionA, shared_region, 0x80000000, SHARED_SIZE,
    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);

// Map into VM-B's GPA space at address 0x8000_0000
WHvMapGpaRange(partitionB, shared_region, 0x80000000, SHARED_SIZE,
    WHvMapGpaRangeFlagRead | WHvMapGpaRangeFlagWrite);

// Now both VMs can read/write the same physical memory at GPA 0x8000_0000!
```

**Performance:**
- **Latency**: Sub-microsecond for shared memory access (it's literal shared RAM)
- **Bandwidth**: Memory bus speed (tens of GB/s)
- **Signaling**: Custom doorbell via monitored page or IPI emulation

**This is the ivshmem-equivalent for Hyper-V** — but you have to build the entire VMM yourself.

**Architecture:**
```
Custom VMM (host process, using WHP API)
┌──────────────────────────────────────────────────────────┐
│                                                          │
│  Shared Host Memory Region (VirtualAlloc)                │
│  ┌──────────────────────────────────────────────────┐    │
│  │           Portal / Shared Memory Region           │    │
│  │    (mapped into both partitions at same GPA)      │    │
│  └────────────┬─────────────────────┬───────────────┘    │
│               │                     │                     │
│    WHvMapGpaRange()          WHvMapGpaRange()             │
│               │                     │                     │
│  ┌────────────▼──────┐  ┌──────────▼────────────┐       │
│  │ Partition A       │  │ Partition B            │       │
│  │ (Kernel Node 0)   │  │ (Kernel Node 1)        │       │
│  │                   │  │                        │       │
│  │ GPA 0x8000_0000  │  │ GPA 0x8000_0000       │       │
│  │ = shared region   │  │ = shared region        │       │
│  └───────────────────┘  └────────────────────────┘       │
│                                                          │
│  WHvRunVirtualProcessor() loops for each partition        │
│  Device emulation (virtio-blk, virtio-net, etc.)         │
└──────────────────────────────────────────────────────────┘
```

**Pros**: True zero-copy shared memory; sub-μs latency; full control; ivshmem-equivalent
**Cons**: Must build entire VMM; lose all HCS/VMBus infrastructure; enormous engineering effort; lose GPU-P, dynamic memory, live migration

### The OpenVMM path: having it both ways

**OpenVMM (hvlite)** is Microsoft's open-source Rust VMM that can use WHP as a backend. It already provides:
- VMBus synthetic device support
- virtio device support
- Flexible device composition

In theory, one could **fork/extend OpenVMM** to:
1. Add a `virtio-ivshmem` device (shared PCI BAR backed by host memory)
2. Map the same host memory into multiple OpenVMM-managed partitions
3. Get true shared memory between VMs while retaining the rest of the device stack

This is the most viable path for WSLX's distributed kernel research.

### Performance comparison summary

| Approach | Latency | Bandwidth | Zero-Copy | Hardware Req | Engineering Effort |
|---|---|---|---|---|---|
| SR-IOV + RDMA | 1-10 μs | 90+ Gb/s | Near (DMA) | RDMA NIC | Medium |
| VMBus proxy | 25-100 μs | ~10 Gb/s | No | None | High (WDK) |
| Hypercall messaging | 5-200 μs | Low (240B/msg) | No | None | Medium |
| WHP shared memory | **<1 μs** | **Memory bus** | **Yes** | None | **Very High** |
| OpenVMM + ivshmem | **<1 μs** | **Memory bus** | **Yes** | None | High |

### Comparison: how other hypervisors solve this

| Hypervisor | Mechanism | Latency | How It Works |
|---|---|---|---|
| **KVM** | ivshmem | <1 μs | Virtual PCI device with shared BAR; multiple VMs map same POSIX shm region |
| **Xen** | Grant tables | ~1-5 μs | Domain grants page-level access to another domain; hypervisor mediates |
| **ACRN** | ivshmem (ACRN variant) | <1 μs | Similar to KVM ivshmem; designed for safety-critical automotive/IoT |
| **Hyper-V** | (none built-in) | N/A | Must use workarounds (RDMA, proxy, WHP hack) |

### Relevance to WSLX research

For the **Portal** shared memory system (defined in the Gemini + Portal research doc), the most viable path is:

1. **Near-term**: Use **virtio-pmem** via OpenVMM for host↔guest shared memory (already analyzed in the Hyper-V Virtualization Primitives doc)
2. **Medium-term**: Extend OpenVMM to support **shared virtio-pmem regions across multiple partitions** for guest↔guest shared memory
3. **Long-term**: Evaluate whether a full **WHP-based custom VMM** is warranted if multi-VM shared memory becomes a core requirement

The virtio-pmem approach is attractive because:
- Host file → guest `/dev/pmem0` with DAX gives zero-copy semantics for host↔guest
- For guest↔guest, two partitions could map the same host backing file
- OpenVMM already supports virtio-pmem as a device type
- Portal arena allocators work unchanged regardless of the underlying transport

---

## 6. References

### Microsoft Documentation
- [Hyper-V TLFS Overview](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs)
- [Inter-Partition Communication (TLFS)](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/inter-partition-communication)
- [Hypercall Reference](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/hypercalls/overview)
- [Virtual Interrupt Controller (SynIC)](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/virtual-interrupts)
- [Windows Hypervisor Platform API](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform)
- [Host Compute Service API](https://learn.microsoft.com/en-us/virtualization/api/hcs/overview)
- [Make Your Own Integration Services](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/user-guide/make-integration-service)
- [GPU Partitioning in Hyper-V](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/gpu-partitioning)

### Linux Kernel Documentation
- [VMBus — The Linux Kernel](https://docs.kernel.org/virt/hyperv/vmbus.html)
- [Hyper-V Overview — The Linux Kernel](https://docs.kernel.org/virt/hyperv/overview.html)

### OpenVMM
- [OpenVMM GitHub](https://github.com/microsoft/openvmm)
- [OpenVMM Guide](https://openvmm.dev/guide/)

### Cross-Hypervisor Shared Memory
- [ivshmem — QEMU documentation](https://www.qemu.org/docs/master/system/devices/ivshmem.html)
- [Grant Tables in Xen](https://xcp-ng.org/blog/2022/07/27/grant-table-in-xen/)
- [ACRN ivshmem HLD](https://projectacrn.github.io/latest/developer-guides/hld/ivshmem-hld.html)

### Academic Work
- Baumann et al., "The Multikernel: A new OS architecture for scalable multicore systems" (SOSP 2009)
- "GiantVM: A Novel Distributed Hypervisor" (ACM, 2020)
- "Efficient Shared Memory Message Passing for Inter-VM Communications" (Springer)
- "ZIVM: A Zero-Copy Inter-VM Communication Mechanism for Cloud Computing"

### WSLX Internal References
- `doc/research/Hyper-V Virtualization Primitives for WSLX.md`
- `doc/research/Gemini + Portal/seminal - Gemini + Portal.md`
- `doc/research/WARP/seminal - WSLX WARP.md`

---

*Document created 2026-02-07. Subject to revision as research progresses.*
