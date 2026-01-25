import json
import socket
import struct
import sys
import time

from PyQt5 import QtCore, QtGui, QtWidgets


class UdpFrameReceiver(QtCore.QObject):
    frame_received = QtCore.pyqtSignal(int, int, int, bytes)

    def __init__(self, host: str, port: int, parent=None):
        super().__init__(parent)
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind((host, port))
        self._socket.setblocking(False)

    def poll(self):
        try:
            data, _ = self._socket.recvfrom(1024 * 1024)
        except BlockingIOError:
            return
        if len(data) < 16:
            return
        width, height, timestamp_ns = struct.unpack("<IIQ", data[:16])
        payload = data[16:]
        if width == 0 or height == 0:
            return
        self.frame_received.emit(width, height, timestamp_ns, payload)


class PyQtRtoViewer(QtWidgets.QWidget):
    def __init__(self, host: str, port: int, params_path: str | None):
        super().__init__()
        self.setWindowTitle("SAR RTO Viewer")

        self._image_label = QtWidgets.QLabel("Waiting for frames...")
        self._image_label.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self._image_label.setMinimumSize(640, 480)

        self._stats_label = QtWidgets.QLabel("Latency: n/a")
        self._range_label = QtWidgets.QLabel("Range: n/a")
        self._params_label = QtWidgets.QLabel("Input: n/a")
        self._params_label.setWordWrap(True)

        self._pause_btn = QtWidgets.QPushButton("Pause")
        self._pause_btn.setCheckable(True)
        self._pause_btn.setStyleSheet(
            "QPushButton { background-color: #ff7a59; color: #0f1115; padding: 6px 12px; border-radius: 6px; }"
            "QPushButton:checked { background-color: #4fd1c5; color: #0f1115; }"
        )
        self._fit_btn = QtWidgets.QPushButton("Fit")
        self._fit_btn.setStyleSheet(
            "QPushButton { background-color: #6aa5ff; color: #0f1115; padding: 6px 12px; border-radius: 6px; }"
        )
        self._fit_btn.clicked.connect(self._fit_image)

        controls = QtWidgets.QHBoxLayout()
        controls.addWidget(self._pause_btn)
        controls.addWidget(self._fit_btn)
        controls.addStretch(1)

        info_layout = QtWidgets.QVBoxLayout()
        info_layout.addWidget(self._stats_label)
        info_layout.addWidget(self._range_label)
        info_layout.addWidget(self._params_label)
        info_layout.addStretch(1)

        self._scale_label = QtWidgets.QLabel()
        self._scale_label.setFixedWidth(48)

        image_row = QtWidgets.QHBoxLayout()
        image_row.addWidget(self._image_label, 1)
        image_row.addWidget(self._scale_label)

        layout = QtWidgets.QVBoxLayout(self)
        layout.addLayout(controls)
        layout.addLayout(image_row, 1)
        layout.addLayout(info_layout)

        self._receiver = UdpFrameReceiver(host, port, self)
        self._receiver.frame_received.connect(self._on_frame)

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._receiver.poll)
        self._timer.start(16)

        self._latencies = []
        self._max_samples = 120
        self._paused = False
        self._pause_btn.toggled.connect(self._set_paused)

        self._params_path = params_path
        self._params = self._load_params(params_path)
        if self._params:
            self._params_label.setText(self._format_params(self._params))

        self._last_pixmap = None
        self._apply_dark_theme()

    def _apply_dark_theme(self):
        self.setStyleSheet(
            "QWidget { background-color: #0f1115; color: #e6edf3; font-family: 'Segoe UI'; }"
            "QLabel { color: #e6edf3; }"
        )

    def _set_paused(self, checked: bool):
        self._paused = checked
        self._pause_btn.setText("Resume" if checked else "Pause")

    def _fit_image(self):
        if self._last_pixmap is not None:
            self._image_label.setPixmap(self._last_pixmap.scaled(
                self._image_label.size(),
                QtCore.Qt.AspectRatioMode.KeepAspectRatio,
                QtCore.Qt.TransformationMode.SmoothTransformation,
            ))

    def _load_params(self, path: str | None) -> dict:
        if not path:
            return {}
        try:
            with open(path, "r", encoding="utf-8") as handle:
                return json.load(handle)
        except (OSError, json.JSONDecodeError):
            return {}

    def _format_params(self, params: dict) -> str:
        keys = [
            ("CPI", "cpi"),
            ("BW", "bandwidth_hz"),
            ("Pulses", "num_pulses"),
            ("PRF", "prf_hz"),
            ("Fs", "sampling_rate_hz"),
            ("Pulse", "pulse_width_s"),
        ]
        lines = []
        for label, key in keys:
            if key in params:
                lines.append(f"{label}: {params[key]}")
        if not lines:
            lines.append("Input: custom params loaded")
        return " | ".join(lines)

    def _colorize(self, value: float, min_val: float, max_val: float) -> QtGui.QColor:
        if max_val <= min_val:
            return QtGui.QColor(30, 30, 30)
        t = (value - min_val) / (max_val - min_val)
        t = max(0.0, min(1.0, t))
        if t < 0.33:
            r = 0
            g = int(255 * (t / 0.33))
            b = 180
        elif t < 0.66:
            r = int(255 * ((t - 0.33) / 0.33))
            g = 255
            b = int(180 * (1 - (t - 0.33) / 0.33))
        else:
            r = 255
            g = int(255 * (1 - (t - 0.66) / 0.34))
            b = 0
        return QtGui.QColor(r, g, b)

    def _update_scale(self, min_val: float, max_val: float):
        height = 256
        width = 24
        bar = QtGui.QImage(width, height, QtGui.QImage.Format_RGB32)
        for y in range(height):
            t = 1.0 - (y / (height - 1))
            color = self._colorize(min_val + t * (max_val - min_val), min_val, max_val)
            for x in range(width):
                bar.setPixelColor(x, y, color)
        pixmap = QtGui.QPixmap.fromImage(bar)
        self._scale_label.setPixmap(pixmap)
        self._range_label.setText(f"Range: {min_val:.3f} to {max_val:.3f}")

    def _on_frame(self, width: int, height: int, timestamp_ns: int, payload: bytes):
        if self._paused:
            return
        expected = width * height * 4
        if len(payload) < expected:
            return

        now_ns = time.time_ns()
        latency_ns = max(0, now_ns - timestamp_ns)
        self._latencies.append(latency_ns)
        if len(self._latencies) > self._max_samples:
            self._latencies.pop(0)
        mean_latency = sum(self._latencies) / len(self._latencies)
        self._stats_label.setText(f"Latency: {mean_latency / 1e6:.2f} ms")

        pixels = struct.unpack_from(f"<{width*height}f", payload)
        min_val = min(pixels)
        max_val = max(pixels)
        img = QtGui.QImage(width, height, QtGui.QImage.Format_RGB32)
        for y in range(height):
            for x in range(width):
                val = pixels[y * width + x]
                color = self._colorize(val, min_val, max_val)
                img.setPixelColor(x, y, color)
        pixmap = QtGui.QPixmap.fromImage(img)
        self._last_pixmap = pixmap
        self._fit_image()
        self._update_scale(min_val, max_val)


def main() -> int:
    host = "0.0.0.0"
    port = 5000
    params_path = None
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    if len(sys.argv) > 2:
        params_path = sys.argv[2]

    app = QtWidgets.QApplication(sys.argv)
    viewer = PyQtRtoViewer(host, port, params_path)
    viewer.resize(1100, 800)
    viewer.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
