"""Project resource paths remain confined and preserve existing data."""
import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, sys.argv[1])
import ii_file_provider as provider
owner = Path(sys.argv[2]).resolve()
project = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("web_resource_paths", project / "tools/resource_paths.py")
paths = importlib.util.module_from_spec(spec)
spec.loader.exec_module(paths)

class PathsTest(unittest.TestCase):
    def test_paths_are_confined(self):
        root = owner / "project"
        provider.mkdir(root / "resources", parents=True, exist_ok=False)
        self.assertEqual(paths.resource_subdir(root, "dependencies", "protocol"), root / "resources/dependencies/protocol")
        for bad in ("..", "a/b", ".", ""):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                paths.resource_subdir(root, bad)
        outside = owner / "outside"
        provider.mkdir(outside, exist_ok=False)
        link = root / "resources/dependencies"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Cannot create a symbolic link on this platform: {error}")
        with self.assertRaises(ValueError):
            paths.resource_subdir(root, "dependencies", "protocol")
        self.assertTrue(link.is_symlink())

if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
