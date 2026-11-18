#include "CTestSupport.h"
#include "QtEventWait.h"
#include "SignalRecorder.h"
#include <QCoreApplication>
#include "NetworkFixture.h"
#include "TestPaths.h"
#include <QJsonDocument>
#include <iiFileProviderPresentation.h>
#include <iiWebServer.h>
#include <QFile>
#include <QJsonArray>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>

using namespace iiWebServer;
namespace {
Authenticator accounts() {
    return [](const QByteArray &token, AuthCompletion done) {
        done({token.startsWith("alice") ? "alice" : token == "bob" ? "bob" : QString(),
              QDateTime::currentDateTimeUtc().addSecs(60)});
    };
}
PeerOptions options(quint16 port, const QString &id, const QByteArray &token = "alice") {
    PeerOptions o;
    o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(port));
    o.credential = token; o.peerId = id; o.name = id;
    o.listenAddress = Address::loopback();
    o.localAddresses = {"127.0.0.1"};
    o.localTimeoutMs = 150; o.requestTimeoutMs = 2000;
    return o;
}
QJsonObject reply(Peer &client, const QString &target, const QJsonObject &payload, QString *route = nullptr) {
    web_test::SignalRecorder done(&client, &Peer::completed);
    const auto id = client.request(target, payload);
    QElapsedTimer time; time.start();
    while (time.elapsed() < 5000) {
        for (const auto &event : done) if (event[0].toString() == id) {
            if (route) *route = event[2].toString();
            return event[1].toJsonObject();
        }
        web_test::processFor(5);
    }
    return {{"error", "test_timeout"}};
}
}

