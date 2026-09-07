# VCV Rack

*Rack* is the host application for the VCV virtual Eurorack modular synthesizer platform.

This is a fork of [VCVRack/Rack](https://github.com/VCVRack/Rack). See [Fork changes](#fork-changes) for what it adds.

- [VCV website](https://vcvrack.com/)
- [Manual](https://vcvrack.com/manual/)
- [Support](https://vcvrack.com/support)
- [Module Library](https://library.vcvrack.com/)
- [Rack source code](https://github.com/VCVRack/Rack)
- [Building](https://vcvrack.com/manual/Building)
- [Communities](https://vcvrack.com/manual/Communities)
- [Licenses](LICENSE.md) ([HTML](LICENSE.html))

## Fork changes

Everything below this section is upstream Rack.

### Patching several cables by clicking

Enable **View → Click ports to patch multiple cables**, stored as `multiPatch` in `settings.json` and off by default.

Clicking a port then picks up a cable whose free end follows the cursor. Click more ports to pick up more cables, then click one port for each to patch them, in the order they were picked up. A stereo pair is four clicks, and every cable lands in a single undo step:

- Click the mixer's **In L** — its cable is unplugged and follows the cursor.
- Click the mixer's **In R** — that one comes along too.
- Click the recorder's **In L** — the first cable is patched there.
- Click the recorder's **In R** — the second is patched, and the sequence ends.

Dragging cables is untouched. A drag counts as a click only if it changed no cable and the mouse never strayed from the port, so patching, unpatching, or dragging a cable away and dropping it back where it came from is never mistaken for a click.

Each click either picks up a cable or wires one in, and the port under the cursor shows which with a badge. Once cables are held, a plain click always wires one in, so a cable can be patched into a port whatever is already plugged into it. Ctrl and Ctrl+shift pick up cables instead, one click at a time, reaching the ones stacked underneath (Ctrl is Command on Mac):

| Gesture | What it picks up |
| --- | --- |
| Ctrl+click | The cable plugged into this port, unplugged from it. |
| Ctrl+shift+click | A copy of that cable, keeping its color and its other end. |
| Click, on a port of the other type | A new cable started here. |
| Ctrl+shift+click, on a port of the other type | A new cable here, wearing the color of the cable already on it. |

**View → Take cables** chooses which gesture picks up the very first cable, when nothing is held yet:

- *Click takes the first, Ctrl+click the rest* — a plain click on a patched port picks up its cable, so moving a single cable is two plain clicks. Emptying a port takes a plain click and then Ctrl+clicks.
- *Ctrl+click takes every cable* — a plain click never picks up: on an idle rack it starts a new cable from that port. Every cable is picked up the same way, at the cost of a modifier for the common single move.

Ctrl+shift+click duplicates a cable under either choice.

Picking a cable up hands over the plug that was in the port you clicked, so those cables are wired into ports of the same type. A new cable hands over a plug for the opposite type instead — click outputs, wire into inputs. A sequence only grows with cables whose free end matches the ones already held, so it is never a mix of the two.

Press Escape or click the empty rack to cancel. Cables that were unplugged are plugged back in where they came from, and nothing reaches the undo history. Wiring a cable back into the port it came from also puts it back, so a port emptied of several cables can be filled again one click at a time.

### Tests

`make test` builds `test/multiPatchTest.cpp` against the library and checks the click rules, which are a pure function of the state a port is in. It covers both take schemes and sweeps every reachable state, asserting that a click never picks up a plug of the wrong type, never starts a cable of the wrong type, and never takes a cable from a port that has none.

### Fixes

- Hovering a port whose module reports no `PortInfo` crashed Rack in `PortTooltip::step()`, which dereferenced the result without checking it. Reproducible on stock Rack, not caused by the above. The tooltip now skips such ports, and holds its `PortWidget` weakly so a tooltip that outlives one cannot read freed memory.

## Acknowledgments

- [Andrew Belt](https://github.com/AndrewBelt): Lead Rack developer
- [Pyer](https://www.pyer.be/): Module design, component graphics
- [Richie Hindle](http://entrian.com/audio/): Rack developer, bug fixes
- [Grayscale](https://grayscale.info/): Module design, branding
- Christoph Scholtes: [Library reviews](https://github.com/VCVRack/library) and [plugin toolchain](https://github.com/VCVRack/rack-plugin-toolchain)
- Translators
	- German: Stephan Müsch, Norbert Denninger
	- Spanish: Kevin U. Cano Guerra, Coriander V. Pines
	- French: Pyer
	- Italian: Alessandro Paglia
	- Chinese (Simplified): NoiseTone
	- Japanese: [Leo Kuroshita](https://x.com/kurogedelic)
- Rack plugin developers: Authorship shown on each plugin's [VCV Library](https://library.vcvrack.com/) page
- Rack users like you: [Bug reports and feature requests](https://vcvrack.com/support)

## Dependency libraries

- [GLFW](https://www.glfw.org/)
- [GLEW](http://glew.sourceforge.net/)
- [NanoVG](https://github.com/memononen/nanovg)
- [NanoSVG](https://github.com/memononen/nanosvg)
- [oui-blendish](https://hg.sr.ht/~duangle/oui-blendish)
- [osdialog](https://github.com/AndrewBelt/osdialog) (written by Andrew Belt for VCV Rack)
- [ghc::filesystem](https://github.com/gulrak/filesystem)
- [Jansson](https://digip.org/jansson/)
- [libcurl](https://curl.se/libcurl/)
- [OpenSSL](https://www.openssl.org/)
- [Zstandard](https://facebook.github.io/zstd/) (for Rack's `.tar.zstd` patch format)
- [libarchive](https://libarchive.org/) (for Rack's `.tar.zstd` patch format)
- [PFFFT](https://bitbucket.org/jpommier/pffft/)
- [libspeexdsp](https://gitlab.xiph.org/xiph/speexdsp/-/tree/master/libspeexdsp) (for Rack's fixed-ratio resampler)
- [libsamplerate](https://github.com/libsndfile/libsamplerate) (for Rack's variable-ratio resampler)
- [RtMidi](https://www.music.mcgill.ca/~gary/rtmidi/)
- [RtAudio](https://www.music.mcgill.ca/~gary/rtaudio/)
- [Fuzzy Search Database](https://bitbucket.org/j_norberg/fuzzysearchdatabase) (written by Nils Jonas Norberg for VCV Rack's module browser)
- [TinyExpr](https://codeplea.com/tinyexpr) (for math evaluation in parameter context menu)

## Contributions

VCV is unable to accept outside code contributions, but if you wish to contribute to the VCV Rack software, you can:
- Request a feature or report a bug to [VCV Support](https://vcvrack.com/support).
- [Learn about Rack](https://vcvrack.com/manual/) and answer questions in the [VCV communities](https://vcvrack.com/manual/Communities).
- [Develop your own Rack plugin](https://vcvrack.com/manual/PluginDevelopmentTutorial), or help maintain an existing plugin.
- Apply for a [job at VCV](https://vcvrack.com/jobs).
