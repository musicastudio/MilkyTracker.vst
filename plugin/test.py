#!/usr/bin/env python3
"""The plug-in's whole test run: every format built and every check run, here and on the other two platforms.

    python plugin/test.py                    Windows here, Linux on legion, macOS on the Mac VM
    python plugin/test.py windows linux      just those
    python plugin/test.py --no-build mac     the checks alone, on what is already built

Windows builds x64 (CLAP, VST3, VST2, standalone) and x86 (the 32-bit VST2 with the DXi in it). Linux and macOS are
reached over ssh, as the hosts "legion" and "macvm" of ~/.ssh/config (../VirtualMac/README.txt): the source goes over
as a tar that keeps each file's time, so a rebuild there is incremental, it builds there, and the checks run there,
under Xvfb on Linux. macOS also has its AU checked by Apple's auval. Each platform's screenshots and logs come back to
build/test-report/<platform>/, and build/test-report/summary.md says what passed.

The checks, all ctest tests of the build (plugin/CMakeLists.txt):
    plugin           the processor as a host drives it (check_plugin.cpp)
    gui              the editor driven as a mouse and a keyboard drive it, without a window (check_gui.cpp)
    host_<format>    the built binary loaded as a host loads it, in a real window (check_host.cpp)
    host_standalone  the standalone app started, captured and closed (check_host.cpp)
    vst3_validator   Steinberg's VST3 validator, built from the SDK
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPORT = os.path.join(ROOT, 'build', 'test-report')
SKIP_DIRS = {'.git', 'build', 'bin', 'blender', '__pycache__'}
REMOTE = '~/milkytracker-vst'
SSH = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15']


def log(msg):
    print(msg, flush=True)


def run(cmd, cwd=ROOT, check=True, **kw):
    log('$ ' + ' '.join(cmd))
    r = subprocess.run(cmd, cwd=cwd, **kw)
    if check and r.returncode:
        raise SystemExit(f'failed ({r.returncode}): {" ".join(cmd)}')
    return r


def ctest_results(text):
    """[(test, passed)] from ctest's output"""
    out = []
    for m in re.finditer(r'Test\s+#\d+: (\S+) \.+\s*(\*{0,3}\w+)', text):
        out.append((m.group(1), m.group(2) == 'Passed'))
    return out


