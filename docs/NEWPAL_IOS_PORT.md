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
launch. It also compares the hashes of every deployed runtime executable and
library against this branch's package and rejects any config that refers to a
`ref` library. A live `/proc/PID/maps` check on the device showed only the
packaged `libsword3_*`, `libsystem_shim.so`, C++ runtimes, and `machismo` under
`/roms/ports/newpal-ios`; nothing was loaded from `ref`.

The C++ runtimes come from `scripts/build-libcxx-rocknix.sh`, using the pinned
`extern/llvm-project` submodule and the RK3326 toolchain. A clean rebuild on
this branch completed 856 Ninja steps. Its `libc++.so.1` and
`libc++abi.so.1` hashes equal the copies in `ref`; those bytes are reproducible
from the checked out source, and the package script copies from
`build-libcxx-rocknix`, never from `ref`.

For a smoke test, the caller sources `/storage/env.txt` on the device
to provide Sway's Wayland environment. The installed launcher does not source
that file. It does not launch `gptokeyb`.

## Input findings

The host SDL2 library opens the ROCKNIX game controller and forwards its
events to NewPAL. `SDL_GAMECONTROLLERCONFIG_FILE` points to the device's
controller mapping database. The path happens to be under the `gptokeyb`
configuration directory; no `gptokeyb` process runs and no key map is loaded.
The game's `SDL_IsGameController` call is hooked into host SDL2. The game's
joystick initialization and controller open calls run normally.

| Control | Device observation |
| --- | --- |
| A | Opens a highlighted battle command and confirms the highlighted enemy after Attack is selected. |
| Select + Start | Exits NewPAL from the host SDL event path. Guide + Start also exits. |

An isolated device run used the current executable and a virtual SDL2
controller. The native A button opened the highlighted Item battle command.
After Attack was selected, A made the hero attack the highlighted enemy in a
two-enemy battle. This addresses the earlier failure where a `gptokeyb`
mouse-click mapping clicked the pointer's old position. Controller-only title
activation and other touch menus have not yet been verified. An earlier
controller-only title test did not activate New Game with A; this is a
remaining input gap.

The Select + Start exit was tested on the device with a virtual controller:
the runtime logged the combination, exited with status 0, and left no
`machismo` or `gptokeyb` process. The bridge uses `_exit(0)` because the
guest's exit destructors currently abort on an invalid free.

## Sound effects

BGM uses the `AVAudioPlayer` bridge and host SDL_mixer. The game's sound
effects use a separate SDL callback device. NewPAL calls its bundled
`SDL_PauseAudio(0)` after opening that device; the bundled SDL still looks for
device ID 1, while host SDL opened ID 2. The NewPAL hooks now forward
`SDL_PauseAudioDevice` to the host bridge, which translates ID 1 to the actual
device. The bundled `SDL_MixAudio` also cannot see the host device's format,
so its calls now use host `SDL_MixAudioFormat`. Legacy audio locking uses the
host device as well. A device run installed all 46 hooks and logged
`PauseAudioDevice 1 -> 2 pause=0`; sound effects were then reported working.

## Trainer

Y opens the host trainer overlay using the Paladin 2 menu layout and the same
six entries: money, full HP, no encounters, level up after battle, one-hit
kill, and close. D-pad or left stick selects an entry; A activates it. Y, B, or
Select closes the overlay. The implementation in `host_cheat_newpal.c` uses
addresses from the pinned NewPAL executable above and reads its live game
state. It does not load code from `ref` or use the Paladin 2 data layout.

Money is set to the game's script limit of 60000. Full HP uses the game's role
property updater to restore each party member's HP and MP. The no-encounter
hook recognizes visible monster scripts, removes the monster with the game's
actor, collision, and spatial-list cleanup calls, and advances past the
contact delay and battle request opcodes. The interpreter still finishes the
rest of the script. A fallback clears a battle request only after it identifies
and removes a matching visible monster object. Scripted and boss battle
requests pass through unchanged. While a recognized monster script is active,
the scene mode hook keeps field movement input enabled; the game otherwise
hides the virtual stick until that script finishes.
The battle reward hook raises party EXP to the next
level threshold and raises the character's raw level cap to 99; the game checks
both values before granting a level. The one-hit hook increases player-to-monster damage in
the game's damage calculation path for enemy IDs below 2000. The three toggle
effects reset when the game
exits.

The hook installer checks the original ARM64 instructions at every patch site.
These effects require a live run with a playable save to confirm their behavior
through contact, combat, and the reward screen.

## Device `ref` library analysis

The user supplied `/roms/ports/newpal-ios/ref` as a reference runtime. Its
`libnewpal_host.so` opens a host SDL2 game controller and includes host menu
and battle controls. Its exported symbols include `host_battle_button`,
`host_battle_confirm_command`, and `host_battle_confirm_entry`.

The reference `address-hooks.conf` comments name SHA-256
`f7df6df91783bcb19abbd6be365651cbf1878982d8d280509f5784b6fa331b8e`,
which differs from the current executable. Nevertheless, all 59 hook
fingerprints match. An isolated device run loaded the **current** executable
with the reference's `machismo-partial-sdl.conf`: all 59 hooks installed, the
intro rendered, and SDL found one `retrogame_joypad` with a controller mapping.
The SHA comment is therefore not sufficient evidence of incompatibility.

An isolated reference run also reached a two-enemy battle with the current
executable. Native controller A selected a command and confirmed a highlighted
enemy, so the relevant battle path works with this binary despite the differing
SHA comment. The reference's default `machismo.conf` comments out
`address_hooks`; its `machismo-partial-sdl.conf` enables them. No launcher was
included in `ref`.
