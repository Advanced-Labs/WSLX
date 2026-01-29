```md
# WSLX Snapshot R&D Agenda: Process-Level + VM-Level (Not File Backups)

**Scope:** Define what we want for **true snapshots** (process- and VM-state), why WSL2/WSLX is a unique substrate, what blocks us today, and which research/implementation directions are plausible.

**Non-goals:**  
- No “backup the VHDX” / export-import. Those are *filesystem* captures, not execution snapshots.  
- No “save my files” features. We want **running state**: memory, CPU context, kernel object state, sockets, etc.

---

## 0) Definitions (pin these early)

### Process-level snapshot
Capture/restore of one process or a process tree:
- CPU registers + address space
- open FDs, pipes, epoll state
- network sockets (as feasible)
- namespaces/cgroups state (containers)
- signal/ptrace state
- optional GPU/accelerator context (usually hard)

Typical implementation family: **CRIU-like** checkpoint/restore.

### VM-level snapshot
Capture/restore of the entire running Linux VM:
- guest RAM + vCPU state
- virtual devices state (storage controller state, virtio queues, vsock, etc.)
- optional device-backed state (GPU virtualization state is tricky)
- restore implies “resume the whole machine exactly where it was” ⚡

Typical implementation family: **hypervisor checkpoint** / “save/restore VM state” primitives.

---

## 1) Why WSLX is interesting for real snapshots 🧪

WSL2 isn’t a classic user-managed VM. It is a **utility VM** managed by Windows components, with tight host↔guest integration:
- host service orchestrates VM lifecycle + distro sessions
- host↔guest IPC via vsock/HV sockets + internal services
- custom kernel + init + integration daemons

WSLX (identity-forked, SxS-functional) gives us leverage:
- we control the **user-mode** orchestrator and policy decisions
- we can instrument, change boot/launch flow, and experiment with alternate state models
- we can explore alternative kernel settings / init behavior (within WSL constraints)

---

## 2) Desired user experience (what “snapshot” should mean)

### 2.1 Process snapshots UX (developer-facing)
- `wslx snap proc --pid <pid> --name <snap>`  
- `wslx restore proc --name <snap>`  
- `wslx snap session --distro <d> --scope <app|tree|all>`  
- `wslx diff proc --name A --name B` (optional: metadata-only)

**Key:** restore without “rebooting the distro”.

### 2.2 VM snapshots UX (system-level)
- `wslx snap vm --distro <d> --name <snap> [--quiesce]`
- `wslx restore vm --name <snap>`
- `wslx snap vm --all-distros` (future)
- optional scheduling policy:
  - manual
  - periodic (dev-only)
  - on suspend/hibernate of Windows

**Key:** restore returns you to the same running shell sessions, sockets, background services.

---

## 3) Constraints and hard problems (WSL2/WSLX realities)

### 3.1 Process-level snapshot blockers (CRIU class) 🔥
Likely issues in WSL2-like environments:
- **seccomp inheritance / PID 1 policy**: if PID 1 is under seccomp filter and children inherit, CRIU may fail dumping/restoring seccomp.
- **ptrace permissions + namespaces**: restore may require capabilities not granted or blocked by policy.
- **network sockets**: CRIU’s TCP restore is delicate even on bare Linux; with WSL’s virtual networking it can be worse.
- **vsock/HV sockets**: host integration channels may not be restorable without cooperation.
- **GPU compute**: CUDA contexts typically cannot be restored generically.

### 3.2 VM-level snapshot blockers (hypervisor class) 🧱
- **Utility VM not surfaced** as a normal Hyper-V VM: no straightforward “checkpoint” UX.
- Even if the underlying compute system supports some save/restore, **WSL’s orchestration layer** may not be compatible:
  - host side services expect certain handshakes on boot
  - reattaching host integration (9p/plan9, vsock endpoints, port proxies) after restore may desync
- **Device state**:
  - network virtual devices must resume consistently
  - GPU virtualization state is often not checkpointable (or not exposed)
- **Security model**:
  - host may intentionally prevent VM state capture due to confidentiality/integrity guarantees.

---

## 4) Architecture map: where snapshot hooks could live

### 4.1 Process snapshot architecture (inside guest + controlled host support)
Two-tier model:
1) **Guest agent**: checkpoint/restore engine (CRIU-derived or custom)  
2) **Host coordinator** (WSLX): orchestrates policy, storage, quiescing, and integration endpoints

**Reason:** host integration services (vsock endpoints, port forwarders) often live outside the guest and must participate.

### 4.2 VM snapshot architecture (host-centric, guest-assisted)
Potential components:
- **WSLX host service**: requests VM “save state”
- **Compute system layer** (Windows virtualization substrate): performs memory+device state capture
- **Guest quiesce agent** (optional): freeze I/O, drain integration channels, flush filesystem caches

---

## 5) Research directions (ranked) ✅

### Direction A — “Cooperative process checkpoint” (highest leverage)
Build a WSLX-managed checkpoint system that is *CRIU-inspired* but cooperative with WSL integration.

Ideas:
- Add a **guest agent** that understands WSL integration devices and can:
  - temporarily disable or neutralize seccomp constraints (if possible)
  - checkpoint only safe subsets first (no network, no vsock) then expand
- Use *application-level* strategies for hard parts:
  - for TCP sockets: reconnect on restore (app shim)
  - for vsock endpoints: re-handshake with host services
- Provide “checkpoint profiles”:
  - `--profile minimal` (memory + regs + files)
  - `--profile net` (attempt sockets)
  - `--profile full` (best-effort, may fail)

Why it’s promising:
- VM-level snapshots may be blocked by Windows platform constraints.
- Process-level can be achieved incrementally and still offers huge value.

### Direction B — “Quiesced VM save/restore” via compute system (medium leverage, high risk)
Try to implement VM save/restore by interacting with the compute system layer.
- Investigate whether the compute system API exposes:
  - save state
  - restore state
  - or equivalent “hibernation” primitives

Risks:
- The substrate may not permit it for WSL’s VM type.
- Even if supported, WSL integration may break on resume.

### Direction C — Hybrid: “VM snapshot of a *subset* of state”
If full VM snapshot is impossible, emulate it:
- Freeze guest processes
- Save RAM-equivalent content at user level (huge)
- Reconstruct on restore via process checkpoint
This approximates VM snapshot without hypervisor support (but is complex and slow).

---

## 6) Proposed experimental plan (concrete milestones)

### Milestone 1 — Baseline observability 🧩
Goal: map the boundary and lifecycle precisely.
- Identify how WSLX starts the utility VM and establishes channels.
- Inventory host↔guest endpoints:
  - vsock/HV socket services
  - 9p/plan9 mounts
  - port forwarding proxies
- Build a “state report” command:
  - `wslx diag snapshot-readiness`

Deliverables:
- lifecycle diagram
- list of critical channels + restart behavior

### Milestone 2 — Minimal process snapshot POC (no sockets) ⚙️
Goal: checkpoint/restore a single process tree that:
- uses only local files + pipes
- no TCP sockets
- no GPU

Success criteria:
- restore returns process to same computation point
- file descriptors preserved
- works repeatedly

### Milestone 3 — Expand: TCP sockets (best-effort) 🌐
Goal: restore a local TCP server/client pair.
- document what breaks:
  - sequence numbers, NAT mappings, port proxies

Add “restore policies”:
- strict restore (must resume sockets)
- reconnect restore (app-level rebind + handshake)

### Milestone 4 — Host integration participation 🤝
Goal: checkpoint a process that uses WSL-specific channels (vsock / integration services).
- require WSLX host service to:
  - pause the channel
  - allow re-registration on restore
  - validate and re-bind endpoints

### Milestone 5 — VM snapshot feasibility probe 🧨
Goal: determine if true VM save/restore is possible at all.
- attempt to create a “save state” of the utility VM via the compute system layer
- if not possible, document why (policy vs missing API vs unsupported VM type)

---

## 7) Key technical questions to answer (research checklist)

### Process-level
- Are seccomp filters applied in a way that blocks checkpoint/restore? If yes:
  - can WSLX alter PID 1 behavior / security profile?
  - can we checkpoint without seccomp state? (partial)
- Which namespaces/cgroups are used by the distro init?
- What kernel features required by CRIU are enabled?
- What kernel features are missing or restricted in WSL kernel config?

### Integration
- Which channels must be “cooperatively restored”?
  - 9p mount sessions
  - vsock endpoints
  - port proxies
- Can host services tolerate a “rewind” in guest state?

### VM-level
- Does the compute system layer expose save/restore for this VM category?
- If yes, does it include device state needed for WSL integration?
- What happens to GPU virtualization context on resume?

---

## 8) Threat model + safety constraints 🔒
Snapshots are powerful; we must define guardrails:
- Protect secrets in memory snapshots.
- Ensure snapshots cannot be used to bypass authentication or replay privileged sessions.
- Consider encryption-at-rest for snapshot artifacts.
- Consider Windows security boundaries: do not weaken host integrity.

---

## 9) Suggested implementation split (WSLX team roles)

### Guest-side (Linux)
- checkpoint agent / CRIU integration or custom engine
- quiesce helpers (fs sync, service pauses)
- restore-time rebinders for WSL integration

### Host-side (Windows / WSLX)
- orchestration CLI + service endpoints
- snapshot storage management + encryption
- integration channel coordination
- VM save/restore experiments (if any)

---

## 10) Outcome matrix (what “success” looks like) 📌

| Capability | Minimum Viable | Target | Stretch |
|---|---|---|---|
| Process snapshot | single process tree, no net | process tree + local TCP with reconnect policy | best-effort TCP + vsock cooperative restore |
| Session snapshot | subset of user session apps | interactive shell session restored | multi-session, multi-distro orchestration |
| VM snapshot | feasibility determination | quiesced VM state save/restore (if possible) | seamless restore incl. integration + networking |

---

## 11) Notes: why “CONFIG_CHECKPOINT_RESTORE” is not enough
Kernel support for checkpoint/restore is necessary but insufficient:
- policy restrictions (seccomp, ptrace)
- virtualization integration channels
- device state (network/gpu)
- host orchestration expectations

We should treat the kernel config as “enables a path”, not “solves the feature”.

---

## 12) Immediate next actions (high signal)
1) Build `wslx diag snapshot-readiness`:
   - seccomp status of PID 1
   - ptrace restrictions
   - kernel config probes for CRIU-required features
2) Create POC “minimal checkpoint” demo app and validate restore loop.
3) Enumerate and classify WSL integration channels by restorable/non-restorable.

---
End.
```
