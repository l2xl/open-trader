#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Render req_status.json as a static, navigable list + per-requirement card site."""

import argparse
import json
import sys
from pathlib import Path

import jinja2
from markupsafe import Markup

ROOT = Path(__file__).resolve().parent.parent

STATUS_LABEL = {
    "not_implemented": "NOT IMPLEMENTED",
    "partially_implemented": "PARTIALLY IMPLEMENTED",
    "test_passed": "TEST PASSED",
    "test_failed": "TEST FAILED",
}

STATUS_CLASS = {
    "not_implemented": "gray",
    "partially_implemented": "yellow",
    "test_passed": "green",
    "test_failed": "red",
}

BASE_CSS = """
body { font-family: -apple-system, Segoe UI, sans-serif; margin: 0; padding: 2rem; background: #f6f7f9; color: #1b222b; }
a { color: #2e5c6e; text-decoration: none; }
a:hover { text-decoration: underline; }
table { border-collapse: collapse; width: 100%; }
th, td { text-align: left; padding: 0.5rem 0.8rem; border-bottom: 1px solid #dde1e6; font-size: 0.9rem; }
th { text-transform: uppercase; font-size: 0.7rem; letter-spacing: 0.04em; color: #5b6472; }
.status { font-family: monospace; font-size: 0.72rem; padding: 0.15em 0.5em 0.15em 0.4em; border-radius: 3px; white-space: nowrap; }
.ball { display: inline-block; width: 0.65em; height: 0.65em; border-radius: 50%; margin-right: 0.4em; }
.status-gray   { background: #7a7f8722; color: #5b6472; }
.status-gray   .ball { background: #7a7f87; }
.status-yellow { background: #b8862b22; color: #9a6f1c; }
.status-yellow .ball { background: #d9a63d; }
.status-green  { background: #3f7d5222; color: #3f7d52; }
.status-green  .ball { background: #3f7d52; }
.status-red    { background: #a8482e22; color: #a8482e; }
.status-red    .ball { background: #a8482e; }
.card { background: #fff; border: 1px solid #dde1e6; border-radius: 4px; padding: 1.5rem; max-width: 800px; }
.field { margin-bottom: 1rem; }
.field .label { font-family: monospace; font-size: 0.7rem; text-transform: uppercase; color: #5b6472; }
.back { display: inline-block; margin-bottom: 1rem; }
ul.links { list-style: none; padding: 0; margin: 0; }
ul.links li { margin-bottom: 0.3rem; }
nav.views { margin-bottom: 1.5rem; font-size: 0.85rem; }
nav.views a { margin-right: 1rem; }
ul.tree, ul.tree ul { list-style: none; margin: 0; padding-left: 1.3rem; }
ul.tree { padding-left: 0; }
ul.tree li { margin: 0.25rem 0; }
.tree-dup { color: #7a7f87; font-size: 0.78rem; font-style: italic; }
details.testlog { margin-bottom: 0.4rem; }
details.testlog summary { cursor: pointer; font-size: 0.85rem; }
details.testlog pre { background: #eef1ee; border: 1px solid #dde1e6; border-radius: 3px; padding: 0.6rem 0.8rem; font-size: 0.75rem; overflow-x: auto; }
"""

NAV = Markup('<nav class="views"><a href="index.html">List</a><a href="tree.html">Tree</a></nav>')

INDEX_TEMPLATE = """<!doctype html><meta charset="utf-8"><title>Requirements Status</title><style>{{ css }}</style>
{{ nav }}
<h1>Requirements Status</h1>
<p>{{ items|length }} items -- {% for k, v in counts.items() %}{{ v }} {{ k }} {% endfor %}</p>
<table>
<tr><th>UID</th><th>Folder</th><th>Requirement</th><th>Status</th></tr>
{% for it in items %}
<tr>
<td><a href="items/{{ it.uid }}.html">{{ it.uid }}</a></td>
<td>{{ it.folder }}</td>
<td>{{ it.header or (it.description[:70] ~ ('...' if it.description|length > 70 else '')) }}</td>
<td><span class="status status-{{ it.status_class }}"><span class="ball"></span>{{ it.status_label }}</span></td>
</tr>
{% endfor %}
</table>
"""

