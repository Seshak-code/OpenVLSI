"""OpenVLSI from Python: every command of the app as a method.

    import openvlsi
    with openvlsi.connect("~/OpenVLSI/openvlsi.libs") as ov:
        run = ov.flow_run(design="gcd", verilog=["/abs/gcd.v"], top="gcd", platform="ihp-sg13g2", clock_period_ns=10)
        print(ov.flow_report(run=run["run"])["clean"])
        ov.flow_open_layout(run=run["run"])   # when the app has this workspace open, the window follows

connect() starts openvlsi-mcp, which attaches to a running app on that workspace, or else works on the database itself; the
window's own commands (ui_*, *_view_*) then need the app. ov.commands() lists what is available; docs/api.md describes them all.
A command that refuses raises OpenVlsiError with its reason.
"""
import json
import os
import shutil
import subprocess


class OpenVlsiError(RuntimeError):
    """A command refused, and why."""


class Client:
    def __init__(self, workspace, mcp=None):
        exe = mcp or os.environ.get("OPENVLSI_MCP") or shutil.which("openvlsi-mcp")
        if not exe:
            raise OpenVlsiError("openvlsi-mcp not found: pass mcp=, or set OPENVLSI_MCP")
        self._process = subprocess.Popen([exe, "--workspace", os.path.abspath(os.path.expanduser(workspace))], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self._id = 0
        self._rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "openvlsi.py", "version": "1"}})
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _send(self, message):
        self._process.stdin.write(json.dumps(message) + "\n")
        self._process.stdin.flush()

    def _rpc(self, method, params):
        self._id += 1
        self._send({"jsonrpc": "2.0", "id": self._id, "method": method, "params": params})
        line = self._process.stdout.readline()
        if not line:
            raise OpenVlsiError("openvlsi-mcp ended")
        reply = json.loads(line)
        if "error" in reply:
            raise OpenVlsiError(reply["error"]["message"])
        return reply["result"]

    def call(self, command, **arguments):
        result = self._rpc("tools/call", {"name": command, "arguments": arguments})
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise OpenVlsiError(f"{command}: {text}")
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text

    def commands(self):
        return self._rpc("tools/list", {})["tools"]

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        return lambda **arguments: self.call(name, **arguments)

    def close(self):
        if self._process.poll() is None:
            self._process.stdin.close()
            self._process.wait()

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()


def connect(workspace, mcp=None):
    return Client(workspace, mcp)
