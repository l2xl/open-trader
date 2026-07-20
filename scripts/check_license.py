#!/usr/bin/env python3
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Replace or verify IPRL v2 copyright/license header in all project source files.

Usage:
  check_license.py [--check] [root]

  --check   Report files with wrong/missing header and exit non-zero; do not write.
  root      Project root (default: parent of the scripts/ directory).
"""

import os
import sys

CPP_HEADER = """\
// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)
"""

HASH_HEADER = """\
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)
"""

# Directories always excluded regardless of .licensecheck-ignore.
ALWAYS_SKIP_DIRS = {'.git', '.claude', 'resources'}

EXTENSIONS = {
    '.cpp': '//', '.hpp': '//', '.h': '//', '.c': '//',
    '.cmake': '#', '.py': '#', '.sh': '#',
}

def comment_style(path):
    name = os.path.basename(path)
    if name == 'CMakeLists.txt':
        return '#'
    return EXTENSIONS.get(os.path.splitext(name)[1].lower())

def load_ignore_patterns(root):
    """Return (dir_prefixes, file_relpaths) from .licensecheck-ignore."""
    ignore_file = os.path.join(root, '.licensecheck-ignore')
    dir_prefixes, file_relpaths = [], []
    if not os.path.exists(ignore_file):
        return dir_prefixes, file_relpaths
    with open(ignore_file) as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            if line.endswith('/'):
                dir_prefixes.append(line.rstrip('/'))
            else:
                file_relpaths.append(line)
    return dir_prefixes, file_relpaths

def is_ignored(relpath, dir_prefixes, file_relpaths):
    parts = relpath.replace(os.sep, '/').split('/')
    for prefix in dir_prefixes:
        prefix_parts = prefix.split('/')
        if parts[:len(prefix_parts)] == prefix_parts:
            return True
    norm = relpath.replace(os.sep, '/')
    return norm in file_relpaths

def strip_old_header(lines, comment):
    """Return (shebang_or_None, remaining_lines) with project header removed."""
    i = 0
    shebang = None
    if lines and lines[0].startswith('#!'):
        shebang = lines[0]
        i = 1

    j, header_end, in_pgp, has_project_header = i, i, False, False

    while j < len(lines):
        raw = lines[j].rstrip('\n')
        if comment == '//':
            is_comment = raw.startswith('//')
            content = raw[2:].strip()
        else:
            is_comment = raw.startswith('#') and not raw.startswith('#!')
            content = raw[1:].strip()

        if not is_comment:
            break

        if 'BEGIN PGP' in content:
            in_pgp = True
        if any(k in content for k in ('Open Trader', 'Copyright', 'IPRL')):
            has_project_header = True

        j += 1
        if 'END PGP' in content:
            in_pgp = False
            header_end = j
        elif not in_pgp:
            header_end = j

    if not has_project_header:
        return shebang, lines[i:]

    if in_pgp:
        header_end = j

    remaining = lines[header_end:]
    while remaining and remaining[0].strip() == '':
        remaining = remaining[1:]

    remaining = _strip_pgp_block(remaining, comment)
    while remaining and remaining[0].strip() == '':
        remaining = remaining[1:]

    return shebang, remaining

def _strip_pgp_block(lines, comment):
    """Remove a leading PGP armored block from lines if present."""
    if not lines:
        return lines
    raw = lines[0].rstrip('\n')
    content = raw[2:].strip() if comment == '//' else raw[1:].strip()
    if 'BEGIN PGP' not in content:
        return lines
    for k, line in enumerate(lines):
        c = line.rstrip('\n')
        c = c[2:].strip() if comment == '//' else c[1:].strip()
        if 'END PGP' in c:
            return lines[k + 1:]
    j = 0
    while j < len(lines) and (lines[j].startswith('//') if comment == '//' else (lines[j].startswith('#') and not lines[j].startswith('#!'))):
        j += 1
    return lines[j:]

def process_file(path, check_only):
    style = comment_style(path)
    if style is None:
        return 'skip', 'unknown type'

    new_header = CPP_HEADER if style == '//' else HASH_HEADER

    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        original = f.read()

    lines = original.splitlines(keepends=True)
    if not lines:
        if check_only:
            return 'MISSING', 'empty file'
        with open(path, 'w', encoding='utf-8') as f:
            f.write(new_header)
        return 'updated', 'was empty'

    shebang, remaining = strip_old_header(lines, style)

    parts = []
    if shebang:
        parts.append(shebang)
    parts.append(new_header)
    if remaining:
        parts.append('\n')
    parts.extend(remaining)

    new_content = ''.join(parts)
    if new_content == original:
        return 'ok', 'correct'

    if check_only:
        return 'MISSING', 'header absent or incorrect'

    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    return 'updated', 'written'

def find_files(root, dir_prefixes, file_relpaths):
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        parts = rel_dir.replace(os.sep, '/').split('/')

        if any(p in ALWAYS_SKIP_DIRS for p in parts) or any(p.startswith('cmake-build') for p in parts):
            dirnames.clear()
            continue

        dirnames[:] = sorted(
            d for d in dirnames
            if d not in ALWAYS_SKIP_DIRS
            and not d.startswith('cmake-build')
            and not is_ignored(
                os.path.relpath(os.path.join(dirpath, d), root) + '/',
                dir_prefixes, [p + '/' for p in []]
            )
        )

        for fname in sorted(filenames):
            path = os.path.join(dirpath, fname)
            relpath = os.path.relpath(path, root)
            if is_ignored(relpath, dir_prefixes, file_relpaths):
                continue
            if comment_style(path) is not None:
                yield path

def main():
    args = sys.argv[1:]
    check_only = '--check' in args
    args = [a for a in args if a != '--check']
    root = args[0] if args else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    dir_prefixes, file_relpaths = load_ignore_patterns(root)

    violations, errors = [], []
    for path in find_files(root, dir_prefixes, file_relpaths):
        try:
            status, detail = process_file(path, check_only)
            print(f'{status:8s} {os.path.relpath(path, root)}  ({detail})')
            if status == 'MISSING':
                violations.append(path)
        except Exception as e:
            errors.append((path, e))
            print(f'ERROR    {os.path.relpath(path, root)}  ({e})')

    if check_only and violations:
        print(f'\n{len(violations)} file(s) missing the required IPRL v2 license header.')
        sys.exit(1)
    if errors:
        print(f'\n{len(errors)} error(s).')
        sys.exit(1)

if __name__ == '__main__':
    main()
