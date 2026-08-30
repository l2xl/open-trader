# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Local requirements editor UI: loopback HTTP server over reqlib.

The app serves the tree model as JSON, writes item edits through the canonical
writer, scaffolds and deletes items, and drives review/clear through the CLI
as a streamed subprocess -- all gated by a per-session token and a loopback
Host check."""

import http.client
import json
import sys
import threading
import urllib.error
import urllib.request

import pytest
import yaml

import req_ui
import reqlib

# Stub CLI: echoes its argv tail and exits with the code named by an rc=N
# argument (charset-valid as a UID pattern), standing in for `req.py review/clear` runs.
STUB = [sys.executable, "-c",
        "import sys; print('stub:', *sys.argv[1:]); rc = [a for a in sys.argv if a.startswith('rc-')]; sys.exit(int(rc[0][3:]) if rc else 0)"]


@pytest.fixture
def app(tmp_path, req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT", "Root branch\n", parents=())
    make_item(req_dir, "LEAF-001", "It shall leaf.\n", parents=("ROOT",), header="First leaf", tests=None)
    return req_ui.ReqUIApp(root=tmp_path, req_dir=req_dir, cli_prefix=STUB)


@pytest.fixture
def server(app):
    httpd = req_ui.make_server(app, port=0)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    yield f"http://127.0.0.1:{httpd.server_address[1]}", app
    httpd.shutdown()
    thread.join()


def call(base, app, path, payload=None, token=True, raw=False):
    request = urllib.request.Request(
        base + path,
        data=json.dumps(payload).encode() if payload is not None else None,
        method="POST" if payload is not None else "GET",
    )
    if token:
        request.add_header("X-Req-Token", app.token if token is True else token)
    with urllib.request.urlopen(request) as response:
        body = response.read()
        return response.status, body if raw else json.loads(body)


@pytest.mark.req("INFRA-071", "tree")
def test_tree_endpoint_serves_statuses_and_stamp_freshness(server, req_tree):
    base, app = server
    req_dir, make_item = req_tree
    stale = reqlib.Item(uid="STALE-001", path=req_dir / "STALE-001.yml", header="h", description="It shall drift.\n", parents=["ROOT"], tests={None: "b" * 64})
    make_item(req_dir, "STALE-001", "It shall drift.\n", parents=("ROOT",), header="h", tests="b" * 64, reviewed=reqlib.compute_stamp(stale))
    (req_dir / "STALE-001.yml").write_text((req_dir / "STALE-001.yml").read_text().replace("It shall drift.", "It shall have drifted."))
    status, tree = call(base, app, "/api/tree")
    assert status == 200
    assert tree["roots"] == ["ROOT"]
    assert tree["items"]["ROOT"]["children"] == ["LEAF-001", "STALE-001"]
    leaf = tree["items"]["LEAF-001"]
    assert leaf["is_leaf"] and leaf["status"] == "not_implemented" and leaf["stamp_fresh"] is None
    assert tree["items"]["STALE-001"]["stamp_fresh"] is False
    assert any("re-review" in p for p in tree["items"]["STALE-001"]["problems"])


@pytest.mark.req("INFRA-071", "auth")
def test_requests_without_token_or_loopback_host_are_refused(server):
    base, app = server
    with pytest.raises(urllib.error.HTTPError) as missing:
        call(base, app, "/api/tree", token=False)
    assert missing.value.code == 401
    with pytest.raises(urllib.error.HTTPError) as forged:
        call(base, app, "/api/tree", token="forged")
    assert forged.value.code == 401
    port = int(base.rsplit(":", 1)[1])
    conn = http.client.HTTPConnection("127.0.0.1", port)  # DNS-rebinding defense: loopback Host names only
    conn.request("GET", "/api/tree", headers={"Host": "evil.example", "X-Req-Token": app.token})
    assert conn.getresponse().status == 403
    conn.close()
    status, _ = call(base, app, f"/api/tree?token={app.token}", token=False)
    assert status == 200  # EventSource cannot set headers; the query token is equivalent


@pytest.mark.req("INFRA-071", "edit")
def test_save_writes_canonical_yaml_and_keeps_stamp_stale(server, req_tree):
    base, app = server
    req_dir, _ = req_tree
    frozen = reqlib.Item(uid="LEAF-002", path=req_dir / "LEAF-002.yml", header="Frozen", description="It shall freeze.\n", parents=["ROOT"], tests={None: "a" * 64})
    frozen.reviewed = reqlib.compute_stamp(frozen)
    reqlib.write_item(frozen)
    status, result = call(base, app, "/api/item/LEAF-002", {
        "header": "Frozen v2", "description": "It shall freeze harder.", "parents": ["ROOT"], "order": 30, "tests": [""],
    })
    assert status == 200 and result["ok"]
    on_disk = (req_dir / "LEAF-002.yml").read_text()
    expected = reqlib.Item(uid="LEAF-002", path=frozen.path, header="Frozen v2", description="It shall freeze harder.\n",
                           parents=["ROOT"], order=30, tests={None: "a" * 64}, reviewed=frozen.reviewed)
    assert on_disk == reqlib.dump_item(expected)  # canonical writer, sha and stamp preserved
    _, tree = call(base, app, "/api/tree")
    assert tree["items"]["LEAF-002"]["stamp_fresh"] is False


@pytest.mark.req("INFRA-071", "edit_guard")
def test_save_rejects_unknown_parent_and_cycle(server, req_tree):
    base, app = server
    req_dir, _ = req_tree
    before = (req_dir / "ROOT.yml").read_text()
    for parents in (["NOPE"], ["LEAF-001"]):  # unknown parent; child-of-own-child cycle
        with pytest.raises(urllib.error.HTTPError) as denied:
            call(base, app, "/api/item/ROOT", {"header": "", "description": "Root branch\n", "parents": parents, "order": 0, "tests": None})
        assert denied.value.code == 400
    assert (req_dir / "ROOT.yml").read_text() == before


@pytest.mark.req("INFRA-071", "scaffold")
def test_new_and_delete_manage_item_files(server, req_tree):
    base, app = server
    req_dir, _ = req_tree
    status, _ = call(base, app, "/api/new", {"uid": "LEAF-003", "parents": ["ROOT"], "dir": "req/sub", "kind": "leaf"})
    assert status == 200
    created = yaml.safe_load((req_dir / "sub" / "LEAF-003.yml").read_text())
    assert created["parents"] == ["ROOT"] and created["tests"] is None
    with pytest.raises(urllib.error.HTTPError) as duplicate:
        call(base, app, "/api/new", {"uid": "LEAF-003", "parents": ["ROOT"]})
    assert duplicate.value.code == 400
    with pytest.raises(urllib.error.HTTPError) as populated:
        call(base, app, "/api/delete/ROOT", {})
    assert populated.value.code == 400  # has children
    status, _ = call(base, app, "/api/delete/LEAF-003", {})
    assert status == 200 and not (req_dir / "sub" / "LEAF-003.yml").exists()


@pytest.mark.req("INFRA-071", "review_stream")
def test_review_runs_cli_and_streams_output_until_exit(server):
    base, app = server
    status, job = call(base, app, "/api/run", {"action": "review", "uids": ["rc-3", "LEAF-001"]})
    assert status == 200
    status, body = call(base, app, f"/api/job/{job['job']}/events?token={app.token}", token=False, raw=True)
    text = body.decode().replace("\r", "")
    assert "stub: review rc-3 LEAF-001" in text
    assert "event: done\ndata: 3\n" in text
    status, snapshot = call(base, app, f"/api/job/{job['job']}")
    assert snapshot["returncode"] == 3 and snapshot["running"] is False
    assert "--build-dir" in snapshot["argv"]  # review forwards the build dir


@pytest.mark.req("INFRA-071", "single_flight")
def test_concurrent_runs_are_refused_while_busy(server):
    base, app = server
    app.cli_prefix = [sys.executable, "-c", "import sys, time; print('held'); sys.stdout.flush(); time.sleep(30)"]
    _, job = call(base, app, "/api/run", {"action": "clear", "uids": ["LEAF-001"]})
    try:
        with pytest.raises(urllib.error.HTTPError) as busy:
            call(base, app, "/api/run", {"action": "clear", "uids": ["LEAF-001"]})
        assert busy.value.code == 409
    finally:
        app.jobs.cancel()
    status, snapshot = call(base, app, f"/api/job/{job['job']}")
    assert snapshot["running"] is False
