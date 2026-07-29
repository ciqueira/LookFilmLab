# Look Film Lab

Simplify the workflow to obtain consistent results. The project prioritizes
reduced controls, calibrated stocks, and stable releases for DaVinci Resolve
workflows, both in the Color page and the new Photos page. The package is
divided into four parts with different roles.

The tools use a modified GPL-3.0 codebase related to `spektrafilm-ofx`, itself a
native OpenFX port and expansion of the original Python `spektrafilm` project.

Look Film Lab Plugins is distributed through
[MCNexus](https://mcnexus.app). Nexus provides distribution,
license delivery, updates, and product support. MCNexus is the desktop
application used to activate, install, update, and manage the plugins.

## Included Plugins

This demonstration distribution includes:

| Plugin | Version | Distribution | Get Key |
| --- | --- | --- | --- |
| Look Film Lab CINE | 0.2.3 | OpenKey | [Get Key](https://bridge.mcnexus.app/github/claim?t=lookfilmlab-oss&tmpl=117f6d51-727b-4981-92d4-d052bb6c0899&sig=071415544dc13856) |
| Look Film Lab PHOTO | 0.1.3 | OpenKey | [Get Key](https://bridge.mcnexus.app/github/claim?t=lookfilmlab-oss&tmpl=8f651471-a116-4231-9d82-5f23c32233f0&sig=a1be7adb4c6af5f3) |
| Look Film Lab SCAN | 0.1.6 | OpenKey | [Get Key](https://bridge.mcnexus.app/github/claim?t=lookfilmlab-oss&tmpl=3c4450eb-d2ab-4cfd-90ba-47e419fa0c20&sig=c6114afbf51bc390) |
| Look Film Lab GRAIN | 0.1.5 | OpenKey | [Get Key](https://bridge.mcnexus.app/github/claim?t=lookfilmlab-oss&tmpl=61688ad0-d853-45bc-8bba-89af413dfe61&sig=0160b75f523987c9) |

## Look Film Lab CINE

Look Film Lab CINE is intended for film-oriented color grading with cinema
stocks. The workflow combines negative selection, print selection, stock
calibration, and laboratory controls in an interface with fewer exposed
parameters.

The process follows the logic of a photochemical chain:

- `Profile Negative`: selects the negative stock used as the base.
- `Profile Print`: selects the print material or output paper.
- `Film Format`: changes the format reference for the spatial response.
- `Exposure EV`: adjusts exposure before the film response.
- `Film Push / Pull Stops`: simulates development variation in the negative.
- `Negative Bleach Bypass`: adds a neutral silver-retention response to the
  negative.
- `Printer Light`: changes color balance during the print stage.
- `Print Bleach Bypass`: applies silver retention in the print stage.

The intent is not to expose the full spectral model, but to keep the controls
that most affect look building.

## Look Film Lab PHOTO

Look Film Lab PHOTO adapts digital photos toward film-stock characteristics
using part of the same spectral base as CINE. The interface keeps negative
selection, paper selection used by photographic stocks, push/pull, and bleach
bypass in a more direct configuration for still images.

The plugin is intended for the Resolve Photos page and for workflows where
multiple images need to keep a consistent visual direction without rebuilding a
node structure for each photo.

## Look Film Lab SCAN

Look Film Lab SCAN is intended for scanned physical negatives. The tool handles
image inversion to positive, film-base compensation, scan exposure, per-channel
density scale, and emulated paper selection.

Main public controls:

- `Film Base Color`, `Film Base Temp`, and `Film Base Tint`: correct the orange
  base or variations in scanned material.
- `Scan Exposure EV`: adjusts scan exposure before conversion.
- `Density Scale R/G/B`: balances channel density.
- `Profile Print`: defines the paper response used after inversion.
- `Print Push / Pull Stops` and `Print Bleach Bypass`: adjust the final print
  stage.

The workflow preserves the physical negative characteristics and applies the
print response on a simulated paper.

## Look Film Lab GRAIN

Look Film Lab GRAIN isolates the production grain model without applying a
negative look, print stock, paper response, or display transform. The selected
working primaries and transfer curve are preserved at the output, so an ARRI
Wide Gamut 3 / LogC3 input returns ARRI Wide Gamut 3 / LogC3 with grain.

A fixed internal physical reference supplies the exposure, development, and
layer structure. Only its stochastic density difference is returned to the
original RGB, so no negative look, color temperature, or characteristic curve
is applied. Public controls are
`Working Color Space`, `Working Gamma`, `Film Format`, `Amount`, `Saturation`,
`Particle Area um2`, `Final Grain Blur`, `Dye Cloud Blur um`, `Seed`, and
`Animate`. A collapsed `Advanced Grain` group exposes separate, precision
controls organized into `Particle Channels`, `Emulsion Layers`,
`Density Distribution`, and `Microstructure` subgroups. Their shorter labels
cover red-, green-, and blue-sensitive particle scale; coarse, medium, and fine
emulsion-layer scale; RGB density floor and uniformity; and microstructure
scale/sigma. `Reset Advanced` restores only those advanced controls to their
factory reference values.

## Platform Support

Current builds support:

- macOS, Apple Silicon and compatible Intel Macs
- Windows x64

Supported processing backends:

- Metal on macOS
- Vulkan in the corresponding Windows builds

## Installation

Each Look Film Lab plugin has its own OpenKey demonstration license.

1. Use the matching `Get Key` link in the table above.
2. Authorize with a GitHub account.
3. Copy the issued license key.
4. Open MCNexus.
5. Activate the matching plugin with that key.
6. Install or update the plugin through MCNexus.

Lost key: open the same claim link with the same GitHub account to recover the
issued license.

## Credits

Original Python `spektrafilm` project:

Andrea Volpato  
https://github.com/andreavolpato/spektrafilm

Native OpenFX port and expansion:

Aedan Oskar Otto Diez / chaert-s  
https://github.com/chaert-s/spektrafilm-ofx

Look Film Lab modification, product split, calibration, MCNexus distribution,
and releases:

Magno Ciqueira  
https://github.com/ciqueira

OpenFX SDK:

Academy Software Foundation OpenFX  
https://github.com/AcademySoftwareFoundation/openfx

## License

Look Film Lab Plugins is an independent modified distribution based on
GPL-3.0 licensed `spektrafilm-ofx` source code. It is not an official
`spektrafilm-ofx` release and is not an official release of the original Python
`spektrafilm` project. No endorsement by the upstream authors is implied.

See:

- [LICENSE.txt](LICENSE.txt)
- [MODIFICATIONS.md](MODIFICATIONS.md)
- [DISTRIBUTION.md](DISTRIBUTION.md)
- [Legal/THIRD_PARTY_NOTICES.txt](Legal/THIRD_PARTY_NOTICES.txt)

## Binary Releases

Binary OFX releases are distributed through Nexus and may also be published
through GitHub Releases.

Each public binary release should include or link to the corresponding source
code for that exact release, along with GPL-3.0 license text, modification
notes, distribution notes, and third-party notices.
