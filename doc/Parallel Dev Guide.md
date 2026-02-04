# WSLX Manual for AI Agents
## Dual/Parallel Development: WSL ↔ Regular Linux (Distro/Baremetal) for
1) Kernel Modules, and 2) Kernel-Core Changes

This manual defines terminology and gives operational workflows for maintaining:
- One module codebase that builds/runs on both:
  - WSL kernel (Microsoft downstream kernel tree)
  - Regular Linux kernel (upstream 6.6.y longterm baseline)
- One kernel-core patchset that is carried across both trees safely.

Target WSL kernel tag:
- linux-msft-wsl-6.6.114.1 (WSL2-Linux-Kernel repo)

Recommended regular Linux kernel baseline:
- Upstream “6.6.y longterm” (same major/minor line as WSL’s 6.6 base)


--------------------------------------------------------------------------------
## 0) Vocabulary: Upstream vs Downstream

Upstream:
- The canonical source line where features/bugfixes originate and where the “core”
  evolution happens (e.g., mainline/stable Linux 6.6.y).

Downstream:
- A consumer/fork/vendor-maintained line derived from upstream, carrying extra
  patches/config for a specific product/platform (e.g., WSL’s kernel).

Why it matters:
- Dual-targeting is mostly about keeping “our patchset” portable and rebasing it
  onto two different bases: (A) upstream 6.6.y, and (B) WSL’s 6.6.y + WSL patches.


--------------------------------------------------------------------------------
## 1) Dual-targeting KERNEL MODULES (Out-of-tree modules)

### 1.1 Core rule: Modules must match the running kernel build
A kernel module must be compiled against the headers/config of the exact target
kernel build (or a very close match). Practically:
- Build once per target kernel flavor (WSL vs upstream/distro kernel).
- Do NOT expect a single .ko binary to load everywhere.

### 1.2 Goal architecture: one source tree, multiple build artifacts
Keep:
- One shared module source tree.
Produce:
- One .ko per target kernel line (at minimum):
  - wsl: linux-msft-wsl-6.6.114.1
  - upstream: linux 6.6.y (choose a pinned tag/commit)

### 1.3 Repository layout (recommended)
repo/
  Kconfig
  Makefile               # standard kbuild out-of-tree pattern
  src/
    ...module code...
  compat/
    compat.h             # minimal shims, compile-time feature gates
    kver.h               # if needed; prefer feature checks over pure version checks
  ci/
    build_wsl.sh
    build_upstream_6_6.sh
    test_smoke.sh
  packaging/
    dkms/                # optional: DKMS packaging skeleton
    deb/ rpm/            # optional: distro packages if desired

### 1.4 Portability techniques (preferred order)
1) Use stable subsystem APIs and official registration points.
2) Feature-based compile gates:
   - #if IS_ENABLED(CONFIG_FOO)
   - #ifdef HAVE_SOME_SYMBOL
3) Small compat layer:
   - Map renamed helpers/struct fields in compat/compat.h
4) Automated compile probes (advanced):
   - CI attempts tiny compile checks and generates autoconf_compat.h

### 1.5 Build workflows

#### A) Standard kbuild out-of-tree build (both targets)
Given kernel build dir KDIR:
  make -C $KDIR M=$PWD modules

Artifacts:
  *.ko (module binary)
  Module.symvers / build logs

#### B) Installing modules (regular Linux distro)
- Copy/install into:
  /lib/modules/$(uname -r)/extra/<yourmodule>.ko
- Run:
  depmod -a
  modprobe <yourmodule>

Optional: DKMS packaging
- Use DKMS to rebuild per kernel update on that distro.

#### C) Installing modules for WSL
Two operational models:

Model C1: Build/install inside the WSL distro (DKMS-like)
- Requires matching kernel headers for the exact WSL kernel version.
- Works well for dev machines; can be fragile if headers lag.

Model C2: Ship a pinned WSL kernel + modules bundle
- Most robust if you control deployment.
- Keep kernel + modules version-locked, and configure WSL to use them.
- Good when modules are “product-critical” and must load reliably.

### 1.6 Test matrix (modules)
Minimum CI lanes:
- Lane L1: Build against upstream 6.6.y (pinned) + smoke load/unload test
- Lane L2: Build against WSL tag linux-msft-wsl-6.6.114.1 + smoke load/unload test

Smoke test examples:
- insmod/rmmod succeeds
- modinfo/”vermagic” matches running kernel
- basic functional probe:
  - if char device: open/ioctl minimal
  - if net: create interface / simple netlink query
  - if sysfs: validate nodes appear and behave

AI Agent responsibilities:
- Pull/pin kernel sources
- Build both lanes
- Detect compile errors and propose compat fixes
- Run smoke tests and summarize results
- Produce artifacts (.ko, logs, packaging bundle)


--------------------------------------------------------------------------------
## 2) Dual-targeting KERNEL-CORE CHANGES (in-tree modifications)

### 2.1 Why “same major/minor line” (6.6.y) is safer
Kernel internals churn quickly across major/minor versions. Keeping both targets
on 6.6.y reduces:
- API/struct refactors between versions
- merge conflicts
- semantic drift in subsystems

