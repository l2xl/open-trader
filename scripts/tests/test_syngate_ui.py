# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Local syngate editor UI: loopback HTTP server over syngatelib.

The app serves the tree model as JSON, writes per-field item edits and
drag-and-drop placements through the canonical writer, scaffolds and deletes
items, and drives review/clear through the CLI as a streamed subprocess -- all
gated by a per-session token and a loopback Host check."""

import fcntl
import http.client
import json
import os
import select
import shutil
import subprocess
import sys
import threading
import time
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


class Cdp:
    """Headless Chrome over --remote-debugging-pipe: NUL-delimited JSON on fds 3 (in) / 4 (out)."""

    def __init__(self, binary, profile):
        to_chrome, from_chrome = os.pipe(), os.pipe()

        def wire():
            source, sink = fcntl.fcntl(to_chrome[0], fcntl.F_DUPFD, 10), fcntl.fcntl(from_chrome[1], fcntl.F_DUPFD, 10)
            os.dup2(source, 3)
            os.dup2(sink, 4)

        argv = [binary, "--headless=new", "--remote-debugging-pipe", f"--user-data-dir={profile}", "--no-first-run", "--disable-gpu"]
        if os.geteuid() == 0:
            argv.append("--no-sandbox")
        self.process = subprocess.Popen(argv, preexec_fn=wire, pass_fds=(3, 4), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(to_chrome[0])
        os.close(from_chrome[1])
        self.sink, self.source, self.buffer, self.serial = to_chrome[1], from_chrome[0], b"", 0

    def call(self, method, params=None, session=None):
        self.serial += 1
        message = {"id": self.serial, "method": method, "params": params or {}}
        if session:
            message["sessionId"] = session
        os.write(self.sink, json.dumps(message).encode() + b"\0")
        while True:
            while b"\0" not in self.buffer:
                if not select.select([self.source], [], [], 20)[0]:
                    raise TimeoutError(f"no CDP reply to {method}")
                self.buffer += os.read(self.source, 1 << 16)
            raw, self.buffer = self.buffer.split(b"\0", 1)
            reply = json.loads(raw)
            if reply.get("id") == self.serial:
                if "error" in reply:
                    raise RuntimeError(reply["error"])
                return reply["result"]

    def close(self):
        self.process.kill()
        self.process.wait()
        os.close(self.sink)
        os.close(self.source)


class Page:
    def __init__(self, cdp, url):
        self.cdp = cdp
        target = cdp.call("Target.createTarget", {"url": url})["targetId"]
        self.session = cdp.call("Target.attachToTarget", {"targetId": target, "flatten": True})["sessionId"]

    def eval(self, script):
        result = self.cdp.call("Runtime.evaluate", {"expression": script, "awaitPromise": True, "returnByValue": True}, self.session)
        assert "exceptionDetails" not in result, result["exceptionDetails"]
        return result["result"].get("value")

    def mouse(self, kind, x, y, clicks=1):
        self.cdp.call("Input.dispatchMouseEvent", {"type": kind, "x": x, "y": y, "button": "left", "buttons": 1, "clickCount": clicks}, self.session)

    def wait(self, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while not self.eval(f"Boolean({predicate})"):
            assert time.monotonic() < deadline, f"page never reached: {predicate}"
            time.sleep(0.05)


@pytest.fixture(scope="session")
def chrome(tmp_path_factory):
    binary = next(filter(None, map(shutil.which, ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser"))), None)
    if binary is None:
        pytest.skip("no headless Chrome")
    cdp = Cdp(binary, tmp_path_factory.mktemp("chrome-profile"))
    yield cdp
    cdp.close()


@pytest.fixture
def page(server, chrome):
    base, app = server
    opened = Page(chrome, f"{base}/?token={app.token}")
    opened.wait("document.querySelector('#tree [data-path]')")
    return opened


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


@pytest.mark.syngate("SYNGATE_UI-031", "model")
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


@pytest.mark.syngate("SYNGATE_UI-010")
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


@pytest.mark.syngate("SYNGATE_UI-039")
def test_edit_of_a_stamped_field_clears_the_review(server, syngate_tree):
    base, app = server
    syngate_dir, make_item = syngate_tree
    frozen = syngatelib.Item(uid="LEAF-002", path=syngate_dir / "LEAF-002.yml", header="Frozen", description="It shall freeze.\n", parents=["ROOT"], tests={None: "a" * 64})
    frozen.reviewed = syngatelib.compute_stamp(frozen)
    syngatelib.write_item(frozen)
    status, result = call(base, app, "/api/item/LEAF-002", {"order": 30})
    assert status == 200 and result["stamp_fresh"] is True  # order sits outside the stamp
    status, result = call(base, app, "/api/item/LEAF-002", {"header": "Frozen v2", "description": "It shall freeze harder."})
    assert status == 200 and result["stamp_fresh"] is None
    expected = syngatelib.Item(uid="LEAF-002", path=frozen.path, header="Frozen v2", description="It shall freeze harder.\n",
                           parents=["ROOT"], order=30, tests={None: None})
    assert (syngate_dir / "LEAF-002.yml").read_text() == syngatelib.dump_item(expected)  # canonical writer, stamp and sha dropped
    make_item(syngate_dir, "BRANCH-A", "Groups.\n", parents=("ROOT",))
    syngatelib.write_item(frozen)
    call(base, app, "/api/move/LEAF-002", {"from": "ROOT", "to": "BRANCH-A", "before": None})
    moved = yaml.safe_load((syngate_dir / "LEAF-002.yml").read_text())
    assert moved["parents"] == ["BRANCH-A"] and "reviewed" not in moved and moved["tests"] is None


@pytest.mark.syngate("SYNGATE_UI-032", "guard")
def test_save_rejects_unknown_parent_and_cycle(server, syngate_tree):
    base, app = server
    syngate_dir, _ = syngate_tree
    before = (syngate_dir / "ROOT.yml").read_text()
    for parents in (["NOPE"], ["LEAF-001"]):  # unknown parent; child-of-own-child cycle
        with pytest.raises(urllib.error.HTTPError) as denied:
            call(base, app, "/api/item/ROOT", {"header": "", "description": "Root branch\n", "parents": parents, "order": 0, "tests": None})
        assert denied.value.code == 400
    assert (syngate_dir / "ROOT.yml").read_text() == before


@pytest.mark.syngate("SYNGATE_UI-032", "cas")
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


@pytest.mark.syngate("SYNGATE_UI-033", "reorder")
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


@pytest.mark.syngate("SYNGATE_UI-033", "reparent")
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


@pytest.mark.syngate("SYNGATE_UI-034")
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


@pytest.mark.syngate("SYNGATE_UI-035", "stream")
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


@pytest.mark.syngate("SYNGATE_UI-035", "single_flight")
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


@pytest.mark.syngate("SYNGATE_UI-035", "branch")
def test_a_branch_run_addresses_every_leaf_below_it(server, syngate_tree):
    base, app = server
    syngate_dir, make_item = syngate_tree
    make_item(syngate_dir, "BRANCH-A", "Groups.\n", parents=("ROOT",))
    make_item(syngate_dir, "BRANCH-B", "Groups nothing yet.\n", parents=("ROOT",))
    make_item(syngate_dir, "LEAF-002", "It shall nest.\n", parents=("BRANCH-A",), tests=None)
    _, job = call(base, app, "/api/run", {"action": "test", "uids": ["ROOT"]})
    _, body = call(base, app, f"/api/job/{job['job']}/events?token={app.token}", token=False, raw=True)
    assert f"stub: test LEAF-002 LEAF-001 --build-dir {app.build_dir} --coverage-out {app.run_coverage}" in body.decode()
    with pytest.raises(urllib.error.HTTPError) as barren:
        call(base, app, "/api/run", {"action": "review", "uids": ["BRANCH-B"]})
    assert barren.value.code == 400


@pytest.mark.syngate("SYNGATE_UI-036", "read")
def test_linked_files_are_served_only_from_inside_the_project(server, tmp_path_factory):
    base, app = server
    (app.root / "docs").mkdir()
    (app.root / "docs" / "GUIDE.md").write_text("# Guide\n")
    (app.root / "key.txt").write_text("secret\n")
    outside = tmp_path_factory.mktemp("outside") / "LEAK.md"
    outside.write_text("# Leak\n")
    (app.root / "docs" / "LINK.md").symlink_to(outside)
    status, served = call(base, app, "/api/file?path=docs/GUIDE.md")
    assert status == 200 and served["text"] == "# Guide\n" and served["writable"] is True
    _, state = call(base, app, "/api/fingerprint?file=docs/GUIDE.md&file=docs/GONE.md")
    assert state["files"] == {"docs/GUIDE.md": served["sha"], "docs/GONE.md": None}
    for path, code in (("key.txt", 403), ("../GUIDE.md", 400), ("docs/../key.txt", 400), ("docs/LINK.md", 404), ("docs/GONE.md", 404)):  # untracked non-markdown; traversal; symlink escape; missing
        with pytest.raises(urllib.error.HTTPError) as denied:
            call(base, app, f"/api/file?path={path}")
        assert denied.value.code == code, path


@pytest.mark.syngate("SYNGATE_UI-036", "save")
def test_file_save_is_a_compare_and_swap_limited_to_markdown(server):
    base, app = server
    guide = app.root / "GUIDE.md"
    guide.write_text("# Guide\n")
    (app.root / "notes.txt").write_text("plain\n")
    status, saved = call(base, app, "/api/file", {"path": "GUIDE.md", "text": "# Guide\n\nTyped.\n", "base": "# Guide\n"})
    assert status == 200 and saved["stored"] == {"file": "# Guide\n\nTyped.\n"} and guide.read_text() == "# Guide\n\nTyped.\n"
    guide.write_text("# Guide\n\nSaid the agent.\n")
    with pytest.raises(urllib.error.HTTPError) as clash:
        call(base, app, "/api/file", {"path": "GUIDE.md", "text": "# Guide\n\nTyped more.\n", "base": "# Guide\n\nTyped.\n"})
    assert clash.value.code == 409 and json.loads(clash.value.read())["current"] == {"file": "# Guide\n\nSaid the agent.\n"}
    assert guide.read_text() == "# Guide\n\nSaid the agent.\n"
    with pytest.raises(urllib.error.HTTPError) as foreign:
        call(base, app, "/api/file", {"path": "notes.txt", "text": "edited\n", "base": "plain\n"})
    assert foreign.value.code == 403 and (app.root / "notes.txt").read_text() == "plain\n"


@pytest.mark.syngate("SYNGATE_UI-020", "csp")
def test_the_page_runs_only_its_own_nonced_script(server):
    base, app = server
    request = urllib.request.Request(base + "/", headers={"X-Syngate-Token": app.token})
    with urllib.request.urlopen(request) as response:
        policy, page = response.headers["Content-Security-Policy"], response.read().decode()
    nonce = policy.split("script-src 'nonce-")[1].split("'")[0]
    assert "default-src 'none'" in policy and f'<script nonce="{nonce}">' in page and "<script>" not in page


PATHS = "[...document.querySelectorAll('%s [data-path]')].map((el) => el.dataset.path)"


@pytest.mark.syngate("SYNGATE_UI-020", "render")
def test_the_page_renders_every_tree_item_from_the_served_model(page):
    assert page.eval(PATHS % "#tree") == ["ROOT", "ROOT/LEAF-001"]
    assert page.eval(PATHS % "#doc") == ["ROOT", "ROOT/LEAF-001"]


@pytest.mark.syngate("SYNGATE_UI-031", "rows")
def test_a_row_carries_uid_editable_header_and_the_review_badge(page, syngate_tree):
    syngate_dir, _ = syngate_tree
    frozen = syngatelib.Item(uid="LEAF-002", path=syngate_dir / "LEAF-002.yml", header="Frozen", description="It shall freeze.\n", parents=["ROOT"], tests={None: "a" * 64})
    frozen.reviewed = syngatelib.compute_stamp(frozen)
    syngatelib.write_item(frozen)
    page.wait("document.querySelector('#tree [data-uid=\"LEAF-002\"]')")  # picked up by the fingerprint poll
    row = "(() => { const row = document.querySelector('#tree [data-uid=\"%s\"]'), title = row.querySelector('.title');" \
          " return [row.querySelector('.node-uid').textContent, title.value, title.readOnly, [...row.querySelectorAll('.badge')].map((el) => el.textContent)]; })()"
    assert page.eval(row % "LEAF-001") == ["LEAF-001", "First leaf", False, []]
    assert page.eval(row % "LEAF-002") == ["LEAF-002", "Frozen", False, ["✗"]]  # reviewed, but its stamped routine is gone
    page.eval("(() => { select('LEAF-002', 'ROOT/LEAF-002'); startEditing('ROOT/LEAF-002'); })()")
    assert page.eval("(() => { const block = document.querySelector('#doc .block.editing'); return [block.querySelector('textarea').readOnly, Boolean(block.querySelector('.block-problems'))]; })()") == [False, False]


@pytest.fixture
def violated_page(page, syngate_tree):
    """The page with LEAF-002 selected: reviewed, but its stamped routine is gone -- test unknown, review violated."""
    syngate_dir, _ = syngate_tree
    frozen = syngatelib.Item(uid="LEAF-002", path=syngate_dir / "LEAF-002.yml", header="Frozen", description="It shall freeze.\n", parents=["ROOT"], tests={None: "a" * 64})
    frozen.reviewed = syngatelib.compute_stamp(frozen)
    syngatelib.write_item(frozen)
    page.wait("document.querySelector('#tree [data-uid=\"LEAF-002\"]')")
    page.eval("select('LEAF-002', 'ROOT/LEAF-002')")
    return page


LABEL = "(() => { const el = document.querySelector('%s'), dot = el.querySelector('.dot');" \
        " return [dot.className, dot.nextElementSibling.textContent, [...el.querySelectorAll('.badge')].map((b) => [b.className, b.textContent])]; })()"


@pytest.mark.syngate("SYNGATE_UI-041")
def test_row_and_panel_label_an_item_by_status_dot_id_and_review_mark(violated_page, server, syngate_tree):
    page, (_, app), (syngate_dir, _) = violated_page, server, syngate_tree
    routines = app.root / "scripts" / "tests"
    routines.mkdir(parents=True)
    (routines / "test_leaf.py").write_text('import pytest\n\n\n@pytest.mark.syngate("LEAF-003")\ndef test_leaf():\n    pass\n')
    sha = syngatelib.routine_sha(syngatelib.discover_bindings(app.root)[("LEAF-003", None)][0])
    approved = syngatelib.Item(uid="LEAF-003", path=syngate_dir / "LEAF-003.yml", header="Approved", description="It shall pass.\n", parents=["ROOT"], tests={None: sha})
    approved.reviewed = syngatelib.compute_stamp(approved)
    syngatelib.write_item(approved)
    for uid in ("LEAF-001", "LEAF-003"):
        with open(app.run_coverage, "a") as coverage:
            coverage.write(json.dumps({"tags": [uid], "passed": True, "name": "", "log": ""}) + "\n")
    page.wait("document.querySelector('#tree [data-uid=\"LEAF-003\"] .dot.green')")
    for place in ("#tree [data-uid=\"%s\"]", "#doc .block[data-uid=\"%s\"] legend"):
        assert page.eval(LABEL % (place % "LEAF-002")) == ["dot red", "LEAF-002", [["badge problem", "✗"]]]  # review violated
        assert page.eval(LABEL % (place % "LEAF-001")) == ["dot gray", "LEAF-001", []]  # passed but not reviewed: no mark, not green
        assert page.eval(LABEL % (place % "LEAF-003")) == ["dot green", "LEAF-003", [["badge fresh", "✓"]]]  # passed and reviewed
    assert page.eval("document.querySelector('#tree [data-uid=\"LEAF-001\"] .dot').title") == "test passed · not reviewed"


@pytest.mark.syngate("SYNGATE_UI-042")
def test_a_tree_row_shows_collapse_mark_label_title_and_menu_only(violated_page):
    page = violated_page
    parts = "[...document.querySelector('#tree [data-uid=\"LEAF-002\"]').querySelectorAll('.twist, .dot, .node-uid, .badge, .dupmark, .title, .tools button:not([hidden])')]" \
            ".map((el) => el.dataset.act || el.className.split(' ')[0])"
    assert page.eval(parts) == ["twist", "dot", "node-uid", "badge", "title", "tests", "menu"]
    assert page.eval("Boolean(document.querySelector('#tree .row-problems'))") is False
    page.eval("document.querySelector('#tree [data-uid=\"LEAF-002\"] [data-act=\"menu\"]').click()")
    entries = "[document.getElementById('status-menu').matches(':popover-open'), [...document.querySelectorAll('#status-menu [data-row-act]')].map((el) => el.dataset.rowAct)]"
    assert page.eval(entries) == [True, ["add", "delete"]]  # structure edits only behind the explicit menu
    page.eval("document.querySelector('#tree [data-uid=\"ROOT\"] [data-act=\"menu\"]').click()")
    assert page.eval(entries) == [True, ["add"]]  # an item with children cannot be deleted
    page.eval("document.querySelector('#status-menu [data-row-act=\"add\"]').click()")
    assert page.eval("Boolean(document.querySelector('#tree .row.adding .new-uid'))")


@pytest.mark.syngate("SYNGATE_UI-043")
def test_a_content_panel_shows_label_status_button_with_popup_and_ai_chat_button(violated_page):
    page = violated_page
    label = "(() => { const tag = document.querySelector('#doc .block[data-uid=\"LEAF-002\"] legend .tag'); return [tag.querySelector('.caption').textContent, tag.querySelectorAll('button').length]; })()"
    assert page.eval(label) == ["Frozen", 0]
    foot = "(() => { const foot = document.querySelector('#doc .block[data-uid=\"LEAF-002\"] .block-foot'), box = foot.getBoundingClientRect()," \
           " ai = foot.querySelector('.block-ai .chat-open').getBoundingClientRect(), pill = foot.querySelector('.block-status .pill').getBoundingClientRect();" \
           " return [ai.left - box.left < 2, box.right - pill.right < 2]; })()"
    assert page.eval(foot) == [True, True]
    tip = "[...document.querySelectorAll('#doc .block[data-uid=\"LEAF-002\"] .block-status .tip > div')].map((el) => [el.className, el.textContent])"
    lines = page.eval(tip)
    assert lines[0] == ["tip-unknown", "unknown"] and lines[1][0] == "tip-unknown" and "no tagged routine yet" in lines[1][1]
    assert lines[-1][0] == "tip-review_violated" and "no test routine tagged [LEAF-002] found" in lines[-1][1]
    assert page.eval("getComputedStyle(document.querySelector('#doc .block[data-uid=\"LEAF-002\"] .tip')).display") == "none"  # shown on hover only


class EchoConnector:
    models, efforts, modes = ("model-a", "model-b"), ("low", "high"), {"edit": True, "internet": True, "workflows": False}

    def __init__(self, label="Echo"):
        self.label = label

    def settings(self):
        return {"label": self.label, "models": [{"id": model, "name": model.removeprefix("model-")} for model in self.models], "efforts": list(self.efforts),
                "modes": [{"id": mode, "label": mode.capitalize(), "hint": mode + " hint", "default": default} for mode, default in self.modes.items()]}

    def dispatch(self, context, text, model=None, effort=None, modes=(), session=None):
        return f"{context}|{text}|{model}|{effort}|{','.join(modes)}|{session}", "session-1"


SETUP = "document.querySelector('.chat-setup').textContent"


def open_chat(server, chrome, **connectors):
    base, app = server
    app.connectors = connectors or {"echo": EchoConnector()}
    page = Page(chrome, f"{base}/?token={app.token}")
    page.wait("document.querySelector('#tree [data-uid=\"LEAF-001\"]')")
    page.wait("document.querySelector('#doc .block[data-uid=\"LEAF-001\"] .chat-open')")
    page.eval("document.querySelector('#doc .block[data-uid=\"LEAF-001\"] .chat-open').click()")
    return page


def pick(page, attribute, value):
    page.eval("document.querySelector('.chat-setup').click()")
    assert page.eval("document.getElementById('status-menu').matches(':popover-open')")
    page.eval(f"document.querySelector('#status-menu [data-chat-{attribute}=\"{value}\"]').click()")


@pytest.mark.syngate("AI_CHAT-010")
def test_the_connector_menu_selects_the_setup(server, chrome):
    page = open_chat(server, chrome, echo=EchoConnector(), other=EchoConnector("Other"))
    pick(page, "model", "model-b")
    assert page.eval(SETUP) == "Echo · b · low ▾"
    pick(page, "connector", "other")
    assert page.eval(SETUP) == "Other · a · low ▾"  # a new connector starts from its own defaults


def css_color(page, var):
    return page.eval(f"(() => {{ const probe = document.createElement('span'); probe.style.color = 'var({var})'; document.body.append(probe);"
                     " const color = getComputedStyle(probe).color; probe.remove(); return color; })()")


@pytest.mark.syngate("AI_CHAT-011")
def test_the_permission_switches_show_their_labels_defaults_and_state_colors(server, chrome):
    page = open_chat(server, chrome)
    modes = "[...document.querySelectorAll('.block-ai .chat-mode')].map((el) => [el.textContent, el.getAttribute('aria-checked')])"
    assert page.eval(modes) == [["Edit", "true"], ["Internet", "true"], ["Workflows", "false"]]
    look = "[...document.querySelectorAll('.block-ai .chat-mode')].map((el) => [el.className, getComputedStyle(el).color, getComputedStyle(el.querySelector('.ball')).backgroundColor])"
    green, gray = css_color(page, "--green"), css_color(page, "--gray")
    assert [entry[1:] for entry in page.eval(look)] == [[green, green], [green, green], [gray, gray]]
    assert all(entry[0].startswith("pill chat-mode st-") for entry in page.eval(look))  # the status label's own pill and tint classes
    page.eval("document.querySelector('.chat-mode[data-chat-mode=\"edit\"]').click()")
    page.eval("document.querySelector('.chat-mode[data-chat-mode=\"workflows\"]').click()")
    assert page.eval(modes) == [["Edit", "false"], ["Internet", "true"], ["Workflows", "true"]]
    page.eval("(() => { document.querySelector('.chat-input').value = 'Go.'; document.querySelector('.chat-send').click(); })()")
    page.wait("document.querySelector('.chat-msg.ai')")
    assert page.eval("document.querySelector('.chat-msg.ai').textContent").endswith("|Go.|model-a|low|internet,workflows|None")


@pytest.mark.syngate("AI_CHAT-020")
def test_the_input_field_takes_the_text_of_the_exchange(server, chrome):
    page = open_chat(server, chrome)
    assert page.eval("(() => { const input = document.querySelector('.block-chat textarea.chat-input'); return [document.activeElement === input, input.value]; })()") == [True, ""]
    page.eval("(() => { const input = document.querySelector('.chat-input'); input.value = 'Typed by the user';"
              " input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', ctrlKey: true, bubbles: true })); })()")
    page.wait("document.querySelector('.chat-msg.ai')")
    assert page.eval("[document.querySelector('.chat-msg.user').textContent, document.querySelector('.chat-input').value]") == ["Typed by the user", ""]
    page.eval("document.querySelector('.chat-send').click()")  # an empty field sends nothing
    assert page.eval("document.querySelectorAll('.chat-msg').length") == 2


@pytest.mark.syngate("AI_CHAT-021")
def test_the_history_shows_above_the_input_field_once_there_is_any(server, chrome):
    page = open_chat(server, chrome)
    log = "document.querySelector('.block-chat .chat-log')"
    assert page.eval(f"[{log}.children.length, getComputedStyle({log}).display]") == [0, "none"]
    page.eval("(() => { document.querySelector('.chat-input').value = 'First'; document.querySelector('.chat-send').click(); })()")
    page.wait("document.querySelector('.chat-msg.ai')")
    assert page.eval(f"getComputedStyle({log}).display") != "none"
    assert page.eval(f"{log}.getBoundingClientRect().bottom <= document.querySelector('.chat-input').getBoundingClientRect().top")
    assert page.eval(f"[...{log}.querySelectorAll('.chat-msg')].map((el) => el.classList.contains('user') ? 'user' : 'ai')") == ["user", "ai"]


@pytest.mark.syngate("AI_CHAT-030")
def test_the_green_check_mark_closes_the_window(server, chrome):
    page = open_chat(server, chrome)
    closer = "document.querySelector('.block-ai .chat-close')"
    assert page.eval(f"[{closer}.textContent, getComputedStyle({closer}).color]") == ["✓", css_color(page, "--green")]
    page.eval(f"{closer}.click()")
    assert page.eval("[Boolean(document.querySelector('.chat-input')), Boolean(document.querySelector('.block[data-uid=\"LEAF-001\"] .chat-open'))]") == [False, True]


@pytest.mark.syngate("AI_CHAT")
def test_the_chat_window_opens_under_the_item_and_sends_through_the_chosen_setup(server, chrome):
    page = open_chat(server, chrome)
    assert page.eval(PATHS % "#doc") == ["ROOT", "ROOT/LEAF-001"]  # no separate element: the item's own block expands
    assert page.eval("Boolean(document.querySelector('.block[data-uid=\"LEAF-001\"] .block-chat .chat-input'))")
    assert page.eval(SETUP) == "Echo · a · low ▾"
    pick(page, "model", "model-b")
    pick(page, "effort", "high")
    assert page.eval(SETUP) == "Echo · b · high ▾"
    page.eval("(() => { document.querySelector('.chat-input').value = 'What is missing?'; document.querySelector('.chat-send').click(); })()")
    page.wait("document.querySelector('.chat-msg.ai')")
    assert page.eval("[...document.querySelectorAll('.chat-msg')].map((el) => el.textContent.trim())") == [
        "What is missing?", "Root branch It shall leaf.|What is missing?|model-b|high|edit,internet|None"]
    page.eval("(() => { document.querySelector('.chat-input').value = 'And next?'; document.querySelector('.chat-send').click(); })()")
    page.wait("document.querySelectorAll('.chat-msg.ai').length === 2")
    assert page.eval("document.querySelectorAll('.chat-msg.ai')[1].textContent").endswith("|And next?|model-b|high|edit,internet|session-1")


@pytest.mark.syngate("SYNGATE_UI-038")
def test_the_splitter_resizes_the_outline_and_double_click_resets_it(page):
    width = "Math.round(document.getElementById('tree-pane').getBoundingClientRect().width)"
    default = page.eval(width)
    x, y = page.eval("(() => { const r = document.getElementById('splitter').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()")
    target = x - 150
    page.mouse("mousePressed", x, y)
    page.mouse("mouseMoved", target, y)
    page.mouse("mouseReleased", target, y)
    assert page.eval(width) == round(target) != default
    assert page.eval("sessionStorage.getItem('syngate-ui-tree-width')") == str(round(target))
    page.mouse("mousePressed", target + 2, y, clicks=2)
    page.mouse("mouseReleased", target + 2, y, clicks=2)
    assert page.eval(width) == default and page.eval("sessionStorage.getItem('syngate-ui-tree-width')") is None
