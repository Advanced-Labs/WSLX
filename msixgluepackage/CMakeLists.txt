add_appx_target(msixgluepackage
                "wslx.exe"
                "${CMAKE_CURRENT_LIST_DIR}/AppxManifest.in"
                "${BIN}/gluepackage.msix"
                "wslx")
