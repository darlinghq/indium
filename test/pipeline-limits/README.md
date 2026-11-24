# pipeline-limits

The threadgroup size limits of a compute pipeline. `unlimited` declares no limit, so its pipeline
allows what the device does, up to Metal's 1024 threads; `limited` declares at most 64 threads, as
MSL's `[[max_total_threads_per_threadgroup(64)]]` does, and its pipeline allows no more than that
or than the descriptor's `maxTotalThreadsPerThreadgroup`; `too_large` declares 2048, more than
any device allows, and has no pipeline. A descriptor's
`requiredThreadsPerThreadgroup` is reported by the pipeline, makes its size the pipeline's limit,
and runs; a required size the pipeline cannot allow, or one with a zero dimension, fails to create
the pipeline.

## Provenance of the .metallib

No Metal compiler was used. `shadersrc/pipeline-limits.ll` is hand-written AIR for
`shadersrc/pipeline-limits.metal`, and `pipeline-limits.metallib` wraps it with air-ops'
`make-metallib.py`:

    llvm-as shadersrc/pipeline-limits.ll -o pipeline-limits.air
    ../air-ops/make-metallib.py pipeline-limits.air pipeline-limits.metallib kernel:unlimited kernel:limited kernel:too_large

Assemble it with LLVM 19 or older, since bitcode written by a newer LLVM may not be readable by an
older one.
