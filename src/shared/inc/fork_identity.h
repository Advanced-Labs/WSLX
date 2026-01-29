// Copyright (c) WSLX Contributors
// SPDX-License-Identifier: MIT
//
// ============================================================================
// WSLX Fork Identity Constants
// ============================================================================
// This header defines all identity constants for the WSLX fork.
// Changing these values creates an independent SxS installation that can
// run alongside canonical WSL without conflicts.
//
// IMPORTANT: All values must be changed together for a consistent fork.
// Do not mix canonical and fork identities.
//
// Generated: 2026-01-28
// ============================================================================

#pragma once

// ============================================================================
// Build Mode
// ============================================================================
// Define WSLX_FORK_MODE to enable all fork identities.
// When not defined, canonical identities are used (for reference builds).

#define WSLX_FORK_MODE 1

#ifdef WSLX_FORK_MODE

// ============================================================================
// Service Identity
// ============================================================================

#define WSLX_SERVICE_NAME           L"WSLXService"
#define WSLX_SERVICE_DISPLAY_NAME   L"WSLX Service"
#define WSLX_SERVICE_DESCRIPTION    L"Provides support for running WSLX Linux distributions"

// ============================================================================
// COM Identity
// ============================================================================
// All GUIDs generated fresh for WSLX fork - do not reuse canonical GUIDs.

// CLSID for WslXUserSession (was: LxssUserSession)
// Canonical: {a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}
// Fork:      {21ad80bd-b800-4027-b84a-1e0d074ae507}
#define WSLX_CLSID_USER_SESSION \
    { 0x21ad80bd, 0xb800, 0x4027, { 0xb8, 0x4a, 0x1e, 0x0d, 0x07, 0x4a, 0xe5, 0x07 } }

// String form for IDL/registry
#define WSLX_CLSID_USER_SESSION_STRING L"{21ad80bd-b800-4027-b84a-1e0d074ae507}"

// AppID for service hosting
// Canonical: {370121D2-AA7E-4608-A86D-0BBAB9DA1A60}
// Fork:      {4424eff5-6510-481d-b912-606d81c43c52}
#define WSLX_APPID \
    { 0x4424eff5, 0x6510, 0x481d, { 0xb9, 0x12, 0x60, 0x6d, 0x81, 0xc4, 0x3c, 0x52 } }

// String form for IDL/registry
#define WSLX_APPID_STRING L"{4424eff5-6510-481d-b912-606d81c43c52}"

// ILxssUserSession interface IID
// Canonical: {38541BDC-F54F-4CEB-85D0-37F0F3D2617E}
// Fork:      {8b283ac3-d362-46f2-b68f-3ba6a1607cc8}
#define WSLX_IID_USER_SESSION \
    { 0x8b283ac3, 0xd362, 0x46f2, { 0xb6, 0x8f, 0x3b, 0xa6, 0xa1, 0x60, 0x7c, 0xc8 } }

// String form for IDL
#define WSLX_IID_USER_SESSION_STRING L"{8b283ac3-d362-46f2-b68f-3ba6a1607cc8}"

// ProxyStub CLSID
// Canonical: {4EA0C6DD-E9FF-48E7-994E-13A31D10DC60}
// Fork:      {27a1899d-d923-4ddd-91b7-454b90109e50}
#define WSLX_CLSID_PROXY_STUB \
    { 0x27a1899d, 0xd923, 0x4ddd, { 0x91, 0xb7, 0x45, 0x4b, 0x90, 0x10, 0x9e, 0x50 } }

// String form for registry
#define WSLX_CLSID_PROXY_STUB_STRING L"{27a1899d-d923-4ddd-91b7-454b90109e50}"

// ============================================================================
// Registry Identity
// ============================================================================

// User registry path for distro registration
// Canonical: Software\Microsoft\Windows\CurrentVersion\Lxss
// Fork:      Software\Microsoft\Windows\CurrentVersion\WslX
#define WSLX_REGISTRY_PATH L"Software\\Microsoft\\Windows\\CurrentVersion\\WslX"

// ============================================================================
// HNS Network Identity
// ============================================================================

// NAT Network GUID (legacy, without firewall)
// Canonical: {b95d0c5e-57d4-412b-b571-18a81a16e005}
// Fork:      {9437b4d2-808d-4521-b349-a0467c5eb190}
#define WSLX_NAT_NETWORK_ID \
    { 0x9437b4d2, 0x808d, 0x4521, { 0xb3, 0x49, 0xa0, 0x46, 0x7c, 0x5e, 0xb1, 0x90 } }

