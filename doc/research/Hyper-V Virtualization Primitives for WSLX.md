# Hyper-V Virtualization Primitives for WSLX
**Project:** WSLX Research
**Status:** Research document
**Date:** 2026-02-04

---

## Abstract

This document analyzes two key Hyper-V virtualization primitives — **virtio-pmem** (via OpenVMM/hvlite) and the **Hyper-V Hypercall Interface** (TLFS) — in the context of WSLX research goals. It maps these technologies to the requirements of **Gemini + Portal**, **Process and VM Snapshots**, **WSLX NXIA**, and **WARP**, identifying concrete implementation paths.

---

## 1. Technologies Under Analysis

### 1.1 OpenVMM (hvlite) and virtio-pmem

**OpenVMM** (formerly hvlite) is Microsoft's open-source virtual machine monitor:
- Repository: https://github.com/microsoft/openvmm
- Guide: https://openvmm.dev/guide/

**virtio-pmem** is a paravirtualized persistent memory device that:
- Provides a byte-addressable memory region backed by a host file
- Enables Direct Access (DAX) — true zero-copy memory mapping from guest to host
- Supports explicit flush operations for durability guarantees
- Kernel driver: `Kernel/drivers/nvdimm/virtio_pmem.c`

### 1.2 Hyper-V Hypercall Interface (TLFS)

The **Hypervisor Top-Level Functional Specification (TLFS)** defines the guest-visible interface:
- Documentation: https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/hypercall-interface
- Hypercall Reference: https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/hypercalls/overview

Key characteristics:
- Hypercalls execute from CPL0 (kernel mode) in the guest
- All buffer pointers must be Guest Physical Addresses (GPA)
- Two calling conventions: memory-based and register-based (for small parameters)
- Classes: simple hypercalls (single operation) and rep hypercalls (batch operations)

### 1.3 Host Compute Service (HCS)

HCS is the Windows service that orchestrates VMs and containers:
- Binary: `%windir%\System32\vmcompute.exe`
- Go bindings: https://github.com/microsoft/hcsshim
- Provides VM lifecycle, checkpoint/restore, and resource management

---

## 2. Mapping to WSLX Research Goals

### 2.1 Research Document Summary

| Document | Core Vision |
|----------|-------------|
| **Gemini + Portal** | Bidirectional cross-OS execution fabric with shared memory (Portal) |
| **Process and VM Snapshots** | True state snapshots — CRIU-like process checkpoints + hypervisor VM checkpoints |
| **WSLX NXIA** | WSLX as the Linux half of a dual-kernel Virtual OS with Portal as system bus |
| **WARP** | Generalized micro-VM runtime for arbitrary kernels (Linux-personality or New-OS) |

### 2.2 Technology Relevance Matrix

| Research Goal | virtio-pmem | Hypercalls | HCS API |
|---------------|-------------|------------|---------|
| Portal shared memory | **Primary solution** | Not directly | Not relevant |
| Portal signaling | Not relevant | VMBus/HV sockets | Not relevant |
| VM Snapshots | State persistence | Memory flush | **Primary solution** |
| Process Snapshots | Not relevant | Not relevant | Not relevant (CRIU) |
| Gemini transport | Data plane | Control plane | Not relevant |
| WARP Track A | Portal device | Existing enlightenments | Existing lifecycle |
| WARP Track B | Portal device | **Must implement** | VM lifecycle |

---

## 3. Portal Shared Memory System

### 3.1 The Portal Vision (from Gemini doc)

Portal defines a shared memory system for zero-copy cross-OS communication:

```
Portal primitives:
├── PortalRegion    — shared memory segment mapped into both processes
├── PortalArena     — allocator within a region
├── PortalSpan      — (portal_id, offset, length, flags, generation)
└── Rings           — SubmitRing, CompleteRing, EventRing, UpcallRing
```

Key requirement: "True shared memory" (Tier 3) for zero-copy marshalling between Windows host and Linux guest.

