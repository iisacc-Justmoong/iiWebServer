from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CASES = {'lan': ['idleHostSchedulesOnlyActualDeadlines', 'accountProofRequiredBeforeFiles', 'authenticatedClientRejectsLegacyHost', 'binaryFrameBoundsAndWireSavings', 'negotiatedBinaryUploadAndDownload', 'binaryBeforePairingCannotReadFiles', 'oldJsonClientsRemainCompatible', 'discoveredDeviceNeedsConfirmationBeforeFiles', 'discoveredDeviceCancellationClosesPendingConnection', 'strictLocalQr', 'noRelayPairingAndFiles', 'certificatePinMismatchNeverPairs', 'cancellationExpiryAndFilesFailure'], 'hosting': ['peerIdleDeadlineRetiresAfterRequest', 'peerMayBeDestroyedByCompletion', 'relayTracksOnlyLiveDeadlinesAndHeartbeats', 'reopeningCanDestroyTheSocketFromDisconnect', 'hostNameDialUsesReachableAddress', 'cancelledQueuedOpenStaysClosed', 'privatePresentationsDoNotConsumePublicPagination', 'localFirstAndSameAccountOnly', 'remoteRelayAndUnreachableLanFallback', 'rejectsCredentialsAndUnsafeTransport', 'expiredAuthorityAndStoppedHostDisappear', 'localListenerRejectsUnauthenticatedRequests', 'filesAreBoundedAndConfined', 'sessionVerifierIsolatesCookiesAndRejectsRedirect', 'tlsLocalPinAndRelayTrust'], 'transfer': ['binaryRoundTripAndIntegrity', 'failedDownloadPreservesDestination', 'policyRejectsBeforeIO', 'httpErrorsRedirectsAndCancellation']}
TARGETS = {"lan": "iiWebServerLanTests", "hosting": "iiWebServerTests", "transfer": "iiWebServerTransferTests"}

class NetworkCTestContracts(unittest.TestCase):
    def test_cases_rows_and_safe_selections_are_preserved(self):
        for suite, cases in CASES.items():
            source = (ROOT / f"tests/{suite}.cpp").read_text()
            for case in cases:
                suffix = ':' if case == "accountProofRequiredBeforeFiles" else '"'
                self.assertIn(f'runner.run("{case}{suffix}', source)
            self.assertIn('"--offline"', source)
            self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy|QAbstractItemModelTester)\b|\bQTEST_|<QtTest|<QTest>|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")
        source = (ROOT / "tests/lan.cpp").read_text()
        for row in ("same-account", "different-account", "legacy-client"):
            self.assertIn(f':{row}"', source)

    def test_installed_consumers_are_qttest_free(self):
        cmake = (ROOT / "tests/consumer/CMakeLists.txt").read_text()
        self.assertNotRegex(cmake, r"Qt6::Test|find_package\(Qt6[^)]*\bTest\b")
        for suite in ("lan", "hosting", "transfer", "pairing"):
            self.assertIn(f"iiWebServer.installed_{suite}_offline", cmake)

    def test_targets_do_not_link_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for suite, target in TARGETS.items():
            link = re.search(r"target_link_libraries\(" + target + r"\s+([^)]*)\)", cmake)
            self.assertIsNotNone(link)
            self.assertNotIn("Qt6::Test", link.group(1))
            self.assertIn(f"iiWebServer.{suite}_offline", cmake)
        self.assertNotRegex(cmake, r"Qt6::Test|find_package\(Qt6[^)]*\bTest\b")

if __name__ == "__main__":
    unittest.main()
