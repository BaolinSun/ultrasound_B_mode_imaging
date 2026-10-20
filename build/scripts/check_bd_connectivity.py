"""Compare structural BD HDL, ignoring metadata and simple wire aliases.

This is a connection-graph check, not general RTL formal equivalence.
Reject behavioural HDL and unsupported instances instead of claiming a pass.
"""
import argparse
import re
from pathlib import Path


def _module_signature(text):
    ports = tuple(sorted((name, direction, re.sub(r'\s+', '', width))
                         for direction, width, name in re.findall(
                             r'\b(input|output|inout)\s+(?:wire\s+)?(\[[^\]]+\])?\s*(\w+)\s*;', text)))
    parent = {}

    def root(name):
        parent.setdefault(name, name)
        if parent[name] != name:
            parent[name] = root(parent[name])
        return parent[name]

    assigns = re.findall(r'\bassign\s+([^;]+);', text)
    expressions = []
    for assignment in assigns:
        match = re.fullmatch(r'\s*([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*', assignment)
        if match:
            a, b = map(root, match.groups())
            parent[max(a, b)] = min(a, b)
        else:
            expressions.append(assignment)

    def normalize(expression):
        return re.sub(r'[A-Za-z_]\w*', lambda m: root(m[0]) if m[0] in parent else m[0],
                      re.sub(r'\s+', '', expression))

    instances = []
    for kind, name, body in re.findall(r'^\s*(\w+)\s+(\w+)\s*\((.*?)\)\s*;', text, re.M | re.S):
        if kind == 'module':
            continue
        connections = re.findall(r'\.(\w+)\s*\(([^()]*)\)', body)
        if not connections:
            raise ValueError(f'unsupported instance {kind} {name}')
        remainder = re.sub(r'\.\w+\s*\([^()]*\)', '', body).replace(',', '').strip()
        if remainder:
            raise ValueError(f'unparsed instance connections: {name}')
        instances.append((kind, name, tuple(sorted((p, normalize(v)) for p, v in connections))))
    # Keep public port aliases too; internal graph alone could miss a swapped
    # external port whose net name remains connected to the same IP pin.
    public = tuple((name, normalize(name)) for name, _, _ in ports)
    return ports, tuple(sorted(instances)), tuple(sorted(map(normalize, expressions))), public


def signature(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*|\(\*.*?\*\)', '', text, flags=re.S)
    if re.search(r'\b(always\w*|initial|function|task)\b', text):
        raise ValueError('behavioural HDL is outside this structural check')
    modules = re.findall(r'\bmodule\s+(\w+)\b(.*?)\bendmodule\b', text, re.S)
    if not modules or len({name for name, _ in modules}) != len(modules):
        raise ValueError('missing or duplicate module definitions')
    columns = [[], [], [], []]
    for name, body in modules:
        for column, values in zip(columns, _module_signature(body)):
            # Net aliases must NEVER merge across module scopes.
            column.extend((name, value) for value in values)
    if len(columns[0]) < 10 or len(columns[1]) < 10:
        raise ValueError('expected generated BD ports/instances, not an empty parse')
    return tuple(tuple(sorted(values)) for values in columns)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    args = parser.parse_args()
    old, new = signature(args.before.read_text()), signature(args.after.read_text())
    if old != new:
        for label, a, b in zip(('ports', 'instances', 'assigns', 'public aliases'), old, new):
            if a != b:
                print(f'BD_CONNECTIVITY_DIFFERENT: {label}')
        return 1
    modules = len({name for name, _ in old[0]})
    print(f'BD_CONNECTIVITY_IDENTICAL: {modules} modules, {len(old[0])} module port declarations, '
          f'{len(old[1])} instances; metadata/wire aliases ignored')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
