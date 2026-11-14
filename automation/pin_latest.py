#!/usr/bin/env python3
"""Pin the latest promoted train as the owner's desktop Zene (owner 2026-10-30: "create a part of the train that pins
the latest version of zene studio and pins it to my taskbar").

Called from merge_train.promote() while zene-cachyos is on the promoted tip with its build complete:

  1. `cmake --install` that build into ~/.local/opt/zene-studio/<train>.new (plus libwasmtime.so, which the install
     leaves out), smoke-run `zene --version` from it, then rename it to <train> and point `current` at it - so the
     launcher never runs a half-built tree, and later trains/bisects rebuilding zene-cachyos do not touch it;
  2. write ~/.local/bin/zene-studio-latest (sets the zene-deps library path) and
     ~/.local/share/applications/zene-studio-latest.desktop ("Zene Studio (latest)");
  3. make sure that entry is a launcher on the Plasma taskbar (re-added each train if missing; touch
     qwen/PIN-TASKBAR-OFF to stop that);
  4. keep the KEEP newest installs, delete older ones.

The owner's own zene-studio.desktop (the 0.4.0 AppImage) is left alone. A failure here never fails the train: it is
logged and the owner told. `pin_latest.py [--train NAME] [--no-install]` runs it by hand.
"""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

HOME = Path.home()
ROOT = Path(__file__).resolve().parent
WT = HOME / "Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-cachyos"
OPT = HOME / ".local/opt/zene-studio"
BIN = HOME / ".local/bin/zene-studio-latest"
APPS = HOME / ".local/share/applications"
DESKTOP_ID = "zene-studio-latest.desktop"
DEPS = HOME / ".local/zene-deps"
KEEP = 2
TASKBAR_OFF = ROOT / "PIN-TASKBAR-OFF"     # touch it to stop (re)adding the taskbar launcher


def install(train):
    name = train.split("/")[-1]
    dest, tmp = OPT / name, OPT / (name + ".new")
    shutil.rmtree(tmp, ignore_errors=True)
    OPT.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(["cmake", "--install", str(WT / "build"), "--prefix", str(tmp)], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"cmake --install failed: {(r.stderr or r.stdout)[-400:]}")
    (tmp / "lib/zene-runtime").mkdir(parents=True, exist_ok=True)
    shutil.copy2(WT / "third_party/wasmtime/lib/libwasmtime.so", tmp / "lib/zene-runtime/")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", LD_LIBRARY_PATH=lib_path(tmp))
    env.pop("LMMS_PLUGIN_DIR", None)
    r = subprocess.run([str(tmp / "bin/zene"), "--version"], env=env, capture_output=True, text=True, timeout=60)
    if r.returncode != 0 or "Zene Studio" not in r.stdout:
        raise RuntimeError(f"installed zene does not start: {(r.stderr or r.stdout)[-400:]}")
    import train_versions
    release = dict(train_versions.current(train), binary_version=r.stdout.splitlines()[0])
    (tmp / 'train-version.json').write_text(json.dumps(release, indent=2) + '\n')
    shutil.rmtree(dest, ignore_errors=True)
    tmp.rename(dest)
    link = OPT / "current.new"
    link.unlink(missing_ok=True)
    link.symlink_to(name)
    link.replace(OPT / "current")
    return dest, r.stdout.splitlines()[0]


def lib_path(prefix):
    return ":".join(str(p) for p in (prefix / "lib/zene-runtime", DEPS / "root/usr/lib",
                                     DEPS / "onnxruntime-linux-x64-1.28.0/lib"))


def write_launcher(version):
    cur = OPT / "current"
    BIN.parent.mkdir(parents=True, exist_ok=True)
    BIN.write_text("#!/bin/sh\n# written by zene-bunny-fix/qwen/pin_latest.py on each promoted train\n"
                   "unset LMMS_PLUGIN_DIR QT_QPA_PLATFORM\n"
                   f'export LD_LIBRARY_PATH="{lib_path(cur)}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"\n'
                   f'exec "{cur}/bin/zene" "$@"\n')
    BIN.chmod(0o755)
    APPS.mkdir(parents=True, exist_ok=True)
    (APPS / DESKTOP_ID).write_text(
        "[Desktop Entry]\nType=Application\nName=Zene Studio (latest)\nGenericName=Digital Audio Workstation\n"
        f"Comment={version}\nExec={BIN} %f\nIcon={cur}/share/icons/hicolor/256x256/apps/zene.png\n"
        "Terminal=false\nCategories=AudioVideo;Audio;Music;\nMimeType=application/x-lmms-project;\n"
        "StartupWMClass=zene\n")
    subprocess.run(["update-desktop-database", str(APPS)], capture_output=True)
    subprocess.run(["kbuildsycoca6"], capture_output=True)


# Plasma's scripting API edits the live task manager config (editing appletsrc by hand is overwritten by plasmashell).
PIN_JS = """
var id = "applications:%s", done = 0;
panels().forEach(function (p) { p.widgets().forEach(function (w) {
  if (w.type != "org.kde.plasma.icontasks" && w.type != "org.kde.plasma.taskmanager") return;
  w.currentConfigGroup = ["General"];
  var l = w.readConfig("launchers", []);
  if (typeof l == "string") l = l ? l.split(",") : [];
  if (l.indexOf(id) < 0) { l.push(id); w.writeConfig("launchers", l); done++; }
}); });
print(done);
""" % DESKTOP_ID


def pin_taskbar():
    if TASKBAR_OFF.exists():
        return "taskbar pin off (PIN-TASKBAR-OFF)"
    env = dict(os.environ)
    env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path=/run/user/{os.getuid()}/bus")
    r = subprocess.run(["qdbus6", "org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell.evaluateScript", PIN_JS],
                       capture_output=True, text=True, env=env, timeout=30)
    if r.returncode != 0:
        return f"taskbar pin failed: {(r.stderr or r.stdout).strip()[:200]}"
    return "pinned to the taskbar" if r.stdout.strip() not in ("", "0") else "already on the taskbar"


def prune():
    cur = (OPT / "current").resolve().name
    installs = sorted((p for p in OPT.iterdir() if p.is_dir() and not p.is_symlink() and not p.name.endswith(".new")),
                      key=lambda p: p.stat().st_mtime, reverse=True)
    for p in installs[KEEP:]:
        if p.name != cur:
            shutil.rmtree(p)


def pin(train, do_install=True):
    """Returns a one-line result for the train log; raises on failure."""
    if do_install:
        _, version = install(train)
    else:
        version = train
    import train_versions
    release = train_versions.current(train)
    write_launcher(f"Zene Studio {release['display_version']} (feature train {release['train_label']}); {version}")
    note = pin_taskbar()
    prune()
    return f"{version} {'installed' if do_install else 'launcher rewritten'} as Zene Studio (latest); {note}"


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--train", default=(ROOT / "FIX-BASE").read_text().strip())
    ap.add_argument("--no-install", action="store_true", help="only rewrite the launcher and re-pin")
    a = ap.parse_args()
    print(pin(a.train, not a.no_install))
