"""An unused preconnection must not block subsequent API requests."""
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

solver = str(Path(sys.argv[1]).resolve())
with socket.socket() as pick:
    pick.bind(('127.0.0.1', 0))
    port = pick.getsockname()[1]
with tempfile.TemporaryDirectory() as folder:
    env = dict(os.environ, TIMETABLE_HOST='127.0.0.1')
    env.pop('TIMETABLE_DATABASE_URL', None)
    process = subprocess.Popen([solver, str(port)], cwd=folder, env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for attempt in range(100):
            try:
                idle = socket.create_connection(('127.0.0.1', port), timeout=1)
                break
            except OSError:
                time.sleep(.05)
        else:
            raise AssertionError('API did not start')
        with idle:
            with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/auth/status', timeout=4) as r:
                assert r.status == 200
        print('Idle preconnection cannot block API: passed')
    finally:
        process.terminate()
        process.wait(timeout=5)
