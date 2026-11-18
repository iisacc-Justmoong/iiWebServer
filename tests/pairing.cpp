#include <iiWebServer.h>
#include "SignalRecorder.h"
#include <QCoreApplication>
#include <QUrlQuery>

using namespace iiWebServer;
using PairingEvents = web_test::SignalRecorder<decltype(&Peer::pairingEvent)>;
namespace {
PeerOptions options(quint16 port, QString id, QByteArray account = "alice", bool host = false) {
    PeerOptions o; o.relayUrl = QUrl(QString("ws://127.0.0.1:%1").arg(port));
    o.peerId = id; o.name = id; o.credential = account; o.hostFiles = host;
    o.localEnabled = false; o.requestTimeoutMs = 1000; return o;
}
QJsonObject pairEvent(PairingEvents &spy, const QString &id, const QString &status) {
    for (const auto &entry : spy) {
        const auto value = entry[0].toJsonObject();
        if (value.value("id").toString() == id && value.value("status").toString() == status) return value;
    }
    return {};
}
}
class PairingTests {
public:
    void qrRoundTripAndStrictParsing() {
        PairingLink link{QUrl("wss://iisacc.com/society/relay"), "desktop-1", QString(64, 'a')}, decoded;
        CTEST_CHECK(PairingLink::decode(link.encode(), &decoded));
        CTEST_COMPARE(decoded.relayUrl, link.relayUrl); CTEST_COMPARE(decoded.hostId, link.hostId); CTEST_COMPARE(decoded.code, link.code);
        const auto plainRelay = link.relayUrl;
        link.relayUrl = QUrl("wss://iisacc.com/relay%2Fsegment/%25encoded");
        CTEST_CHECK(PairingLink::decode(link.encode(), &decoded));
        CTEST_COMPARE(decoded.relayUrl, link.relayUrl);
        link.relayUrl = plainRelay;
        CTEST_CHECK(!PairingLink::decode(link.encode() + "&code=" + QString(64, 'b'), &decoded));
        CTEST_CHECK(!PairingLink::decode(link.encode() + "#extra", &decoded));
        CTEST_CHECK(!PairingLink::decode("https://example.com", &decoded));
        CTEST_CHECK(!PairingLink::decode(QString(3000, 'a'), &decoded));
        link.relayUrl = QUrl("ws://192.168.1.20:9443"); CTEST_CHECK(link.encode().isEmpty());
        link.relayUrl = QUrl("wss://user:secret@iisacc.com"); CTEST_CHECK(link.encode().isEmpty());
        link.relayUrl = QUrl("wss://iisacc.com?token=secret"); CTEST_CHECK(link.encode().isEmpty());
        link.relayUrl = QUrl("ws://127.0.0.1:1234"); CTEST_CHECK(!link.encode().isEmpty());
    }
    void oneUsePairingNeedsClientConfirmation() {
        RelayServer relay([](const auto &account, AuthCompletion done) { done({QString::fromUtf8(account), QDateTime::currentDateTimeUtc().addSecs(60)}); });
        CTEST_CHECK(relay.listen(Address::loopback()));
        Peer host, client, other, stranger;
        CTEST_CHECK(host.start(options(relay.port(), "Desktop", "alice", true), [](const auto &, const auto &) { return QJsonObject{{"ok", true}}; }));
        CTEST_CHECK(client.start(options(relay.port(), "iPhone")));
        CTEST_CHECK(other.start(options(relay.port(), "other")));
        CTEST_CHECK(stranger.start(options(relay.port(), "stranger", "bob")));
        CTEST_WAIT_DEFAULT(host.isReady() && client.isReady() && other.isReady() && stranger.isReady());
        PairingEvents hostEvents(&host, &Peer::pairingEvent), clientEvents(&client, &Peer::pairingEvent),
            otherEvents(&other, &Peer::pairingEvent), strangerEvents(&stranger, &Peer::pairingEvent);
        const auto offer = host.createPairingOffer();
        CTEST_WAIT_DEFAULT(!pairEvent(hostEvents, offer, "offered").isEmpty());
        const auto code = pairEvent(hostEvents, offer, "offered").value("code").toString();
        CTEST_COMPARE(code.size(), 64);
        const auto wrongAccount = stranger.claimPairingOffer(code, "Desktop");
        CTEST_WAIT_DEFAULT(!pairEvent(strangerEvents, wrongAccount, "error").isEmpty());
        const auto wrongHost = other.claimPairingOffer(code, "different-host");
        CTEST_WAIT_DEFAULT(!pairEvent(otherEvents, wrongHost, "error").isEmpty());
        const auto claim = client.claimPairingOffer(code, "Desktop");
        CTEST_WAIT_DEFAULT(!pairEvent(clientEvents, claim, "claimed").isEmpty());
        CTEST_WAIT_DEFAULT(!pairEvent(hostEvents, offer, "claimed").isEmpty());
        CTEST_CHECK(pairEvent(clientEvents, claim, "paired").isEmpty());
        const auto pairId = pairEvent(clientEvents, claim, "claimed").value("pairingId").toString();
        const auto replay = other.claimPairingOffer(code, "Desktop");
        CTEST_WAIT_DEFAULT(!pairEvent(otherEvents, replay, "error").isEmpty());
        other.confirmPairing(pairId);
        web_test::processFor(30); CTEST_CHECK(pairEvent(hostEvents, offer, "paired").isEmpty());
        client.confirmPairing(pairId);
        CTEST_WAIT_DEFAULT(!pairEvent(clientEvents, claim, "paired").isEmpty());
        CTEST_WAIT_DEFAULT(!pairEvent(hostEvents, offer, "paired").isEmpty());
        CTEST_COMPARE(pairEvent(hostEvents, offer, "paired").value("peerId").toString(), QString("iPhone"));
    }
    void cancellationRotationAndHostDisconnectInvalidateOffers() {
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        CTEST_CHECK(relay.listen(Address::loopback())); Peer host, client;
        CTEST_CHECK(host.start(options(relay.port(), "host", "alice", true)));
        CTEST_CHECK(client.start(options(relay.port(), "client")));
        CTEST_WAIT_DEFAULT(host.isReady() && client.isReady());
        PairingEvents h(&host, &Peer::pairingEvent), c(&client, &Peer::pairingEvent);
        auto first = host.createPairingOffer(); CTEST_WAIT_DEFAULT(!pairEvent(h, first, "offered").isEmpty());
        auto code = pairEvent(h, first, "offered").value("code").toString();
        auto second = host.createPairingOffer(); CTEST_WAIT_DEFAULT(!pairEvent(h, second, "offered").isEmpty());
        auto rejected = client.claimPairingOffer(code, "host"); CTEST_WAIT_DEFAULT(!pairEvent(c, rejected, "error").isEmpty());
        code = pairEvent(h, second, "offered").value("code").toString();
        host.cancelPairing(second); CTEST_WAIT_DEFAULT(!pairEvent(h, second, "cancelled").isEmpty());
        rejected = client.claimPairingOffer(code, "host"); CTEST_WAIT_DEFAULT(!pairEvent(c, rejected, "error").isEmpty());
        auto third = host.createPairingOffer(); CTEST_WAIT_DEFAULT(!pairEvent(h, third, "offered").isEmpty());
        code = pairEvent(h, third, "offered").value("code").toString();
        auto claim = client.claimPairingOffer(code, "host"); CTEST_WAIT_DEFAULT(!pairEvent(c, claim, "claimed").isEmpty());
        host.stop(); CTEST_WAIT_DEFAULT(!pairEvent(c, claim, "cancelled").isEmpty());
        CTEST_CHECK(pairEvent(c, claim, "paired").isEmpty());
    }
    void clientsCannotOfferAndAttemptsAreLimited() {
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addSecs(60)}); });
        CTEST_CHECK(relay.listen(Address::loopback())); Peer client;
        CTEST_CHECK(client.start(options(relay.port(), "client"))); CTEST_WAIT_DEFAULT(client.isReady());
        PairingEvents events(&client, &Peer::pairingEvent);
        const auto offer = client.createPairingOffer(); CTEST_WAIT_DEFAULT(!pairEvent(events, offer, "error").isEmpty());
        QString claim;
        for (int i = 0; i < 7; ++i) claim = client.claimPairingOffer(QString(64, 'a'), "host");
        CTEST_WAIT_DEFAULT(!pairEvent(events, claim, "error").isEmpty());
        CTEST_COMPARE(pairEvent(events, claim, "error").value("error").toString(), QString("pairing_rate_limited"));
    }
    void expiredAuthenticationCannotCompletePairing() {
        RelayServer relay([](const auto &, AuthCompletion done) { done({"alice", QDateTime::currentDateTimeUtc().addMSecs(800)}); });
        CTEST_CHECK(relay.listen(Address::loopback())); Peer host, client;
        CTEST_CHECK(host.start(options(relay.port(), "host", "alice", true)));
        CTEST_CHECK(client.start(options(relay.port(), "client")));
        CTEST_WAIT_DEFAULT(host.isReady() && client.isReady());
        PairingEvents h(&host, &Peer::pairingEvent), c(&client, &Peer::pairingEvent);
        auto offer = host.createPairingOffer(); CTEST_WAIT_DEFAULT(!pairEvent(h, offer, "offered").isEmpty());
        auto claim = client.claimPairingOffer(pairEvent(h, offer, "offered").value("code").toString(), "host");
        CTEST_WAIT_DEFAULT(!pairEvent(c, claim, "claimed").isEmpty());
        web_test::processFor(850);
        client.confirmPairing(pairEvent(c, claim, "claimed").value("pairingId").toString());
        web_test::processFor(80); CTEST_CHECK(pairEvent(c, claim, "paired").isEmpty());
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
    PairingTests tests;
    runner.run("qrRoundTripAndStrictParsing", [&] { tests.qrRoundTripAndStrictParsing(); });
    if (!offline)
        runner.run("oneUsePairingNeedsClientConfirmation", [&] { tests.oneUsePairingNeedsClientConfirmation(); });
    if (!offline)
        runner.run("cancellationRotationAndHostDisconnectInvalidateOffers", [&] { tests.cancellationRotationAndHostDisconnectInvalidateOffers(); });
    if (!offline)
        runner.run("clientsCannotOfferAndAttemptsAreLimited", [&] { tests.clientsCannotOfferAndAttemptsAreLimited(); });
    if (!offline)
        runner.run("expiredAuthenticationCannotCompletePairing", [&] { tests.expiredAuthenticationCannotCompletePairing(); });
    return runner.result();
}
