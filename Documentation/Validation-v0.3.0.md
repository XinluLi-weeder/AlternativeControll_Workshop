# Windows v0.3.0 verification — 2026-11-14

UE 5.7 Win64 Development BuildCookRun completed successfully. The standalone executable was launched at 1280×720 with offscreen rendering, independent of the editor.

`Tools/test_packaged_v03.py` passed the actual HUD hit-box routes for start, settings, mode selection, resume and quit. Synthetic float OSC packets exercised the native UDP receiver: five angles [90,180,270,45,135], amplified yaw/pitch, bed alignment and two-peak shake restoration, stale-data disconnection, reconnection, and ignoring OSC in mouse mode. Screenshots verified the menu, connected state, hidden physical-mode button bar and restored mouse-mode bar. This is simulated network input, not a new physical ESP32/Wi-Fi hardware acceptance test.

The test caught a menu click crash from accessing the transient Canvas outside DrawHUD; click coordinates now use the player viewport dimensions.

Known visual limitations inherited from the scene: eight unbuilt static-lighting interactions and cook warnings for two InfinityAssets wood material instances. The room renders with its current runtime lighting; this release does not claim a rebuilt light bake or repaired third-party materials. Standalone engine diagnostics are retained in logs instead of covering the playtest HUD. Editor diagnostics remain enabled.

Third-party source assets are excluded from GitHub source archives. Their cooked runtime content is included in the Windows playtest. Other PCs and macOS were not tested.
