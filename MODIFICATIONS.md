# LookFilmLab Modifications

LookFilmLab is an independent modified build based on GPL-3.0 licensed
`spektrafilm-ofx` source code.

This project is not an official `spektrafilm-ofx` release, is not an official
release of the original Python `spektrafilm` project, and is not endorsed by
their authors or distributors.

## Upstream attribution

Immediate upstream:

```text
spektrafilm-ofx
```

LookFilmLab inherits the native OpenFX implementation, build structure,
renderer integration, resource pipeline, and plugin workflow from
`spektrafilm-ofx`.

Original research foundation:

The `spektrafilm-ofx` project ports and expands the original Python
`spektrafilm` project by Andrea Volpato:

```text
https://github.com/andreavolpato/spektrafilm
```

The native OFX implementation also includes work and notices documented in:

```text
Legal/THIRD_PARTY_NOTICES.txt
```

## Main modifications in this fork

- Rebranded the public plugin identity to `LookFilmLab`.
- Changed the public OpenFX grouping to `MC Plugins`.
- Changed the public plugin identifier to:

```text
com.MCLookFilmLab
```

- Changed the public bundle artifact to:

```text
MCLookFilmLab.ofx.bundle
```

- Added a separate public product version file:

```text
VERSION
```

- Added Production and Calibration build modes for the Pro workflow.
- Reduced the Production UI to approved public controls.
- Added the standalone `Look Film Lab GRAIN` product
  (`com.mclookfilmlab.grain`) with a neutral grain-only Metal/Vulkan path,
  a fixed Kodak Vision3 5219 500T density reference, calibrated grain strength,
  and same-space output.
- Added Calibration workflow for saving and loading an active production
  calibration master.
- Added support for embedding the active calibration into the Production
  bundle.
- Added validation for `lookfilmlab-calibration-v1` calibration files before
  they are copied into a Production bundle.
- Restricted Production presets, clipboard operations, and user defaults to the
  approved public control set.
- Added source-controlled release calibration support through:

```text
calibration/active_production_calibration.lookfilmlab.json
```

- Removed public plugin icon resources from the distributed bundle.

## Versioning

LookFilmLab public version:

```text
0.1.1
```

Core/internal SpektraFilm version remains controlled by the existing CMake
variables and may differ from the public LookFilmLab version.

## License

This modified source remains licensed under GPL-3.0. See:

```text
LICENSE.txt
```

Binary distributions must provide the corresponding source code under GPL-3.0.