// NAT Network GUID (with Hyper-V firewall)
// Canonical: {790e58b4-7939-4434-9358-89ae7ddbe87e}
// Fork:      {bdc5e13c-a7b3-4794-978d-4238a6a41144}
#define WSLX_NAT_NETWORK_FIREWALL_ID \
    { 0xbdc5e13c, 0xa7b3, 0x4794, { 0x97, 0x8d, 0x42, 0x38, 0xa6, 0xa4, 0x11, 0x44 } }

// Network names
#define WSLX_NAT_NETWORK_NAME           L"WSLX"
#define WSLX_NAT_NETWORK_FIREWALL_NAME  L"WSLX (Hyper-V firewall)"

// ============================================================================
// HCS Identity
// ============================================================================

// VM Owner string for HCS compute systems
// Canonical: "WSL"
// Fork:      "WSLX"
#define WSLX_VM_OWNER L"WSLX"

// ============================================================================
// HvSocket Ports
// ============================================================================
// Offset by +1000 from canonical to avoid collision.
// Canonical: 50000-50005
// Fork:      51000-51005

#define WSLX_INIT_PORT                  (51000)
#define WSLX_PLAN9_PORT                 (51001)
#define WSLX_PLAN9_DRVFS_PORT           (51002)
#define WSLX_PLAN9_DRVFS_ADMIN_PORT     (51003)
#define WSLX_VIRTIOFS_PORT              (51004)
#define WSLX_CRASH_DUMP_PORT            (51005)

// ============================================================================
// Named Objects
// ============================================================================

// Global mutex for install logging
// Canonical: Global\WslInstallLog
// Fork:      Global\WslXInstallLog
#define WSLX_INSTALL_LOG_MUTEX L"Global\\WslXInstallLog"

// Debug shell pipe prefix
// Canonical: wsl_debugshell_
// Fork:      wslx_debugshell_
#define WSLX_DEBUG_SHELL_PIPE_PREFIX L"wslx_debugshell_"

// ============================================================================
// Shell/Explorer Integration
// ============================================================================

// Shell folder CLSID for Explorer integration
// Canonical: {B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}
// Fork:      {b0a2dcf3-a071-44b6-a4e8-11d1dcc1733c}
#define WSLX_SHELL_FOLDER_CLSID \
    { 0xb0a2dcf3, 0xa071, 0x44b6, { 0xa4, 0xe8, 0x11, 0xd1, 0xdc, 0xc1, 0x73, 0x3c } }

// String form for registry
#define WSLX_SHELL_FOLDER_CLSID_STRING L"{b0a2dcf3-a071-44b6-a4e8-11d1dcc1733c}"

// Shell folder display name
#define WSLX_SHELL_FOLDER_NAME L"Linux (WSLX)"

// ============================================================================
// MSI Identity
// ============================================================================

// MSI UpgradeCode - must be unique per product line
// Canonical: {6D5B792B-1EDC-4DE9-8EAD-201B820F8E82}
// Fork:      {abb3b349-a099-4615-97ba-7f1b0729ea5c}
// Note: This is used in package.wix.in, not in C++ code
#define WSLX_MSI_UPGRADE_CODE "{abb3b349-a099-4615-97ba-7f1b0729ea5c}"

// Install directory
#define WSLX_INSTALL_DIR L"WSLX"

#endif // WSLX_FORK_MODE

// ============================================================================
// GUID Reference Table (for documentation)
// ============================================================================
//
// | Purpose                | Canonical GUID                           | Fork GUID                                |
// |------------------------|------------------------------------------|------------------------------------------|
// | COM CLSID (UserSession)| {a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}   | {21ad80bd-b800-4027-b84a-1e0d074ae507}   |
// | COM AppID              | {370121D2-AA7E-4608-A86D-0BBAB9DA1A60}   | {4424eff5-6510-481d-b912-606d81c43c52}   |
// | COM IID (IUserSession) | {38541BDC-F54F-4CEB-85D0-37F0F3D2617E}   | {8b283ac3-d362-46f2-b68f-3ba6a1607cc8}   |
// | COM ProxyStub CLSID    | {4EA0C6DD-E9FF-48E7-994E-13A31D10DC60}   | {27a1899d-d923-4ddd-91b7-454b90109e50}   |
// | HNS Network (legacy)   | {b95d0c5e-57d4-412b-b571-18a81a16e005}   | {9437b4d2-808d-4521-b349-a0467c5eb190}   |
// | HNS Network (firewall) | {790e58b4-7939-4434-9358-89ae7ddbe87e}   | {bdc5e13c-a7b3-4794-978d-4238a6a41144}   |
// | Shell Folder CLSID     | {B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}   | {b0a2dcf3-a071-44b6-a4e8-11d1dcc1733c}   |
// | MSI UpgradeCode        | {6D5B792B-1EDC-4DE9-8EAD-201B820F8E82}   | {abb3b349-a099-4615-97ba-7f1b0729ea5c}   |
//
// ============================================================================