Interpretation:
- Treat upstream 6.6.y as the “clean base”
- Treat WSL 6.6.y as “clean base + WSL patchset/config”
- Carry our feature as a patch series that replays onto both.

### 2.2 Which kernel should be primary for implementation/testing first?

Default: Upstream 6.6.y first, then port to WSL
Reasons:
- Cleaner substrate: fewer downstream deltas
- Wider test surface (VMs, CI, baremetal)
- Avoid baking WSL-specific assumptions into generic logic

Exception: WSL-specific integration changes
If the change fundamentally relies on WSL integration plumbing:
- Prototype in WSL kernel first (because it is the ground-truth environment)
- Then factor and port generic parts back to upstream-like base

### 2.3 Source control model: patch series, not ad-hoc merges
Use a patch-stack discipline:
- base/upstream-6.6.y          (tracks upstream stable line)
- base/wsl-6.6.114.1           (tracks WSL tag line)
- topic/our-feature             (our patch series)

Rules:
- Keep “our feature” as a linear, reviewable series of commits.
- Regularly rebase topic onto each base:
  - topic → upstream base
  - topic → wsl base
- Use tooling to preserve intent across rebases:
  - git rerere: re-use conflict resolutions
  - git range-diff: verify patch intent stays consistent

### 2.4 CI lanes (kernel-core changes)

Lane K1: Upstream 6.6.y validation (regular Linux)
- Build kernel
- Boot in VM (QEMU/KVM or Hyper-V)
- Run targeted tests:
  - kselftest subsets relevant to touched subsystems
  - any subsystem-specific tests (e.g., net tests, fs tests)
- Optional: boot on baremetal lab if available

Lane K2: WSL kernel validation
- Build WSL kernel with our patchset
- Boot in WSL environment configured to use that kernel
- Run:
  - smoke checks
  - our feature tests
  - any relevant selftests feasible under WSL constraints

Hyper-V note:
- Hyper-V is a valid VM environment for running a regular Linux distro to test
  an upstream 6.6.y kernel. It is “good enough” for most kernel validation,
  with the main differences being ecosystem/tooling convenience (many kernel
  scripts assume QEMU/KVM, but the underlying idea is the same).

### 2.5 kselftest (high-level)
What it is:
- In-kernel selftests living in the kernel source tree:
  tools/testing/selftests/

How it’s run (conceptual):
- Build tests
- Run tests in the target system (VM or baremetal) on the kernel under test
- Use subset selection where possible to keep runtime reasonable

Agent guidance:
- Do not run the entire suite by default
- Run only relevant directories to the changed subsystem(s)
- Keep a short, stable “smoke subset” plus an expanded “nightly” subset

### 2.6 AI agent tasking for kernel-core dual maintenance

Automation-friendly tasks:
- Sync/update base branches (upstream 6.6.y, WSL tag)
- Rebase patch series onto each base
- Build both kernels
- Boot/test in:
  - VM lane for upstream (Hyper-V or QEMU/KVM)
  - WSL lane for downstream
- Summarize:
  - conflicts
  - failing tests
  - performance deltas (if measured)
- Suggest minimal patches:
  - compat adjustments
  - config toggles
  - subsystem-specific fixes

Human-required review points:
- Semantic correctness of conflict resolutions
- Any changes impacting memory ordering/locking
- Security boundary changes
- ABI/UAPI changes exposed to userland


--------------------------------------------------------------------------------
## 3) Recommended baseline choices

### 3.1 Kernel baselines
- WSL target: linux-msft-wsl-6.6.114.1 (pinned)
- Regular Linux target: upstream 6.6.y longterm (pinned tag/commit)

### 3.2 Distro choices (for “regular Linux” test lane)
Goal: minimize variance, keep kernel close to 6.6.y.
Approach:
- Use a distro VM where you can boot a pinned upstream 6.6.y kernel easily.
- The distro userland is less important than the kernel line for the test lane,
  as long as it supports your tooling and test harness.

Operational recommendation:
- Maintain a “kernel-test VM image” (or script) that:
  - installs build deps
  - boots a pinned upstream 6.6.y kernel
  - runs your test harness + selected kselftests


--------------------------------------------------------------------------------
## 4) Deliverables checklist (what agents must produce each cycle)

For modules:
- Build logs (WSL + upstream)
- .ko artifacts (WSL + upstream)
- Smoke test logs (load/unload + minimal functional)

For kernel-core:
- Patch series (clean, linear)
- Build artifacts (vmlinux, bzImage, modules where applicable)
- Boot proof (console log)
- Test proof (selected kselftest results)
- Conflict report + applied resolutions (if rebased)
- Summary: “works on upstream lane” and “works on WSL lane” with details


--------------------------------------------------------------------------------
## 5) When to stop and escalate
Escalate to humans when:
- Conflicts touch core locking/MM/scheduler semantics
- Tests fail in subtle, non-deterministic ways
- WSL lane fails due to missing integration features/config differences that
  require architectural decisions
- Any change impacts UAPI/ABI stability expectations
