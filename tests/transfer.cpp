#include "CTestSupport.h"
#include "QtEventWait.h"
#include "SignalRecorder.h"
#include <QCoreApplication>
#include "TestPaths.h"
#include "NetworkFixture.h"
#include "Transfer.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

using namespace iiWebServer;

class TransferTests : public QObject {
public:
    void binaryRoundTripAndIntegrity();
    void failedDownloadPreservesDestination();
    void policyRejectsBeforeIO();
    void httpErrorsRedirectsAndCancellation();
};

static bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static TransferResult waitResult(FileTransfer &transfer, const TransferRequest &request) {
    web_test::SignalRecorder spy(&transfer, &FileTransfer::finished);
    const auto id = transfer.start(request);
    if (spy.isEmpty()) spy.wait(10000);
    if (spy.size() != 1 || spy[0][0].toString() != id) return {false, false, "test_timeout"};
    return qvariant_cast<TransferResult>(spy[0][1]);
}

void TransferTests::binaryRoundTripAndIntegrity() {
    if (FileTransfer::backendVersion().isEmpty()) throw web_test::Skip("libcurl disabled");
    QTemporaryDir dir(testDirectory() + "/transfer-XXXXXX");
    CTEST_CHECK(dir.isValid());
    QByteArray bytes(3 * 1024 * 1024 + 123, '\0');
    for (qsizetype i = 0; i < bytes.size(); ++i) bytes[i] = char(i % 256);
    CTEST_CHECK(writeFile(dir.filePath("소스 model.bin"), bytes));
    FileTransfer transfer;
    TransferRequest request;
    request.url = QUrl::fromLocalFile(dir.filePath("소스 model.bin"));
    request.localPath = dir.filePath("out.any-extension");
    request.expectedSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    auto result = waitResult(transfer, request);
    CTEST_CHECK_MESSAGE(result.ok, qPrintable(result.error));
    CTEST_COMPARE(result.bytes, bytes.size());
    CTEST_COMPARE(result.sha256, request.expectedSha256);
    CTEST_COMPARE(readFile(request.localPath), bytes);
    request.operation = TransferOperation::Upload;
    request.url = QUrl::fromLocalFile(dir.filePath("uploaded"));
    result = waitResult(transfer, request);
    CTEST_CHECK_MESSAGE(result.ok, qPrintable(result.error));
    CTEST_COMPARE(readFile(request.url.toLocalFile()), bytes);
    CTEST_COMPARE(transfer.activeCount(), 0);
    // Zero-byte files are still files and must not be treated as failed reads.
    CTEST_CHECK(writeFile(dir.filePath("empty"), {}));
    request.operation = TransferOperation::Download;
    request.url = QUrl::fromLocalFile(dir.filePath("empty"));
    request.expectedSha256 = QCryptographicHash::hash(QByteArray(), QCryptographicHash::Sha256).toHex();
    result = waitResult(transfer, request);
    CTEST_CHECK_MESSAGE(result.ok, qPrintable(result.error));
    CTEST_COMPARE(result.bytes, 0);
    CTEST_COMPARE(readFile(request.localPath), QByteArray());
}

void TransferTests::failedDownloadPreservesDestination() {
    if (FileTransfer::backendVersion().isEmpty()) throw web_test::Skip("libcurl disabled");
    QTemporaryDir dir(testDirectory() + "/transfer-XXXXXX");
    CTEST_CHECK(writeFile(dir.filePath("source"), QByteArray(400000, 'x')));
    const auto destination = dir.filePath("destination");
    CTEST_CHECK(writeFile(destination, "keep"));
    FileTransfer transfer;
    TransferRequest request;
    request.url = QUrl::fromLocalFile(dir.filePath("source"));
    request.localPath = destination;
    request.expectedSha256 = QByteArray(64, '0');
    CTEST_COMPARE(waitResult(transfer, request).error, "checksum_mismatch");
    CTEST_COMPARE(readFile(destination), "keep");
    request.expectedSha256.clear();
    request.maximumBytes = 1024;
    CTEST_CHECK(!waitResult(transfer, request).ok);
    CTEST_COMPARE(readFile(destination), "keep");
}

