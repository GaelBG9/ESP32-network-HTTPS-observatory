import unittest
from pathlib import Path
import ast
import numpy as np
import pandas as pd
from streamlit.testing.v1 import AppTest

ROOT = Path(__file__).resolve().parents[1]

# Execute the real pure transformation functions, without starting the UI.
module = ast.parse((ROOT / 'app.py').read_text())
functions = [n for n in module.body if isinstance(n, ast.FunctionDef) and n.name in ('prepare', 'assign_sessions', 'session_summary')]
ns = {'pd': pd, 'np': np, 'NUMERIC_COLUMNS': ['rtt_ms', 'gateway_ms', 'http_ms', 'http_code', 'rssi_dbm'], 'GAP_SECONDS': 120}
exec(compile(ast.Module(body=functions, type_ignores=[]), 'app.py', 'exec'), ns)

class DashboardTests(unittest.TestCase):
    def test_demo_renders(self):
        app = AppTest.from_file(str(ROOT / 'app.py')).run(timeout=30)
        self.assertEqual(len(app.exception), 0)
        self.assertEqual(len(app.metric), 4)
        self.assertTrue(any('SIMUL' in msg.value for msg in app.info))

    def test_sessions_duplicates_and_loss_denominator(self):
        rows = [
            {'id': 'a', 'timestamp': '2026-01-01T00:00:00Z', 'status': 'ok', 'rtt_ms': 10},
            {'id': 'a', 'timestamp': '2026-01-01T00:00:00Z', 'status': 'ok', 'rtt_ms': 10},
            {'id': 'b', 'timestamp': '2026-01-01T00:00:01Z', 'status': 'timeout'},
            {'id': 'c', 'timestamp': '2026-01-01T00:00:02Z', 'status': 'wifi_disconnected'},
            {'id': 'd', 'timestamp': '2026-01-01T00:30:00Z', 'status': 'ok', 'rtt_ms': 20},
        ]
        frame = ns['assign_sessions'](ns['prepare'](pd.DataFrame(rows)))
        summary = ns['session_summary'](frame)
        self.assertEqual(len(frame), 4)
        self.assertEqual(len(summary), 2)
        self.assertEqual(summary.iloc[0]['sans réponse (%)'], 50)
        self.assertEqual(summary.iloc[0]['médiane'], 10)

    def test_recovered_rows_are_marked(self):
        frame = ns['prepare'](pd.DataFrame([{'timestamp': '2026-01-01T00:00:00Z', 'device': 'probe_recovered', 'status': 'ok'}]))
        self.assertEqual(frame.iloc[0]['sonde'], 'probe')
        self.assertTrue(frame.iloc[0]['recovered'])

if __name__ == '__main__':
    unittest.main()
