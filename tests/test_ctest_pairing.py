from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CASES = ['qrRoundTripAndStrictParsing', 'oneUsePairingNeedsClientConfirmation', 'cancellationRotationAndHostDisconnectInvalidateOffers', 'clientsCannotOfferAndAttemptsAreLimited', 'expiredAuthenticationCannotCompletePairing']

class PairingCTestContract(unittest.TestCase):
    def test_cases_and_offline_selection_are_preserved(self):
        source = (ROOT / "tests/pairing.cpp").read_text()
        for case in CASES:
            self.assertIn(f'runner.run("{case}"', source)
        self.assertIn('"--offline"', source)
        self.assertIn("web_test::SignalRecorder", source)
        self.assertNotRegex(source, r"\b(?:QTest|QSignalSpy|QAbstractItemModelTester)\b|\bQTEST_|<QtTest|<QTest>|\bQ(?:VERIFY|COMPARE|TRY|FETCH|SKIP|FAIL)\w*")

    def test_target_does_not_link_qttest(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        link = re.search(r"target_link_libraries\(iiWebServerPairingTests\s+([^)]*)\)", cmake)
        self.assertIsNotNone(link)
        self.assertNotIn("Qt6::Test", link.group(1))
        self.assertIn("iiWebServer.pairing_offline", cmake)

if __name__ == "__main__":
    unittest.main()
