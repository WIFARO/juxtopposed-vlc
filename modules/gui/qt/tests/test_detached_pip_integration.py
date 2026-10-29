# SPDX-License-Identifier: GPL-2.0-or-later
# Guards the integration contract; native rendering is tested separately.
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1] / 'maininterface'

class IntegrationContract(unittest.TestCase):
    def test_real_opt_in_api(self):
        header = (ROOT / 'mainctx.hpp').read_text()
        source = (ROOT / 'compositor_dcomp.cpp').read_text()
        self.assertNotIn('bool detachedPipAvailable() const { return false; }', header)
        self.assertIn('"qt-detached-pip"', source)
        self.assertIn('setDetachedPipHandler', source)
        self.assertIn('m_pipTransfer.move', source)
    def test_one_geometry_owner(self):
        source = (ROOT / 'videosurface.cpp').read_text()
        self.assertIn('VideoSurfaceProvider::setExternalSurface', source)
        self.assertIn('VideoSurfaceProvider::reportExternalSurface', source)
        self.assertIn('if (m_externalSurface)', source)
    def test_lifecycle_hooks(self):
        source = (ROOT / 'compositor_dcomp.cpp').read_text()
        self.assertIn('destroyDetachedPip', source)
        self.assertIn('CompositorDirectComposition::windowSetFullscreen', source)
        interface = (ROOT / 'interface_window_handler.cpp').read_text()
        self.assertIn('!m_mainCtx->detachedPipActive()', interface)
    def test_controls_share_engine_but_not_cross_device_visuals(self):
        source = (ROOT / 'compositor_dcomp.cpp').read_text()
        self.assertIn('std::make_unique<QQuickView>(m_quickView->engine(), nullptr)', source)
        self.assertIn('pipUiSwapchain(view)', source)
        self.assertIn('m_pipUiVisual->SetContent(content.Get())', source)
        self.assertIn('m_pipUiVisual->SetContent(nullptr)', source)
        self.assertIn('pip ? detachedUi : ui', source)
    def test_controls_handle_keys_before_hotkeys(self):
        source = (ROOT / 'compositor_dcomp.cpp').read_text()
        event_filter = source.split('bool CompositorDirectComposition::eventFilter', 1)[1]
        self.assertNotIn('QEvent::KeyPress', event_filter)
        self.assertNotIn('QEvent::MouseButtonDblClick', event_filter)
    def test_pip_qml_is_in_both_build_manifests(self):
        for manifest in ('Makefile.am', 'meson.build'):
            source = (ROOT.parent / manifest).read_text()
            for name in ('redesign/RedesignPipControls.qml', 'PipPlayerControls.qml', 'DetachedPipWindow.qml'):
                self.assertIn('player/qml/' + name, source)

if __name__ == '__main__':
    unittest.main()
