#!/usr/bin/env python3
"""Bundle non-system Mach-O dependencies, then verify the bundle is self-contained.

Run after macdeployqt. Third-party libraries are namespaced by their original path
so Qt's own media libraries cannot collide with a different FFmpeg installation.
No downloads, shell evaluation, or signing credentials are used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


MAGICS = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
          b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
          b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}
SYSTEM_PREFIXES = ('/System/Library/', '/usr/lib/')


def run(*args):
    result = subprocess.run(args, check=True, capture_output=True, text=True)
    return result.stdout


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open('rb') as handle:
        return handle.read(4) in MAGICS


def dependencies(path):
    return list(dict.fromkeys(
        match.group(1) for line in run('/usr/bin/otool', '-L', str(path)).splitlines()
        if (match := re.match(r'^\s+(.+?) \(compatibility version ', line))
    ))


def install_ids(path):
    result = subprocess.run(['/usr/bin/otool', '-D', str(path)], capture_output=True, text=True)
    return {line.strip() for line in result.stdout.splitlines()
            if line.strip() and not line.rstrip().endswith(':') and 'architecture ' not in line}


def rpaths(path):
    paths, active = [], False
    for line in run('/usr/bin/otool', '-l', str(path)).splitlines():
        if line.strip() == 'cmd LC_RPATH':
            active = True
        elif active and (match := re.match(r'^\s+path (.+) \(offset \d+\)', line)):
            paths.append(match.group(1))
            active = False
    return list(dict.fromkeys(paths))


def within(path, parent):
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


class Bundler:
    def __init__(self, bundle, qt_prefix, audit_only, prepare=False, required_arch=None):
        self.bundle = bundle.resolve()
        self.frameworks = self.bundle / 'Contents/Frameworks'
        self.executables = self.bundle / 'Contents/MacOS'
        self.qt_prefix = qt_prefix.resolve() if qt_prefix else None
        self.audit_only = audit_only
        self.prepare = prepare
        self.required_arch = required_arch
        self.origins = {}
        self.bundled_sources = {}
        self.manifest = []
        manifest_path = self.bundle / 'Contents/Resources/bundled-library-manifest.json'
        if manifest_path.exists():
            self.manifest = json.loads(manifest_path.read_text())
        self.app_rpaths = []
        for executable in self.executables.iterdir():
            if macho(executable):
                self.app_rpaths.extend(rpaths(executable))

    def expand(self, text, loader):
        return Path(text.replace('@loader_path', str(loader.parent))
                    .replace('@executable_path', str(self.executables)))

    def resolve(self, dependency, loader):
        if dependency.startswith(SYSTEM_PREFIXES):
            return None
        original = self.origins.get(loader, loader)
        if dependency.startswith('@rpath/'):
            suffix = dependency[len('@rpath/'):]
            candidates = [self.frameworks / suffix]
            for anchor in (loader, original):
                for entry in rpaths(anchor) + self.app_rpaths:
                    candidates.append(self.expand(entry, anchor) / suffix)
            if self.qt_prefix:
                candidates.append(self.qt_prefix / 'lib' / suffix)
        elif dependency.startswith('@loader_path/'):
            candidates = [self.expand(dependency, loader), self.expand(dependency, original)]
        elif dependency.startswith('@executable_path/'):
            candidates = [self.expand(dependency, loader)]
        elif dependency.startswith('/'):
            candidates = [Path(dependency)]
        else:
            raise RuntimeError(f'Unsupported dependency reference {dependency!r} in {loader}')
        for candidate in candidates:
            if candidate.exists():
                return candidate.resolve()
            if str(candidate).startswith(SYSTEM_PREFIXES):
                return None  # system shared-cache libraries need not exist as loose files
        raise RuntimeError(f'Unresolved dependency {dependency!r} in {loader}')

    def copy_library(self, source):
        source = source.resolve()
        if source in self.bundled_sources:
            return self.bundled_sources[source]
        digest = hashlib.sha256(str(source).encode()).hexdigest()[:12]
        target_dir = self.frameworks / 'FramecutLibraries' / digest
        framework_part = next((p for p in source.parents if p.name.endswith('.framework')), None)
        if framework_part:
            destination = target_dir / framework_part.name / source.relative_to(framework_part)
            if not destination.exists():
                target_dir.mkdir(parents=True, exist_ok=True)
                shutil.copytree(framework_part, target_dir / framework_part.name, symlinks=True)
        else:
            destination = target_dir / source.name
            if not destination.exists():
                target_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        destination.chmod(destination.stat().st_mode | 0o200)
        self.origins[destination] = source
        self.bundled_sources[source] = destination
        if install_ids(destination):
            new_id = '@rpath/' + destination.relative_to(self.frameworks).as_posix()
            run('/usr/bin/install_name_tool', '-id', new_id, str(destination))
        self.manifest.append({'source': str(source), 'bundled': str(destination.relative_to(self.bundle))})
        return destination

    def process(self, path):
        own_ids = install_ids(path)
        # Deployment tools may rewrite identities while copying dependencies.
        # Restore a unique identity for each actual library, including private
        # FFmpeg versions with the same basename as Qt's media dependencies.
        if own_ids and within(path, self.frameworks) and not self.audit_only:
            unique_id = '@rpath/' + path.relative_to(self.frameworks).as_posix()
            if own_ids != {unique_id}:
                path.chmod(path.stat().st_mode | 0o200)
                run('/usr/bin/install_name_tool', '-id', unique_id, str(path))
                own_ids = {unique_id}
        for dependency in dependencies(path):
            if dependency in own_ids:
                continue
            if self.prepare and (re.search(r'(^|/)Qt[^/]+\.framework/', dependency)
                                 or re.search(r'(^|/)libQt6[^/]+\.dylib$', dependency)):
                continue
            source = self.resolve(dependency, path)
            if source is None:
                continue
            if not within(source, self.bundle):
                if self.audit_only:
                    raise RuntimeError(f'External dependency remains: {path} -> {source}')
                target = self.copy_library(source)
            else:
                target = source
            new_reference = '@loader_path/' + os.path.relpath(target, path.parent)
            if dependency != new_reference and not self.audit_only:
                path.chmod(path.stat().st_mode | 0o200)
                run('/usr/bin/install_name_tool', '-change', dependency, new_reference, str(path))
        if not self.audit_only and not self.prepare:
            for entry in rpaths(path):
                if entry.startswith('/') and not entry.startswith(SYSTEM_PREFIXES):
                    run('/usr/bin/install_name_tool', '-delete_rpath', entry, str(path))

    def run(self):
        processed = set()
        identity_owners = {}
        while True:
            paths = [p for p in self.bundle.rglob('*') if macho(p) and p not in processed]
            if not paths:
                break
            for path in paths:
                self.process(path)
                if self.audit_only:
                    if self.required_arch:
                        run('/usr/bin/lipo', '-verify_arch', self.required_arch, str(path))
                    for identity in install_ids(path):
                        owner = identity_owners.setdefault(identity, path)
                        if owner != path:
                            raise RuntimeError(f'Duplicate library identity {identity!r}: {owner} and {path}')
                processed.add(path)
        if not self.audit_only:
            resources = self.bundle / 'Contents/Resources'
            resources.mkdir(exist_ok=True)
            unique = {entry['bundled']: entry for entry in self.manifest}
            (resources / 'bundled-library-manifest.json').write_text(json.dumps(list(unique.values()), indent=2) + '\n')
        print(f'{"Audited" if self.audit_only else "Bundled"} {len(processed)} Mach-O files.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--qt-prefix', type=Path)
    parser.add_argument('--audit-only', action='store_true')
    parser.add_argument('--prepare', action='store_true', help='relocate non-Qt libraries before macdeployqt')
    parser.add_argument('--arch', choices=['arm64', 'x86_64'], help='required architecture during final audit')
    args = parser.parse_args()
    if not (args.bundle / 'Contents/MacOS').is_dir():
        parser.error('bundle must be a built macOS .app directory')
    if args.prepare and args.audit_only:
        parser.error('--prepare and --audit-only cannot be used together')
    Bundler(args.bundle, args.qt_prefix, args.audit_only, args.prepare, args.arch).run()


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Bundle verification failed: {error}', file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        sys.exit(1)
