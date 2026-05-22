# Release Checklist

Use this checklist before publishing tinyc changes or updating release claims.

## Local Gates

```bash
make lint
make check
make selfhost LDFLAGS=-Wl,-no_uuid
```

## Compiler Review

- Keep README language aligned with the actually supported C subset.
- Add focused unit, integration, ABI, or self-host coverage for behavior changes.
- Confirm generated assembly, object files, binaries, and temporary self-host artifacts are not committed.
- Keep the Makefile-first workflow working; CMake is secondary.

## GitHub Release Readiness

- CI passes on `macos-15-intel`.
- Self-host stage output remains deterministic with `-Wl,-no_uuid`.
- `SECURITY.md` and README still match release scope.
