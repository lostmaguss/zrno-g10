# Speed model

Research tools that measured the effect kernels' speed before they went to
the camera. `harness.py` compiles a kernel for the ARM946E-S and runs it in
Unicorn, reporting instructions, an approximate cycle count, a D-cache model
and a per-line profile. The other scripts compare and profile particular
kernels.

They don't run from this repository alone: they expect a workspace
(`G12_WORKSPACE`) with the xPack compiler, your own G12 ROM dump and sample
frames from the camera. The frames go in `G12_FRAMES` (default
`work/frames` in the workspace), any full-size G12 shots under these names:

- `smooth.jpg`: a camera JPEG with mostly smooth tones
- `shot-1.jpg`: a camera JPEG graded F400H in the camera
- `shot-2.jpg`: a second camera JPEG
- `highlights.png`: a developed frame with a large bright area

For everyday work, `tests/bench_*_arm.py` count a kernel's instructions
with only the compiler and Unicorn.