### 3.2 virtio-pmem as Portal Transport

virtio-pmem provides exactly what Portal needs:

| Portal Requirement | virtio-pmem Capability |
|--------------------|------------------------|
| Shared memory segment | Host file → guest `/dev/pmem0` with DAX |
| Zero-copy patterns | Memory-mapped, no protocol overhead |
| Deterministic offsets | `PortalSpan = (pmem_base + offset)` |
| Flush for durability | `VIRTIO_PMEM_REQ_TYPE_FLUSH` |

**Architecture**:

```
Host (Windows):
  portal_region.bin ──→ Memory-mapped file ──→ OpenVMM virtio-pmem device
                                                      │
                                                      ▼ (PCI/MMIO)
Guest (Linux):
  /dev/pmem0 ──→ DAX mount or raw mmap ──→ Portal arena allocator
```

### 3.3 Comparison to Existing File Sharing

| Method | Latency | Zero-copy | Control | Portal Suitability |
|--------|---------|-----------|---------|-------------------|
| Plan9 (`\\wsl.localhost`) | High | No | Closed-source (P9rdr.sys) | Poor |
| VirtioFS | Medium | Partial | Open | Moderate |
| **virtio-pmem** | **Low** | **Yes (DAX)** | **Full (OpenVMM)** | **Excellent** |

### 3.4 Implementation Path

**Step 1: Enable kernel support**

```kconfig
# Required kernel config options
CONFIG_VIRTIO_PMEM=y
CONFIG_ND_VIRTIO=y
CONFIG_DAX=y
CONFIG_FS_DAX=y
CONFIG_NVDIMM_DAX=y
```

Driver location: `Kernel/drivers/nvdimm/virtio_pmem.c`

**Step 2: Configure OpenVMM device**

OpenVMM supports virtio-pmem as a device type. Configuration would specify:
- Backing file path on Windows host
- Region size
- Guest PCI slot assignment

**Step 3: Guest-side Portal implementation**

```c
// Portal arena initialization over virtio-pmem
int pmem_fd = open("/dev/pmem0", O_RDWR);
void* portal_base = mmap(NULL, PORTAL_SIZE,
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE,
                         pmem_fd, 0);

// Initialize ring structures at known offsets
SubmitRing* submit = (SubmitRing*)(portal_base + SUBMIT_RING_OFFSET);
CompleteRing* complete = (CompleteRing*)(portal_base + COMPLETE_RING_OFFSET);
```

**Step 4: Host-side Portal implementation**

```cpp
// Windows host maps the same backing file
HANDLE hFile = CreateFile(L"portal_region.bin", ...);
HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, ...);
void* portal_base = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, PORTAL_SIZE);

// Same ring structures visible from host
SubmitRing* submit = (SubmitRing*)((char*)portal_base + SUBMIT_RING_OFFSET);
```

### 3.5 Performance Envelope

Based on NXIA document requirements:

| Operation | Transport | Expected Latency |
|-----------|-----------|------------------|
| Small RPC (< 4KB) | HV socket control plane | 10-50 µs |
| Bulk transfer | virtio-pmem DAX | memcpy speed (GB/s) |
| Ring doorbell | HV socket signal | < 10 µs |

---

## 4. VM Snapshots

### 4.1 The Snapshot Vision (from Process and VM Snapshots doc)

Two levels of snapshot capability:

1. **Process-level**: CRIU-like checkpoint/restore of individual processes
2. **VM-level**: Hypervisor checkpoint of entire guest state (RAM + vCPU + devices)

Key blockers identified:
- seccomp filter state
- ptrace relationships
- Network socket state (especially vsock/HV sockets)
- GPU state
- File descriptor state across namespaces

### 4.2 Hypercall Relevance for Snapshots

Hypercalls operate **within** the guest and do not directly provide snapshot capability. However, some are relevant for snapshot preparation:

