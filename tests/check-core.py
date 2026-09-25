"""Check generated config and endpoint startup without creating a host TUN."""
import json
import pathlib
import signal
import subprocess

root = pathlib.Path(__file__).resolve().parent.parent
core = root / "build/core-linux"
(root / "logs").mkdir(exist_ok=True)
rules = root / "build/test-rules.json"
rules.write_text(json.dumps({"version": 3, "rules": [{"domain_suffix": ["example.com"]}]}))
subprocess.run([core, "rule-set", "compile", "-o", "build/general.srs", rules], cwd=root, check=True)
subprocess.run([core, "check", "-c", "build/awg-test-config.json"], cwd=root, check=True)
config = json.loads((root / "build/awg-test-config.json").read_text())
config["inbounds"] = []
config["log"] = {"level": "info", "disabled": False}
config["route"]["auto_detect_interface"] = False
config["endpoints"][0]["peers"][0]["address"] = "127.0.0.1"
config["endpoints"][0]["peers"][0]["persistent_keepalive_interval"] = 0
process = subprocess.Popen([core, "run", "-c", "stdin"], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=root)
try:
    process.stdin.write(json.dumps(config))
    process.stdin.close()
    # Wait for the actual lifecycle to finish starting, with a bounded timeout.
    import select
    import time
    deadline = time.monotonic() + 10
    output = ""
    while "sing-box started" not in output:
        if time.monotonic() >= deadline:
            raise AssertionError("core did not start: " + output)
        ready, _, _ = select.select([process.stdout], [], [], 0.2)
        if ready:
            line = process.stdout.readline()
            if not line:
                raise AssertionError("core exited before startup: " + output)
            output += line
    process.send_signal(signal.SIGTERM)
    assert process.wait(timeout=10) == 0
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
print("Generated config, endpoint registry, startup and graceful stop: OK")
