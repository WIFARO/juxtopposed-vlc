#!/usr/bin/env python3
"""Deploy runtime-loaded Qt plugins and audit the relocated Linux dependency closure."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

# These belong to the supported host ABI/graphics stack. In particular, do not
# mix a bundled Mesa/libdrm with the user's proprietary or newer GPU drivers.
HOST_LIBRARIES = {
    'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'librt.so.1',
    'libresolv.so.2', 'libutil.so.1', 'ld-linux-x86-64.so.2',
    'libGL.so.1', 'libOpenGL.so.0', 'libGLX.so.0', 'libGLdispatch.so.0',
    'libEGL.so.1', 'libGLESv2.so.2', 'libvulkan.so.1', 'libgbm.so.1',
    'libdrm.so.2', 'libdrm_amdgpu.so.1', 'libdrm_radeon.so.1',
    'libdrm_intel.so.1', 'libdrm_nouveau.so.2',
    'libstdc++.so.6', 'libgcc_s.so.1',
}
WAYLAND_GROUPS = ('wayland-graphics-integration-client', 'wayland-shell-integration',
                  'wayland-decoration-client', 'wayland-graphics-integration-server')
REQUIRED_PLUGINS = ('platforms/libqwayland-egl.so', 'platforms/libqwayland-generic.so',
                    'platforms/libqxcb.so',
                    'wayland-graphics-integration-client/libqt-plugin-wayland-egl.so',
                    'wayland-shell-integration/libxdg-shell.so')


def run(*args, env=None):
    return subprocess.check_output([str(x) for x in args], text=True, env=env)


def elf_files(appdir):
    for path in sorted(appdir.rglob('*')):
        if path.is_symlink():
            if path.readlink().is_absolute() or not path.resolve().is_relative_to(appdir):
                raise ValueError(f'Bundle symlink escapes AppDir: {path}')
            continue
        if path.is_file():
            with path.open('rb') as stream:
                header = stream.read(20)
            if header[:4] == b'\x7fELF' and len(header) >= 18 and int.from_bytes(header[16:18], 'little') in (2, 3):
                if header[4:6] != b'\x02\x01' or int.from_bytes(header[18:20], 'little') != 62:
                    raise ValueError(f'Expected an x86_64 ELF runtime: {path}')
                yield path


def set_runpath(path, library_dir):
    relative = os.path.relpath(library_dir, path.parent)
    paths = ['$ORIGIN']
    if relative != '.':
        paths.append('$ORIGIN/' + relative)
    path.chmod(path.stat().st_mode | 0o200)
    subprocess.run(['patchelf', '--set-rpath', ':'.join(paths), str(path)], check=True)


def dependencies(path, env):
    output = run('ldd', path, env=env)
    if 'not found' in output:
        raise ValueError(f'Unresolved dependencies for {path}:\n{output}')
    # Paths can contain spaces, including the reporter's "VLC [Juxtopposed]".
    return re.findall(r'^\s*(\S+) => (/[^\n]+?)\s+\(0x[0-9a-f]+\)', output, re.M)


def verify_plugins(appdir):
    for relative in REQUIRED_PLUGINS:
        if not (appdir / 'usr/plugins' / relative).is_file():
            raise ValueError(f'Required Qt runtime plugin missing: {relative}')


def deploy(appdir, qt_plugins):
    appdir = appdir.resolve()
    libraries = appdir / 'usr/lib'
    # Mesa/proprietary driver modules load into VLC's process. An older bundled
    # libstdc++ can prevent newer host drivers from loading (GLIBCXX_3.4.32 on
    # Ubuntu24.04). Keep the compiler runtime with the host graphics stack too.
    for path in appdir.rglob('*'):
        if path.name in HOST_LIBRARIES and (path.is_file() or path.is_symlink()):
            path.unlink()
    for group in WAYLAND_GROUPS:
        source = qt_plugins / group
        if source.is_dir():
            shutil.copytree(source, appdir / 'usr/plugins' / group, dirs_exist_ok=True)
    verify_plugins(appdir)
    # This environment is only for build-time dependency discovery. The shipped
    # launcher uses per-ELF RUNPATH and qt.conf, not inherited library overrides.
    discovery = dict(os.environ, LD_LIBRARY_PATH=':'.join(
        [str(libraries), str(qt_plugins.parent / 'lib'), os.environ.get('LD_LIBRARY_PATH', '')]))
    cache = run('ldconfig', '-p')
    for name in ('libharfbuzz.so.0', 'libfribidi.so.0', 'libasound.so.2', 'libfontconfig.so.1'):
        if (libraries / name).exists():
            continue
        match = re.search(r'^\s*' + re.escape(name) + r'\s+.*?=> (\S+)', cache, re.M)
        if not match:
            raise ValueError(f'Required runtime library not installed: {name}')
        shutil.copy2(match[1], libraries / name, follow_symlinks=True)
    pending = list(elf_files(appdir))
    visited = set()
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        set_runpath(path, libraries)
        for name, resolved in dependencies(path, discovery):
            if name in HOST_LIBRARIES:
                continue
            target = libraries / name
            if not target.exists():
                shutil.copy2(resolved, target, follow_symlinks=True)
                pending.append(target)
                print(f'Bundled runtime: {name}')
    # Helpers run in separate processes and need their own relative Qt paths.
    for directory, prefix in [('usr/bin', '..'), ('usr/libexec/vlc', '../..')]:
        (appdir / directory / 'qt.conf').write_text(
            f'[Paths]\nPrefix={prefix}\nPlugins=plugins\nQmlImports=qml\nQml2Imports=qml\n')
    result = audit(appdir)
    (appdir / 'LINUX-RUNTIME.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def audit(appdir):
    appdir = appdir.resolve()
    verify_plugins(appdir)
    for path in appdir.rglob('*'):
        if path.name in HOST_LIBRARIES and path.is_file():
            raise ValueError(f'Bundled host ABI library can conflict with graphics drivers: {path}')
    clean = dict(os.environ)
    clean.pop('LD_LIBRARY_PATH', None)
    clean.pop('LD_PRELOAD', None)
    records = []
    highest_glibc = (0, 0)
    highest_glibcxx = (0, 0, 0)
    for path in elf_files(appdir):
        dynamic = run('readelf', '-d', path)
        paths = re.findall(r'\((?:RUNPATH|RPATH)\).*?\[(.*?)\]', dynamic)
        if not paths or any(not entry.startswith('$ORIGIN') for value in paths for entry in value.split(':')):
            raise ValueError(f'ELF has no fully relative runtime path: {path}')
        for value in paths:
            for entry in value.split(':'):
                suffix = entry[len('$ORIGIN'):]
                if (suffix and not suffix.startswith('/')) or '$' in suffix or not (
                        path.parent / suffix.lstrip('/')).resolve().is_relative_to(appdir):
                    raise ValueError(f'ELF runtime search path escapes AppDir: {path}: {entry}')
        for name, resolved in dependencies(path, clean):
            if not Path(resolved).resolve().is_relative_to(appdir) and name not in HOST_LIBRARIES:
                raise ValueError(f'Unexpected host dependency: {path}: {name} => {resolved}')
        versions = run('readelf', '--version-info', path)
        if 'GLIBC_ABI_DT_RELR' in versions:
            raise ValueError(f'ELF requires a loader newer than the glibc 2.35 baseline: {path}')
        for version in re.findall(r'\bGLIBC_(\d+)\.(\d+)\b', versions):
            highest_glibc = max(highest_glibc, tuple(map(int, version)))
        for version in re.findall(r'\bGLIBCXX_(\d+\.\d+\.\d+)\b', versions):
            highest_glibcxx = max(highest_glibcxx, tuple(map(int, version.split('.'))))
        records.append(dict(path=str(path.relative_to(appdir)), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    if highest_glibc > (2, 35):
        raise ValueError(f'Bundle exceeds declared glibc 2.35 baseline: {highest_glibc}')
    result = dict(elf_files=len(records), required_qt_plugins=list(REQUIRED_PLUGINS),
                  host_libraries=sorted(HOST_LIBRARIES), minimum_glibc='2.35',
                  highest_required_glibc='.'.join(map(str, highest_glibc)), files=records)
    result['minimum_glibcxx'] = '.'.join(map(str, highest_glibcxx))
    print(f'Linux runtime audit passed: {len(records)} ELF files; glibc <= 2.35.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appdir', type=Path)
    parser.add_argument('--qt-plugins', type=Path, help='Matching qmake QT_INSTALL_PLUGINS directory')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    if not args.verify_only and not args.qt_plugins:
        parser.error('--qt-plugins is required when deploying')
    try:
        audit(args.appdir) if args.verify_only else deploy(args.appdir, args.qt_plugins.resolve())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))


if __name__ == '__main__':
    main()