| Hypercall | Code | Purpose | Snapshot Role |
|-----------|------|---------|---------------|
| `HvCallFlushGuestPhysicalAddressSpace` | 0x00AF | Flush GPA mappings | Ensure memory coherence |
| `HvCallFlushVirtualAddressSpace` | 0x0002 | Flush VA mappings | Clean TLB state |
| `HvCallGetVpRegisters` | 0x0050 | Read vCPU registers | Guest-assisted state capture |

Definitions from `Kernel/include/asm-generic/hyperv-tlfs.h`:
```c
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE          0x0002
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST           0x0003
#define HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_SPACE   0x00af
#define HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_LIST    0x00b0
```

### 4.3 HCS API for VM Snapshots

The **Host Compute Service (HCS)** is the correct layer for VM snapshots:

```cpp
// Save VM state
HRESULT HcsSaveComputeSystem(
    HCS_SYSTEM computeSystem,
    HCS_OPERATION operation,
    PCWSTR options
);

// Create VM from saved state
HRESULT HcsCreateComputeSystem(
    PCWSTR id,
    PCWSTR configuration,  // includes saved state reference
    HCS_OPERATION operation,
    const SECURITY_DESCRIPTOR* securityDescriptor,
    HCS_SYSTEM* computeSystem
);
```

**Research question** (from Snapshots doc Milestone 5):
> "Determine if true VM save/restore is possible at all for WSL utility VMs"

This requires probing the HCS API behavior for the specific VM type WSL uses.

### 4.4 virtio-pmem for Snapshot Persistence

Using virtio-pmem for Portal provides a natural snapshot advantage:

```
Snapshot Flow:
1. Guest: AgentX receives quiesce command (via HV socket)
2. Guest: Pause Portal producers, drain in-flight operations
3. Guest: Flush virtio-pmem regions (VIRTIO_PMEM_REQ_TYPE_FLUSH)
4. Host: HcsSaveComputeSystem() → captures RAM + device state
5. Host: Copy virtio-pmem backing file → snapshot artifact

Restore Flow:
1. Host: Restore virtio-pmem backing file from snapshot
2. Host: HcsCreateComputeSystem() with saved state
3. Guest: Resume → Portal regions already valid (same file mapping)
4. Guest: Resume Portal consumers
```

**Key insight**: Portal state survives snapshot/restore automatically because it's backed by a host file.

### 4.5 Quiesce Protocol Design

```
AgentX Quiesce Sequence:
┌─────────────────────────────────────────────────────────┐
│ 1. Receive QUIESCE command from WSLXService            │
│ 2. Set portal_quiescing = true                         │
│ 3. Wait for in-flight operations to complete           │
│ 4. Flush all virtio-pmem regions                       │
│ 5. Send QUIESCE_COMPLETE to WSLXService                │
│ 6. [VM snapshot occurs]                                │
│ 7. On resume: receive RESUME command                   │
│ 8. Set portal_quiescing = false                        │
│ 9. Resume normal operation                             │
└─────────────────────────────────────────────────────────┘
```

---

## 5. Gemini Transport Layer

### 5.1 Transport Requirements (from Gemini doc)

```
Transport adapters (priority order):
1. Hyper-V sockets / vsock — host↔guest optimized
2. TCP loopback — fallback
3. Named pipes — Windows-only fallback
```

### 5.2 Hyper-V Socket Implementation

Hyper-V sockets (AF_HYPERV / vsock) are built on VMBus, which uses hypercalls:

| Hypercall | Code | Purpose |
|-----------|------|---------|
| `HvCallPostMessage` | 0x005C | Send message to host |
| `HvCallSignalEvent` | 0x005D | Signal host for data ready |

Kernel implementation:
- `Kernel/net/vmw_vsock/hyperv_transport.c` — HV socket transport
- `Kernel/drivers/hv/hv_sock.c` — Core implementation

### 5.3 Port Allocation for Gemini