class HostingTests : public QObject {
public:
    void peerIdleDeadlineRetiresAfterRequest() {
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback()));
        Peer peer; auto config = options(relay.port(), "idle-peer"); config.localEnabled = false;
        CTEST_CHECK(peer.start(config)); CTEST_WAIT_DEFAULT(peer.isReady());
        auto *timer = peer.findChild<QTimer *>(QStringLiteral("Peer.deadline"), Qt::FindDirectChildrenOnly);
        CTEST_CHECK(timer); CTEST_CHECK(timer->isSingleShot()); CTEST_CHECK(!timer->isActive());
        web_test::SignalRecorder completed(&peer, &Peer::completed);
        const auto request = peer.request("missing-host", {}); CTEST_CHECK(!request.isEmpty()); CTEST_CHECK(timer->isActive());
        CTEST_WAIT_COMPARE(completed.size(), 1); CTEST_CHECK(!timer->isActive());
    }
    void peerMayBeDestroyedByCompletion() {
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback()));
        auto *peer = new Peer; QPointer<Peer> alive(peer);
        auto config = options(relay.port(), "retiring-peer"); config.localEnabled = false;
        CTEST_CHECK(peer->start(config)); CTEST_WAIT_DEFAULT(peer->isReady());
        connect(peer, &Peer::completed, this, [peer] { delete peer; });
        CTEST_CHECK(!peer->request("missing-host", {}).isEmpty());
        CTEST_WAIT_DEFAULT(alive.isNull());
    }
    void relayTracksOnlyLiveDeadlinesAndHeartbeats() {
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback()));
        auto *deadline = relay.findChild<QTimer *>(QStringLiteral("RelayServer.deadline"), Qt::FindDirectChildrenOnly);
        auto *heartbeat = relay.findChild<QTimer *>(QStringLiteral("RelayServer.heartbeat"), Qt::FindDirectChildrenOnly);
        CTEST_CHECK(deadline); CTEST_CHECK(deadline->isSingleShot()); CTEST_CHECK(!deadline->isActive());
        CTEST_CHECK(heartbeat); CTEST_CHECK(!heartbeat->isActive());
        Peer peer; auto config = options(relay.port(), "heartbeat-peer"); config.localEnabled = false;
        CTEST_CHECK(peer.start(config)); CTEST_WAIT_DEFAULT(peer.isReady());
        CTEST_CHECK(deadline->isActive()); CTEST_CHECK(heartbeat->isActive());
        peer.stop(); CTEST_WAIT_DEFAULT(!deadline->isActive() && !heartbeat->isActive());
        Peer another; config.peerId = "stopping-peer";
        CTEST_CHECK(another.start(config)); CTEST_WAIT_DEFAULT(another.isReady());
        CTEST_CHECK(heartbeat->isActive()); relay.stop();
        CTEST_CHECK(!deadline->isActive() && !heartbeat->isActive());
    }
    void reopeningCanDestroyTheSocketFromDisconnect() {
        WebSocketServer server; CTEST_CHECK(server.listen(Address::loopback(), 0, {}));
        auto *socket = new WebSocket; QPointer<WebSocket> alive(socket);
        const QUrl endpoint(QString("ws://127.0.0.1:%1").arg(server.serverPort()));
        socket->open(endpoint);
        CTEST_WAIT_DEFAULT(socket->state() == ii::network::SocketState::connected);
        connect(socket, &WebSocket::disconnected, this, [socket] { delete socket; });
        socket->open(endpoint);
        CTEST_CHECK(alive.isNull());
    }
    void hostNameDialUsesReachableAddress() {
        WebSocketServer server; CTEST_CHECK(server.listen(Address::loopback(), 0, {}));
        WebSocket socket; web_test::SignalRecorder connected(&socket, &WebSocket::connected);
        socket.open(QUrl(QString("ws://localhost:%1").arg(server.serverPort())));
        CTEST_WAIT_COMPARE_TIMEOUT(connected.size(), 1, 5000);
    }
    void cancelledQueuedOpenStaysClosed() {
        WebSocketServer server; CTEST_CHECK(server.listen(Address::loopback(), 0, {}));
        WebSocket socket; web_test::SignalRecorder accepted(&server, &WebSocketServer::newConnection);
        web_test::SignalRecorder connected(&socket, &WebSocket::connected);
        socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
        socket.abort(); web_test::processFor(100);
        CTEST_COMPARE(socket.state(), ii::network::SocketState::closed);
        CTEST_COMPARE(connected.size(), 0); CTEST_COMPARE(accepted.size(), 0);
    }
    void privatePresentationsDoNotConsumePublicPagination() {
        QTemporaryDir root(testDirectory() + "/presentation-share-XXXXXX"); CTEST_CHECK(root.isValid());
        for (int i = 0; i < 270; ++i) {
            QFile document(root.filePath(QString(".asset-%1.iisacc-preview").arg(i)));
            CTEST_CHECK(document.open(QIODevice::WriteOnly)); document.write("private derived fixture");
        }
        for (const auto &name : {"public.txt", ".public-hidden"}) {
            QFile original(root.filePath(name)); CTEST_CHECK(original.open(QIODevice::WriteOnly)); original.write("original");
        }
        FileShare share(root.path());
        const auto listed = share.handle({{"op", "list"}, {"path", ""}});
        CTEST_CHECK(listed.value("ok").toBool()); CTEST_COMPARE(listed.value("entries").toArray().size(), 2);
        CTEST_CHECK(listed.value("nextCursor").toString().isEmpty());
        CTEST_CHECK(!share.handle({{"op", "stat"}, {"path", ".asset-0.iisacc-preview"}}).value("ok").toBool());
        CTEST_CHECK(!share.handle({{"op", "write"}, {"path", "public.txt.iisacc.json"}, {"data", ""}}).value("ok").toBool());
        CTEST_CHECK(share.handle({{"op", "stat"}, {"path", "public.txt"}}).value("ok").toBool());
        CTEST_CHECK(share.handle({{"op", "write"}, {"path", "uploaded.txt"}, {"data", "cHVibGlzaGVk"}}).value("ok").toBool());
        CTEST_CHECK(iiFileProvider::native::Presentation::waitForIdle(std::chrono::seconds(15)));
        const auto uploaded = iiFileProvider::native::Presentation::load(std::filesystem::path(root.filePath("uploaded.txt").toStdU16String()));
        CTEST_CHECK(uploaded); CTEST_COMPARE(uploaded->details.size, std::uint64_t(9));
    }
    void localFirstAndSameAccountOnly() {
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback()));
        Peer host, client, stranger;
        CTEST_CHECK(host.start(options(relay.port(), "host"), [](const auto &sender, const auto &p) {
            return QJsonObject{{"ok", true}, {"sender", sender}, {"echo", p}};
        }));
        CTEST_CHECK(client.start(options(relay.port(), "client")));
        CTEST_CHECK(stranger.start(options(relay.port(), "stranger", "bob")));
        CTEST_WAIT_DEFAULT(host.isReady() && client.isReady() && stranger.isReady());
        CTEST_WAIT_COMPARE(client.peers().size(), 1);
        CTEST_COMPARE(stranger.peers().size(), 0);
        QString route;
        auto result = reply(client, "host", {{"value", "nearby"}}, &route);
        CTEST_COMPARE(result.value("sender").toString(), "client");
        CTEST_COMPARE(result.value("echo").toObject().value("value").toString(), "nearby");
        CTEST_COMPARE(route, "local");
        CTEST_CHECK(!reply(stranger, "host", {}).value("ok").toBool());
    }
    void remoteRelayAndUnreachableLanFallback() {
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback()));
        for (bool local : {false, true}) {
            Peer host, client;
            auto o = options(relay.port(), "host"); o.localEnabled = local;
            // A deliberately unavailable loopback address makes the local path fail.
            o.localAddresses = {"127.0.0.2"};
            CTEST_CHECK(host.start(o, [](const auto &, const auto &) { return QJsonObject{{"ok", true}, {"data", "remote"}}; }));
            CTEST_CHECK(client.start(options(relay.port(), "client")));
            CTEST_WAIT_DEFAULT(host.isReady() && client.isReady());
            CTEST_WAIT_COMPARE(client.peers().size(), 1);
            QString route;
            CTEST_COMPARE(reply(client, "host", {}, &route).value("data").toString(), "remote");
            CTEST_COMPARE(route, "remote");
        }
    }
    void rejectsCredentialsAndUnsafeTransport() {
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback()));
        Peer p;
        auto o = options(relay.port(), "invalid", "invalid");
        CTEST_CHECK(p.start(o));
        CTEST_WAIT_DEFAULT(!p.errorString().isEmpty());
        CTEST_CHECK(!p.isReady());
        o.relayUrl = QUrl("ws://192.168.1.1:5000");
        CTEST_CHECK(!p.start(o));
        RelayServer unsafe(accounts());
        CTEST_CHECK(!unsafe.listen(Address::any()));
        o = options(relay.port(), "unsafe"); o.listenAddress = Address::any();
        CTEST_CHECK(!p.start(o));
    }
    void expiredAuthorityAndStoppedHostDisappear() {
        bool valid = true;
        RelayServer relay([&](const auto &, AuthCompletion done) {
            done({valid ? "alice" : QString(), QDateTime::currentDateTimeUtc().addMSecs(800)});
        });
        CTEST_CHECK(relay.listen(Address::loopback()));
        Peer host, client;
        CTEST_CHECK(host.start(options(relay.port(), "host")));
        CTEST_CHECK(client.start(options(relay.port(), "client")));
        CTEST_WAIT_DEFAULT(host.isReady() && client.isReady());
        CTEST_WAIT_COMPARE(client.peers().size(), 1);
        host.stop();
        CTEST_WAIT_COMPARE(client.peers().size(), 0);
        valid = false;
        CTEST_WAIT_VERIFY(!client.isReady(), 2000);
    }
    void localListenerRejectsUnauthenticatedRequests() {
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback()));
        Peer host; int calls = 0;
        CTEST_CHECK(host.start(options(relay.port(), "host"), [&](const auto &, const auto &) { ++calls; return QJsonObject{{"ok", true}}; }));
        CTEST_WAIT_DEFAULT(host.isReady());
        WebSocket attack;
        connect(&attack, &WebSocket::connected, &attack, [&] {
            attack.sendTextMessage(R"({"type":"request","id":"attack","payload":{"op":"read","path":"secret"}})");
        });
        attack.open(QUrl(QString("ws://127.0.0.1:%1").arg(host.localPort())));
        web_test::processFor(200);
        CTEST_COMPARE(calls, 0);
        CTEST_CHECK(attack.state() == ii::network::SocketState::closed || attack.state() == ii::network::SocketState::failed);
    }
    void filesAreBoundedAndConfined() {
        QTemporaryDir root(testDirectory() + "/share-XXXXXX"), outside(testDirectory() + "/outside-XXXXXX");
        CTEST_CHECK(root.isValid() && outside.isValid());
        FileShare share(root.path());
        const QByteArray data(700000, 'x');
        QFile file(root.filePath("large.bin")); CTEST_CHECK(file.open(QIODevice::WriteOnly));
        CTEST_COMPARE(file.write(data), data.size()); file.close();
        QByteArray received;
        while (received.size() < data.size()) {
            auto result = share.handle({{"op", "read"}, {"path", "large.bin"}, {"offset", QString::number(received.size())}});
            CTEST_CHECK(result.value("ok").toBool());
            auto chunk = QByteArray::fromBase64(result.value("data").toString().toLatin1());
            CTEST_CHECK(chunk.size() <= FileShare::ChunkBytes); CTEST_CHECK(!chunk.isEmpty()); received += chunk;
        }
        CTEST_COMPARE(received, data);
        const auto oldVersion = share.handle({{"op", "stat"}, {"path", "large.bin"}}).value("version");
        CTEST_CHECK(file.open(QIODevice::Append)); file.write("changed"); file.close();
        CTEST_COMPARE(share.handle({{"op", "read"}, {"path", "large.bin"}, {"version", oldVersion}}).value("error").toString(), "file_changed");
        for (const QString &path : {"../secret", "/etc/passwd", "./large.bin", "a//b", "a\\b", "a:b"})
            CTEST_CHECK(!share.handle({{"op", "read"}, {"path", path}}).value("ok").toBool());
        CTEST_CHECK(QFile::link(outside.path(), root.filePath("link")));
        CTEST_CHECK(!share.handle({{"op", "list"}, {"path", "link"}}).value("ok").toBool());
        CTEST_CHECK(share.handle({{"op", "write"}, {"path", "new.txt"}, {"data", "aGVsbG8="}}).value("ok").toBool());
        CTEST_CHECK(!share.handle({{"op", "write"}, {"path", "new.txt"}, {"data", "Y2hhbmdlZA=="}}).value("ok").toBool());
        CTEST_COMPARE(share.handle({{"op", "read"}, {"path", "new.txt"}}).value("data").toString(), "aGVsbG8=");
        CTEST_CHECK(share.handle({{"op", "mkdir"}, {"path", QString::fromUtf8("자료")}}).value("ok").toBool());
        CTEST_CHECK(share.handle({{"op", "write"}, {"path", QString::fromUtf8("자료/내용.txt")}, {"data", "aGVsbG8="}}).value("ok").toBool());
        CTEST_COMPARE(share.handle({{"op", "read"}, {"path", QString::fromUtf8("자료/내용.txt")}}).value("data").toString(), "aGVsbG8=");
        const auto listed = share.handle({{"op", "list"}, {"path", QString::fromUtf8("자료")}});
        CTEST_CHECK(listed.value("ok").toBool()); CTEST_COMPARE(listed.value("entries").toArray().size(), 1);
        QFile lease(root.filePath(".iisacc-temporary.lock")); CTEST_CHECK(lease.open(QIODevice::WriteOnly)); lease.close();
        QFile nestedLease(root.filePath(QString::fromUtf8("자료/.iisacc-temporary.lock"))); CTEST_CHECK(nestedLease.open(QIODevice::WriteOnly)); nestedLease.close();
        for (const auto &entry : share.handle({{"op", "list"}}).value("entries").toArray())
            CTEST_CHECK(entry.toObject().value("name") != ".iisacc-temporary.lock");
        CTEST_COMPARE(share.handle({{"op", "list"}, {"path", QString::fromUtf8("자료")}}).value("entries").toArray().size(), 1);
        for (const auto &reserved : {QString(".iisacc-temporary.lock"), QString::fromUtf8("자료/.iisacc-temporary.lock")})
            for (const auto &operation : {"read", "stat", "write", "mkdir", "remove"})
                CTEST_CHECK(!share.handle({{"op", operation}, {"path", reserved}, {"data", "aGVsbG8="}}).value("ok").toBool());
        CTEST_CHECK(QFileInfo::exists(lease.fileName()) && QFileInfo(lease.fileName()).size() == 0);
        CTEST_CHECK(QFileInfo::exists(nestedLease.fileName()) && QFileInfo(nestedLease.fileName()).size() == 0);
        bool available = false;
        FileShare guarded(root.path(), [&] { return available; });
        CTEST_CHECK(!guarded.handle({{"op", "list"}}).value("ok").toBool());
        CTEST_CHECK(!share.handle({{"op", "read"}, {"path", "large.bin"}, {"offset", "-1"}}).value("ok").toBool());
    }
    void sessionVerifierIsolatesCookiesAndRejectsRedirect() {
        TestTcpServer http; CTEST_CHECK(http.listen(Address::loopback()));
        QList<QByteArray> seen;
        connect(&http, &TestTcpServer::newConnection, &http, [&] {
            auto *s = http.nextPendingConnection();
            connect(s, &TestTcpSocket::readyRead, s, [&, s] {
                const auto bytes = s->readAll(); if (!bytes.contains("\r\n\r\n")) return;
                seen.append(bytes);
                if (bytes.toLower().contains("cookie: redirect")) {
                    s->write("HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    s->disconnectFromHost(); return;
                }
                const bool alice = bytes.toLower().contains("cookie: alice");
                QByteArray body = alice ? R"({"data":{"accountSession":{"account":{"sub":"verified-alice"}}}})" : R"({"data":{"accountSession":{"account":null}}})";
                if (bytes.toLower().contains("cookie: partial")) body = R"({"data":{"accountSession":{"account":{"sub":"forged"}}},"errors":[{"message":"Failed"}]})";
                s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nSet-Cookie: injected=secret\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                s->disconnectFromHost();
            });
            connect(s, &TestTcpSocket::disconnected, s, &QObject::deleteLater);
        });
        SessionAuthenticator auth(QUrl(QString("http://127.0.0.1:%1/Account/GraphQL").arg(http.serverPort())));
        QList<Principal> verified;
        auth.authenticate("alice", [&](Principal p) { verified.append(p); });
        CTEST_WAIT_COMPARE(verified.size(), 1);
        CTEST_COMPARE(verified[0].accountId, "verified-alice");
        CTEST_CHECK(seen[0].startsWith("POST /Account/GraphQL "));
        const auto input = QJsonDocument::fromJson(seen[0].mid(seen[0].indexOf("\r\n\r\n") + 4)).object();
        CTEST_COMPARE(input.value("query").toString(), QString("query AccountSession { accountSession { account { sub } } }"));
        auth.authenticate("bob", [&](Principal p) { verified.append(p); });
        CTEST_WAIT_COMPARE(verified.size(), 2);
        CTEST_CHECK(verified[1].accountId.isEmpty());
        CTEST_CHECK(!seen[1].contains("injected"));
        auth.authenticate("redirect", [&](Principal p) { verified.append(p); });
        CTEST_WAIT_COMPARE(verified.size(), 3);
        CTEST_CHECK(verified[2].accountId.isEmpty());
        CTEST_COMPARE(seen.size(), 3);
        auth.authenticate("partial", [&](Principal p) { verified.append(p); });
        CTEST_WAIT_COMPARE(verified.size(), 4);
        CTEST_CHECK(verified[3].accountId.isEmpty());
    }
    void tlsLocalPinAndRelayTrust() {
        TlsConfiguration tls;
        tls.policy.identity.certificate_chain_file = (testDirectory() + "/test-cert.pem").toStdString();
        tls.policy.identity.private_key_file = (testDirectory() + "/test-key.pem").toStdString();
        RelayServer relay(accounts()); CTEST_CHECK(relay.listen(Address::loopback(), 0, tls));
        auto o = options(relay.port(), "host"); o.relayUrl.setScheme("wss");
        Peer untrusted;
        CTEST_CHECK(untrusted.start(o));
        CTEST_WAIT_DEFAULT(!untrusted.errorString().isEmpty()); CTEST_CHECK(!untrusted.isReady()); untrusted.stop();
        o.relayTls.policy.ca_file = (testDirectory() + "/test-ca.pem").toStdString(); o.localTls = tls;
        Peer host, client;
        CTEST_CHECK(host.start(o, [](const auto &, const auto &) { return QJsonObject{{"ok", true}, {"tls", true}}; }));
        o.peerId = "client"; o.hostFiles = false;
        CTEST_CHECK(client.start(o));
        CTEST_WAIT_MESSAGE_DEFAULT(host.isReady() && client.isReady(), qPrintable(host.errorString() + " / " + client.errorString()));
        CTEST_WAIT_COMPARE(client.peers().size(), 1);
        QString route;
        CTEST_CHECK(reply(client, "host", {}, &route).value("tls").toBool());
        CTEST_COMPARE(route, "local");
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
    HostingTests tests;
    if (!offline)
        runner.run("peerIdleDeadlineRetiresAfterRequest", [&] { tests.peerIdleDeadlineRetiresAfterRequest(); });
    if (!offline)
        runner.run("peerMayBeDestroyedByCompletion", [&] { tests.peerMayBeDestroyedByCompletion(); });
    if (!offline)
        runner.run("relayTracksOnlyLiveDeadlinesAndHeartbeats", [&] { tests.relayTracksOnlyLiveDeadlinesAndHeartbeats(); });
    if (!offline)
        runner.run("reopeningCanDestroyTheSocketFromDisconnect", [&] { tests.reopeningCanDestroyTheSocketFromDisconnect(); });
    if (!offline)
        runner.run("hostNameDialUsesReachableAddress", [&] { tests.hostNameDialUsesReachableAddress(); });
    if (!offline)
        runner.run("cancelledQueuedOpenStaysClosed", [&] { tests.cancelledQueuedOpenStaysClosed(); });
    if (!offline)
        runner.run("privatePresentationsDoNotConsumePublicPagination", [&] { tests.privatePresentationsDoNotConsumePublicPagination(); });
    if (!offline)
        runner.run("localFirstAndSameAccountOnly", [&] { tests.localFirstAndSameAccountOnly(); });
    if (!offline)
        runner.run("remoteRelayAndUnreachableLanFallback", [&] { tests.remoteRelayAndUnreachableLanFallback(); });
    if (!offline)
        runner.run("rejectsCredentialsAndUnsafeTransport", [&] { tests.rejectsCredentialsAndUnsafeTransport(); });
    if (!offline)
        runner.run("expiredAuthorityAndStoppedHostDisappear", [&] { tests.expiredAuthorityAndStoppedHostDisappear(); });
    if (!offline)
        runner.run("localListenerRejectsUnauthenticatedRequests", [&] { tests.localListenerRejectsUnauthenticatedRequests(); });
    runner.run("filesAreBoundedAndConfined", [&] { tests.filesAreBoundedAndConfined(); });
    if (!offline)
        runner.run("sessionVerifierIsolatesCookiesAndRejectsRedirect", [&] { tests.sessionVerifierIsolatesCookiesAndRejectsRedirect(); });
    if (!offline)
        runner.run("tlsLocalPinAndRelayTrust", [&] { tests.tlsLocalPinAndRelayTrust(); });
    return runner.result();
}
