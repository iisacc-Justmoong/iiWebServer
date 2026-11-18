"""Protect the optional-backend test from a Qt Test dependency."""

from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "tests/optional_backends.cpp").read_text(encoding="utf-8")
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
link = re.search(r"target_link_libraries\(iiWebServerOptionalBackendTests\s+(.*?)\)", cmake, re.DOTALL)
if not link or "Qt6::Test" in link.group(1) or re.search(
    r"#\s*include\s*[<\"](?:QtTest|QTest|QSignalSpy)|"
    r"\b(?:QTEST_[A-Z_]+|QTest::|QSignalSpy|QVERIFY|QCOMPARE|QTRY_[A-Z_]+)\b",
    source,
):
    sys.exit("Optional-backend CTest contract still depends on Qt Test")
