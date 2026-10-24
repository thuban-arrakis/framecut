#!/usr/bin/env python3
"""Install a reviewed local build; upgrades retain the previous app and source."""
import argparse
from datetime import datetime
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

parser = argparse.ArgumentParser()
parser.add_argument("--app", required=True, type=Path)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--destination-app", required=True, type=Path)
parser.add_argument("--destination-source", required=True, type=Path)
parser.add_argument("--upgrade", action="store_true", help="Retain existing destinations as backups before replacing them")
parser.add_argument("--launch", action="store_true")
args = parser.parse_args()

# A checkout must retain its history, index, and local changes in place.
destination_git = args.destination_source / ".git"
if destination_git.exists() or destination_git.is_symlink():
    raise SystemExit("The source destination is a Git checkout. Update it through Git or choose a separate source destination.")

for destination in (args.destination_app, args.destination_source):
    if destination.is_symlink():
        raise SystemExit(f"Refusing a symlink destination: {destination}")
    if destination.exists() and (not args.upgrade or not destination.is_dir()):
        raise SystemExit(f"Destination exists; use --upgrade to retain a backup: {destination}")
inputs = [args.app.resolve(), args.source.resolve()]
destinations = [args.destination_app.resolve(), args.destination_source.resolve()]
for index, destination in enumerate(destinations):
    for other in inputs + destinations[:index]:
        if destination == other or destination in other.parents or other in destination.parents:
            raise SystemExit("Input and destination paths must not overlap.")
subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(args.app)], check=True)

# Prepare and verify both copies before touching either existing destination.
for destination in destinations:
    destination.parent.mkdir(parents=True, exist_ok=True)
stage_source_root = Path(tempfile.mkdtemp(prefix=".framecut-source-stage-", dir=destinations[1].parent))
stage_app_root = Path(tempfile.mkdtemp(prefix=".framecut-app-stage-", dir=destinations[0].parent))
stage_source = stage_source_root / "framecut"
stage_app = stage_app_root / "Framecut.app"
stage_source.mkdir()
for name in ["CMakeLists.txt", "resources.qrc", "README.md", ".gitignore", "src", "tests", "assets", "docs", "packaging", "scripts", "translations"]:
    source = args.source / name
    destination = stage_source / name
    if source.is_dir():
        shutil.copytree(source, destination, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    else:
        shutil.copy2(source, destination)

attributes = args.source / ".gitattributes"
if attributes.is_file():
    shutil.copy2(attributes, stage_source / ".gitattributes")

release = stage_source / "release"
release.mkdir()
for artifact in (args.source / "release").rglob("*"):
    if artifact.is_file() and artifact.suffix in {".zip", ".deb", ".sha256"}:
        destination = release / artifact.relative_to(args.source / "release")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(artifact, destination)
if (args.source / "qa").exists():
    shutil.copytree(args.source / "qa", stage_source / "qa")
subprocess.run(["/usr/bin/ditto", str(args.app), str(stage_app)], check=True)
subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(stage_app)], check=True)

suffix = datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
backups = []
installed = []
try:
    for staged, destination in ((stage_source, destinations[1]), (stage_app, destinations[0])):
        if destination.exists():
            backup = destination.with_name(f".{destination.name}.backup-{suffix}")
            destination.rename(backup)
            backups.append((destination, backup))
        staged.rename(destination)
        installed.append((staged, destination))
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(destinations[0])], check=True)
except BaseException:
    # Return the staged copies and restore the old installation on commit failure.
    # Retain the staging folders on failure so no recovery material is discarded.
    for staged, destination in reversed(installed):
        destination.rename(staged)
    for destination, backup in reversed(backups):
        backup.rename(destination)
    raise

shutil.rmtree(stage_source_root)
shutil.rmtree(stage_app_root)
print(f"Installed app: {args.destination_app}")
print(f"Project source: {args.destination_source}")
for destination, backup in backups:
    print(f"Previous {destination.name}: {backup}")
if args.launch:
    subprocess.run(["/usr/bin/open", str(args.destination_app)], check=True)
