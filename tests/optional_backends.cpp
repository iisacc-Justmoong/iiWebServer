#include "iiWebServer.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(expression) \
    do { \
        if (!(expression)) \
            throw std::runtime_error(std::string(__FILE__) + ":" + \
                                     std::to_string(__LINE__) + ": " + #expression); \
    } while (false)

namespace {
void featureConfigurationMatchesLinkedArtifact()
{
    using namespace iiWebServer;
    CHECK(!FileTransfer::backendVersion().isEmpty() == bool(TEST_CURL_ENABLED));
    CHECK(!FileTransfer::protocols().isEmpty() == bool(TEST_CURL_ENABLED));
    if (!TEST_CURL_ENABLED) {
        FileTransfer transfer;
        QEventLoop loop;
        int count = 0;
        QString error;
        QObject::connect(&transfer, &FileTransfer::finished, &loop,
                         [&](const QString&, const TransferResult& result) {
                             ++count;
                             error = result.error;
                             loop.quit();
                         });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        transfer.start({});
        if (count == 0) loop.exec();
        CHECK(count == 1);
        CHECK(error == "backend_unavailable");
    }
    StorageBridgeOptions backend;
    backend.executable = "/nonexistent/iiWebServer-fixture-rclone";
    StorageBridge storage(backend);
    QEventLoop loop;
    int count = 0;
    QString error;
    QObject::connect(&storage, &StorageBridge::finished, &loop,
                     [&](const QString&, const StorageResult& result) {
                         ++count;
                         error = result.error;
                         loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    storage.start({StorageOperation::Providers});
    if (count == 0) loop.exec();
    CHECK(count == 1);
    CHECK(error == (TEST_PROCESS_ENABLED ? "backend_unavailable" : "external_backend_disabled"));
    FileProtocolServer server(backend);
    CHECK(!server.start({}));
    CHECK(server.errorString() == (TEST_PROCESS_ENABLED ? "backend_unavailable" : "external_backend_disabled"));
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        featureConfigurationMatchesLinkedArtifact();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "featureConfigurationMatchesLinkedArtifact: " << error.what() << '\n';
        return 1;
    }
}
