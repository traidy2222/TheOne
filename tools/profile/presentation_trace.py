"""Optional Windows presentation capture, scoped to our game's PID and ETW session."""
import csv
import os
import subprocess
import uuid
from collections import Counter
from pathlib import Path


class PresentationTrace:
    def __init__(self, executable, pid, output):
        self.executable = Path(executable).resolve()
        self.path = Path(str(output)+".pm.csv")
        self.session = "TheOneCapture"+uuid.uuid4().hex
        self.log = Path(str(output)+".pm.log").open("w")
        self.flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self.process = subprocess.Popen([str(self.executable), "--process_id", str(pid),
            "--output_file", str(self.path), "--qpc_time_ms", "--session_name", self.session,
            "--no_console_stats"], stdout=self.log, stderr=self.log, creationflags=self.flags)

    def finish(self):
        if self.process.poll() is None:
            subprocess.run([str(self.executable), "--session_name", self.session,
                            "--terminate_existing_session"], stdout=self.log, stderr=self.log,
                           creationflags=self.flags, timeout=10)
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait()
        self.log.close()
        result = {"exit_code": self.process.returncode, "path": str(self.path)}
        if self.path.exists():
            with self.path.open(newline="") as stream:
                rows = list(csv.DictReader(stream))
            result["events"] = len(rows)
            result["modes"] = dict(Counter(r.get("PresentMode", "unknown") for r in rows))
            result["displayed_events"] = sum(r.get("MsUntilDisplayed") not in (None, "NA", "") for r in rows)
        return result
