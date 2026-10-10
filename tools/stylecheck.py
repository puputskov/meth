#!/usr/bin/env python3
"""Checks the C sources against the style rules of CLAUDE.md.

Usage: python tools/stylecheck.py [--summary] [--rule NAME] [--fix] [paths...]

--fix inserts the empty line after a closing brace (rule blank-after-brace),
which clang-format cannot do, and turns leading runs of spaces into tabs
(rule indent), which clang-format sometimes writes for the lines after an
aligned assignment. Both are plain text edits.

Without paths it checks source/ and include/. It reports the rules that a
tool can check; clang-format (.clang-format) does the layout. The checks are
heuristics on the text with comments and string literals blanked out, so a
report is a place to look at, not a verdict.

Rules: indent, brace, blank-after-brace, columns, ternary, call-space,
subscript-space, types, early-return, jump, recursion, numeric-designator,
sizeof-count
"""

import os
import re
import sys

KEYWORDS = {
    'if', 'while', 'for', 'switch', 'return', 'sizeof', 'else', 'do',
    'case', 'defined', '_Alignof', '_Static_assert', 'typeof',
}


def blank(text):
    """Replaces comments, strings and character constants by spaces."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(c + ' ' * (j - i - 2) + c if j - i >= 2 else c)
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def columns(line):
    col = 0
    for ch in line:
        col += (4 - col % 4) if ch == '\t' else 1
    return col


class Report:
    def __init__(self):
        self.items = []

    def add(self, rule, path, line, text):
        self.items.append((rule, path, line, text))


def functions(clean_lines):
    """(name, first, last) of the functions: a name at column 0 followed by
    a parameter list, a body that starts with { at column 0."""
    found = []
    i = 0
    n = len(clean_lines)
    while i < n:
        line = clean_lines[i]
        m = re.match(r'^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(', line)
        if m and not line.startswith(('typedef', '#', 'static const', 'struct')) \
                and not line.rstrip().endswith(';'):
            # find the line that opens the body
            j = i
            while j < n and clean_lines[j].rstrip() != '{' and not (
                    clean_lines[j].startswith('}') or clean_lines[j].rstrip().endswith(';')):
                j += 1
            if j < n and clean_lines[j].rstrip() == '{':
                k = j + 1
                while k < n and not clean_lines[k].startswith('}'):
                    k += 1
                found.append((m.group(1), j, k))
                i = k
        i += 1
    return found


def ends_case(clean, idx):
    """True for the `} break;` that ends a braced case of a switch: the brace
    that opened the case stands at the same indent, right under a case label."""
    line = clean[idx]
    if line.strip() != '} break;':
        return False
    indent = len(line) - len(line.lstrip('\t'))
    for k in range(idx - 1, -1, -1):
        above = clean[k]
        if not above.strip():
            continue
        depth = len(above) - len(above.lstrip('\t'))
        if depth == indent and above.strip() == '{':
            head = clean[k - 1].strip() if k > 0 else ''
            return head.startswith('case ') or head.startswith('default:')
    return False


def check_file(path, rel, rep, only):
    raw = open(path, encoding='utf-8', errors='replace', newline='').read()
    raw = raw.replace('\r\n', '\n')
    lines = raw.split('\n')
    clean = blank(raw).split('\n')

    def want(rule):
        return only is None or rule == only

    in_define = False
    for idx, (orig, cl) in enumerate(zip(lines, clean)):
        no = idx + 1
        stripped = cl.strip()
        is_pp = stripped.startswith('#')
        cont = in_define
        in_define = (is_pp or in_define) and cl.rstrip().endswith('\\')

        if want('indent') and orig and orig[0] == ' ' and orig.strip() \
                and cl.strip():
            rep.add('indent', rel, no, 'line starts with spaces')
        if want('indent') and re.match(r'^\t* +\t', orig):
            rep.add('indent', rel, no, 'tab after spaces')

        if want('columns') and columns(orig) > 80:
            rep.add('columns', rel, no, 'column %d' % columns(orig))

        if is_pp or cont:
            continue

        if want('brace'):
            if re.search(r'\S\s*\{\s*$', cl) and not re.search(r'=\s*\{\s*$', cl) \
                    and not re.search(r'\{[^{}]*\}', cl):
                if not re.search(r'\)\s*\{[^}]*$', cl) or True:
                    rep.add('brace', rel, no, 'brace not on its own line')

        if want('blank-after-brace') and stripped in ('}', '};', '},'):
            nxt = clean[idx + 1].strip() if idx + 1 < len(clean) else ''
            if nxt and not nxt.startswith('}') and not nxt.startswith('#') \
                    and stripped != '},':
                rep.add('blank-after-brace', rel, no, 'no empty line after }')

        if want('ternary') and re.search(r'\?', cl) and ':' in cl:
            rep.add('ternary', rel, no, '?: used')

        if want('call-space'):
            for m in re.finditer(r'\b([A-Za-z_]\w*)\(', cl):
                if m.group(1) not in KEYWORDS or m.group(1) in KEYWORDS:
                    rep.add('call-space', rel, no, 'no space before ( after ' + m.group(1))
                    break

        if want('subscript-space') and re.search(r'[\w\)\]]\[', cl):
            rep.add('subscript-space', rel, no, 'no space before [')

        # libc.c and libc/ of the WebAssembly build are the C library, so the
        # signatures have the types the standard gives them
        if want('types') and 'wasm/libc' not in rel.replace('\\', '/'):
            # printf needs (int) for %.*s and (long long) for %llu, static
            # asserts talk about size_t, and main's loop runs over argc
            test = re.sub(r'\((int|long long|unsigned long long)\)', '', cl)
            if 'sizeof (size_t)' in test:
                test = ''
            if re.search(r'\b(size_t|ssize_t|unsigned|long)\b', test):
                rep.add('types', rel, no, 'use fixed width types')
            elif re.search(r'\bint\b', test) and not re.search(r'\bint\s+main\b', test) \
                    and not re.search(r'\bint\s+(argc|i)\b.*argc', test):
                rep.add('types', rel, no, 'int instead of int32_t')

        if want('jump') and re.search(r'\b(break|continue|goto)\b', cl) \
                and not ends_case(clean, idx):
            rep.add('jump', rel, no, 'break/continue/goto')

        if want('numeric-designator') and re.search(r'^\s*\[\d+\]\s*=', cl):
            rep.add('numeric-designator', rel, no, 'numeric designator')

        if want('sizeof-count') and re.search(r'sizeof\b.*/\s*sizeof', cl):
            rep.add('sizeof-count', rel, no, 'use ml_count')

    funcs = functions(clean)
    if want('early-return'):
        for name, first, last in funcs:
            returns = [i for i in range(first, last)
                       if re.search(r'\breturn\b', clean[i])]
            if len(returns) > 1:
                rep.add('early-return', rel, returns[0] + 1,
                        '%s has %d returns' % (name, len(returns)))
            elif len(returns) == 1:
                # the single return must be the last statement
                at = returns[0]
                depth = 0
                end = at
                while end < last:
                    depth += clean[end].count('(') - clean[end].count(')')
                    if depth <= 0 and clean[end].rstrip().endswith(';'):
                        break
                    end += 1
                tail = [clean[i].strip() for i in range(end + 1, last)
                        if clean[i].strip() and clean[i].strip() != '}']
                indent = len(clean[at]) - len(clean[at].lstrip('	'))
                if tail or indent > 1:
                    rep.add('early-return', rel, at + 1,
                            '%s: return is not at the end' % name)
    return funcs, clean


def find_recursion(all_funcs, rep):
    """Reports functions that can reach themselves through calls."""
    # a function is (file, name); a call goes to the function of that name in
    # the same file, else to the only other one (static functions repeat)
    names = {}
    by_name = {}
    for rel, funcs, clean in all_funcs:
        for name, first, last in funcs:
            names.setdefault((rel, name), []).append((rel, first, last, clean))
            by_name.setdefault(name, set()).add(rel)
    calls = {}
    for key, defs in names.items():
        out = set()
        for rel, first, last, clean in defs:
            body = '\n'.join(clean[first + 1:last + 1])
            for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', body):
                callee = m.group(1)
                if callee not in by_name:
                    continue
                if (rel, callee) in names:
                    out.add((rel, callee))
                elif len(by_name[callee]) == 1:
                    out.add((next(iter(by_name[callee])), callee))
        calls[key] = out
    reported = set()
    for start in names:
        stack = [(start, [start])]
        seen = set()
        while stack:
            cur, path = stack.pop()
            for nxt in calls.get(cur, ()):
                if nxt == start:
                    key = tuple(sorted(path))
                    if key not in reported:
                        reported.add(key)
                        rel, first, last, clean = names[start][0]
                        rep.add('recursion', rel, first + 1,
                                ' -> '.join(p[1] for p in path + [start]))
                elif nxt not in seen:
                    seen.add(nxt)
                    stack.append((nxt, path + [nxt]))


def fix_blank_after_brace(path):
    """Fixes blank-after-brace and indent in a file; returns the changes."""
    raw = open(path, encoding='utf-8', errors='replace', newline='').read()
    crlf = '\r\n' in raw
    text = raw.replace('\r\n', '\n')
    lines = text.split('\n')
    clean = blank(text).split('\n')
    out = []
    added = 0
    for idx, line in enumerate(lines):
        # leading spaces of code are tabs: 4 spaces make one
        if line.startswith(' ') and clean[idx].strip():
            n = len(line) - len(line.lstrip(' '))
            line = '\t' * (n // 4) + ' ' * (n % 4) + line[n:]
            added += 1
        out.append(line)
        stripped = clean[idx].strip()
        if stripped in ('}', '};'):
            nxt = clean[idx + 1].strip() if idx + 1 < len(clean) else ''
            if nxt and not nxt.startswith('}') and not nxt.startswith('#'):
                out.append('')
                added += 1
    if added:
        text = '\n'.join(out)
        open(path, 'w', encoding='utf-8', newline='').write(
            text.replace('\n', '\r\n') if crlf else text)
    return added


def main():
    args = sys.argv[1:]
    summary = False
    fix = False
    only = None
    paths = []
    i = 0
    while i < len(args):
        if args[i] == '--summary':
            summary = True
        elif args[i] == '--fix':
            fix = True
        elif args[i] == '--rule':
            only = args[i + 1]
            i += 1
        else:
            paths.append(args[i])
        i += 1
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if not paths:
        paths = [os.path.join(root, 'source'), os.path.join(root, 'include')]
    files = []
    for p in paths:
        if os.path.isdir(p):
            for d, _, names in os.walk(p):
                for n in sorted(names):
                    if n.endswith(('.c', '.h')):
                        files.append(os.path.join(d, n))
        else:
            files.append(p)
    if fix:
        total = 0
        for f in sorted(files):
            total += fix_blank_after_brace(f)
        print('--- %d lines changed' % total)
    rep = Report()
    all_funcs = []
    for f in sorted(files):
        rel = os.path.relpath(f, root).replace('\\', '/')
        funcs, clean = check_file(f, rel, rep, only)
        all_funcs.append((rel, funcs, clean))
    if only is None or only == 'recursion':
        find_recursion(all_funcs, rep)
    counts = {}
    for rule, path, line, text in rep.items:
        counts[rule] = counts.get(rule, 0) + 1
    if not summary:
        for rule, path, line, text in sorted(rep.items):
            print('%s:%d: [%s] %s' % (path, line, rule, text))
    print('--- %d findings' % len(rep.items))
    for rule in sorted(counts):
        print('%6d  %s' % (counts[rule], rule))
    return 1 if rep.items else 0


if __name__ == '__main__':
    sys.exit(main())
