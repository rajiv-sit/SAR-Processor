import sys

from PyQt5 import QtWidgets


class PyQtRtoViewer(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SAR RTO Viewer")
        layout = QtWidgets.QVBoxLayout(self)
        layout.addWidget(QtWidgets.QLabel("RTO viewer stub"))


def main() -> int:
    app = QtWidgets.QApplication(sys.argv)
    viewer = PyQtRtoViewer()
    viewer.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