Existing WSLX ports (from `src/shared/inc/lxinitshared.h`):
```cpp
LX_INIT_MAIN_PORT                    = 50000  // Main control
LX_INIT_STDERR_PORT                  = 50001  // Stderr relay
LX_INIT_PROCESS_FACTORY_PORT         = 50002  // Process creation
LX_INIT_UTILITY_VM_EXTRA_PORT        = 50003  // Utility VM
LX_INIT_UTILITY_VM_PLAN9_PORT        = 50004  // Plan9 filesystem
LX_INIT_UTILITY_VM_VIRTIOFS_PORT     = 50005  // VirtioFS
```

Proposed Gemini ports:
```cpp
LX_INIT_GEMINI_CONTROL_PORT          = 50010  // Gemini RPC control
LX_INIT_GEMINI_PORTAL_SIGNAL_PORT    = 50011  // Portal doorbell/signaling
LX_INIT_GEMINI_UPCALL_PORT           = 50012  // W→L upcall delivery
```

### 5.4 Gemini Data Path Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Control Plane                            │
│                    (HV Socket, low latency)                     │
├─────────────────────────────────────────────────────────────────┤
│  Windows Host                    │  Linux Guest                 │
│  ─────────────────               │  ────────────                │
│  GeminiHost                      │  GeminiAgent                 │
│    │                             │    │                         │
│    ├── RPC dispatch ◄────────────┼────┤ RPC requests            │
│    ├── Upcall send ──────────────┼───►│ Upcall receive          │
│    └── Doorbell ◄────────────────┼────┘ Doorbell                │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                         Data Plane                              │
│                  (virtio-pmem, zero-copy)                       │
├─────────────────────────────────────────────────────────────────┤
│  Windows Host                    │  Linux Guest                 │
│  ─────────────────               │  ────────────                │
│  portal_region.bin               │  /dev/pmem0                  │
│    │                             │    │                         │
│    ├── MapViewOfFile ◄───────────┼────┤ mmap (DAX)              │
│    │                             │    │                         │
│    └── SubmitRing ◄──────────────┼────┘ SubmitRing              │
│        CompleteRing ─────────────┼───►  CompleteRing            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6. WARP (Arbitrary Runtime Platform)

### 6.1 WARP Vision Summary

WARP enables booting nonstandard kernels while retaining:
- Host VM lifecycle management
- Host↔guest control/IO
- Paravirtual devices

Two tracks:
- **Track A**: Linux-personality (preserve syscall ABI, modify internals)
- **Track B**: New-OS (new ABI, new userland)

### 6.2 Hypercall Requirements for WARP

#### Track A: Linux-Personality Kernel

Existing Hyper-V enlightenments in the Linux kernel are sufficient:
- `Kernel/arch/x86/hyperv/` — x86 Hyper-V support
- `Kernel/drivers/hv/` — Hyper-V drivers

No additional hypercall implementation needed.

#### Track B: New-OS Kernel

A new kernel must implement minimal Hyper-V enlightenments:

**Required (must have)**:

| Enlightenment | Purpose | Implementation Notes |
|---------------|---------|---------------------|
| Hypercall page | Enable hypercall instruction | Write to `HV_X64_MSR_HYPERCALL` MSR |
| Partition ID | VM identity | `HvCallGetPartitionId` |
| Synthetic timers | Time synchronization | MSR-based, see TLFS Chapter 12 |
| Synthetic interrupts | Interrupt delivery | APIC emulation + SynIC |
| VMBus | Host communication | Ring buffers + signaling |

**Highly recommended**:

| Enlightenment | Purpose |
|---------------|---------|
| Reference TSC | Fast timestamp reads |
| APIC frequency | Timer calibration |
| TLB flush hypercalls | Performance optimization |

### 6.3 Hypercall Page Setup (Track B)

