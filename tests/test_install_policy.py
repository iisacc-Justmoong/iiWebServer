"""Check the install plan using fake tools; do not build, install or start servers."""
from pathlib import Path
import os
import subprocess
import sys

sys.path.insert(0, sys.argv[1])
import ii_file_provider as provider
project = Path(__file__).resolve().parents[1]
owner = Path(sys.argv[2]).resolve()
bin_dir = owner / "bin"
provider.mkdir(bin_dir, exist_ok=False)
for name in ("cmake", "ctest"):
    script = bin_dir / name
    provider.write_text(script, "#!/bin/sh\nprintf '" + name + ":'\nprintf ' <%s>' \"$@\"\nprintf '\\n'\n")
    script.chmod(0o700)
network = owner / "network"
provider.mkdir(network / "lib/cmake/iiNetwork", parents=True, exist_ok=True)
env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + "/usr/bin:/bin",
           IINETWORK_PREFIX=str(network), INSTALL_PREFIX=str(owner / "install"),
           IIWEBSERVER_BUILD_COMPATIBILITY="ON")
env.pop("IIWEBSERVER_RUN_INTEGRATION_TESTS", None)
def run():
    process = subprocess.run(["/bin/bash", str(project / "install.sh")],
                             env=env, capture_output=True, text=True)
    assert process.returncode == 0, process.stdout + process.stderr
    return process.stdout
output = run()
assert str(project / "build") in output and "<--install>" in output
assert "<--test-dir> <" + str(project / "build") + ">" in output
assert "<-R>" in output and "resource_fixture" in output
assert "consumer/build" not in output
assert "<-DCMAKE_INSTALL_PREFIX=" + env["INSTALL_PREFIX"] + ">" in output
assert str(network) in output
env["IIWEBSERVER_RUN_INTEGRATION_TESTS"] = "1"
output = run()
assert "<--test-dir> <" + str(project / "build") + ">" in output and "<-R>" not in output
assert "consumer/build" not in output
