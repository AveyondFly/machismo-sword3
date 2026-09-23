# NewPAL iOS on ROCKNIX

This branch targets the already extracted app at
`/roms/ports/newpal-ios/game/Payload/NewPAL.app`. Its executable is pinned by
`configs/newpal/binary-manifest.json` and `configs/newpal/sdl-symbol-anchors.json`
to SHA-256 `2056c5b0a921088f96302ce7562884fcb2b7383652912995c3946c104893a0d8`.
No IPA is needed.

## Build and device test

The RK3326 toolchain is selected by `cmake/rocknix-rk3326.cmake`. On this
workspace, ccache needs a writable directory:

```sh
CCACHE_DIR=/tmp/newpal-ccache ./scripts/build-rocknix.sh
./scripts/package-newpal-rocknix.sh
python3 scripts/newpal_device_test.py --audit-only
python3 scripts/newpal_device_test.py --seconds 25
```

The test script deploys the runtime and `/roms/ports/newpal.sh`. It leaves the
extracted `game` directory in place and checks the executable hash before
launch. For a smoke test, the caller sources `/storage/env.txt` on the device
to provide Sway's Wayland environment. The installed launcher does not source
that file. It calls `/storage/.config/gptokeyb/gptokeyb` directly.

## Input findings

The game's bundled SDL handles keyboard, joystick and controller events, but
the iOS SDL controller cannot directly use the ROCKNIX gamepad. This port lets
`gptokeyb` own the gamepad and forward keyboard and mouse events to host SDL.
The device's ROCKNIX `gptokeyb` build enables right stick mouse movement with
`right_analog_up = mouse_movement_up`; `right_analog_as_mouse = true` is not a
supported config entry in that build.

| Control | Mapping | Device observation |
| --- | --- | --- |
| D-pad / left stick | Arrows | Title selection and field movement work. |
| A | Mouse left click | Clicking the title's New Game item starts the intro. |
| Right stick | Mouse movement | Provides pointer access for touch-style menus. |
| X | Space | Skips the intro video and advances dialogue. |
| B / Back | Escape | Mapped; gameplay behavior not yet exercised. |
| Start | Return | Mapped; does not activate the title item. |

Return and Space reach the game as SDL key events but do not activate the title
item. A mouse click at its logical coordinates does. Device screenshots also
confirm title rendering, intro playback, dialogue rendering and field movement.
Physical button presses, battle controls, save/load and every menu have not
been tested, so complete controller support remains unverified.