```c
// Minimal hypercall page initialization for new OS
void init_hyperv_hypercalls(void) {
    uint64_t hypercall_msr;
    void* hypercall_page;

    // Allocate page-aligned memory for hypercall page
    hypercall_page = alloc_page_aligned(PAGE_SIZE);

    // Read current MSR value
    hypercall_msr = rdmsr(HV_X64_MSR_HYPERCALL);

    // Set hypercall page PFN and enable
    hypercall_msr &= ~HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_MASK;
    hypercall_msr |= (virt_to_phys(hypercall_page) & HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_MASK);
    hypercall_msr |= HV_X64_MSR_HYPERCALL_ENABLE;

    wrmsr(HV_X64_MSR_HYPERCALL, hypercall_msr);
}

// Hypercall invocation
uint64_t hv_do_hypercall(uint64_t control, uint64_t input_addr, uint64_t output_addr) {
    uint64_t status;

    // Call into hypercall page (x64 calling convention)
    asm volatile("call *%3"
                 : "=a" (status)
                 : "c" (control), "d" (input_addr), "r8" (output_addr), "m" (hypercall_page)
                 : "memory");

    return status;
}
```

### 6.4 WARP Device Stack

| Device Type | Track A | Track B | Implementation |
|-------------|---------|---------|----------------|
| Storage | Existing virtio-blk/SCSI | virtio-blk | OpenVMM device |
| Network | Existing virtio-net | virtio-net | OpenVMM device |
| Portal | **virtio-pmem** | **virtio-pmem** | OpenVMM device |
| Console | Existing | Custom or virtio-console | For debugging |
| Control | HV socket | VMBus or HV socket | AgentX protocol |

### 6.5 AgentX Communication Protocol

AgentX must implement the WSLX control protocol regardless of kernel:

```
Message Types:
├── HELLO           — AgentX announces readiness
├── CONFIG          — WSLXService sends configuration
├── MOUNT           — Attach ImageX filesystem
├── EXEC            — Launch process
├── STDIO           — stdin/stdout/stderr relay
├── SIGNAL          — Signal delivery
├── QUIESCE         — Prepare for snapshot
├── RESUME          — Resume after snapshot
└── SHUTDOWN        — Graceful termination
```

---

## 7. Kernel Code References

### 7.1 Hyper-V Hypercall Definitions

Location: `Kernel/include/asm-generic/hyperv-tlfs.h`

Key definitions:
```c
// Hypercall numbers
#define HVCALL_POST_MESSAGE                         0x005c
#define HVCALL_SIGNAL_EVENT                         0x005d
#define HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_SPACE   0x00af
#define HVCALL_MODIFY_SPARSE_GPA_PAGE_HOST_VISIBILITY 0x00db
#define HVCALL_MMIO_READ                            0x0106
#define HVCALL_MMIO_WRITE                           0x0107

// Feature discovery
#define HV_ACCESS_PARTITION_ID                      BIT(1)  // Group B
#define HV_ACCESS_HYPERCALL_MSRS                    BIT(2)  // Group B

// Extended hypercalls
#define HV_EXT_CALL_MEMORY_HEAT_HINT                0x8003
```

### 7.2 virtio-pmem Driver

Location: `Kernel/drivers/nvdimm/virtio_pmem.c`

Key structures:
```c
struct virtio_pmem {
    struct virtio_device *vdev;
    struct virtqueue *req_vq;
    // ...
};

// Flush request for durability
#define VIRTIO_PMEM_REQ_TYPE_FLUSH  0
```

### 7.3 Hyper-V Socket Transport

Location: `Kernel/net/vmw_vsock/hyperv_transport.c`

Key functions:
```c
static int hvs_connect(struct vsock_sock *vsk);
static int hvs_send(struct vsock_sock *vsk, struct msghdr *msg, size_t len);
static int hvs_recv(struct vsock_sock *vsk, struct msghdr *msg, size_t len);
```

### 7.4 VMBus Driver

Location: `Kernel/drivers/hv/`

Key files:
- `hv.c` — Core Hyper-V initialization
- `vmbus_drv.c` — VMBus driver
- `channel.c` — VMBus channel management
- `ring_buffer.c` — Ring buffer implementation

---

