# PyQt RTO Viewer

UDP viewer for real-time frames and latency stats.

## Usage
```bash
python pyqt_rto_viewer.py 5000
```

Send frames with the C++ publisher using `rto::RtoDataBus` at `udp://127.0.0.1:5000`.

Optional: pass a JSON file with input parameters to display (CPI, BW, PRF, etc):
```bash
python pyqt_rto_viewer.py 5000 path/to/params.json
```
