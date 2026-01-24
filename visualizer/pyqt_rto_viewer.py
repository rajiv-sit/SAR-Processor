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
    def __init__(self, host: str, port: int):
        super().__init__()
        self.setWindowTitle("SAR RTO Viewer")

        self._image_label = QtWidgets.QLabel("Waiting for frames...")
        self._stats_label = QtWidgets.QLabel("Latency: n/a")

        layout = QtWidgets.QVBoxLayout(self)
        layout.addWidget(self._image_label)
        layout.addWidget(self._stats_label)

        self._receiver = UdpFrameReceiver(host, port, self)
        self._receiver.frame_received.connect(self._on_frame)

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._receiver.poll)
        self._timer.start(16)

        self._latencies = []
        self._max_samples = 120

    def _on_frame(self, width: int, height: int, timestamp_ns: int, payload: bytes):
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
        scale = 255.0 / (max_val - min_val) if max_val > min_val else 1.0
        img = QtGui.QImage(width, height, QtGui.QImage.Format_Grayscale8)
        for y in range(height):
            for x in range(width):
                val = pixels[y * width + x]
                level = int((val - min_val) * scale)
                img.setPixel(x, y, QtGui.qRgb(level, level, level))
        pixmap = QtGui.QPixmap.fromImage(img)
        self._image_label.setPixmap(pixmap)


def main() -> int:
    host = "0.0.0.0"
    port = 5000
    if len(sys.argv) > 1:
        port = int(sys.argv[1])

    app = QtWidgets.QApplication(sys.argv)
    viewer = PyQtRtoViewer(host, port)
    viewer.resize(640, 480)
    viewer.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
