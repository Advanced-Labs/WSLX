````md
# Gemini Project (Host↔Guest Wormhole for Windows↔Linux in WSL2)
**Audience:** AI implementors, systems engineers  
**Context:** Windows = host, Linux = WSL2 guest  
**Shared memory subsystem:** **Portal**

---

## 0. Executive Summary

**Gemini** is a bidirectional cross-OS execution fabric between **Linux (guest)** and **Windows (host)**. It enables:

- **L→W projection:** Linux processes invoke real Windows APIs (Win32/NT/COM/etc.) executed on the Windows host.
- **W→L projection:** Windows processes invoke real Linux APIs (syscalls/POSIX/libc/etc.) executed in the Linux guest.

Gemini is layered:

1. **Gemini Native Layer** (OS/runtime agnostic): sessions, RPC, Portal shared memory, capability tokens, policies, events/upcalls, and transport adapters.
2. **Gemini Runtime Layer** (language/runtime integration): makes wormhole calls *feel native* in target runtimes, starting with **.NET** (Linux + Windows CLR integration).

---

## 1. Non-Goals / Explicit Constraints

### In scope
- **WSL2** host/guest topology only (Windows host ↔ Linux guest).
- Strong support for **paired process** model (one process per side per session), while allowing later multiplexing.

### Out of scope
- Bare-metal Linux host ↔ Windows guest VM (explicitly not cared about here).
- Providing a complete general-purpose “OS emulation” as a first milestone (Gemini is a fabric; “full OS personality” is optional and layered).

---

## 2. Terminology

- **Host:** Windows machine running WSL2.
- **Guest:** Linux VM under WSL2.
- **Direction L→W:** Linux guest calls into Windows host.
- **Direction W→L:** Windows host calls into Linux guest.
- **Session:** A bidirectional channel between a *caller* and an *executor* across OS boundary.
- **Executor process:** The process where calls actually execute (real OS APIs run here).
- **Paired process model:** (Linux process) ↔ (Windows process), 1:1 by default.
- **Capability token:** Session-local opaque identifier for remote resources (Windows HANDLE, Linux fd, COM object, etc).
- **Portal:** Shared memory arenas + rings + signaling for low-copy marshalling.
- **Domain:** API family namespace (WIN32, NT, COM, POSIX, LINUX_SYSCALL, etc).

---

## 3. Design Objectives

1. **Power first, safety by policy**: support broad API routing early, then tighten with policies and allowlists.
2. **Deterministic data movement**: no raw pointers cross boundary; Portal offsets/spans only.
3. **Symmetry**: L→W and W→L share the same session/RPC/Portal machinery.
4. **Incremental performance**: control-plane-only prototype first; Portal activates progressively.
5. **Runtime “naturalness”**: after native layer is stable, make .NET behave as if the foreign OS APIs are local (via interceptors/resolvers/runtime patches).

---

## 4. Overall Architecture

### 4.1 Two-plane fabric

#### Plane A — Control Plane (RPC)
- Session creation/teardown
- Capability negotiation
- Call dispatch, cancellation, returns
- Small argument marshalling
- Event and upcall routing
- Heartbeats, flow control, crash recovery signals

#### Plane B — Data Plane (Portal)
- Shared-memory regions mapped into both sides
- Arena allocators (call-local + session-local)
- Ring buffers for CALL/RETURN (and optional EVENT/UPCALL)
- Bulk buffers and structured blobs
- Zero/low-copy patterns

---

## 5. Process Model

### 5.1 Default: Paired process model (recommended)
- Each caller process is paired with one executor process on the other OS.
- Remote resources remain local to the executor process; the caller only holds capability tokens.

**Benefits**
- Handle virtualization becomes trivial and session-scoped.
- Security is naturally tied to Windows user token / Linux uid context.
- Lifetime cleanup becomes deterministic: session teardown = kill paired executor = close everything.
- Enables bidirectional callbacks safely (within a session boundary).