ITEM_TEMPLATE = """<!doctype html><meta charset="utf-8"><title>{{ uid }}</title><style>{{ css }}</style>
<a class="back" href="../index.html">&larr; all requirements</a>
<nav class="views"><a href="../index.html">List</a><a href="../tree.html">Tree</a></nav>
<div class="card">
<h1>{{ uid }} <span class="status status-{{ status_class }}"><span class="ball"></span>{{ status_label }}</span></h1>
<div class="field"><div class="label">Folder</div>{{ folder }}</div>
{% if header %}<div class="field"><div class="label">Header</div>{{ header }}</div>{% endif %}
<div class="field"><div class="label">Description</div><div>{{ description }}</div></div>
<div class="field"><div class="label">Reviewed / Deferred</div>{{ reviewed }} / {{ deferred }}</div>
<div class="field"><div class="label">Parents</div>
<ul class="links">{% for l in parents %}<li><a href="{{ l }}.html">{{ l }}</a></li>{% else %}<li>(none)</li>{% endfor %}</ul>
</div>
<div class="field"><div class="label">Linked from (children)</div>
<ul class="links">{% for c in children %}<li><a href="{{ c }}.html">{{ c }}</a></li>{% else %}<li>(none)</li>{% endfor %}</ul>
</div>
{% if tests %}<div class="field"><div class="label">Tests</div>
{% for t in tests %}<details class="testlog"><summary><span class="status status-{{ 'green' if t.passed else 'red' }}"><span class="ball"></span>{{ 'PASSED' if t.passed else 'FAILED' }}</span> {{ t.name }}</summary>
<pre>{{ t.log or '(no captured output)' }}</pre></details>
{% endfor %}</div>{% endif %}
</div>
"""

TREE_PAGE_TEMPLATE = """<!doctype html><meta charset="utf-8"><title>Requirements Tree</title><style>{{ css }}</style>
{{ nav }}
<h1>Requirements Tree</h1>
<p>Nodes reachable from more than one parent (a DAG merge point, not a tree edge) are rendered again at each
place they're referenced, marked <span class="tree-dup">(also under ...)</span>.</p>
{{ tree_html }}
"""


def _label(uid, entry):
    title = entry["header"] or (entry["description"][:60] + ("..." if len(entry["description"]) > 60 else ""))
    status = entry["status"]
    cls = STATUS_CLASS[status]
    return (
        f'<a href="items/{uid}.html">{uid}</a> '
        f'<span class="status status-{cls}"><span class="ball"></span>{STATUS_LABEL[status]}</span> {title}'
    )


def _render_tree(report, roots):
    seen_once = set()

    def render_node(uid, path):
        entry = report[uid]
        dup = uid in seen_once
        seen_once.add(uid)
        html = f"<li>{_label(uid, entry)}"
        if dup:
            html += ' <span class="tree-dup">(also under another parent)</span>'
        elif uid in path:
            html += ' <span class="tree-dup">(cycle)</span>'
        else:
            kids = entry["children"]
            if kids:
                html += '<ul class="tree">'
                for child in kids:
                    html += render_node(child, path | {uid})
                html += "</ul>"
        html += "</li>"
        return html

    out = '<ul class="tree">'
    for uid in roots:
        out += render_node(uid, frozenset())
    out += "</ul>"
    return out


def run(status_path, out_dir):
    report = json.loads(status_path.read_text())
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "items").mkdir(exist_ok=True)

    env = jinja2.Environment(autoescape=True)
    index_tpl = env.from_string(INDEX_TEMPLATE)
    item_tpl = env.from_string(ITEM_TEMPLATE)
    tree_tpl = env.from_string(TREE_PAGE_TEMPLATE)

    items = []
    counts = {}
    all_children = set()
    for entry in report.values():
        all_children.update(entry["children"])

    for uid, entry in sorted(report.items()):
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1
        items.append({
            "uid": uid,
            "folder": entry["folder"],
            "order": entry["order"],
            "header": entry["header"],
            "description": entry["description"],
            "status_class": STATUS_CLASS[entry["status"]],
            "status_label": STATUS_LABEL[entry["status"]],
        })
        (out_dir / "items" / f"{uid}.html").write_text(item_tpl.render(
            css=BASE_CSS,
            uid=uid,
            folder=entry["folder"],
            header=entry["header"],
            description=entry["description"],
            reviewed=entry["reviewed"],
            deferred=entry["deferred"],
            status_class=STATUS_CLASS[entry["status"]],
            status_label=STATUS_LABEL[entry["status"]],
            parents=entry["parents"],
            children=entry["children"],
            tests=entry.get("tests") or [],
        ))

    items.sort(key=lambda it: (it["folder"], it["order"], it["uid"]))
    (out_dir / "index.html").write_text(index_tpl.render(css=BASE_CSS, nav=NAV, items=items, counts=counts))

    roots = sorted(uid for uid in report if uid not in all_children)
    tree_html = Markup(_render_tree(report, roots))
    (out_dir / "tree.html").write_text(tree_tpl.render(css=BASE_CSS, nav=NAV, tree_html=tree_html))
    return out_dir


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--status", type=Path, default=ROOT / "req_status.json")
    parser.add_argument("--out", type=Path, default=ROOT / "site")
    args = parser.parse_args()
    run(args.status, args.out)
    print(f"rendered site to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
