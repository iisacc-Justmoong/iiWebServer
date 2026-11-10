#pragma once
#include <QString>
#include <QtGlobal>

inline QString testDirectory() {
    const auto path = qEnvironmentVariable("IIWEBSERVER_TEST_DIRECTORY");
    if (path.isEmpty()) qFatal("IIWEBSERVER_TEST_DIRECTORY is required");
    return path;
}
