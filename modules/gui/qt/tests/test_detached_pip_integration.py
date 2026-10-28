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

if __name__ == '__main__':
    unittest.main()
