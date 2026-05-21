# tinyc Benchmarking Notes

tinyc's release proof is correctness-first: `make check`, sanitizer validation, and deterministic self-hosting matter more than raw speed claims. Benchmark numbers should be recorded only after those gates pass on the same revision.

## Measure Separately

- **Compiler throughput**: time spent turning C source into assembly.
- **Generated-code quality**: runtime behavior of binaries produced by tinyc.
- **Self-host cost**: time and output stability for stage1 and stage2 compiler builds.

Keeping these separate avoids hiding code-generation regressions behind faster compiler execution, or hiding frontend/parser regressions behind a single synthetic benchmark.

## Suggested Commands

```bash
make check
make selfhost LDFLAGS=-Wl,-no_uuid
/usr/bin/time -p ./tools/tinyc-driver.sh tests/integration/structs.c -o /tmp/structs.s
```

For generated-code experiments, compile the same input with tinyc and the platform compiler, run both binaries, and record the exact command line, platform, and optimization flags. Do not compare optimized platform-compiler output against unoptimized tinyc output without labeling that tradeoff.