void TransferTests::policyRejectsBeforeIO() {
    FileTransfer transfer;
    if (FileTransfer::backendVersion().isEmpty()) {
        CTEST_COMPARE(waitResult(transfer, {}).error, "backend_unavailable");
        CTEST_CHECK(FileTransfer::protocols().isEmpty());
        return;
    }
    TransferRequest r;
    r.url = QUrl("http://example.com/file");
    r.localPath = "/nonexistent/destination";
    CTEST_COMPARE(waitResult(transfer, r).error, "cleartext_disabled");
    r.url = QUrl("https://user:secret@example.com/file");
    CTEST_COMPARE(waitResult(transfer, r).error, "invalid_url");
    r.url = QUrl("telnet://example.com/");
    CTEST_COMPARE(waitResult(transfer, r).error, "unsupported_protocol");
    r.url = QUrl("https://example.com/file");
    r.httpVersion = "invalid";
    CTEST_COMPARE(waitResult(transfer, r).error, "unsupported_http_version");
    r.httpVersion.clear();
    r.expectedSha256 = "invalid";
    CTEST_COMPARE(waitResult(transfer, r).error, "invalid_checksum");
    for (const auto &p : FileTransfer::protocols()) {
        CTEST_CHECK(p.download || p.upload);
        if (p.scheme == "sftp") {
            r.expectedSha256.clear();
            r.url = QUrl("sftp://example.com/file");
            CTEST_COMPARE(waitResult(transfer, r).error, "ssh_known_hosts_required");
        }
    }
}

void TransferTests::httpErrorsRedirectsAndCancellation() {
    if (FileTransfer::backendVersion().isEmpty()) throw web_test::Skip("libcurl disabled");
    TestTcpServer server;
    CTEST_CHECK(server.listen(Address::loopback()));
    int requests = 0;
    connect(&server, &TestTcpServer::newConnection, this, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &TestTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &TestTcpSocket::readyRead, socket, [socket, &requests] {
            const auto data = socket->readAll();
            if (!data.contains("\r\n\r\n")) return;
            ++requests;
            if (data.startsWith("GET /slow")) return;
            if (data.startsWith("GET /redirect"))
                socket->write("HTTP/1.1 302 Found\r\nLocation: file:///etc/passwd\r\nContent-Length: 0\r\n\r\n");
            else socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 6\r\n\r\nsecret");
            socket->disconnectFromHost();
        });
    });
    QTemporaryDir dir(testDirectory() + "/transfer-XXXXXX");
    FileTransfer transfer;
    TransferRequest r;
    r.url = QUrl(QString("http://127.0.0.1:%1/missing").arg(server.serverPort()));
    r.localPath = dir.filePath("old");
    CTEST_CHECK(writeFile(r.localPath, "old"));
    const auto missing = waitResult(transfer, r);
    CTEST_COMPARE(missing.responseCode, 404);
    CTEST_CHECK(!missing.ok);
    CTEST_COMPARE(readFile(r.localPath), "old");
    r.url.setPath("/redirect");
    CTEST_COMPARE(waitResult(transfer, r).error, "redirect_requires_new_request");
    CTEST_COMPARE(readFile(r.localPath), "old");
    CTEST_COMPARE(requests, 2);
    r.url.setPath("/slow");
    web_test::SignalRecorder spy(&transfer, &FileTransfer::finished);
    auto id = transfer.start(r);
    CTEST_WAIT_COMPARE_TIMEOUT(requests, 3, 5000);
    transfer.cancel(id);
    CTEST_WAIT_COMPARE_TIMEOUT(spy.size(), 1, 3000);
    CTEST_CHECK(qvariant_cast<TransferResult>(spy[0][1]).cancelled);
    CTEST_COMPARE(readFile(r.localPath), "old");
    CTEST_COMPARE(transfer.activeCount(), 0);
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc > 2) {
        std::cerr << "Expected a case name or --offline" << std::endl;
        return 1;
    }
    const std::string_view selection = argc == 2 ? argv[1] : "";
    const bool offline = selection == "--offline";
    web_test::CTestRunner runner(offline ? std::string_view{} : selection);
    TransferTests tests;
    runner.run("binaryRoundTripAndIntegrity", [&] { tests.binaryRoundTripAndIntegrity(); });
    runner.run("failedDownloadPreservesDestination", [&] { tests.failedDownloadPreservesDestination(); });
    if (!offline)
        runner.run("policyRejectsBeforeIO", [&] { tests.policyRejectsBeforeIO(); });
    if (!offline)
        runner.run("httpErrorsRedirectsAndCancellation", [&] { tests.httpErrorsRedirectsAndCancellation(); });
    return runner.result();
}
