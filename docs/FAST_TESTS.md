# Fast tests and lean CI

CTest labels split the suite so a save-and-rebuild loop stays short. `ctest -L fast` is the default loop; `render` and `slow` are for longer checks before you call a change done.

| Label | What it is | How to run |
|---|---|---|
| `fast` | Unit and math checks, plus the ~1 s offline render smoke test. Each test is meant to finish in a few seconds. | `ctest --test-dir build/<preset> -L fast --output-on-failure` |
| `render` | Short offline / plugin renders, typically 5-30 s each. | `ctest --test-dir build/<preset> -L render` |
| `slow` | Heavy tests (the UI probe today). They share a `RESOURCE_LOCK` (`jidai_heavy`) so only one runs at a time, even with `ctest -j`. | `ctest --test-dir build/<preset> -L slow` |

`smoke` (`JidaiPresetTests --smoke`) is the offline render check: 1 s at 48 kHz through JidaiProcessor, looking for NaN/Inf, subnormal floats, peak bounds, silence on an unpatched rack, and non-silent output on a through-patched rack and a RONIN voice. It is not the standalone `"JIDAI RACK" --smoke out.wav` path, which plays through the default audio device.

## Presets and the compiler cache

From the repository root, with Ninja and CMake 3.22 or later:

CMake presets live in `jidai-rack/CMakePresets.json` (CMake 3.22). From the repository root:

```sh
cmake -S jidai-rack --preset linux-dev    # mac-dev or windows-dev on those machines
cmake --build build/linux-dev
ctest --test-dir build/linux-dev -L fast --output-on-failure
# or: ctest --preset fast-linux-dev  (run from jidai-rack/)
```

Windows presets assume a Visual Studio Developer environment (or `cl` and Ninja already on `PATH`). The macOS `mac-release` preset matches the release workflow's universal build (`arm64;x86_64`, deployment target 11.0).

`JIDAI_USE_CCACHE` is on by default. If `sccache` or `ccache` is on `PATH` and the generator is Ninja or Makefiles, CMake uses it as the compiler launcher. Pass `-DJIDAI_USE_CCACHE=OFF`, or set `CMAKE_CXX_COMPILER_LAUNCHER` yourself, to skip that.

## Watch scripts

`tools/test_watch.sh` (macOS/Linux) and `tools/test_watch.ps1` (Windows) configure if needed, then rebuild and run `ctest -L fast` whenever sources under `jidai-common`, `jidai-rack`, or `cmake` change.

```sh
tools/test_watch.sh --preset linux-dev
tools/test_watch.sh --once --jobs 2
```

```powershell
tools/test_watch.ps1 -Preset windows-dev
tools/test_watch.ps1 -Once -Jobs 2
```

The shell script uses `inotifywait` when it is installed, and otherwise polls every 2 s. No extra package is required.

## CI

Pull requests and pushes to `main` run an Ubuntu-only workflow (Ninja, ccache, a FetchContent cache, `ctest -L fast`). Path filters skip the job when the change is outside the build and test inputs. macOS and Windows plugin builds stay on the Release workflow, which still runs when you push a `v*` tag and still attaches `JIDAI-RACK-macOS.zip` and `JIDAI-RACK-Windows.zip` to a draft release.

## Measured on the Linux box (20 Nov 2026, America/Chicago)

Conditions: GCC 14.2, Ninja, ccache, RelWithDebInfo (`linux-dev`), `nice -n 19`, `-j2`. JUCE 8.0.4 and the pinned plugin sources were already on disk (`FETCHCONTENT_SOURCE_DIR_*`). Load average at the start of the cold rack build was 1.54.

| Step | Wall time |
|---|---|
| `jidai-common` standalone configure+build | 54.1 s (configure 11.6 s, build 42.5 s) |
| `jidai-common` `ctest -L fast` | 0.42 s (4/4 passed) |
| Rack configure (`linux-dev`, juceaide rebuilt) | 28.7 s |
| Rack test-target build (fast tests + `JidaiPresetTests`; no VST3, probe, or `origami_plugin`) | 523 s |
| Configure + that build | 552 s |
| Incremental rebuild after editing `RenderSmokeTests.cpp` (`JidaiPresetTests` only) | 5.1 s (a later compile of the same file, with the JUCE headers, was several minutes) |
| `ctest -L fast -j2` (cold) | 27.4 s, 7/7 passed (`smoke` 15.7 s, `rack` 16.7 s overlapping) |
| `smoke` alone, first version (10 processors, 1 s each case) | 8.9 s, 40/40 checks |
| `smoke` alone, as shipped (3 processors reused; 1 s through-path + 100 ms side cases) | 3.9 s, 40/40 checks |
| `tools/test_watch.sh --once --jobs 2` (warm) | 1.1 s, 7/7 passed |

The ~1 s smoke budget is not met on this box: constructing three `JidaiProcessor`s (JUCE, factory banks, a timer) is most of the 3.9 s. The 1 s through-path itself is a small part of that. `presets` (`render`) was run once and failed in 47 s: it wrote `~/.config/BUSHIDO` and `~/.config/RONIN` on this machine. That run used an earlier `main()` order; the patch restores the original full-suite setup order. `origami_plugin` and `rack_ui` were not built, so the render and slow tiers were not run in full. Windows and macOS presets and watchers were not executed.
