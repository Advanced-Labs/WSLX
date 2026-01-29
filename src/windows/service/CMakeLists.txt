# WSLX Fork: Updated ProxyStub CLSID for SxS operation
# Canonical: {4EA0C6DD-E9FF-48E7-994E-13A31D10DC60}
# Fork:      {27a1899d-d923-4ddd-91b7-454b90109e50}
add_compile_definitions("PROXY_CLSID_IS={0x27a1899d,0xd923,0x4ddd,{0x91,0xb7,0x45,0x4b,0x90,0x10,0x9e,0x50}}")
add_compile_definitions("REGISTER_PROXY_DLL")

add_subdirectory(exe)
add_subdirectory(inc)
add_subdirectory(stub)
add_subdirectory(mc)