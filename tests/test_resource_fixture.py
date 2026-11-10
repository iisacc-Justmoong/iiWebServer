"""Test runtime staging and certificates without starting a server."""
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, sys.argv[2])
import ii_file_provider as provider
cmake, provider_python, native_lib, work = sys.argv[1:]
project = Path(__file__).resolve().parents[1]
owner = Path(work).resolve()
probe = owner / "probe.py"
provider.write_text(probe, '''import os, ssl, stat, sys\nfrom pathlib import Path\nroot = Path.cwd()\nassert os.environ["IIWEBSERVER_TEST_DIRECTORY"] == str(root)\nassert (root / "ftp_server.py").is_file() and (root / "provider_fs.py").is_file()\nassert (root / "ii_file_provider/__init__.py").is_file()\nfor name in ("test-ca.pem", "test-cert.pem", "test-key.pem"):\n    assert (root / name).is_file()\nif os.name != "nt":\n    assert stat.S_IMODE((root / "test-key.pem").stat().st_mode) == 0o600\ncertificate = ssl._ssl._test_decode_cert(str(root / "test-cert.pem"))\nassert certificate["subject"] and ("IP Address", "127.0.0.1") in certificate["subjectAltName"]\nsys.path.insert(0, str(root))\nimport ii_file_provider as provider\nprovider.mkdir(root / "provider-probe")\nprint("WORKSPACE=" + str(root))\n''')
command = [sys.executable, "-B", str(project / "tests/resource_runner.py"),
           "--provider-python", provider_python, "--resources", str(project / "resources"),
           "--cmake", cmake, "--certificate-script", str(project / "cmake/TestCertificate.cmake"),
           "--script-dir", str(project / "tools"), "--provider-lib", native_lib,
           "--certificates", "--ftp", "--", sys.executable, "-B", str(probe)]
result = subprocess.run(command, capture_output=True, text=True)
assert result.returncode == 0, result.stdout + result.stderr
paths = [line.removeprefix("WORKSPACE=") for line in result.stdout.splitlines() if line.startswith("WORKSPACE=")]
assert len(paths) == 1 and not Path(paths[0]).exists()
result = subprocess.run(command[:-2] + ["-c", "import os,sys; print('WORKSPACE='+os.getcwd(), flush=True); sys.exit(7)"], capture_output=True, text=True)
assert result.returncode == 7, result.stdout + result.stderr
paths = [line.removeprefix("WORKSPACE=") for line in result.stdout.splitlines() if line.startswith("WORKSPACE=")]
assert len(paths) == 1 and not Path(paths[0]).exists()
print("isolated certificate and provider staging passed")
