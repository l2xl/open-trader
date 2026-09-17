# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Local syngate editor UI: loopback HTTP server over syngatelib.

The app serves the tree model as JSON, writes per-field item edits and
drag-and-drop placements through the canonical writer, scaffolds and deletes
items, and drives review/clear through the CLI as a streamed subprocess -- all
gated by a per-session token and a loopback Host check."""

import http.client
import json
import sys
import threading
import urllib.error
import urllib.request

import pytest
import yaml

import syngate_ui
import syngatelib

# Stub CLI: echoes its argv tail and exits with the code named by an rc=N
# argument (charset-valid as a UID pattern), standing in for `syngate.py review/clear` runs.
STUB = [sys.executable, "-c",
        "import sys; print('stub:', *sys.argv[1:]); rc = [a for a in sys.argv if a.startswith('rc-')]; sys.exit(int(rc[0][3:]) if rc else 0)"]


@pytest.fixture
def app(tmp_path, syngate_tree):
    syngate_dir, make_item = syngate_tree
    make_item(syngate_dir, "ROOT", "Root branch\n", parents=())
    make_item(syngate_dir, "LEAF-001", "It shall leaf.\n", parents=("ROOT",), header="First leaf", tests=None)
    return syngate_ui.SyngateUIApp(root=tmp_path, syngate_dir=syngate_dir, cli_prefix=STUB)


@pytest.fixture
def server(app):
    httpd = syngate_ui.make_server(app, port=0)
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
        request.add_header("X-Syngate-Token", app.token if token is True else token)
    with urllib.request.urlopen(request) as response:
        body = response.read()
        return response.status, body if raw else json.loads(body)


@pytest.mark.syngate("INFRA-071", "tree")
def test_tree_endpoint_serves_statuses_and_stamp_freshness(server, syngate_tree):
    base, app = server
    syngate_dir, make_item = syngate_tree
    stale = syngatelib.Item(uid="STALE-001", path=syngate_dir / "STALE-001.yml", header="h", description="It shall drift.\n", parents=["ROOT"], tests={None: "b" * 64})
    make_item(syngate_dir, "STALE-001", "It shall drift.\n", parents=("ROOT",), header="h", tests="b" * 64, reviewed=syngatelib.compute_stamp(stale))
    (syngate_dir / "STALE-001.yml").write_text((syngate_dir / "STALE-001.yml").read_text().replace("It shall drift.", "It shall have drifted."))
    status, tree = call(base, app, "/api/tree")
    assert status == 200
    assert tree["roots"] == ["ROOT"]
    assert tree["items"]["ROOT"]["children"] == ["LEAF-001", "STALE-001"]
    leaf = tree["items"]["LEAF-001"]
    assert leaf["is_leaf"] and leaf["status"] == "not_implemented" and leaf["stamp_fresh"] is None
    assert tree["items"]["STALE-001"]["stamp_fresh"] is False
    assert any("re-review" in p for p in tree["items"]["STALE-001"]["problems"])


@pytest.mark.syngate("INFRA-071", "auth")
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
    conn.request("GET", "/api/tree", headers={"Host": "evil.example", "X-Syngate-Token": app.token})
    assert conn.getresponse().status == 403
    conn.close()
    status, _ = call(base, app, f"/api/tree?token={app.token}", token=False)
    assert status == 200  # EventSource cannot set headers; the query token is equivalent


@pytest.mark.syngate("INFRA-071", "edit")
def test_save_writes_canonical_yaml_and_keeps_stamp_stale(server, syngate_tree):
    base, app = server
    syngate_dir, _ = syngate_tree
    frozen = syngatelib.Item(uid="LEAF-002", path=syngate_dir / "LEAF-002.yml", header="Frozen", description="It shall freeze.\n", parents=["ROOT"], tests={None: "a" * 64})
    frozen.reviewed = syngatelib.compute_stamp(frozen)
    syngatelib.write_item(frozen)
    status, result = call(base, app, "/api/item/LEAF-002", {
        "header": "Frozen v2", "description": "It shall freeze harder.", "parents": ["ROOT"], "order": 30, "tests": [""],
    })
    assert status == 200 and result["ok"]
    on_disk = (syngate_dir / "LEAF-002.yml").read_text()
    expected = syngatelib.Item(uid="LEAF-002", path=frozen.path, header="Frozen v2", description="It shall freeze harder.\n",
                           parents=["ROOT"], order=30, tests={None: "a" * 64}, reviewed=frozen.reviewed)
    assert on_disk == syngatelib.dump_item(expected)  # canonical writer, sha and stamp preserved
    _, tree = call(base, app, "/api/tree")
    assert tree["items"]["LEAF-002"]["stamp_fresh"] is False


@pytest.mark.syngate("INFRA-071", "edit_guard")
def test_save_rejects_unknown_parent_and_cycle(server, syngate_tree):
    base, app = server
    syngate_dir, _ = syngate_tree
    before = (syngate_dir / "ROOT.yml").read_text()
    for parents in (["NOPE"], ["LEAF-001"]):  # unknown parent; child-of-own-child cycle
        with pytest.raises(urllib.error.HTTPError) as denied:
            call(base, app, "/api/item/ROOT", {"header": "", "description": "Root branch\n", "parents": parents, "order": 0, "tests": None})
        assert denied.value.code == 400
    assert (syngate_dir / "ROOT.yml").read_text() == before


@pytest.mark.syngate("INFRA-071", "autosave")
def test_partial_save_touches_only_named_fields_and_refuses_to_clobber_disk_edits(server, syngate_tree):
    base, app = server
    syngate_dir, _ = syngate_tree
    path = syngate_dir / "LEAF-001.yml"
    status, result = call(base, app, "/api/item/LEAF-001", {"header": "Typed", "base": {"header": "First leaf"}})
    assert status == 200 and result["stored"]["header"] == "Typed"
    on_disk = yaml.safe_load(path.read_text())
    assert on_disk == {"header": "Typed", "description": "It shall leaf.\n", "parents": ["ROOT"], "tests": None}
    path.write_text(path.read_text().replace("It shall leaf.", "It shall leaf, said the agent."))
    status, _ = call(base, app, "/api/item/LEAF-001", {"header": "Typed more", "base": {"header": "Typed"}})
    assert status == 200  # an on-disk edit of another field rebases silently
    assert yaml.safe_load(path.read_text())["description"] == "It shall leaf, said the agent.\n"
    with pytest.raises(urllib.error.HTTPError) as clash:
        call(base, app, "/api/item/LEAF-001", {"description": "It shall leaf, said the user.", "base": {"description": "It shall leaf.\n"}})
    assert clash.value.code == 409
    assert json.loads(clash.value.read())["current"] == {"description": "It shall leaf, said the agent.\n"}
    assert yaml.safe_load(path.read_text())["description"] == "It shall leaf, said the agent.\n"


def _orders(syngate_dir, *uids):
    return [yaml.safe_load((syngate_dir / f"{uid}.yml").read_text()).get("order", 0) for uid in uids]


@pytest.mark.syngate("INFRA-071", "reorder")
def test_move_takes_a_free_order_key_else_renumbers_the_family(server, syngate_tree):
    base, app = server
    syngate_dir, make_item = syngate_tree
    for uid, order in (("KID-A", 10), ("KID-B", 11), ("KID-C", 30)):
        make_item(syngate_dir, uid, "It shall sort.\n", parents=("ROOT",), order=order, tests=None)
    _, moved = call(base, app, "/api/move/LEAF-001", {"from": "ROOT", "to": "ROOT", "before": "KID-C"})
    assert moved["written"] == ["LEAF-001"] and _orders(syngate_dir, "LEAF-001") == [20]
    _, tree = call(base, app, "/api/tree")
    assert tree["items"]["ROOT"]["children"] == ["KID-A", "KID-B", "LEAF-001", "KID-C"]
    _, unmoved = call(base, app, "/api/move/LEAF-001", {"from": "ROOT", "to": "ROOT", "before": "KID-C"})
    assert unmoved["written"] == []
    _, moved = call(base, app, "/api/move/KID-C", {"from": "ROOT", "to": "ROOT", "before": "KID-B"})  # no integer between 10 and 11
    assert moved["written"] == ["KID-B", "KID-C", "LEAF-001"]
    assert _orders(syngate_dir, "KID-A", "KID-C", "KID-B", "LEAF-001") == [10, 20, 30, 40]


@pytest.mark.syngate("INFRA-071", "reparent")
def test_move_repoints_or_adds_the_parent_link_and_guards_the_dag(server, syngate_tree):
    base, app = server
    syngate_dir, make_item = syngate_tree
    make_item(syngate_dir, "BRANCH-A", "Groups.\n", parents=("ROOT",))
    make_item(syngate_dir, "BRANCH-B", "Groups too.\n", parents=("BRANCH-A",))
    call(base, app, "/api/move/LEAF-001", {"from": "ROOT", "to": "BRANCH-A", "before": "BRANCH-B"})
    _, tree = call(base, app, "/api/tree")
    assert tree["items"]["LEAF-001"]["parents"] == ["BRANCH-A"]
    assert tree["items"]["BRANCH-A"]["children"] == ["LEAF-001", "BRANCH-B"]
    call(base, app, "/api/move/LEAF-001", {"from": "BRANCH-A", "to": "BRANCH-B", "before": None, "link": True})
    _, tree = call(base, app, "/api/tree")
    assert tree["items"]["LEAF-001"]["parents"] == ["BRANCH-A", "BRANCH-B"]
    before = (syngate_dir / "BRANCH-A.yml").read_text()
    for target in ("BRANCH-B", "LEAF-001"):  # under its own descendant; under a test-bearing leaf
        with pytest.raises(urllib.error.HTTPError) as denied:
            call(base, app, "/api/move/BRANCH-A", {"from": "ROOT", "to": target, "before": None})
        assert denied.value.code == 400
    assert (syngate_dir / "BRANCH-A.yml").read_text() == before


@pytest.mark.syngate("INFRA-071", "scaffold")
def test_new_and_delete_manage_item_files(server, syngate_tree):
    base, app = server
    syngate_dir, _ = syngate_tree
    status, _ = call(base, app, "/api/new", {"uid": "LEAF-003", "parents": ["ROOT"], "dir": "syngate/sub", "kind": "leaf"})
    assert status == 200
    created = yaml.safe_load((syngate_dir / "sub" / "LEAF-003.yml").read_text())
    assert created["parents"] == ["ROOT"] and created["tests"] is None
    with pytest.raises(urllib.error.HTTPError) as duplicate:
        call(base, app, "/api/new", {"uid": "LEAF-003", "parents": ["ROOT"]})
    assert duplicate.value.code == 400
    with pytest.raises(urllib.error.HTTPError) as populated:
        call(base, app, "/api/delete/ROOT", {})
    assert populated.value.code == 400  # has children
    status, _ = call(base, app, "/api/delete/LEAF-003", {})
    assert status == 200 and not (syngate_dir / "sub" / "LEAF-003.yml").exists()


@pytest.mark.syngate("INFRA-071", "review_stream")
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


@pytest.mark.syngate("INFRA-071", "single_flight")
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
