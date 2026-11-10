"""Run one contract test in a disposable project resource workspace."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import uuid

_issued = set()


def run(resources, command, *, execute=subprocess.run, prepare=None):
    import ii_file_provider as provider
    resources = Path(resources)
    if resources.is_symlink():
        raise ValueError("Resource root cannot be a symlink")
    resources = resources.resolve(strict=True)
    # Short, isolated names avoid collisions with concurrently running tests.
    # FileProvider intentionally excludes hidden ancestors from automatic
    # presentation intake; keep test fixtures in a visible resource tree.
    state = resources / "t"
    if state.is_symlink():
        raise ValueError("Test state cannot be a symlink")
    provider.mkdir(state, parents=True, exist_ok=True)
    for _ in range(32):
        token = uuid.uuid4().hex[:4]
        if token in _issued:
            continue
        owner = state / token
        try:
            provider.mkdir(owner, exist_ok=False)
            _issued.add(token)
            break
        except FileExistsError:
            continue
    else:
        raise FileExistsError("Cannot reserve a test workspace")
    try:
        if prepare is not None:
            prepare(owner)
        expanded = [part.replace("@WORK@", str(owner)) for part in command]
        env = dict(os.environ, TMPDIR=str(owner), TMP=str(owner), TEMP=str(owner),
                   IIWEBSERVER_TEST_DIRECTORY=str(owner))
        return execute(expanded, cwd=owner, env=env, check=False).returncode
    finally:
        shutil.rmtree(owner)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider-python", required=True)
    parser.add_argument("--ftp-provider-python")
    parser.add_argument("--resources", required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--certificate-script", required=True)
    parser.add_argument("--script-dir", required=True)
    parser.add_argument("--provider-lib", default="-")
    parser.add_argument("--sqlite-lib", default="-")
    parser.add_argument("--certificates", action="store_true")
    parser.add_argument("--ftp", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("A test command is required")
    sys.path.insert(0, args.provider_python)
    import ii_file_provider as provider

    def prepare(owner):
        if args.certificates:
            subprocess.run([args.cmake, "-DIIWEBSERVER_TEST_DIRECTORY=" + str(owner),
                            "-P", args.certificate_script], check=True)
        if args.ftp:
            for name in ("ftp_server.py", "provider_fs.py"):
                provider.copyfile(Path(args.script_dir) / name, owner / name, overwrite=False)
            package = owner / "ii_file_provider"
            provider.mkdir(package, exist_ok=False)
            for source in (Path(args.ftp_provider_python or args.provider_python) / "ii_file_provider").glob("*.py"):
                provider.copyfile(source, package / source.name, overwrite=False)
            libraries = [Path(path) for path in (args.provider_lib, args.sqlite_lib) if path != "-"]
            if libraries:
                provider.mkdir(owner / "lib", exist_ok=False)
                for source in libraries:
                    provider.copyfile(source, owner / "lib" / source.name, overwrite=False)

    return run(args.resources, command, prepare=prepare)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
