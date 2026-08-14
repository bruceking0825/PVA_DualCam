from __future__ import annotations

import ast
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
PAGE_MODULES = tuple(path.name for path in sorted((PROJECT_ROOT / "src" / "modules").glob("page_*.py")))
REQUIRED_HOOKS = {
    "_init_state",
    "_setup_ui",
    "_bind_events",
    "_bind_signals",
}


def _page_class(module_name: str) -> ast.ClassDef:
    path = PROJECT_ROOT / "src" / "modules" / module_name
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    expected_name = "Page" + "".join(part.title() for part in path.stem.removeprefix("page_").split("_"))
    return next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == expected_name)


class PageStructureTest(unittest.TestCase):
    def test_all_pages_follow_base_page_contract(self) -> None:
        for module_name in PAGE_MODULES:
            with self.subTest(module=module_name):
                page_class = _page_class(module_name)
                base_names = {base.id for base in page_class.bases if isinstance(base, ast.Name)}
                method_names = {node.name for node in page_class.body if isinstance(node, ast.FunctionDef)}
                assigned_names = {
                    target.id
                    for node in page_class.body
                    if isinstance(node, ast.Assign)
                    for target in node.targets
                    if isinstance(target, ast.Name)
                }

                self.assertIn("BasePage", base_names)
                self.assertIn("UI_CLASS", assigned_names)
                self.assertNotIn("__init__", method_names)
                self.assertLessEqual(REQUIRED_HOOKS, method_names)


if __name__ == "__main__":
    unittest.main()
