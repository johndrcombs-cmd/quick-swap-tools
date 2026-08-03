import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
LINUX_CONFIG = ROOT / "native" / "quick-swap-config.cpp"
WINDOWS_CONFIG = ROOT / "native" / "windows" / "quick-swap-tools.cpp"


class DesktopDarkThemeSourceTests(unittest.TestCase):
    def test_linux_configurator_uses_fixed_modern_dark_theme(self):
        source = LINUX_CONFIG.read_text()

        self.assertIn('QStyleFactory::create(QStringLiteral("Fusion"))', source)
        self.assertIn("app.setPalette(darkPalette())", source)
        self.assertIn("app.setStyleSheet(darkStyleSheet())", source)
        self.assertIn("#0b0f14", source.lower())
        self.assertIn("#161b22", source.lower())
        self.assertIn("#58a6ff", source.lower())
        self.assertIn('setObjectName(QStringLiteral("controlCard"))', source)
        self.assertIn('setObjectName(QStringLiteral("deviceCard"))', source)

    def test_windows_configurator_uses_fixed_modern_dark_theme(self):
        source = WINDOWS_CONFIG.read_text()

        self.assertIn("DWMWA_USE_IMMERSIVE_DARK_MODE", source)
        self.assertIn("WM_CTLCOLORSTATIC", source)
        self.assertIn("WM_DRAWITEM", source)
        self.assertIn("BS_OWNERDRAW", source)
        self.assertIn("WS_TABSTOP", source)
        self.assertIn("recordingAction_ != 0 || !IsDialogMessageW", source)
        self.assertIn("windowClass.hbrBackground = nullptr;", source)
        self.assertIn('L"Segoe UI"', source)
        self.assertIn("kWindowBackground", source)
        self.assertIn("kSurfaceBackground", source)
        self.assertIn("kAccentColor", source)
        self.assertIn("RoundRect", source)

    def test_windows_status_updates_erase_the_previous_message(self):
        source = WINDOWS_CONFIG.read_text()

        self.assertIn("void setStatusText(const wchar_t *text)", source)
        self.assertIn("RedrawWindow(", source)
        self.assertIn("RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW", source)
        self.assertIn("SetBkColor(dc, kWindowBackground)", source)
        self.assertIn("reinterpret_cast<LRESULT>(windowBackgroundBrush_)", source)


if __name__ == "__main__":
    unittest.main()
