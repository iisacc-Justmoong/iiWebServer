if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(EXISTS "${SOURCE_ROOT}/src/iiWebServer")
    message(FATAL_ERROR "The source tree must not repeat the project name")
endif()
foreach(_file Http/HttpServer.h Http/HttpServer.cpp Http/HttpExport.h
        Compatibility/iiWebServer.h Compatibility/iiWebServer.cpp
        Transport/NetworkTransport.h Transport/NetworkTransport.cpp
        Protocol/Protocol.h Protocol/BinaryFrame.h Peers/LanPeer.h Peers/LanPeer.cpp
        FileSharing/FileShare.cpp Transfer/Transfer.h Transfer/Transfer.cpp
        Storage/StorageBridge.h Storage/StorageBridge.cpp)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
