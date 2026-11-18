#include "TestPaths.h"
#include "StorageBridge.h"
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtCore/QProcess>
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include "CTestSupport.h"

using namespace iiWebServer;
class StorageBridgeTests final {
public:
    void actualCatalogueAndFileOperations();
    void rejectUnsafeConfiguration();
    void serverRequiresAuthentication();
};
static StorageResult run(StorageBridge &bridge, const StorageRequest &request) {
    QEventLoop loop;
    int count = 0;
    QString responseId;
    StorageResult response;
    QObject::connect(&bridge, &StorageBridge::finished, &loop,
                     [&](const QString& id, const StorageResult& value) {
                         ++count;
                         responseId = id;
                         response = value;
                         loop.quit();
                     });
    const auto id = bridge.start(request);
    if (count == 0) {
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (count != 1 || responseId != id) return {false, false, -1, "test_timeout"};
    return response;
}
void StorageBridgeTests::actualCatalogueAndFileOperations() {
    const auto executable = qEnvironmentVariable("IIWEBSERVER_TEST_RCLONE");
    if (executable.isEmpty()) throw web_test::Skip("Set IIWEBSERVER_TEST_RCLONE for real backend integration tests");
    QTemporaryDir dir(testDirectory() + "/storage-XXXXXX");
    CTEST_CHECK(dir.isValid());
    StorageBridge bridge({executable, {}, {}, dir.path()});
    auto providers = run(bridge, {StorageOperation::Providers});
    CTEST_CHECK_MESSAGE(providers.ok, qPrintable(providers.error));
    CTEST_CHECK(providers.data.isArray());
    QStringList names;
    for (const auto &value : providers.data.toArray()) names.append(value.toObject()["Name"].toString());
    for (const auto &name : {"sftp", "smb", "s3", "webdav", "ftp", "azureblob", "drive"}) CTEST_CHECK(names.contains(name));
    QFile source(dir.filePath("model.bin"));
    CTEST_CHECK(source.open(QIODevice::WriteOnly));
    const QByteArray bytes(1500000, char(0xff));
    CTEST_COMPARE(source.write(bytes), bytes.size()); source.close();
    const auto destination = dir.filePath("out.bin");
    auto copied = run(bridge, {StorageOperation::CopyFile, source.fileName(), destination});
    CTEST_CHECK_MESSAGE(copied.ok, qPrintable(copied.error));
    QFile file(destination); CTEST_CHECK(file.open(QIODevice::ReadOnly)); CTEST_COMPARE(file.readAll(), bytes); file.close();
    auto stat = run(bridge, {StorageOperation::Stat, destination});
    CTEST_CHECK_MESSAGE(stat.ok, qPrintable(stat.error));
    CTEST_COMPARE(stat.data.toObject()["Size"].toInteger(), bytes.size());
    auto list = run(bridge, {StorageOperation::List, dir.path()});
    CTEST_CHECK(list.ok); CTEST_CHECK(list.data.isArray());
    CTEST_CHECK(source.open(QIODevice::WriteOnly | QIODevice::Truncate)); source.write("changed"); source.close();
    CTEST_CHECK(run(bridge, {StorageOperation::CopyFile, source.fileName(), destination}).ok);
    CTEST_CHECK(file.open(QIODevice::ReadOnly)); CTEST_COMPARE(file.readAll(), bytes); file.close();
    auto removed = run(bridge, {StorageOperation::RemoveFile, destination});
    CTEST_CHECK(removed.ok); CTEST_CHECK(!file.exists());
}
void StorageBridgeTests::rejectUnsafeConfiguration() {
    QTemporaryDir dir(testDirectory() + "/storage-XXXXXX");
    StorageBridge missing({dir.filePath("missing-rclone"), {}, {}, dir.path()});
    CTEST_COMPARE(run(missing, {StorageOperation::Providers}).error, "backend_unavailable");
    const auto executable = qEnvironmentVariable("IIWEBSERVER_TEST_RCLONE");
    if (executable.isEmpty()) return;
    StorageBridge bridge({executable, {}, {}, dir.path()});
    CTEST_COMPARE(run(bridge, {StorageOperation::List, "--config=/private/file"}).error, "invalid_location");
    CTEST_COMPARE(run(bridge, {StorageOperation::List, ":sftp,host=example.com:/"}).error, "invalid_location");
    CTEST_COMPARE(run(bridge, {StorageOperation::List, "remote:path"}).error, "explicit_config_required");
    const auto config = dir.filePath("open.conf");
    QFile f(config); CTEST_CHECK(f.open(QIODevice::WriteOnly)); f.write("[test]\ntype = local\n"); f.close();
#ifdef Q_OS_WIN
    CTEST_COMPARE(QProcess::execute("icacls.exe", {QDir::toNativeSeparators(config), "/grant", "*S-1-1-0:(R)"}), 0);
#else
    CTEST_CHECK(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
#endif
    StorageBridge unsafe({executable, config, {}, dir.path()});
    CTEST_COMPARE(run(unsafe, {StorageOperation::Providers}).error, "private_config_required");
}
void StorageBridgeTests::serverRequiresAuthentication() {
    const auto executable = qEnvironmentVariable("IIWEBSERVER_TEST_RCLONE");
    if (executable.isEmpty()) throw web_test::Skip("Set IIWEBSERVER_TEST_RCLONE");
    QTemporaryDir dir(testDirectory() + "/storage-XXXXXX");
    CTEST_CHECK(QDir(dir.path()).mkdir("files"));
    FileProtocolServer server({executable, {}, {}, dir.path()});
    FileServerOptions options;
    options.root = dir.filePath("files"); options.port = 19876; options.protocol = FileServerProtocol::WebDav;
    CTEST_CHECK(!server.start(options)); CTEST_COMPARE(server.errorString(), "authentication_required");
    options.username = "account"; options.password = "password"; options.address = Address::any();
    CTEST_CHECK(!server.start(options)); CTEST_COMPARE(server.errorString(), "cleartext_disabled");
    options.protocol = FileServerProtocol::WebDavs;
    CTEST_CHECK(!server.start(options)); CTEST_COMPARE(server.errorString(), "tls_identity_required");
    options.protocol = FileServerProtocol::Sftp;
    CTEST_CHECK(!server.start(options)); CTEST_COMPARE(server.errorString(), "ssh_host_key_required");
#ifdef Q_OS_WIN
    options.protocol = FileServerProtocol::Nfs;
    CTEST_CHECK(!server.start(options)); CTEST_COMPARE(server.errorString(), "unsupported_protocol");
#endif
    CTEST_CHECK(!server.isRunning());
}
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    web_test::CTestRunner runner;
    StorageBridgeTests tests;
    runner.run("actualCatalogueAndFileOperations", [&] { tests.actualCatalogueAndFileOperations(); });
    runner.run("rejectUnsafeConfiguration", [&] { tests.rejectUnsafeConfiguration(); });
    runner.run("serverRequiresAuthentication", [&] { tests.serverRequiresAuthentication(); });
    return runner.result();
}
