# PiP controls — source changes

The desktop PiP remains experimental and opt-in (`--qt-detached-pip`, Windows
DirectComposition). This change does not rebuild or overwrite `Player-Wil` or
the existing ZIP.

## Behavior

- Drag the picture to move the window; a picture click never toggles playback.
- Separate play/pause, back/forward 10 seconds, volume/mute, and seek controls.
- Subtitle popup: select a loaded track, turn subtitles off, or load a local file.
- Controls hide during playback, but remain available while paused, scrubbing,
  using the subtitle popup, or navigating controls with the keyboard.
- Frameless, topmost desktop window with native edge/corner resizing. Return,
  close, double-clicking the picture, or Escape returns video to the main player;
  Escape dismisses the subtitle popup first.
- Both in-window PiP implementations also consume picture clicks before the
  video surface can forward them as pause hotkeys.

## Tests

Run the QML suites with the existing pinned runner (Qt 6.8.3):

```powershell
uv run --isolated --no-project --python 3.12 --with PySide6==6.8.3 python extras/package/wil/run_qml_tests.py -input modules/gui/qt/tests/tst_WilPipControls.qml
uv run --isolated --no-project --python 3.12 --with PySide6==6.8.3 python extras/package/wil/run_qml_tests.py -input modules/gui/qt/tests/tst_Redesign.qml
python -m unittest discover -s modules/gui/qt/tests -p test_detached_pip_integration.py
uv run --isolated --no-project --python 3.12 --with PySide6==6.8.3 python modules/gui/qt/tests/test_dcomp_pip_overlay.py
```

The last test needs an interactive Windows desktop. It uses synthetic D3D11
video and the actual Qt Quick controls. Since PySide does not expose the private
QRhi swapchain field, the fixture uploads Qt's premultiplied UI render into a
composition swapchain. This checks alpha composition, sharing swapchain content
between different DComp devices, continuing video updates, and input isolation.
It does **not** test production's direct QRhi swapchain extraction or VLC decoding.
The Python integration-contract tests do not compile C++.

## Required before shipping a new binary

Build the complete player from the changed source, then use an isolated test
profile and non-personal video with embedded/external subtitles. Check:

1. Click/drag the picture without changing playback or position; operate every
   control without moving the window. Skip/seek stay disabled for live streams
   that cannot seek.
2. Confirm real VLC video and subtitles remain live under the controls, including
   after minimizing the main window. Test volume and playback changes from both
   windows, and subtitle selection/off/local-file loading.
3. Resize repeatedly, move between monitors/DPI settings, and exercise repeated
   detach/return cycles. Verify Qt swapchain recreation rebinds the UI overlay.
4. Escape, return and close PiP must reattach without quitting. Stop, next item,
   audio-only transitions, fullscreen, and application quit must clean up safely.

Keep the opt-in gate until these compiled-application checks pass. The existing
EXE/ZIP is not evidence for changes made after its recorded source commit.