### 5.2 Alternatives (later)
- **Multiplexed executor** (many callers → one executor): reduces processes, increases complexity.
- **Broker service**: central host/guest services coordinate sessions and optionally proxy calls.

**Guidance:** do not start with multiplexing.

---

## 6. Transport Adapters

Gemini must be transport-pluggable. Implement at least:

### 6.1 Control plane transport
- **Preferred:** Hyper-V sockets / vsock (host↔guest optimized)
- **Fallbacks:** TCP loopback (dev), named pipes (if feasible), unix sockets inside guest (internal)

Transport requirements:
- Message-oriented framing or stream with robust framing layer
- Bidirectional async IO
- Low latency and stable reconnect semantics
- Ability to map “session identity” reliably (see 6.3)

### 6.2 Portal mapping transport (shared memory)
Portal requires:
- A host↔guest shared memory mechanism (ideal)
- OR a control-plane fallback (copy path)

Portal mapping options (implementation choices):
- **True shared memory** (best): VMBus/GPADL-like mapping or equivalent
- **Emulated shared memory** (fallback): mirrored buffers + invalidation protocol (slower, complex)

### 6.3 Session identity & discovery
You need a way for Linux caller to reach the correct host endpoint (and vice versa):
- Stable endpoint naming scheme (per-VM, per-user, per-session)
- Token-based handshake to bind Linux process ↔ Windows executor process
- Reconnect strategy when VM restarts or host service restarts

---

## 7. Portal (Shared Memory System)

### 7.1 Portal primitives

- **PortalRegion**: a shared memory segment, mapped into both processes.
- **PortalArena**: allocator within a region (supports call-local and session-local).
- **PortalSpan**: `(portal_id, offset, length, flags, generation)`
- **Rings**:
  - **SubmitRing**: CALL descriptors (producer: caller; consumer: executor)
  - **CompleteRing**: RETURN descriptors (producer: executor; consumer: caller)
  - Optional **EventRing**: async events / notifications
  - Optional **UpcallRing**: reverse calls/callbacks

### 7.2 No cross-boundary pointers
All “pointer” parameters become PortalSpans or scalar offsets.
No raw `void*` crosses boundary.

### 7.3 Signaling (“doorbells”)
Options:
- Transport-based notify (small control message “ring advanced”)
- Event primitives per session (preferred once stable)
- Hybrid: notify on threshold / periodic polling

### 7.4 Memory safety requirements
- Bounds checks for every span access
- Generation numbers to prevent UAF on reused offsets
- Optional canaries/poisoning in debug mode
- Optional zeroization for sensitive spans

### 7.5 Allocators (where mimalloc etc. can matter)
PortalArena can plug in:
- simple bump allocator (call-local)
- slab allocator (structured args)
- mimalloc-like allocator tuned for fragmentation and concurrency (session-local)

---

## 8. RPC Protocol Spec (Control Plane)

### 8.1 Protocol goals
- Deterministic binary layout, versioned
- Efficient marshalling for scalars + PortalSpans
- Async-first, sync as convenience
- Supports cancellation, timeouts, flow control
- Supports upcalls safely

### 8.2 Message types (minimum)
- `HELLO`, `HELLO_ACK`
- `CAPS`, `CAPS_ACK`
- `OPEN_SESSION`, `OPEN_SESSION_ACK`
- `CLOSE_SESSION`
- `CALL`, `RETURN`
- `EVENT`
- `UPCALL`, `UPRETURN` (or unify under CALL/RETURN with direction flag)
- `CANCEL`, `CANCEL_ACK`
- `PING`, `PONG`
- `FAULT` (session fault notification)

### 8.3 Versioning rules
- Protocol version `major.minor`
- Major bump for breaking binary/semantic changes
- Minor bump for additive fields with length-prefixed encoding

### 8.4 Recommended encoding strategy
- Fixed header + TLV payload
- OR fixed structs for hot path (CALL/RETURN) + TLVs for optional metadata

