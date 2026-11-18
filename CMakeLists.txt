cmake_minimum_required(VERSION 3.24)
project(iiWebServer VERSION 0.6.2 LANGUAGES CXX)
include(./cmake/ModuleImports.cmake)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
if(PROJECT_IS_TOP_LEVEL AND UNIX AND CMAKE_CURRENT_BINARY_DIR STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/build")
    include(cmake/ResourceLinks.cmake)
    web_compat_links("${CMAKE_CURRENT_SOURCE_DIR}/resources" "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    set(_web_user_home "$ENV{HOME}")
    if(_web_user_home STREQUAL "")
        set(_web_user_home "$ENV{USERPROFILE}")
    endif()
    set(IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR
        "${_web_user_home}/.local/SDK/iiFileProvider/share/iiFileProvider/python"
        CACHE PATH "iiFileProvider Python adapter used by the resource test runner")
    unset(_web_user_home)
    function(web_resource_test name)
        add_test(NAME "${name}" COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/resource_runner.py"
            --provider-python "${IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR}"
            --resources "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            --cmake "${CMAKE_COMMAND}"
            --certificate-script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestCertificate.cmake"
            --script-dir "${CMAKE_CURRENT_SOURCE_DIR}/tools" ${ARGN})
    endfunction()
endif()
option(BUILD_SHARED_LIBS "Build shared libraries (disable for iOS)" ON)
option(IIWEBSERVER_BUILD_COMPATIBILITY "Build the existing Qt Core LAN/relay API and tools" ON)
set(IIWEBSERVER_NEEDS_CRYPTO FALSE)
if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{HOME})
        set(_user_home "$ENV{HOME}")
    elseif(DEFINED ENV{USERPROFILE})
        set(_user_home "$ENV{USERPROFILE}")
    else()
        message(FATAL_ERROR "Set CMAKE_INSTALL_PREFIX explicitly: no user home is available")
    endif()
    set(CMAKE_INSTALL_PREFIX "${_user_home}/.local/SDK/iiWebServer" CACHE PATH "Install prefix" FORCE)
endif()

set(_iiwebserver_network_version 0.5)
if(IIWEBSERVER_BUILD_COMPATIBILITY)
    # Resolve the matching JSON value ABI before iiNetwork discovers iiJson.
    find_package(iiJson 0.1.2 CONFIG REQUIRED HINTS "$ENV{IIJSON_PREFIX}" "$ENV{HOME}/.local/SDK/iiJson")
    set(_iiwebserver_network_version 0.6.1)
endif()
find_package(iiNetwork ${_iiwebserver_network_version} CONFIG REQUIRED HINTS "$ENV{IINETWORK_PREFIX}" "$ENV{HOME}/.local/SDK/iiNetwork")
unset(_iiwebserver_network_version)
if(NOT iiNetwork_HTTP_SERVER_LIFECYCLE AND NOT TARGET iiNetwork)
    message(FATAL_ERROR "Rebuild iiNetwork with HTTP shutdown and response deadlines before building iiWebServer")
endif()
foreach(_header HttpServer HttpExport)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/iiWebServer/${_header}.h"
        CONTENT "#pragma once\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/Http/${_header}.h\"\n")
