if(NOT DEFINED IIWEBSERVER_TEST_DIRECTORY OR NOT IS_DIRECTORY "${IIWEBSERVER_TEST_DIRECTORY}")
    message(FATAL_ERROR "IIWEBSERVER_TEST_DIRECTORY must name an existing test workspace")
endif()
find_program(IIWEBSERVER_TEST_OPENSSL openssl REQUIRED)
execute_process(COMMAND "${IIWEBSERVER_TEST_OPENSSL}" req -x509 -newkey rsa:2048 -nodes
    -keyout "${IIWEBSERVER_TEST_DIRECTORY}/test-ca-key.pem" -out "${IIWEBSERVER_TEST_DIRECTORY}/test-ca.pem"
    -days 2 -subj /CN=iiWebServer-Test-CA -addext basicConstraints=critical,CA:TRUE
    -addext keyUsage=critical,keyCertSign,cRLSign
    RESULT_VARIABLE test_certificate_result OUTPUT_QUIET ERROR_QUIET)
if(NOT test_certificate_result EQUAL 0)
    message(FATAL_ERROR "Could not generate the ephemeral TLS test certificate")
endif()
execute_process(COMMAND "${IIWEBSERVER_TEST_OPENSSL}" req -new -newkey rsa:2048 -nodes
    -keyout "${IIWEBSERVER_TEST_DIRECTORY}/test-key.pem" -out "${IIWEBSERVER_TEST_DIRECTORY}/test.csr"
    -subj /CN=127.0.0.1 RESULT_VARIABLE test_certificate_result OUTPUT_QUIET ERROR_QUIET)
if(NOT test_certificate_result EQUAL 0)
    message(FATAL_ERROR "Could not generate the TLS test request")
endif()
file(WRITE "${IIWEBSERVER_TEST_DIRECTORY}/test-cert.ext"
    "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1\n")
execute_process(COMMAND "${IIWEBSERVER_TEST_OPENSSL}" x509 -req -in "${IIWEBSERVER_TEST_DIRECTORY}/test.csr"
    -CA "${IIWEBSERVER_TEST_DIRECTORY}/test-ca.pem" -CAkey "${IIWEBSERVER_TEST_DIRECTORY}/test-ca-key.pem"
    -CAcreateserial -out "${IIWEBSERVER_TEST_DIRECTORY}/test-cert.pem" -days 2 -sha256
    -extfile "${IIWEBSERVER_TEST_DIRECTORY}/test-cert.ext"
    RESULT_VARIABLE test_certificate_result OUTPUT_QUIET ERROR_QUIET)
if(NOT test_certificate_result EQUAL 0)
    message(FATAL_ERROR "Could not sign the TLS test certificate")
endif()
file(CHMOD "${IIWEBSERVER_TEST_DIRECTORY}/test-key.pem" PERMISSIONS OWNER_READ OWNER_WRITE)
file(CHMOD "${IIWEBSERVER_TEST_DIRECTORY}/test-ca-key.pem" PERMISSIONS OWNER_READ OWNER_WRITE)
if(WIN32)
    # chmod controls a read-only attribute on NTFS, not the access-control list.
    # OpenSSH rejects inherited access by other Windows identities.
    foreach(test_private_key IN ITEMS test-key.pem test-ca-key.pem)
        execute_process(COMMAND icacls "${IIWEBSERVER_TEST_DIRECTORY}/${test_private_key}"
            /inheritance:r /grant:r "$ENV{USERDOMAIN}\\$ENV{USERNAME}:(F)"
            RESULT_VARIABLE test_key_acl OUTPUT_QUIET ERROR_QUIET)
        if(NOT test_key_acl EQUAL 0)
            message(FATAL_ERROR "Could not restrict the ephemeral Windows test key ACL")
        endif()
    endforeach()
endif()