# ---- Windows, here ---------------------------------------------------------------------------------------------------
def windows(build):
    results = []
    dest = os.path.join(REPORT, 'windows')
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    for arch, d in (('x64', 'build/plugin'), ('Win32', 'build/plugin-x86')):
        if build:
            run(['cmake', '-S', '.', '-B', d, '-G', 'Visual Studio 17 2022', '-A', arch, '-DMILKYTRACKER_PLUGIN=ON'],
                stdout=subprocess.DEVNULL)
            run(['cmake', '--build', d, '--config', 'Release', '--', '-m', '-v:minimal'])
        shutil.rmtree(os.path.join(ROOT, d, 'shots'), ignore_errors=True)
        r = run(['ctest', '--test-dir', d, '-C', 'Release', '--output-on-failure', '--timeout', '600'], check=False,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
        open(os.path.join(dest, f'ctest-{arch}.log'), 'w', encoding='utf-8').write(r.stdout)
        results += [(f'{arch} {t}', ok) for t, ok in ctest_results(r.stdout)]
        if not ctest_results(r.stdout):
            results.append((f'{arch} ctest', False))
            log(r.stdout[-3000:])
        shots = os.path.join(ROOT, d, 'shots')
        if os.path.isdir(shots):
            shutil.copytree(shots, os.path.join(dest, arch), dirs_exist_ok=True)
    return results


# ---- Linux and macOS, over ssh -------------------------------------------------------------------------------------
def tarball():
    """The source as a .tar.gz, each file's time kept, without build output, git or the Blender art"""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w:gz', compresslevel=5) as t:
        for top, dirs, files in os.walk(ROOT):
            rel = os.path.relpath(top, ROOT)
            if rel == '.':
                dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            else:
                dirs[:] = [d for d in dirs if d not in ('__pycache__', '.git')]
            for f in files:
                if f.endswith(('.blend1', '.pyc')):
                    continue
                p = os.path.join(top, f)
                t.add(p, arcname=os.path.relpath(p, ROOT).replace(os.sep, '/'), recursive=False)
    return buf.getvalue()


def ssh(host, script, data=None, capture=False):
    """Runs a bash script on the host, its input data if given"""
    cmd = SSH + [host, 'bash -s' if data is None else script]
    if data is None:
        return subprocess.run(cmd, input=script.encode(), stdout=subprocess.PIPE if capture else None, stderr=subprocess.STDOUT if capture else None)
    return subprocess.run(cmd, input=data, stdout=subprocess.PIPE if capture else None, stderr=subprocess.STDOUT if capture else None)


def remote(name, host, build, env, wrap, extra=''):
    results = []
    dest = os.path.join(REPORT, name)
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    log(f'== {name} on {host}')
    if ssh(host, 'true', capture=True).returncode:
        log(f'{host} cannot be reached')
        return [(f'{name} reachable', False)]
    if build:
        data = tarball()
        log(f'sending the source ({len(data) // 1024} KB)')
        if ssh(host, f'mkdir -p {REMOTE}/src && tar -xzf - -C {REMOTE}/src', data).returncode:   # times kept: only what changed rebuilds
            return [(f'{name} source sent', False)]
        r = ssh(host, f'''set -e
{env}
cd {REMOTE}
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMILKYTRACKER_PLUGIN=ON > configure.log 2>&1 || {{ tail -40 configure.log; exit 1; }}
cmake --build build > build.log 2>&1 || {{ grep -E "error|FAILED|Error" build.log | head -60; exit 1; }}
echo built''')
        if r.returncode:
            return [(f'{name} build', False)]
    r = ssh(host, f'''{env}
cd {REMOTE}
rm -rf build/shots
{wrap.format(cmd='ctest --test-dir build --output-on-failure --timeout 600')} > ctest.log 2>&1
cat ctest.log
{extra}''', capture=True)
    text = r.stdout.decode('utf-8', 'replace')
    open(os.path.join(dest, 'ctest.log'), 'w', encoding='utf-8').write(text)
    results += [(t, ok) for t, ok in ctest_results(text)]
    if not results:
        results.append(('ctest', False))
        log(text[-3000:])
    for m in re.finditer(r'^extra: (\S+) (passed|failed|skipped)(.*)', text, re.M):
        results.append((m.group(1) + m.group(3), None if m.group(2) == 'skipped' else m.group(2) == 'passed'))
    # the screenshots and logs back
    got = subprocess.run(SSH + [host, f'cd {REMOTE} && tar -czf - build/shots configure.log build.log 2>/dev/null'], stdout=subprocess.PIPE)
    if got.stdout:
        with tarfile.open(fileobj=io.BytesIO(got.stdout), mode='r:gz') as t:
            for m in t.getmembers():
                if m.isfile():
                    m.name = os.path.relpath(m.name, 'build') if m.name.startswith('build/') else m.name
                    t.extract(m, dest, filter='data')
    return results


def linux(build):
    # Xvfb's screen is big enough for the editor at 150%, 24-bit as Hollow's X11 window draws
    return remote('linux', 'legion', build, '', 'xvfb-run -a -s "-screen 0 2800x2000x24" {cmd}')


# A command run in the Mac's logged-in GUI session, where the window server is and where the audio component
# registrar lists third-party AUs (an ssh session sees Apple's alone): a .command file opened in the background, as
# FM8.plus's tools/mac/ingui.sh does on the same VM. Its output and exit status come back. With no one logged in to the
# Mac's desktop there is no such session, and it runs here instead (the editor's checks then use an offscreen window).
INGUI = r'''ingui() {
  f=$(mktemp /tmp/ingui.XXXXXX) && mv "$f" "$f.command" && f="$f.command"
  printf '#!/bin/sh\ncd "%s"\nexport PATH="%s"\n( %s ) > "%s.out" 2>&1\necho $? > "%s.rc"\n' "$PWD" "$PATH" "$1" "$f" "$f" > "$f"
  chmod +x "$f"
  if ! open -g "$f" 2>/dev/null; then rm -f "$f"; sh -c "$1"; return $?; fi
  i=0; while [ ! -f "$f.rc" ] && [ $i -lt 1800 ]; do sleep 1; i=$((i+1)); done
  cat "$f.out"; rc=$(cat "$f.rc" 2>/dev/null || echo 124)
  rm -f "$f" "$f.out" "$f.rc"
  return "$rc"
}'''


def mac(build):
    # cmake and ninja from pip (python3 -m pip install --user cmake ninja); auval wants the AU where hosts find it
    env = 'export PATH="$(echo ~/Library/Python/*/bin):$PATH"\n' + INGUI
    extra = '''mkdir -p ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/Components/MilkyTracker.component ~/Library/Audio/Plug-Ins/Components/MilkyTrackerVST.component
cp -R src/bin/AU/MilkyTrackerVST.component ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar 2>/dev/null
if [ "$(stat -f %Su /dev/console)" != "$(whoami)" ]; then
  echo "extra: auval skipped: no one is logged in to the Mac's desktop, where the AU registrar lists third-party AUs"
elif ingui "auval -v aumu MkTr Msst" > auval.log 2>&1; then echo "extra: auval passed"
else tail -40 auval.log; echo "extra: auval failed"; fi
mkdir -p build/shots && cp auval.log build/shots/'''
    return remote('mac', 'macvm', build, env, 'ingui "{cmd}"', extra)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('platforms', nargs='*', help='windows, linux, mac (all three when none is named)')
    ap.add_argument('--no-build', action='store_true', help='run the checks on what is already built')
    a = ap.parse_args()
    platforms = a.platforms or ['windows', 'linux', 'mac']
    for p in platforms:
        if p not in ('windows', 'linux', 'mac'):
            ap.error(f'no platform {p}')
    os.makedirs(REPORT, exist_ok=True)
    started = time.time()
    table = {}
    for p in platforms:
        try:
            table[p] = {'windows': windows, 'linux': linux, 'mac': mac}[p](not a.no_build)
        except SystemExit as e:
            log(str(e))
            table[p] = [('build', False)]
    lines = ['# MilkyTracker.vst test run', '', f'{time.strftime("%Y-%m-%d %H:%M")}, {round(time.time() - started)} s', '']
    bad = 0
    for p, results in table.items():
        lines.append(f'## {p}')
        lines.append('')
        for t, ok in results:   # ok: True, False, or None for a check that could not run
            lines.append(f'- {"skipped" if ok is None else "passed" if ok else "**FAILED**"}: {t}')
            bad += ok is False
        lines.append('')
    lines.append('All passed.' if not bad else f'{bad} failed.')
    open(os.path.join(REPORT, 'summary.md'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    log('\n'.join(lines))
    log(f'report: {REPORT}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
