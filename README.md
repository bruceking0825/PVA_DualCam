# PVA DualCam C++

This directory is the Qt 6 / OpenCV C++ migration of the Python application. The C++ application compiles and uses the original Qt Designer files directly:

- `src/modules/main.ui`
- `src/modules/PageHome.ui`
- `src/modules/PageCamera.ui`
- `src/modules/PageParameters.ui`

The main-window/page/worker/signal structure mirrors the Python application. Static layouts are not recreated in C++.

Implemented scope:

- no calibration page or calibration core;
- no triangulation, reflector detection/elevation, or melt-level calculation; Crown/Body use the two manual reflector ROIs in `[Measurement]`;
- Neck uses only Camera 1 and keeps the diameter calculation `major_axis_camera1 / neck_pixels_per_mm`;
- fixed per-camera reflector ROIs limit the Neck ellipse search and the Crown/Body meniscus search; during Crown, Camera 1 Neck diameter tracking overlaps Crown meniscus detection between the two configured diameter thresholds;
- Endcone keeps the neck/body-state based diameter calculation;
- the original Home/Camera/Parameters page framework and offline composite-image sequence workflow;
- `CustomGraphicsView`, measurement worker, application signals, and page controllers are separate classes;
- Neck, meniscus, and Endcone detectors are separate algorithm translation units;
- Teledyne DALSA Sapera LT online stereo capture, trigger and camera-parameter control for Nano-M2020;
- Idle/Neck automatic exposure with persisted per-camera exposure values;
- Sherlock 7 compatible PLC communication: TCP 5000 receives commands and TCP 5001 returns framed results;
- measurement-state persistence, parameter hot reload, parameter row editing, and `graph.json` image-pipeline execution/load/save;
- Python-style side-menu/auxiliary-panel animations and image/overlay figure save/load actions.

## Build (Windows / Visual Studio 2022)

Open a **Developer PowerShell for VS 2022**, then run:

```powershell
cmake -S cpp -B cpp/build -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.0/msvc2022_64 `
  -DOpenCV_DIR=C:/source/opencv/build/x64/vc16/lib
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

The executable is `cpp/build/pva_dualcam_cpp.exe`. Run it from the repository root so it automatically loads `src/cnf.ini`.

Both offline and online workflows are implemented. Online mode requires Sapera LT at `SAPERA_ROOT` (default `C:/Program Files/Teledyne DALSA/Sapera`) and two Nano-M2020 cameras. The two logical measurement slots and their Sapera Device User IDs are defined centrally by `CameraManager`; they are not operator parameters. The Camera page still enumerates every available Sapera Device User ID. The application listens as a Sherlock-compatible TCP server on ports 5000 and 5001; the PLC connects as the client.

Relative paths in `cnf.ini` are resolved from the configuration directory, matching Python. The Parameters page displays values without outer brackets and writes them back as `key = [value]`. Offline composite images are decoded with Qt file IO, split equally into Camera 1 and Camera 2, then submitted to `MeasurementWorker`.

## Sherlock 7 PLC compatibility

The replacement implements the wire format found in
`docs/cz_investigation_scam_CGS1218_V4_14_TEST.ivs`: the PLC sends commands to
TCP 5000 and receives framed replies from TCP 5001. Input accepts CR, LF, or
CRLF; output uses `!`, maximum length byte `0xff`, actual length byte, payload,
and CRLF. Numeric fields are multiplied by 100 and clamped to 999900. The
original 90-byte payload workaround (CRLFLF) is preserved.

Implemented commands are `dia_msr`, `dia_rec`, `dip_msr`, `mlt_msr`,
`acq_on_`, `acq_off`, `acq_get`, `ver_get`, `rfr_get`, `rfr_set`, `cfit_ne`,
and `pfit_sb`. Unsupported legacy ROI/property commands return `err_unk`
instead of being silently acknowledged. The current C++ measurement engine
does not yet reproduce every Sherlock inspection: notably the old melt blob
count is returned as zero, and diameter telegram fields are populated from the
available C++ diagnostics. Validate these field semantics against the PLC and
real images before production use.
