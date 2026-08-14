# PVA DualCam C++

This directory is the Qt 6 / OpenCV C++ migration of the Python application. The C++ application compiles and uses the original Qt Designer files directly:

- `src/modules/main.ui`
- `src/modules/PageHome.ui`
- `src/modules/PageCamera.ui`
- `src/modules/PageParameters.ui`

The main-window/page/worker/signal structure mirrors the Python application. Static layouts are not recreated in C++.

Implemented scope:

- no calibration page or calibration core;
- no triangulation, reflector elevation, or melt-level calculation; reflector boundaries are retained only to define Crown/Body ROIs;
- Neck keeps the diameter calculation `major_axis_camera2 / neck_pixels_per_mm`;
- Idle and Neck calculate reflector boundaries dynamically; Crown and Body reuse those boundaries as their ROIs and retain only the meniscus lower-vertex result;
- Endcone keeps the neck/body-state based diameter calculation;
- the original Home/Camera/Parameters page framework and offline composite-image sequence workflow;
- `CustomGraphicsView`, measurement worker, application signals, and page controllers are separate classes;
- Neck, meniscus, and Endcone detectors are separate algorithm translation units;
- Teledyne DALSA Sapera LT online stereo capture, trigger and camera-parameter control for Nano-M2020;
- Idle/Neck automatic exposure with persisted per-camera exposure values;
- OPC UA PLC stage input and diameter/heartbeat output via embedded open62541;
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

Both offline and online workflows are implemented. Online mode requires Sapera LT at `SAPERA_ROOT` (default `C:/Program Files/Teledyne DALSA/Sapera`) and two Nano-M2020 cameras. Set their CamExpert **User Name** (`DeviceUserID`) values to `1` and `2`; these values map strictly to CAM1 and CAM2. It also requires access to the configured plant OPC UA endpoint.

Relative paths in `cnf.ini` are resolved from the configuration directory, matching Python. The Parameters page displays values without outer brackets and writes them back as `key = [value]`. Offline composite images are decoded with Qt file IO, split equally into Camera 1 and Camera 2, then submitted to `MeasurementWorker`.
