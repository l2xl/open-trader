# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Synthetic agent library: item seed context and AI connections."""

import json
import subprocess
import tempfile

import syngatelib

CLAUDE_MODELS = ("claude-fable-5-1", "claude-opus-5", "claude-sonnet-5", "claude-haiku-4-5-20251001")
CLAUDE_EFFORTS = ("low", "medium", "high", "xhigh", "max")


def seed_context(items, uid):
    """Descriptions of every ancestor, parents first, then the item's own; an item reached through several parents enters once."""
    return "".join(items[selected].description for selected in syngatelib.walk(items, uid))


class connection_error(Exception):
    pass


class query_error(ValueError):
    pass


def query(connectors, items, uid, text, connector, model, effort, modes=(), session=None):
    """(reply text, session id) of one exchange anchored at `uid`, sent through the named connection with the exact model, effort and call modes."""
    connection = connectors.get(connector)
    if uid not in items:
        raise query_error(f"unknown UID '{uid}'")
    if connection is None:
        raise query_error(f"unknown connector '{connector}'")
    if model not in connection.models or effort not in connection.efforts:
        raise query_error(f"unknown model '{model}' or effort '{effort}'")
    if not set(modes) <= set(connection.modes):
        raise query_error(f"unknown mode in {sorted(modes)}")
    if not text.strip():
        raise query_error("empty input")
    return connection.dispatch(seed_context(items, uid), text, model=model, effort=effort, modes=modes, session=session)


class claude_code_connector:
    label = "Claude Code"
    models = CLAUDE_MODELS
    efforts = CLAUDE_EFFORTS
    always_allowed = ("Agent",)
    # Call modes: what a turn may do without prompting, as (label, hint, on by default, permission mode, allowed tools).
    modes = {
        "edit": ("Edit", "project files", True, "acceptEdits", ()),
        "internet": ("Internet", "access", True, None, ("WebSearch", "WebFetch")),
        "workflows": ("Workflows", "run subagents", False, None, ("Workflow",)),
    }

    def __init__(self, cli=("claude",), cwd=None):
        self.cli = list(cli)
        self.cwd = cwd

    def settings(self):
        return {"label": self.label,
                "models": [{"id": model, "name": model.removeprefix("claude-")} for model in self.models],
                "efforts": list(self.efforts),
                "modes": [{"id": mode, "label": label, "hint": hint, "default": default} for mode, (label, hint, default, _, _) in self.modes.items()]}

    def dispatch(self, context, text, model=None, effort=None, modes=(), session=None):
        """(reply text, session id) of one non-interactive `claude -p` turn; `session` continues an earlier one."""
        permission = next((self.modes[mode][3] for mode in modes if self.modes[mode][3]), None)
        tools = list(dict.fromkeys([*self.always_allowed, *(tool for mode in modes for tool in self.modes[mode][4])]))
        with tempfile.NamedTemporaryFile("w", suffix=".md", encoding="utf-8") as seed:
            seed.write(context)
            seed.flush()
            argv = [*self.cli, "-p", text, "--output-format", "json", "--append-system-prompt-file", seed.name,
                    "--strict-mcp-config", "--mcp-config", '{"mcpServers":{}}']
            for flag, value in (("--model", model), ("--effort", effort), ("--resume", session), ("--permission-mode", permission),
                                ("--allowedTools", ",".join(tools))):
                if value:
                    argv += [flag, value]
            done = subprocess.run(argv, cwd=self.cwd, capture_output=True, text=True)
        try:
            reply = json.loads(done.stdout)
        except ValueError:
            raise connection_error((done.stderr or done.stdout).strip() or f"claude exited with {done.returncode}") from None
        if done.returncode or reply.get("is_error"):
            raise connection_error(str(reply.get("result") or done.stderr.strip() or f"claude exited with {done.returncode}"))
        return reply["result"], reply["session_id"]


CONNECTORS = {"claude_code": claude_code_connector}
