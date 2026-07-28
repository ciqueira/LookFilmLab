# LookFilmLab Distribution Notes

LookFilmLab binary releases are distributed as modified GPL-3.0 builds derived
from `spektrafilm-ofx` source code.

This is not an official `spektrafilm-ofx` binary distribution and is not an
official release of the original Python `spektrafilm` project.

Attribution chain:

```text
Python spektrafilm research project
        ↓
spektrafilm-ofx native OpenFX port/expansion
        ↓
LookFilmLab independent fork/modification
```

## Required files for public binary releases

Each public binary release should include:

```text
MCLookFilmLabCINE.ofx.bundle
MCLookFilmLabPHOTO.ofx.bundle
MCLookFilmLabSCAN.ofx.bundle
MCLookFilmLabGRAIN.ofx.bundle
LICENSE.txt or GPL-3.0.txt
MODIFICATIONS.md
DISTRIBUTION.md
Legal/THIRD_PARTY_NOTICES.txt
```

The old upstream `SPEKTRAFILM_OFX_LUT_LICENSE.txt` is not bundled with
LookFilmLab. If LookFilmLab exposes LUT export in a public release, create and
review LookFilmLab-specific LUT terms before distribution.

## Corresponding source

For every binary release, publish the corresponding source code used to build
that binary.

Recommended release tag format:

```text
lookfilmlab-v0.1.4
```

Recommended release note:

```text
Source code for this binary release is available at:
<repository/tag/source-archive-url>
```

## Build summary

Production build:

```bash
cmake -S . -B build-release \
  -DSPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION \
  -DSPEKTRAFILM_PRO_CALIBRATION_FILE="$PWD/calibration/active_production_calibration.lookfilmlab.json" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-release --target spektrafilm
```

Calibration build:

```bash
cmake -S . -B build-calibration \
  -DSPEKTRAFILM_PRO_BUILD_MODE=CALIBRATION \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-calibration --target spektrafilm
```

The aggregate CMake target remains `spektrafilm`; the public bundle artifacts
are:

```text
MCLookFilmLabCINE.ofx.bundle
MCLookFilmLabPHOTO.ofx.bundle
MCLookFilmLabSCAN.ofx.bundle
MCLookFilmLabGRAIN.ofx.bundle
```

For calibration work, the plugin writes the user's active working master to:

```text
~/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json
```

Before a public release, promote the intended calibration master into the source
tree or pass the exact exported file through `SPEKTRAFILM_PRO_CALIBRATION_FILE`
so the Production bundle contains a frozen copy.

## Important restrictions

- Do not repackage official `spektrafilm-ofx` or spektrafilm binary
  distributions as LookFilmLab.
- Do not include non-redistributable standards data or licensed resources unless
  you have permission to redistribute them.
- Do not imply endorsement by `spektrafilm-ofx`, the original Python
  `spektrafilm` project, or their authors.
- Keep GPL-3.0 license and third-party notices with the distribution.

## Public host identity

Expected GRAIN OpenFX host display:

```text
MC Plugins > Look Film Lab GRAIN v0.1.4
```

Expected identifier:

```text
com.mclookfilmlab.grain
```
