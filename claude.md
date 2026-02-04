# WSLX - Claude Code Instructions

## Project Overview

WSLX is a fork of Microsoft's WSL (Windows Subsystem for Linux) that runs side-by-side with the canonical WSL installation. The repository contains:

- **Windows components** (`src/windows/`, `src/shared/`) - Built on Windows with Visual Studio/CMake
- **Linux components** (`src/linux/`) - Built on Linux
- **Linux Kernel sources** (`Kernel/`) - Built exclusively on Linux/WSL

---

## CRITICAL: Kernel Directory - Platform-Specific Rules

> **THIS SECTION ONLY APPLIES TO AGENTS RUNNING IN A WINDOWS HARNESS**
> (PowerShell, cmd.exe, Windows Terminal, Git Bash on Windows, etc.)
>
> **Linux/WSL agents: You have FULL access to `Kernel/` - read, modify, commit, push freely.**

### The Problem (Windows Only)

The `Kernel/` directory contains the full Linux kernel source tree. This directory has **two incompatibilities with Windows**:

1. **Reserved filenames**: Files named `aux.c`, `aux.h` exist in the kernel (e.g., `Kernel/drivers/gpu/drm/nouveau/nvkm/subdev/i2c/aux.c`). "AUX" is a reserved device name on Windows dating back to DOS.

2. **Case-collision files**: The kernel contains file pairs that differ only by case (e.g., `xt_CONNMARK.h` and `xt_connmark.h`). Windows/NTFS is case-insensitive, so these files collide.

The repo has `core.protectNTFS=false` set to allow checkout. Additionally, ~13 kernel files with case collisions must be marked as "assume-unchanged" to hide them from git status.

### Windows Clone Setup (One-Time, WINDOWS ONLY)

After cloning on Windows, run this to hide the case-collision files (not needed on Linux):

```bash
# Mark case-collision kernel files as assume-unchanged
git status --short -- Kernel/ | awk '{print $2}' | xargs -I{} git update-index --assume-unchanged "{}"
```

If you see `Kernel/` files showing as modified in `git status`, run the command above to hide them.

### Rules for Windows Agents (WINDOWS ONLY)

**These rules ONLY apply when running in a Windows harness. Linux agents: ignore this section.**

#### DO NOT:
- Read files under `Kernel/` (use Linux/WSL agents for kernel work)
- Search/grep inside `Kernel/`
- Modify any files under `Kernel/`
- Include `Kernel/` paths in glob patterns
- Stage or commit changes to `Kernel/` files

#### DO:
- Exclude `Kernel/` from all file operations
- Focus on `src/`, `cmake/`, `tools/`, `msipackage/`, and other non-kernel directories
- Delegate any kernel-related work to Linux/WSL environments
- If kernel files appear in `git status`, run the assume-unchanged command from the setup section above

#### When searching for code:
```bash
# Exclude Kernel/ from searches
rg "some_pattern" --glob '!Kernel/**'
```

With the assume-unchanged setup applied, normal `git add .` operations are safe - the broken kernel files won't be staged. However, as a best practice, prefer explicit paths when staging:
```bash
git add src/ cmake/ tools/  # Be explicit about what to add
```

### How to Detect Your Environment

**You are in a WINDOWS harness (restrictions apply) if:**
- Working directory path starts with a drive letter (e.g., `D:\dev\wslx`, `C:\...`)
- Platform is `win32`, `windows`, or `msys`
- Shell is PowerShell, cmd.exe, or Git Bash on Windows

**You are in a LINUX harness (NO restrictions) if:**
- Working directory path starts with `/` (e.g., `/home/user/wslx`, `/mnt/...`)
- Platform is `linux`, `darwin`, or similar Unix
- Running inside WSL, Ubuntu, or any Linux distribution

### Rules for Linux/WSL Agents

**When running in Linux/WSL, you have FULL access to `Kernel/`:**

- ✅ Read any files under `Kernel/`
- ✅ Modify kernel source files
- ✅ Search/grep inside `Kernel/`
- ✅ Include `Kernel/` changes in commits
- ✅ Push branches with kernel modifications
- ✅ Build the kernel using standard Linux kernel build process

The kernel is a normal part of the repository when accessed from Linux. All restrictions in this document are exclusively for Windows environments.

---

## Build Information

### Windows Build (Visual Studio/CMake)
- Requires: Visual Studio 2022, Windows SDK 26100+, CMake 3.25+
- Does NOT compile anything from `Kernel/`
- The kernel is fetched as a pre-built NuGet package (`Microsoft.WSL.Kernel`)

### Linux/Kernel Build (WSL/Ubuntu)
- Kernel is built separately in Linux using standard kernel build process
- See `Kernel/README.md` for build instructions
- Requires: `build-essential flex bison dwarves libssl-dev libelf-dev`

---

## Key Directories

| Directory | Platform | Description |
|-----------|----------|-------------|
| `src/windows/` | Windows | Windows service, CLI, COM components |
| `src/linux/` | Linux | Linux-side utilities |
| `src/shared/` | Both | Shared headers and interfaces |
| `Kernel/` | **Linux only** | Linux kernel sources - DO NOT ACCESS FROM WINDOWS |
| `cmake/` | Windows | CMake build modules |
| `msipackage/` | Windows | MSI installer definitions |
| `tools/` | Both | Build and development tools |

---

## WSLX-Specific Files

Key files containing fork-specific modifications:

- `src/shared/inc/fork_identity.h` - All WSLX identity constants (GUIDs, names, ports)
- `src/windows/wslservice/inc/wslservice.idl` - COM interface definitions
- `msipackage/package.wix.in` - MSI installer configuration

When making changes, ensure WSLX identity constants remain centralized in `fork_identity.h`.