endforeach()
add_library(iiWebServerHttp src/Http/HttpServer.cpp src/Http/HttpServer.h src/Http/HttpExport.h)
add_library(iiWebServer::Http ALIAS iiWebServerHttp)
set_target_properties(iiWebServerHttp PROPERTIES EXPORT_NAME Http CXX_EXTENSIONS OFF
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES INSTALL_RPATH_USE_LINK_PATH TRUE)
target_compile_features(iiWebServerHttp PUBLIC cxx_std_23)
target_compile_definitions(iiWebServerHttp PRIVATE IIWEBSERVER_HTTP_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiWebServerHttp PUBLIC IIWEBSERVER_HTTP_STATIC)
endif()
target_link_libraries(iiWebServerHttp PUBLIC iiNetwork::iiNetwork)
target_include_directories(iiWebServerHttp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(APPLE)
    set_property(TARGET iiWebServerHttp PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET iiWebServerHttp PROPERTY INSTALL_RPATH "$ORIGIN")
endif()
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(ii-http-server tools/http.cpp)
    target_link_libraries(ii-http-server PRIVATE iiWebServer::Http)
    set_target_properties(ii-http-server PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_EXTENSIONS OFF INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set_property(TARGET ii-http-server PROPERTY INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_property(TARGET ii-http-server PROPERTY INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS ii-http-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(BUILD_TESTING)
    add_test(NAME iiWebServer.source_layout COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_layout.cmake")
    add_executable(iiWebServerHttpTests tests/http.cpp)
    target_link_libraries(iiWebServerHttpTests PRIVATE iiWebServer::Http)
    set_target_properties(iiWebServerHttpTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_EXTENSIONS OFF)
    foreach(_case routing protocol lifetime)
        add_test(NAME iiWebServer.http.${_case} COMMAND iiWebServerHttpTests ${_case})
        set_tests_properties(iiWebServer.http.${_case} PROPERTIES TIMEOUT 20)
    endforeach()
    if(TARGET ii-http-server)
        find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
        web_resource_test(iiWebServer.http.cli -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_cli.py" "$<TARGET_FILE:ii-http-server>" "@WORK@")
        set_tests_properties(iiWebServer.http.cli PROPERTIES TIMEOUT 45)
        set(IIWEBSERVER_TEST_NGINX "" CACHE FILEPATH "Optional nginx binary for real TLS reverse-proxy integration")
        if(IIWEBSERVER_TEST_NGINX)
            get_filename_component(_http_openssl_prefix "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
            find_program(IIWEBSERVER_TEST_OPENSSL_COMMAND NAMES openssl HINTS "${_http_openssl_prefix}/bin" REQUIRED)
            find_program(IIWEBSERVER_TEST_CURL_COMMAND NAMES curl REQUIRED)
            web_resource_test(iiWebServer.http.nginx -- "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/nginx_http.py" "${IIWEBSERVER_TEST_NGINX}"
                "$<TARGET_FILE:ii-http-server>" "@WORK@"
                "${IIWEBSERVER_TEST_OPENSSL_COMMAND}" "${IIWEBSERVER_TEST_CURL_COMMAND}")
            set_tests_properties(iiWebServer.http.nginx PROPERTIES TIMEOUT 120)
        endif()
    endif()
endif()
install(TARGETS iiWebServerHttp EXPORT iiWebServerHttpTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/Http/HttpServer.h src/Http/HttpExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiWebServer)
install(EXPORT iiWebServerHttpTargets FILE iiWebServerHttpTargets.cmake NAMESPACE iiWebServer:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer)
install(FILES docs/HttpServer.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/docs)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer)
install(FILES deploy/nginx/iiwebserver.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/nginx)

if(IIWEBSERVER_BUILD_COMPATIBILITY)
# File creation is owned by iiFileProvider.
find_package(iiFileProvider 0.7 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(iiFileProviderNative 0.7 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")






find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core)
set(CMAKE_AUTOMOC ON)

set(_compat_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Transport/NetworkTypes.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Transport/NetworkTransport.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Compatibility/iiWebServer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Peers/WebServer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Peers/PairingLink.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Peers/LanPeer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Transfer/Transfer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Storage/StorageBridge.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Compatibility/iiWebServerExport.h"
)
foreach(_header IN LISTS _compat_public_headers)
    get_filename_component(_name "${_header}" NAME)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/${_name}"
        CONTENT "#pragma once\n#include \"${_header}\"\n")
endforeach()
add_library(iiWebServer src/Compatibility/iiWebServer.h src/Compatibility/iiWebServer.cpp src/Peers/WebServer.h src/Compatibility/iiWebServerExport.h
    src/Transport/NetworkTransport.h src/Transport/NetworkTransport.cpp src/Transport/NetworkTypes.h src/Transport/RuntimePump.h src/Transport/RuntimePump.cpp
    src/Protocol/Protocol.h src/Protocol/BinaryFrame.h src/Peers/SessionAuthenticator.cpp src/Peers/SessionResponse.cpp src/Peers/SessionResponse.h src/Peers/RelayServer.cpp src/Peers/Peer.cpp src/FileSharing/FileShare.cpp src/Peers/PairingLink.cpp src/Peers/PairingLink.h src/Peers/LanPeer.cpp src/Peers/LanPeer.h
    src/Transfer/Transfer.h src/Transfer/Transfer.cpp src/Storage/StorageBridge.h src/Storage/StorageBridge.cpp)
set(_transfer_default ON)
if(IOS OR ANDROID)
    set(_transfer_default OFF)
endif()
option(IIWEBSERVER_WITH_CURL "Enable standard file-transfer protocols using libcurl >= 7.85" ${_transfer_default})
option(IIWEBSERVER_WITH_STORAGE_PROCESS "Enable external rclone processes (desktop/server only)" ${_transfer_default})
if(IIWEBSERVER_WITH_STORAGE_PROCESS)
    if(IOS OR ANDROID)
        message(FATAL_ERROR "External storage processes are not supported by the mobile SDK")
    endif()
    target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_STORAGE_PROCESS=1)
    if(WIN32)
        target_link_libraries(iiWebServer PRIVATE advapi32)
    endif()
endif()
if(IIWEBSERVER_WITH_CURL)
    find_package(CURL 7.85 REQUIRED)
    target_link_libraries(iiWebServer PRIVATE CURL::libcurl)
    target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_CURL=1)
endif()
if(NOT IOS AND NOT ANDROID)
    install(FILES tools/ftp_server.py tools/provider_fs.py tools/ftp-requirements.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer)
    install(DIRECTORY "${iiFileProviderNative_PYTHON_DIRECTORY}/ii_file_provider"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer
        FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
    get_target_property(_provider_kind iiFileProvider::Native TYPE)
    get_target_property(_provider_imported iiFileProvider::Native IMPORTED)
    if(_provider_kind STREQUAL "SHARED_LIBRARY")
        if(WIN32)
            # ctypes resolves transitive dependencies beside the loaded DLL;
            # PATH entries alone are insufficient on current Python versions.
            get_target_property(_sqlite_library SQLite3::SQLite3 IMPORTED_LOCATION_RELEASE)
            if(NOT _sqlite_library)
                get_target_property(_sqlite_library SQLite3::SQLite3 IMPORTED_LOCATION)
            endif()
            if(_sqlite_library)
                get_filename_component(_sqlite_library_directory "${_sqlite_library}" DIRECTORY)
                find_file(IIWEBSERVER_SQLITE_RUNTIME NAMES sqlite3.dll
                    HINTS "${_sqlite_library_directory}/../bin" NO_DEFAULT_PATH)
            endif()
            if(IIWEBSERVER_SQLITE_RUNTIME)
                install(FILES "${IIWEBSERVER_SQLITE_RUNTIME}" DESTINATION ${CMAKE_INSTALL_BINDIR})
            endif()
        endif()
        if(_provider_imported)
            install(IMPORTED_RUNTIME_ARTIFACTS iiFileProvider::Native
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
        endif()
    endif()
    install(DIRECTORY "${iiFileProviderNative_LICENSE_DIRECTORY}/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiFileProviderNative)
    set(IIWEBSERVER_NEEDS_CRYPTO TRUE)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    target_sources(iiWebServer PRIVATE src/Peers/LanIdentity.cpp src/Peers/LanIdentity.h)
    target_link_libraries(iiWebServer PRIVATE OpenSSL::Crypto)
    target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_LAN_HOST=1)
endif()
add_library(iiWebServer::iiWebServer ALIAS iiWebServer)
target_compile_features(iiWebServer PUBLIC cxx_std_23)
target_compile_definitions(iiWebServer PRIVATE IIWEBSERVER_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiWebServer PUBLIC IIWEBSERVER_STATIC)
endif()
target_link_libraries(iiWebServer PUBLIC Qt6::Core iiNetwork::iiNetwork iiWebServer::Http iiFileProvider::iiFileProvider iiFileProvider::Native)
target_link_libraries(iiWebServer PRIVATE iiJson::iiJson)
target_include_directories(iiWebServer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src/Compatibility src/Transport src/Protocol src/Peers src/FileSharing src/Transfer src/Storage
)
set_target_properties(iiWebServer PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
if(APPLE)
    set_property(TARGET iiWebServer PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET iiWebServer PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

if(BUILD_TESTING)
    add_executable(iiWebServerRuntimePumpTests tests/runtime_pump.cpp src/Transport/RuntimePump.cpp)
    target_link_libraries(iiWebServerRuntimePumpTests PRIVATE iiWebServer::iiWebServer Qt6::Core)
    add_test(NAME iiWebServer.runtime_pump COMMAND iiWebServerRuntimePumpTests)
    set_tests_properties(iiWebServer.runtime_pump PROPERTIES TIMEOUT 20)
    add_test(NAME iiWebServer.resource_runner COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR}" "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resource_runner.py")
    web_resource_test(iiWebServer.resource_paths -- "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resource_paths.py"
        "${IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR}" "@WORK@")
    if(UNIX)
        web_resource_test(iiWebServer.resource_links -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resource_links.py" "${CMAKE_COMMAND}"
            "${IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR}" "@WORK@")
        web_resource_test(iiWebServer.install_policy -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_install_policy.py"
            "${IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR}" "@WORK@")
    endif()
    if(_provider_kind STREQUAL "SHARED_LIBRARY")
        web_resource_test(iiWebServer.resource_fixture -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resource_fixture.py" "${CMAKE_COMMAND}"
            "${IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR}" "$<TARGET_FILE:iiFileProvider::Native>" "@WORK@")
    endif()
    add_executable(iiWebServerLanTests tests/lan.cpp)
    target_link_libraries(iiWebServerLanTests PRIVATE iiWebServer::iiWebServer)
    web_resource_test(iiWebServer.lan -- "$<TARGET_FILE:iiWebServerLanTests>")
    web_resource_test(iiWebServer.lan_offline -- "$<TARGET_FILE:iiWebServerLanTests>" --offline)
    set_tests_properties(iiWebServer.lan PROPERTIES TIMEOUT 45)
    add_executable(iiWebServerSessionResponseTests tests/session_response.cpp src/Peers/SessionResponse.cpp src/Peers/SessionResponse.h)
    set_target_properties(iiWebServerSessionResponseTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(iiWebServerSessionResponseTests PRIVATE iiJson::iiJson iiNetwork::iiNetwork)
    add_test(NAME iiWebServer.session_response COMMAND iiWebServerSessionResponseTests)
    add_executable(iiWebServerTests tests/hosting.cpp tests/NetworkFixture.h)
    target_link_libraries(iiWebServerTests PRIVATE iiWebServer::iiWebServer)
    web_resource_test(iiWebServer.hosting --certificates -- "$<TARGET_FILE:iiWebServerTests>")
    web_resource_test(iiWebServer.hosting_offline -- "$<TARGET_FILE:iiWebServerTests>" --offline)
    add_executable(iiWebServerSignalRecorderTests tests/signal_recorder_tests.cpp)
    set_target_properties(iiWebServerSignalRecorderTests PROPERTIES AUTOMOC ON)
    target_link_libraries(iiWebServerSignalRecorderTests PRIVATE Qt6::Core)
    add_test(NAME iiWebServer.signal_recorder COMMAND iiWebServerSignalRecorderTests)
    add_executable(iiWebServerPairingTests tests/pairing.cpp)
    target_link_libraries(iiWebServerPairingTests PRIVATE iiWebServer::iiWebServer)
    add_test(NAME iiWebServer.pairing COMMAND iiWebServerPairingTests)
    add_test(NAME iiWebServer.pairing_offline COMMAND iiWebServerPairingTests --offline)
    set_tests_properties(iiWebServer.pairing_offline PROPERTIES TIMEOUT 45)
    add_test(NAME iiWebServer.network_suites_ctest_contract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_network_suites.py")
    add_test(NAME iiWebServer.pairing_ctest_contract COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_pairing.py")
    set_tests_properties(iiWebServer.pairing PROPERTIES TIMEOUT 45)
    set_tests_properties(iiWebServer.hosting PROPERTIES TIMEOUT 60)
    add_executable(iiWebServerHelloWorld tests/hello_world.cpp)
    target_link_libraries(iiWebServerHelloWorld PRIVATE iiWebServer::iiWebServer)
    set_target_properties(iiWebServerHelloWorld PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME iiWebServer.hello_world COMMAND iiWebServerHelloWorld)
    add_executable(iiWebServerTransferTests tests/transfer.cpp tests/NetworkFixture.h)
    target_link_libraries(iiWebServerTransferTests PRIVATE iiWebServer::iiWebServer)
    web_resource_test(iiWebServer.transfer -- "$<TARGET_FILE:iiWebServerTransferTests>")
    web_resource_test(iiWebServer.transfer_offline -- "$<TARGET_FILE:iiWebServerTransferTests>" --offline)
    set_tests_properties(iiWebServer.transfer iiWebServer.transfer_offline PROPERTIES SKIP_RETURN_CODE 77)
    set_tests_properties(iiWebServer.transfer_offline PROPERTIES TIMEOUT 45)
    set_tests_properties(iiWebServer.transfer PROPERTIES TIMEOUT 45)
    add_executable(iiWebServerOptionalBackendTests tests/optional_backends.cpp)
    target_link_libraries(iiWebServerOptionalBackendTests PRIVATE iiWebServer::iiWebServer)
    target_compile_definitions(iiWebServerOptionalBackendTests PRIVATE
        TEST_CURL_ENABLED=$<BOOL:${IIWEBSERVER_WITH_CURL}> TEST_PROCESS_ENABLED=$<BOOL:${IIWEBSERVER_WITH_STORAGE_PROCESS}>)
    add_test(NAME iiWebServer.optional_backends COMMAND iiWebServerOptionalBackendTests)
    add_test(NAME iiWebServer.optional_backends_ctest_contract
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_optional_backend.py")
    if(IIWEBSERVER_WITH_STORAGE_PROCESS)
        add_executable(iiWebServerCtestSupportTests tests/ctest_support.cpp)
        target_compile_features(iiWebServerCtestSupportTests PRIVATE cxx_std_23)
        add_test(NAME iiWebServer.ctest_support COMMAND iiWebServerCtestSupportTests)
        add_test(NAME iiWebServer.storage_ctest_contract
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctest_storage_bridge.py")
        add_executable(iiWebServerStorageTests tests/storage_bridge.cpp)
        target_link_libraries(iiWebServerStorageTests PRIVATE iiWebServer::iiWebServer)
        web_resource_test(iiWebServer.storage -- "$<TARGET_FILE:iiWebServerStorageTests>")
        set_tests_properties(iiWebServer.storage PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
    endif()
endif()

if(NOT IOS AND NOT ANDROID)
    add_executable(ii-file-transfer tools/transfer.cpp)
    add_executable(ii-storage tools/storage.cpp)
    foreach(_tool ii-file-transfer ii-storage)
        target_link_libraries(${_tool} PRIVATE iiWebServer::iiWebServer)
        set_target_properties(${_tool} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
        if(APPLE)
            set_target_properties(${_tool} PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
        elseif(UNIX)
            set_target_properties(${_tool} PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        endif()
        install(TARGETS ${_tool} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()
    add_executable(ii-server-relay tools/relay.cpp)
    target_link_libraries(ii-server-relay PRIVATE iiWebServer::iiWebServer)
    add_executable(ii-web-server tools/host.cpp)
    target_link_libraries(ii-web-server PRIVATE iiWebServer::iiWebServer)
    set_target_properties(ii-web-server PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set_target_properties(ii-web-server ii-server-relay PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_target_properties(ii-web-server ii-server-relay PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS ii-web-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set_target_properties(ii-server-relay PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
    install(TARGETS ii-server-relay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(BUILD_TESTING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(_smb_fixture_arguments)
        if(WIN32)
            find_program(IIWEBSERVER_DOTNET_EXECUTABLE NAMES dotnet REQUIRED)
            set(_smb_fixture_directory "${CMAKE_CURRENT_BINARY_DIR}/smb-fixture")
            set(_smb_fixture_resources "${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/smb-fixture")
            set(_smb_fixture_project "${CMAKE_CURRENT_SOURCE_DIR}/tests/SmbWindowsFixture/SmbWindowsFixture.csproj")
            set(_smb_fixture_dll "${_smb_fixture_directory}/SmbWindowsFixture.dll")
            add_custom_command(OUTPUT "${_smb_fixture_dll}"
                COMMAND "${CMAKE_COMMAND}" -E env
                    "DOTNET_CLI_HOME=${_smb_fixture_resources}/home"
                    "NUGET_PACKAGES=${_smb_fixture_resources}/packages"
                    DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_GENERATE_ASPNET_CERTIFICATE=false
                    "${Python3_EXECUTABLE}" -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/SmbWindowsFixture/prepare_source.py"
                    "${_smb_fixture_resources}"
                COMMAND "${CMAKE_COMMAND}" -E env
                    "DOTNET_CLI_HOME=${_smb_fixture_resources}/home"
                    "NUGET_PACKAGES=${_smb_fixture_resources}/packages"
                    DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_GENERATE_ASPNET_CERTIFICATE=false
                    "${IIWEBSERVER_DOTNET_EXECUTABLE}" build "${_smb_fixture_project}"
                    --configuration Release --output "${_smb_fixture_directory}" --nologo
                    "-p:BaseIntermediateOutputPath=${_smb_fixture_directory}/obj/"
                    "-p:SMBSourceRoot=${_smb_fixture_resources}/SMBLibrary-582635541d2ec0c84eac5f66cbebd41e2d35c7ae"
                DEPENDS "${_smb_fixture_project}"
                    tests/SmbWindowsFixture/Program.cs tests/SmbWindowsFixture/prepare_source.py
                VERBATIM)
            add_custom_target(iiWebServerSmbFixture ALL DEPENDS "${_smb_fixture_dll}")
            set(_smb_fixture_arguments "${IIWEBSERVER_DOTNET_EXECUTABLE}" "${_smb_fixture_dll}")
        endif()
        set(_web_ftp_args --ftp)
        if(_provider_kind STREQUAL "SHARED_LIBRARY")
            list(APPEND _web_ftp_args --provider-lib "$<TARGET_FILE:iiFileProvider::Native>")
        endif()
        if(IIWEBSERVER_SQLITE_RUNTIME)
            list(APPEND _web_ftp_args --sqlite-lib "${IIWEBSERVER_SQLITE_RUNTIME}")
        endif()
        web_resource_test(iiWebServer.provider_ftp --certificates ${_web_ftp_args} -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/provider_ftp.py" "@WORK@")
        set_tests_properties(iiWebServer.provider_ftp PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        web_resource_test(iiWebServer.provider_ftps --certificates ${_web_ftp_args} -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/provider_ftp.py" "@WORK@" --implicit)
        set_tests_properties(iiWebServer.provider_ftps PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_test(NAME iiWebServer.fixture_cleanup COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture_directory.py")
        web_resource_test(iiWebServer.protocol_processes --certificates ${_web_ftp_args} -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_protocols.py" "$<TARGET_FILE:ii-file-transfer>"
            "$<TARGET_FILE:ii-storage>" "@WORK@" ${_smb_fixture_arguments})
        set_tests_properties(iiWebServer.protocol_processes PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
        web_resource_test(iiWebServer.process_hosting -- "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_hosting.py" "$<TARGET_FILE:ii-server-relay>"
            "$<TARGET_FILE:ii-web-server>" "@WORK@")
        set_tests_properties(iiWebServer.process_hosting PROPERTIES TIMEOUT 45)
    endif()
endif()

install(TARGETS iiWebServer EXPORT iiWebServerTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES ${_compat_public_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES docs/Network.md docs/FileTransfer.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/docs)
install(FILES ThirdParty/Curl-LICENSE.txt ThirdParty/Rclone-LICENSE.txt
    ThirdParty/Pyftpdlib-LICENSE.txt ThirdParty/PyOpenSSL-LICENSE.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/licenses)
if(IIWEBSERVER_NEEDS_CRYPTO)
    install(FILES ThirdParty/OpenSSL-LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/iiWebServer/licenses)
endif()
install(EXPORT iiWebServerTargets
    FILE iiWebServerTargets.cmake
    NAMESPACE iiWebServer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer
)
else()
    set(IIWEBSERVER_WITH_CURL OFF)
    set(IIWEBSERVER_WITH_STORAGE_PROCESS OFF)
endif()

configure_package_config_file(cmake/iiWebServerConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/iiWebServerConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiWebServer
)

if(APPLE AND BUILD_TESTING AND IIWEBSERVER_BUILD_COMPATIBILITY)
    get_property(_web_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${_web_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:" "DYLD_FALLBACK_LIBRARY_PATH=unset:")
endif()
