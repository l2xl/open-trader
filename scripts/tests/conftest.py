# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Shared fixture helpers for building synthetic Doorstop trees under tmp_path."""

import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))


def make_document(base, folder, prefix, parent=None, digits=3, extensions=None):
    doc_dir = base / folder
    doc_dir.mkdir(parents=True, exist_ok=True)
    settings = {"prefix": prefix, "digits": digits, "sep": "-"}
    if parent:
        settings["parent"] = parent
    doc = {"settings": settings}
    if extensions:
        doc["extensions"] = extensions
    (doc_dir / ".doorstop.yml").write_text(yaml.safe_dump(doc))
    return doc_dir


def make_item(doc_dir, uid, level, text, normative=False, links=None, verify=None, references=None, header=""):
    data = {
        "active": True,
        "derived": False,
        "header": header,
        "level": level,
        "links": list(links or []),
        "normative": normative,
        "ref": "",
        "reviewed": None,
        "text": text,
    }
    if verify is not None:
        data["verify"] = verify
    if references is not None:
        data["references"] = references
    (doc_dir / f"{uid}.yml").write_text(yaml.safe_dump(data, sort_keys=True))


@pytest.fixture
def tree_builder(tmp_path):
    """Yields (base_path, make_document, make_item); call doorstop.build(root=base_path) once ready."""
    return tmp_path, make_document, make_item
