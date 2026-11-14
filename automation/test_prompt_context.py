import importlib.util
import io
import json
import sys
import unittest
from importlib.machinery import SourceFileLoader
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

import prompt_context
import opencode_free

ROOT = Path(__file__).resolve().parent
PROG = Path.home() / "Documents/AI_KOS_PROJECT/projects/zene-studio-program"
SENTENCE = prompt_context.OWNER_CONTEXT


class PromptContextTest(unittest.TestCase):
    def test_stamp_is_idempotent_and_keeps_prompt(self):
        original = "Preserve every concrete constraint."
        once = prompt_context.stamp(original)
        self.assertIn(SENTENCE, once)
        self.assertIn(original, once)
        self.assertEqual(prompt_context.stamp(once), once)

    def test_luna_wrapper_stamps_the_actual_codex_dispatch(self):
        path = PROG / "tools/luna/luna-exec"
        loader = SourceFileLoader("luna_exec_prompt_test", str(path))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with TemporaryDirectory() as tmp:
            calls = []
            def run(argv, **kwargs):
                calls.append((argv, kwargs))
                return SimpleNamespace(stdout="", returncode=0)
            with patch.object(sys, "argv", [str(path), "-C", tmp, "-o", str(Path(tmp) / "out"), "-"]), \
                 patch.object(sys, "stdin", io.StringIO("Do the assigned task.")), \
                 patch.object(module.subprocess, "run", side_effect=run):
                self.assertEqual(module.main(), 0)
            delivered = calls[-1][1]["input"]
            self.assertIn(SENTENCE, delivered)
            self.assertIn("Do the assigned task.", delivered)

    def test_duet_codex_adapter_stamps_the_prompt_before_streaming(self):
        sys.path.insert(0, str(ROOT.parent / "duet"))
        try:
            import adapters
            with TemporaryDirectory() as tmp:
                root = Path(tmp)
                captured = {}
                def stream(cmd, cwd, env, prompt, log, on_line):
                    captured["prompt"] = prompt
                    Path(log).write_text("")
                    return 0, None
                with patch.object(adapters, "_stream", side_effect=stream):
                    adapters.run_codex({"model": "gpt-6-luna"}, "Review this change.", root,
                                       root / "out", root / "log", write=False)
                self.assertIn(SENTENCE, captured["prompt"])
                self.assertIn("Review this change.", captured["prompt"])
        finally:
            sys.path.remove(str(ROOT.parent / "duet"))

    def test_opencode_harness_stamps_before_provider_dispatch(self):
        class Stdin:
            value = ""
            def write(self, value):
                self.value += value
            def close(self):
                pass
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            proc = SimpleNamespace(stdin=Stdin(), stdout=[json.dumps({"type": "text", "part": {"text": "done"}}) + "\n"],
                                   poll=lambda: 0, wait=lambda: 0, pid=1)
            with patch.object(opencode_free, "uses_mcode", return_value=False), \
                 patch.object(opencode_free, "uses_dsh", return_value=False), \
                 patch.object(opencode_free.subprocess, "Popen", return_value=proc), \
                 patch.object(opencode_free.signal, "signal"):
                opencode_free.run_model("space-bunny-free", root, "Implement the task.", root / "final.md")
            self.assertIn(SENTENCE, proc.stdin.value)
            self.assertIn("Implement the task.", proc.stdin.value)


if __name__ == "__main__":
    unittest.main()
