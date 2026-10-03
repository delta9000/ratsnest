"""End-to-end stdio MCP -> local socket -> live session verification."""
import json
import pathlib
import subprocess
import sys
import tempfile
import time

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="ratsnest-mcp-") as directory:
    patch = pathlib.Path(directory) / "test.patch"
    patch.write_text("tone = vco { freq: 110 }\ntone -> out.in : 0.1\n")
    socket = str(pathlib.Path(directory) / "session.sock")
    server = subprocess.Popen([binary, str(patch), "--headless", "--silent", "--socket", socket], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    clients = []
    try:
        for _ in range(200):
            if pathlib.Path(socket).exists():
                break
            if server.poll() is not None:
                raise AssertionError(server.stderr.read().decode())
            time.sleep(.01)
        assert pathlib.Path(socket).exists(), "server did not open socket"

        def client():
            proc = subprocess.Popen([binary, "--mcp", socket], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            clients.append(proc)
            return proc

        serial = 0
        def rpc(proc, method, params=None):
            global serial
            serial += 1
            proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": serial, "method": method, "params": params or {}}) + "\n")
            proc.stdin.flush()
            response = json.loads(proc.stdout.readline())
            assert response["id"] == serial
            return response

        def call(proc, name, **args):
            return rpc(proc, "tools/call", {"name": name, "arguments": args})["result"]

        a, b = client(), client()
        for proc in (a, b):
            result = rpc(proc, "initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}})["result"]
            assert result["protocolVersion"] == "2025-11-25"
            proc.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
            proc.stdin.flush()
            assert rpc(proc, "ping")["result"] == {}
        assert rpc(a, "resources/list")["result"]["resources"][0]["uri"] == "ratsnest://modules"
        assert "## `transport`" in rpc(a, "resources/read", {"uri": "ratsnest://modules"})["result"]["contents"][0]["text"]
        names = {t["name"] for t in rpc(a, "tools/list")["result"]["tools"]}
        assert {"state", "describe", "set_parameter", "replace_patch", "save"} <= names
        assert rpc(a, "tools/call", {"name": "nonexistent", "arguments": {}})["error"]["code"] == -32602
        state = call(a, "state")["structuredContent"]
        revision = state["revision"]
        assert state["source"] == patch.read_text()
        assert not call(a, "set_parameter", revision=revision, module="tone", parameter="freq", value="220")["isError"]
        assert call(b, "set_parameter", revision=revision, module="tone", parameter="freq", value="330")["isError"]
        state = call(b, "state")["structuredContent"]
        assert state["revision"] == revision + 1
        assert next(n for n in state["modules"] if n["name"] == "tone")["inputs"]["freq"] == 220
        revision = state["revision"]
        assert call(a, "replace_patch", revision=revision, source="bad = nonexistent {}")["isError"]
        assert call(a, "state")["structuredContent"]["revision"] == revision
        assert not call(a, "save", revision=revision)["isError"]
        assert "freq: 220" in patch.read_text()
        # An external writer must never be overwritten with our stale source.
        patch.write_text("# external owner\ntone = vco {freq: 333}\ntone -> out.in\n")
        assert call(a, "save", revision=revision)["isError"]
        assert "external owner" in patch.read_text()
        a.stdin.write("{\n"); a.stdin.flush()
        assert json.loads(a.stdout.readline())["error"]["code"] == -32700
        assert rpc(a, "ping")["result"] == {}
        print("MCP integration: handshake, discovery, shared edits, stale revisions, invalid patches, save conflicts and recovery passed")
    finally:
        for proc in clients:
            proc.stdin.close()
            proc.wait(timeout=3)
        server.terminate()
        server.wait(timeout=3)