#### Suggested fixed header (conceptual)
```text
struct MsgHeader {
  u32 magic;         // "GEMI"
  u16 major;
  u16 minor;
  u16 type;          // enum
  u16 flags;
  u32 length;        // bytes following header
  u64 session_id;
  u64 message_id;    // monotonically increasing per session
}
````

---

## 9. Call Model

### 9.1 Domains and API identifiers

Calls are addressed by:

* `domain` (enum)
* `op` (u32 or u64)
* Optional `interface_id` (for COM-style interface selection)
* Optional `symbol_hash` (for late-bound exports)

### 9.2 Argument slots

Each arg is one of:

* scalar (`u64`, `i64`, `double`)
* handle token (`GemHandle`)
* string (UTF-8/UTF-16 encoded into PortalSpan)
* span (`PortalSpan`)
* struct blob (`PortalSpan` + type tag)
* arrays (`PortalSpan` + count/stride + type tag)

### 9.3 Return model

Return includes:

* `domain_status` (e.g., HRESULT, NTSTATUS, errno, GetLastError)
* `gemini_status` (transport/protocol/caller fault)
* optional out-args (same slot types)
* diagnostics TLV (optional)

### 9.4 Cancellation and timeouts

* Caller may send `CANCEL(call_id)`
* Executor must attempt cooperative cancel:

  * if safe: abort operation and return cancellation status
  * if not: mark as “cancel requested” and return when possible
* Timeouts are advisory; enforcement can be implemented by executor policy.

### 9.5 Flow control

* Credits for ring space or outstanding calls
* Backpressure signaling to prevent Portal exhaustion

---

## 10. Capability Token Model

### 10.1 Tokens, not raw OS handles

Remote HANDLE/fd/object pointers must never be exposed directly.

### 10.2 Token table is per session

* `GemHandle` = opaque u64
* Maps to:

  * Windows HANDLE
  * Linux fd
  * COM object reference
  * Shared state object (pipes, memory maps, etc.)

### 10.3 Lifetime rules

* Explicit close: `HANDLE_CLOSE(GemHandle)`
* Implicit close: session teardown
* Optional duplication: `HANDLE_DUP` (rarely needed early)

### 10.4 Rights enforcement

Each token carries:

* object kind
* rights bitmask
* origin domain
* debug info (optional)

---

## 11. Security Model

### 11.1 Trust boundaries

* The host (Windows) is the primary authority.
* Guest calls are “remote” even if same machine.
* All calls are subject to policy.

### 11.2 Policy module (must exist even in prototype)

Minimum policy features:

* Denylist catastrophic ops (disk wipe equivalents, raw device writes, privileged process injection)
* Logging and auditing
* “Development mode” overrides behind explicit flags
* Rate limiting per session for high-cost operations

### 11.3 Identity binding

* L→W: Windows executor process runs under a user token chosen by host service policy.
* W→L: Linux executor process runs under a uid/gid/capability context chosen by guest service policy.

### 11.4 Optional future security

* Mutual authentication (key exchange)
* Attestation between host/guest services
* Per-domain isolation or sandboxing on executor side

---

## 12. Symmetric Directions (Detailed)

# Part A — L→W (Linux calls Windows host)

## A1. Execution placement

**Default:** paired Windows executor process per Linux caller.

## A2. What can be called?

* Broad: Win32, NT native APIs, COM, services
* Mechanism: RPC CALL(domain=WIN32/NT/COM, op=…)
* For “everything early prototype,” begin with wide exposure but gated by policy.

## A3. COM strategy options

1. **COM stays entirely in Windows executor** (recommended)

   * Linux gets COM tokens; invocation is remoted.
2. Full distributed COM semantics (very hard; likely unnecessary early).

## A4. Wait/synchronization

Windows has waitable handles; faithfully exporting `WaitForMultipleObjects` across boundary is complex.
Recommended staging:

* Phase 1: async call completion only, no general waits.
* Phase 2+: introduce a “wait broker” concept.

---

# Part B — W→L (Windows calls Linux guest)

## B1. Execution placement

**Default:** paired Linux executor process per Windows caller.

## B2. Linux API export choices

1. **Syscall ABI export** (lowest level; maximal complexity)
2. **POSIX/libc export** (recommended for early implementation)
3. **Service export** (dbus/HTTP/gRPC bridging; complementary)

## B3. Syscall ABI modeling requirements (if chosen)

* syscall ID namespace, arch calling conventions normalized
* fd table and file descriptor semantics
* signals/interruption semantics (or explicitly absent initially)
* futex/poll/epoll semantics (hard)
* memory pointers become PortalSpans

Recommended staging:

* Start with POSIX-like operations (open/read/write/close/stat/poll-ish) before raw syscall exports.

---

## 13. Native SDK (Gemini Native Layer)

### 13.1 Deliverables

* Linux: `libgemini.so`, `gemini-guestd` (daemon)
* Windows: `gemini.dll`, `GeminiHostSvc`, `gemini_executor.exe` template

### 13.2 C ABI surface (sketch)

Core session:

* `gemini_open_session(...)`
* `gemini_close_session(...)`

Calls:

* `gemini_call_async(...)`
* `gemini_call_poll(...)` / `gemini_call_wait(...)`
* `gemini_cancel_call(...)`

Portal:

* `portal_create_region(...)`
* `portal_map_region(...)`
* `portal_alloc(...)`, `portal_free(...)`
* `portal_span_validate(...)`

Handles:

* `gemini_handle_close(...)`
* `gemini_handle_query(...)`

Events/upcalls:

* `gemini_register_upcall_handler(...)`
* `gemini_pump_events(...)`

---

## 14. Runtime Layer: .NET Integration (First-class target)

Gemini Runtime Layer makes foreign APIs “natural” to use in .NET.

### 14.1 .NET on Linux → Windows APIs (L→W)

**Goal:** `DllImport("kernel32.dll")` et al. “just work” on Linux by routing through Gemini.

Interception tiers:

1. **Managed resolver** (fast path)

   * Use a DllImport resolver to remap Windows DLL imports to Gemini shim libraries.
2. **CLR native loader patch** (natural mode)

   * Patch Linux CoreCLR’s unmanaged library resolution to recognize Win32 module names and route to Gemini automatically.
3. **Interop stub enhancements**

   * Portal-backed marshalling for large buffers/strings/structs
   * Efficient pinning strategies

Callbacks:

* Windows executor sends UPCALL to Linux runtime adapter, which enters CLR and invokes managed delegates.

### 14.2 .NET on Windows → Linux APIs (W→L)

**Goal:** Windows .NET can `DllImport("libc.so.6")` or use a stable “liblinux” shim routed to Gemini.

Same tiering as above:

* resolver first, runtime patch later

Portal-backed marshalling is even more valuable for:

* large reads/writes
* vectors (iovecs)
* struct-by-pointer patterns

---

## 15. Cross-Architecture and ABI Notes (Do Not Ignore)

Gemini must explicitly define:

* pointer width: 32 vs 64 (prefer 64-only for early milestones)
* endianness (likely little-endian only initially)
* struct packing and alignment rules (must be specified per domain)
* string encoding strategy (UTF-8 internal vs domain-native encoding)
* calling conventions (do not “remote call by stack”; always marshal to canonical slots/spans)

Recommendation:

* Start with **x86_64 host + x86_64 guest**, 64-bit only.
* Add 32-bit and ARM64 later with explicit ABI packs.

---

## 16. Reliability: Faults, Recovery, and Crash Semantics

### 16.1 Fault classes

* Transport fault (disconnect)
* Executor crash
* Caller crash
* Portal corruption / validation failure
* Policy violation

### 16.2 Required behaviors

* If caller dies: paired executor dies (default).
* If executor dies: session faults; caller receives `FAULT` and all outstanding calls fail.
* Portal corruption triggers session quarantine and teardown (fail closed).

### 16.3 Idempotency

Calls must declare:

* idempotent vs non-idempotent (hint to retry logic)
* retries should be policy-governed and conservative

---

## 17. Observability

Minimum:

* per session: open/close, identity, transport, Portal enabled/disabled
* per call: domain/op, duration, bytes moved, Portal allocations, error codes
* policy decisions: allow/deny, rationale code
* crash dumps/logs for executors (configurable)

---

## 18. Testing Strategy (AI Implementor Guidance)

### 18.1 Test tiers

1. Protocol tests (encoding/decoding, versioning, fuzz)
2. Portal tests (allocator correctness, bounds, ring wrap, concurrency)
3. Domain tests (Win32 subset, POSIX subset)
4. Stress tests (high concurrency, large transfers, cancellation storms)
5. Fault injection (disconnect mid-call, executor kill, Portal corruption)
6. Security tests (policy bypass attempts, malformed descriptors)

### 18.2 Fuzzing targets

* CALL descriptors (slot parsing)
* PortalSpan validation
* Ring buffer indices
* Domain-specific structured blobs

---

## 19. Reuse Notes: Wine / gVisor / Gramine / LKL

### 19.1 Wine (highest reuse for L→W façade)

Likely reusable components:

* API surface definitions (headers/IDLs/constants)
* Export-layer scaffolding (stubs that can be rewritten to RPC forwarders)
* Conformance tests to verify behaviors match real Windows

Recommended approach:

* Use Wine as a *client façade generator/test suite*, not as an “implementation.”

### 19.2 gVisor/Gramine/LKL (more relevant to W→L ABI and modeling)

* Useful for Linux ABI modeling patterns and (depending on license strategy) selective reuse.
* LKL implies GPL gravity; if used, keep it isolated as a separate process/component.

---

## 20. Roadmap (Phased, Implementable)

### Phase 0 — Control Plane MVP

* vsock/hvsock endpoint + HELLO/CAPS
* session open/close
* CALL/RETURN with copy-marshalling
* paired executor spawn/attach on both sides

### Phase 1 — Portal MVP

* one PortalRegion per session
* SubmitRing + CompleteRing
* PortalArena bump allocator (call-local)
* doorbell notifications

### Phase 2 — Core primitives (both directions)

* L→W: file IO + process spawn + basic sys info
* W→L: file IO + process spawn + basic sys info
* handle close/lifetime correctness

### Phase 3 — Upcalls + Cancellation

* UPCALL/UPRETURN
* cancellation protocol
* reentrancy rules + deadlock avoidance

### Phase 4 — .NET Runtime integration

* resolver-based routing
* optional CoreCLR patches for “natural import”
* Portal-backed marshalling helpers

### Phase 5+ — Expansion

* broader domains (COM subset, async IO, wait semantics, services)
* multiplexing (optional)
* stronger auth/attestation (optional)

---

## 21. Implementation Checklist (Minimal “Done” Definition)

1. Stable protocol encoding + versioning
2. Host service + guest daemon with session manager
3. Paired executors with deterministic teardown
4. Portal MVP (rings + arena + validation)
5. Capability token table and lifetime rules
6. Policy module with denylist + auditing
7. Bidirectional calls for a core primitive set
8. Upcalls for callbacks/events
9. Observability + fuzz harnesses
10. .NET resolver adapter (at least) for both directions

---

## 22. Summary

Gemini is an OS-bridge fabric for WSL2 with:

* a **native layer** (sessions, RPC, Portal, capabilities, policies)
* a **runtime layer** (starting with .NET) to make cross-OS calls feel native

“Everything” is approached by enabling broad routing early while enforcing **policy** and **capability tokens**, then tightening semantics and expanding performance via **Portal**.

---

```
::contentReference[oaicite:0]{index=0}
```
