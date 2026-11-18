#include "CTestSupport.h"
#include "QtEventWait.h"
#include "SignalRecorder.h"
#include <QCoreApplication>
#include "TestPaths.h"
#include <NetworkTransport.h>
#include <iiWebServer.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include "../src/Protocol/BinaryFrame.h"
#include <QMessageAuthenticationCode>

using namespace iiWebServer;
namespace {
TlsConfiguration trustCertificate(const QString &fingerprint) {
    TlsConfiguration result; const auto bytes = QByteArray::fromHex(fingerprint.toLatin1());
    if (bytes.size() == 32) {
        std::array<unsigned char, 32> pin{}; std::copy(bytes.begin(), bytes.end(), pin.begin());
        result.policy.trusted_certificate_sha256 = pin;
    }
    return result;
}
}
class LanTests : public QObject {
public:
    void idleHostSchedulesOnlyActualDeadlines() {
        LanPeer host;
        CTEST_CHECK(host.startHost("desktop", "Desktop", [](const auto &, const auto &) {
            return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, Address::loopback()));
        auto *timer = host.findChild<QTimer *>(QStringLiteral("LanPeer.deadline"), Qt::FindDirectChildrenOnly);
        CTEST_CHECK(timer); CTEST_CHECK(timer->isSingleShot()); CTEST_CHECK(!timer->isActive());
        CTEST_CHECK(!host.createOffer(2).isEmpty()); CTEST_CHECK(timer->isActive());
        web_test::SignalRecorder changed(&host, &LanPeer::changed);
        CTEST_WAIT_COMPARE_TIMEOUT(host.secondsRemaining(), 1, 1800);
        CTEST_CHECK(!changed.isEmpty());
        host.cancelPairing(); CTEST_CHECK(!timer->isActive());
    }
    void accountProofRequiredBeforeFiles(const QByteArray &clientKey) {
        const auto authenticate = [](LanPeer &peer, QByteArray key) {
            peer.setPairingAuthentication([key](const QByteArray &context) {
                return QJsonObject{{"mac", QString::fromLatin1(QMessageAuthenticationCode::hash(context, key, QCryptographicHash::Sha256).toHex())}};
            }, [key](const QByteArray &context, const QJsonObject &proof) {
                return proof.value("mac").toString().toLatin1() == QMessageAuthenticationCode::hash(context, key, QCryptographicHash::Sha256).toHex();
            });
        };
        LanPeer host, client; int reads = 0;
        authenticate(host, "account-one");
        if (!clientKey.isEmpty()) authenticate(client, clientKey);
        CTEST_CHECK(host.startHost("desktop", "Desktop", [&](const auto &, const auto &) {
            ++reads; return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, Address::loopback()));
        CTEST_CHECK(client.join(host.createOffer(), "phone", "Phone"));
        if (clientKey == "account-one") { CTEST_WAIT_DEFAULT(client.connected()); CTEST_COMPARE(reads, 1); }
        else { CTEST_WAIT_COMPARE(client.phase(), QString("error")); CTEST_COMPARE(reads, 0); CTEST_CHECK(host.pairedDeviceIds().isEmpty()); }
    }
    void authenticatedClientRejectsLegacyHost() {
        LanPeer host, client;
        client.setPairingAuthentication([](const QByteArray &) { return QJsonObject{{"proof", "client"}}; },
            [](const QByteArray &, const QJsonObject &) { return false; });
        CTEST_CHECK(host.startHost("desktop", "Desktop", [](const auto &, const auto &) {
            return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, Address::loopback()));
        CTEST_CHECK(client.join(host.createOffer(), "phone", "Phone"));
        CTEST_WAIT_COMPARE(client.phase(), QString("error")); CTEST_CHECK(!client.connected());
        CTEST_WAIT_DEFAULT(host.pairedDeviceIds().isEmpty());
    }
    void binaryFrameBoundsAndWireSavings() {
        const QByteArray data(256 * 1024, '\xff');
        for (const auto &message : {
            QJsonObject{{"type", "request"}, {"payload", QJsonObject{{"message", QJsonObject{{"data", QString::fromLatin1(data.toBase64())}}}}}},
            QJsonObject{{"type", "response"}, {"result", QJsonObject{{"result", QJsonObject{{"data", QString::fromLatin1(data.toBase64())}}}}}},
            QJsonObject{{"type", "response"}, {"result", QJsonObject{{"data", QString::fromLatin1(data.toBase64())}}}}
        }) {
            const auto frame = protocol::BinaryFrame::encode(message); CTEST_CHECK(!frame.isEmpty());
            CTEST_COMPARE(protocol::BinaryFrame::decode(frame), message);
            const auto json = QJsonDocument(message).toJson(QJsonDocument::Compact);
            CTEST_CHECK(frame.size() < json.size() * 0.76); CTEST_CHECK(frame.size() < data.size() + 256);
            qInfo("binary frame=%lld json=%lld payload=%lld", qlonglong(frame.size()), qlonglong(json.size()), qlonglong(data.size()));
            auto malformed = frame; malformed[0] = 'X'; CTEST_CHECK(protocol::BinaryFrame::decode(malformed).isEmpty());
            malformed = frame; malformed[4] = '\x7f'; CTEST_CHECK(protocol::BinaryFrame::decode(malformed).isEmpty());
            CTEST_CHECK(protocol::BinaryFrame::decode(frame.left(8)).isEmpty());
            CTEST_CHECK(protocol::BinaryFrame::decode(frame + QByteArray(256 * 1024, 'x')).isEmpty());
        }
        CTEST_CHECK(protocol::BinaryFrame::encode({{"data", "not a known file path"}}).isEmpty());
    }
    void negotiatedBinaryUploadAndDownload() {
        LanPeer host, client; const QByteArray bytes(256 * 1024, '\x81'); int chunks = 0;
        CTEST_CHECK(host.startHost("desktop", "Desktop", [&](const auto &, const auto &request) {
            if (request.value("op") == "list") return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
            const auto data = request.value("message").toObject().value("data").toString();
            if (QByteArray::fromBase64(data.toLatin1()) == bytes) ++chunks;
            return QJsonObject{{"ok", true}, {"result", QJsonObject{{"ok", true}, {"data", data}}}};
        }, {"127.0.0.1"}, Address::loopback()));
        CTEST_CHECK(client.join(host.createOffer(), "phone", "Phone")); CTEST_WAIT_DEFAULT(client.connected()); CTEST_CHECK(client.binaryTransferEnabled());
        web_test::SignalRecorder completed(&client, &LanPeer::completed);
        for (int i = 0; i < 4; ++i) CTEST_CHECK(!client.request("desktop", {{"op", "society.sync"},
            {"message", QJsonObject{{"data", QString::fromLatin1(bytes.toBase64())}}}}).isEmpty());
        CTEST_WAIT_COMPARE(completed.size(), 4); CTEST_COMPARE(chunks, 4);
        for (const auto &result : completed) CTEST_COMPARE(QByteArray::fromBase64(result[1].toJsonObject().value("result").toObject().value("data").toString().toLatin1()), bytes);
    }
    void binaryBeforePairingCannotReadFiles() {
        LanPeer host; int reads = 0;
        CTEST_CHECK(host.startHost("desktop", "Desktop", [&](const auto &, const auto &) {
            ++reads; return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, Address::loopback()));
        LanLink offer; CTEST_CHECK(LanLink::decode(host.createOffer(), &offer));
        WebSocket unpaired; bool encodedFrameValid = false; web_test::SignalRecorder disconnected(&unpaired, &WebSocket::disconnected);
        connect(&unpaired, &WebSocket::connected, &unpaired, [&] {
            const auto frame = protocol::BinaryFrame::encode({{"type", "request"}, {"id", "unpaired"},
                {"payload", QJsonObject{{"message", QJsonObject{{"data", "YWJj"}}}}}});
            encodedFrameValid = !frame.isEmpty(); if (encodedFrameValid) unpaired.sendBinaryMessage(frame);
        });
        unpaired.setTlsConfiguration(trustCertificate(offer.fingerprint));
        unpaired.open(QUrl("wss://127.0.0.1:" + QString::number(offer.port)));
        CTEST_WAIT_COMPARE(disconnected.size(), 1); CTEST_CHECK(encodedFrameValid); CTEST_COMPARE(reads, 0); CTEST_CHECK(host.pairedDeviceIds().isEmpty());
    }
    void oldJsonClientsRemainCompatible() {
        LanPeer host; const QByteArray bytes(8192, 'L');
        CTEST_CHECK(host.startHost("desktop", "Desktop", [&](const auto &, const auto &request) {
            return request.value("op") == "list" ? QJsonObject{{"ok", true}, {"entries", QJsonArray()}}
                : QJsonObject{{"ok", true}, {"data", QString::fromLatin1(bytes.toBase64())}};
        }, {"127.0.0.1"}, Address::loopback()));
        LanLink offer; CTEST_CHECK(LanLink::decode(host.createOffer(), &offer));
        WebSocket old; web_test::SignalRecorder binary(&old, &WebSocket::binaryMessageReceived); bool paired = false, received = false;
        const auto send = [&](const QJsonObject &value) { old.sendTextMessage(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))); };
        connect(&old, &WebSocket::connected, &old, [&] { send({{"type", "pair"}, {"host", "desktop"}, {"code", offer.code}, {"peerId", "old"}, {"name", "Old"}}); });
        connect(&old, &WebSocket::textMessageReceived, &old, [&](const QString &text) {
            const auto value = QJsonDocument::fromJson(text.toUtf8()).object();
            if (value.value("type") == "ready") send({{"type", "confirm"}});
            if (value.value("type") == "paired") { paired = true; send({{"type", "request"}, {"id", "request"}, {"payload", QJsonObject{{"op", "read"}}}}); }
            if (value.value("type") == "response") received = QByteArray::fromBase64(value.value("result").toObject().value("data").toString().toLatin1()) == bytes;
        });
        old.setTlsConfiguration(trustCertificate(offer.fingerprint));
        old.open(QUrl("wss://127.0.0.1:" + QString::number(offer.port)));
        CTEST_WAIT_DEFAULT(paired && received); CTEST_COMPARE(binary.size(), 0);
    }
    void discoveredDeviceNeedsConfirmationBeforeFiles() {
        LanPeer host, client, wrong;
        int reads = 0;
        CTEST_CHECK(host.startHost("desktop", "Desktop", [&](const auto &, const auto &) {
            ++reads; return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, Address::loopback()));
        const auto offer = host.createDeviceOffer("phone"); CTEST_CHECK(!offer.isEmpty());
        CTEST_CHECK(wrong.join(offer, "other", "Other")); CTEST_WAIT_COMPARE(wrong.phase(), "error");
        CTEST_COMPARE(reads, 0);
        CTEST_CHECK(client.join(offer, "phone", "Phone"));
        CTEST_WAIT_COMPARE(host.phase(), "confirming"); CTEST_WAIT_COMPARE(client.phase(), "confirming");
        CTEST_CHECK(!client.connected()); CTEST_COMPARE(reads, 0);
        CTEST_COMPARE(host.verificationCode(), client.verificationCode());
        CTEST_COMPARE(host.verificationCode().size(), 14);
        CTEST_CHECK(host.confirmDevice()); CTEST_WAIT_DEFAULT(client.connected());
        CTEST_COMPARE(host.pairedDeviceIds(), QStringList{"phone"});
        CTEST_COMPARE(client.pairedDeviceIds(), QStringList{"desktop"});
        CTEST_COMPARE(reads, 1); CTEST_CHECK(!host.confirmDevice());
        CTEST_CHECK(host.createDeviceOffer("desktop").isEmpty());
    }
    void discoveredDeviceCancellationClosesPendingConnection() {
        LanPeer host, client;
        CTEST_CHECK(host.startHost("desktop", "Desktop", [](const auto &, const auto &) {
            return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
        }, {"127.0.0.1"}, Address::loopback()));
        CTEST_CHECK(client.join(host.createDeviceOffer("phone"), "phone", "Phone"));
        CTEST_WAIT_COMPARE(host.phase(), "confirming"); host.cancelPairing();
        CTEST_WAIT_COMPARE(client.phase(), "error"); CTEST_CHECK(!client.connected());
        CTEST_CHECK(host.verificationCode().isEmpty());
    }
    void strictLocalQr() {
        LanLink link{{"192.168.10.20", "10.0.0.8"}, 9443, "desktop", "Desktop", QString(64, 'a'), QString(64, 'b'), QDateTime::currentDateTimeUtc().addSecs(60)}, decoded;
        CTEST_CHECK(LanLink::decode(link.encode(), &decoded));
        CTEST_COMPARE(decoded.addresses, link.addresses); CTEST_COMPARE(decoded.code, link.code);
        link.name = QString::fromUtf8("작업실 & 100% Desktop");
        CTEST_CHECK(LanLink::decode(link.encode(), &decoded)); CTEST_COMPARE(decoded.name, link.name);
        CTEST_CHECK(!LanLink::decode(link.encode() + "&code=" + QString(64, 'c'), &decoded));
        CTEST_CHECK(!LanLink::decode(link.encode() + "#fragment", &decoded));
        for (const auto &host : {"iisacc.com", "8.8.8.8", "0.0.0.0", "224.1.1.1", "169.254.169.254"}) {
            link.addresses = {host}; CTEST_CHECK_MESSAGE(link.encode().isEmpty(), host);
        }
    }
    void noRelayPairingAndFiles() {
        QTemporaryDir root(testDirectory() + "/lan-XXXXXX");
        QFile file(root.filePath("hello.txt")); CTEST_CHECK(file.open(QIODevice::WriteOnly)); file.write("local bytes"); file.close();
        FileShare share(root.path()); LanPeer host, client, replay;
        CTEST_CHECK(host.startHost("desktop", "My Desktop", [&](const auto &, const auto &r) { return share.handle(r); }, {"127.0.0.1"}, Address::loopback()));
        const auto qr = host.createOffer(); CTEST_CHECK(!qr.isEmpty());
        web_test::SignalRecorder h(&host, &LanPeer::paired); web_test::SignalRecorder c(&client, &LanPeer::paired);
        CTEST_CHECK(client.join(qr, "phone", "My iPhone"));
        CTEST_WAIT_DEFAULT(client.phase() == "paired" || client.phase() == "error");
        CTEST_CHECK_MESSAGE(client.connected(), qPrintable(client.errorString()));
        CTEST_WAIT_COMPARE(c.size(), 1); CTEST_WAIT_COMPARE(h.size(), 1);
        CTEST_CHECK(host.qrText().isEmpty()); CTEST_CHECK(client.connected()); CTEST_COMPARE(client.peerName(), "My Desktop");
        web_test::SignalRecorder response(&client, &LanPeer::completed);
        const auto id = client.request("desktop", {{"op", "read"}, {"path", "hello.txt"}, {"offset", "0"}});
        CTEST_WAIT_COMPARE(response.size(), 1); CTEST_COMPARE(response[0][0].toString(), id);
        const auto result = response[0][1].toJsonObject(); CTEST_CHECK(result["ok"].toBool());
        CTEST_COMPARE(QByteArray::fromBase64(result["data"].toString().toLatin1()), QByteArray("local bytes"));
        CTEST_COMPARE(response[0][2].toString(), "local");
        CTEST_CHECK(replay.join(qr, "replay", "Replay")); CTEST_WAIT_COMPARE(replay.phase(), "error"); CTEST_CHECK(!replay.connected());
        host.stop(); CTEST_WAIT_DEFAULT(!client.connected());
    }
    void certificatePinMismatchNeverPairs() {
        LanPeer host, client;
        CTEST_CHECK(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject{{"ok", true}, {"entries", QJsonArray()}}; }, {"127.0.0.1"}, Address::loopback()));
        LanLink link; CTEST_CHECK(LanLink::decode(host.createOffer(), &link)); link.fingerprint = QString(64, '0');
        web_test::SignalRecorder paired(&host, &LanPeer::paired); CTEST_CHECK(client.join(link.encode(), "phone", "Phone"));
        CTEST_WAIT_COMPARE(client.phase(), "error"); CTEST_CHECK(!client.connected()); CTEST_COMPARE(paired.size(), 0);
        CTEST_CHECK_MESSAGE(client.errorString().contains("certificate"), qPrintable(client.errorString()));
    }
    void cancellationExpiryAndFilesFailure() {
        LanPeer host, client;
        CTEST_CHECK(host.startHost("desktop", "Desktop", [](const auto &, const auto &) { return QJsonObject{{"ok", false}, {"error", "container_unavailable"}}; }, {"127.0.0.1"}, Address::loopback()));
        const auto cancelled = host.createOffer(); host.cancelPairing();
        CTEST_CHECK(client.join(cancelled, "phone", "Phone")); CTEST_WAIT_COMPARE(client.phase(), "error");
        auto qr = host.createOffer(1); CTEST_WAIT_DEFAULT(host.qrText().isEmpty());
        CTEST_CHECK(!client.join(qr, "phone", "Phone"));
        qr = host.createOffer(); web_test::SignalRecorder paired(&host, &LanPeer::paired);
        CTEST_CHECK(client.join(qr, "phone", "Phone")); CTEST_WAIT_COMPARE(client.phase(), "error");
        CTEST_COMPARE(paired.size(), 0); CTEST_CHECK(!client.connected());
    }
};
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
    LanTests tests;
    if (!offline)
        runner.run("idleHostSchedulesOnlyActualDeadlines", [&] { tests.idleHostSchedulesOnlyActualDeadlines(); });
    if (!offline)
        runner.run("accountProofRequiredBeforeFiles:same-account", [&] { tests.accountProofRequiredBeforeFiles(QByteArray("account-one")); });
    if (!offline)
        runner.run("accountProofRequiredBeforeFiles:different-account", [&] { tests.accountProofRequiredBeforeFiles(QByteArray("account-two")); });
    if (!offline)
        runner.run("accountProofRequiredBeforeFiles:legacy-client", [&] { tests.accountProofRequiredBeforeFiles(QByteArray()); });
    if (!offline)
        runner.run("authenticatedClientRejectsLegacyHost", [&] { tests.authenticatedClientRejectsLegacyHost(); });
    runner.run("binaryFrameBoundsAndWireSavings", [&] { tests.binaryFrameBoundsAndWireSavings(); });
    if (!offline)
        runner.run("negotiatedBinaryUploadAndDownload", [&] { tests.negotiatedBinaryUploadAndDownload(); });
    if (!offline)
        runner.run("binaryBeforePairingCannotReadFiles", [&] { tests.binaryBeforePairingCannotReadFiles(); });
    if (!offline)
        runner.run("oldJsonClientsRemainCompatible", [&] { tests.oldJsonClientsRemainCompatible(); });
    if (!offline)
        runner.run("discoveredDeviceNeedsConfirmationBeforeFiles", [&] { tests.discoveredDeviceNeedsConfirmationBeforeFiles(); });
    if (!offline)
        runner.run("discoveredDeviceCancellationClosesPendingConnection", [&] { tests.discoveredDeviceCancellationClosesPendingConnection(); });
    runner.run("strictLocalQr", [&] { tests.strictLocalQr(); });
    if (!offline)
        runner.run("noRelayPairingAndFiles", [&] { tests.noRelayPairingAndFiles(); });
    if (!offline)
        runner.run("certificatePinMismatchNeverPairs", [&] { tests.certificatePinMismatchNeverPairs(); });
    if (!offline)
        runner.run("cancellationExpiryAndFilesFailure", [&] { tests.cancellationExpiryAndFilesFailure(); });
    return runner.result();
}
