cmake_minimum_required(VERSION 3.24)
project(iiWebServerConsumer LANGUAGES CXX)

find_package(iiWebServer 0.5.0 CONFIG REQUIRED)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(host_prefix "${iiWebServer_DIR}/../../.." ABSOLUTE)
set(IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR "${iiFileProviderNative_PYTHON_DIRECTORY}"
    CACHE PATH "iiFileProvider Python adapter used by the resource test runner")
function(web_resource_test name)
    add_test(NAME "${name}" COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/../resource_runner.py"
        --provider-python "${IIWEBSERVER_TEST_PROVIDER_PYTHON_DIR}"
        --ftp-provider-python "${host_prefix}/share/iiWebServer"
        --resources "${CMAKE_CURRENT_SOURCE_DIR}/../../resources"
        --cmake "${CMAKE_COMMAND}"
        --certificate-script "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/TestCertificate.cmake"
        --script-dir "${host_prefix}/share/iiWebServer" ${ARGN})
endfunction()
add_executable(iiWebServerConsumerHosting ../hosting.cpp ../NetworkFixture.h)
target_link_libraries(iiWebServerConsumerHosting PRIVATE iiWebServer::iiWebServer Qt6::Test)
add_executable(iiWebServerConsumer ../hello_world.cpp)
target_link_libraries(iiWebServerConsumer PRIVATE iiWebServer::iiWebServer)
set_target_properties(iiWebServerConsumer PROPERTIES CXX_EXTENSIONS OFF)
enable_testing()
add_executable(iiWebServerConsumerLan ../lan.cpp)
target_link_libraries(iiWebServerConsumerLan PRIVATE iiWebServer::iiWebServer Qt6::Test)
web_resource_test(iiWebServer.installed_lan -- "$<TARGET_FILE:iiWebServerConsumerLan>")
add_test(NAME iiWebServer.installed_consumer COMMAND iiWebServerConsumer)
web_resource_test(iiWebServer.installed_hosting --certificates -- "$<TARGET_FILE:iiWebServerConsumerHosting>")
add_executable(iiWebServerConsumerPairing ../pairing.cpp)
target_link_libraries(iiWebServerConsumerPairing PRIVATE iiWebServer::iiWebServer Qt6::Test)
add_test(NAME iiWebServer.installed_pairing COMMAND iiWebServerConsumerPairing)
add_executable(iiWebServerConsumerTransfer ../transfer.cpp ../NetworkFixture.h)
target_link_libraries(iiWebServerConsumerTransfer PRIVATE iiWebServer::iiWebServer Qt6::Test)
web_resource_test(iiWebServer.installed_transfer -- "$<TARGET_FILE:iiWebServerConsumerTransfer>")
add_executable(iiWebServerConsumerOptionalBackends ../optional_backends.cpp)
target_link_libraries(iiWebServerConsumerOptionalBackends PRIVATE iiWebServer::iiWebServer Qt6::Test)
target_compile_definitions(iiWebServerConsumerOptionalBackends PRIVATE
    TEST_CURL_ENABLED=$<BOOL:${iiWebServer_CURL_ENABLED}> TEST_PROCESS_ENABLED=$<BOOL:${iiWebServer_STORAGE_PROCESS_ENABLED}>)
add_test(NAME iiWebServer.installed_optional_backends COMMAND iiWebServerConsumerOptionalBackends)
if(iiWebServer_STORAGE_PROCESS_ENABLED)
    add_executable(iiWebServerConsumerStorage ../storage_bridge.cpp)
    target_link_libraries(iiWebServerConsumerStorage PRIVATE iiWebServer::iiWebServer Qt6::Test)
    web_resource_test(iiWebServer.installed_storage -- "$<TARGET_FILE:iiWebServerConsumerStorage>")
    set_tests_properties(iiWebServer.installed_storage PROPERTIES TIMEOUT 180)
    set(_installed_ftp_args --ftp)
    get_target_property(_provider_kind iiFileProvider::Native TYPE)
    if(_provider_kind STREQUAL "SHARED_LIBRARY")
        if(WIN32)
            set(_provider_install_directory "${host_prefix}/bin")
        else()
            set(_provider_install_directory "${host_prefix}/lib")
        endif()
        list(APPEND _installed_ftp_args --provider-lib
            "${_provider_install_directory}/$<TARGET_FILE_NAME:iiFileProvider::Native>")
        if(WIN32 AND EXISTS "${host_prefix}/bin/sqlite3.dll")
            list(APPEND _installed_ftp_args --sqlite-lib "${host_prefix}/bin/sqlite3.dll")
        endif()
    endif()
    web_resource_test(iiWebServer.installed_protocol_processes --certificates ${_installed_ftp_args} -- "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/../process_protocols.py"
        "${host_prefix}/bin/ii-file-transfer${CMAKE_EXECUTABLE_SUFFIX}"
        "${host_prefix}/bin/ii-storage${CMAKE_EXECUTABLE_SUFFIX}" "@WORK@")
    set_tests_properties(iiWebServer.installed_protocol_processes PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77
        ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:")
endif()
web_resource_test(iiWebServer.installed_process_hosting -- "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/../process_hosting.py"
    "${host_prefix}/bin/ii-server-relay${CMAKE_EXECUTABLE_SUFFIX}"
    "${host_prefix}/bin/ii-web-server${CMAKE_EXECUTABLE_SUFFIX}"
    "@WORK@")
set_tests_properties(iiWebServer.installed_process_hosting PROPERTIES TIMEOUT 45
    ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:")
if(APPLE)
    get_property(_web_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${_web_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:" "DYLD_FALLBACK_LIBRARY_PATH=unset:")
endif()
