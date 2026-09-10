# V4L2 gain controls

Baseline: OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`.
License: Apache-2.0, inherited from the patched NuttX sources.

`0001-disambiguate-gain-controls.patch` fixes the numeric aliases between
AUTOGAIN/HFLIP and GAIN/VFLIP. Existing flip IDs remain unchanged. AUTOGAIN
and GAIN move to the unused user-class offsets 19 and 20; imgsensor IDs and
the V4L2 control-name table are extended accordingly.

This changes the gain-control ABI. Rebuild the kernel and every camera client
together; do not combine old gain-control clients with the patched kernel.
The old ABI could not distinguish gain from vertical flip. The patch does
not add sensor support by itself, nor claim Linux binary ABI compatibility.

The official checkout remains unchanged. Apply in the isolated validation
tree with `git apply --check` followed by `git apply`. Run the source-based
host check from the team repository:

```sh
python3 tests/host/bk7258/test_camera_control_patches.py
```

The check compiles distinct control IDs and injects SDK register-transport
errors. Full target build and real V4L2 ioctl acceptance are separate gates.

`0002-initialize-scalar-set-control.patch` initializes the temporary extended
control structures in `VIDIOC_S_CTRL`, matching `VIDIOC_G_CTRL`. Scalar
controls have size zero; leaving it uninitialized passes stack data into
the sensor callback. Apply after patch 0001 against the same baseline.