## 8. Implementation Roadmap

### Phase 1: Portal MVP (virtio-pmem)

| Step | Task | Files |
|------|------|-------|
| 1.1 | Enable kernel config | `.config` |
| 1.2 | Configure OpenVMM virtio-pmem device | OpenVMM config |
| 1.3 | Implement Portal arena allocator | New: `src/linux/portal/` |
| 1.4 | Implement ring structures | New: `src/shared/inc/portal.h` |
| 1.5 | Host-side Portal mapping | New: `src/windows/service/Portal/` |
| 1.6 | Basic RPC over Portal | Test harness |

### Phase 2: Snapshot Support

| Step | Task | Files |
|------|------|-------|
| 2.1 | Probe HCS save/restore API | `src/windows/wslcore/` |
| 2.2 | Implement AgentX quiesce protocol | `src/linux/init/` |
| 2.3 | Add virtio-pmem flush on quiesce | `src/linux/init/` |
| 2.4 | Test VM snapshot/restore | Test harness |

### Phase 3: Gemini Integration

| Step | Task | Files |
|------|------|-------|
| 3.1 | Define Gemini port allocation | `src/shared/inc/lxinitshared.h` |
| 3.2 | Implement control plane (HV socket) | `src/linux/init/gemini.cpp` |
| 3.3 | Integrate Portal data plane | Connect to Phase 1 |
| 3.4 | L→W projection prototype | Gemini subsystem |
| 3.5 | W→L projection prototype | Gemini subsystem |

### Phase 4: WARP Foundation

| Step | Task | Files |
|------|------|-------|
| 4.1 | Document minimal enlightenment set | This doc |
| 4.2 | Create reference AgentX | `src/warp/agent/` |
| 4.3 | Test with modified Linux kernel | Track A validation |
| 4.4 | Prototype minimal new-OS kernel | Track B exploration |

---

## 9. Open Research Questions

### 9.1 Portal

1. **Optimal region size**: What's the ideal virtio-pmem region size for typical Gemini workloads?
2. **Multiple regions**: Should Portal use multiple pmem devices for isolation between Portal arenas?
3. **Signaling latency**: Is HV socket signaling fast enough, or should we use VMBus directly?

### 9.2 Snapshots

1. **HCS utility VM support**: Does `HcsSaveComputeSystem` work for WSL-style utility VMs?
2. **Device state**: How is virtio-pmem device state captured in VM snapshot?
3. **Resume ordering**: What's the correct order for resuming Portal after snapshot restore?

### 9.3 WARP

1. **Boot method flexibility**: How far can WSLX deviate from Linux direct boot for Track B?
2. **Minimal VMBus**: What's the minimal VMBus implementation for AgentX communication?
3. **Device reuse**: Can Track B kernels reuse Linux virtio drivers without full Linux?

---

## 10. References

### Microsoft Documentation

- [Hyper-V TLFS Overview](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs)
- [Hypercall Interface](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/hypercall-interface)
- [Hypercall Reference](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/hypercalls/overview)
- [Hyper-V Architecture](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/architecture)
- [Hyper-V Checkpoints](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/checkpoints)

### OpenVMM

- [OpenVMM GitHub](https://github.com/microsoft/openvmm)
- [OpenVMM Guide](https://openvmm.dev/guide/)

### HCS

- [hcsshim Go bindings](https://github.com/microsoft/hcsshim)
- [vmcompute.exe Analysis](https://medium.com/@boutnaru/the-windows-process-journey-vmcompute-exe-hyper-v-host-compute-service-01259486ab19)

### WSLX Research Documents

- `doc/research/Gemini + Portal/seminal - Gemini + Portal.md`
- `doc/research/Process and VM Snapshots.md/seminal - Process and VM Snapshots.md`
- `doc/research/Gemini + Portal/intro WSLX NXIA.md`
- `doc/research/WARP/seminal - WSLX WARP.md`

---

*Document created 2026-02-04. Subject to revision as research progresses.*
