# Test suite (legacy redirect)

The canonical test suite and documentation are in **`tests/`** (repo root).

- `make tests` — build host suite, kernel-memsafe, and kernel with ktest
- `make health` — full check (kernel-analyze, test-run, memsafe, kernel-memsafe, kernel-tests)
- See **`../tests/README.md`** for details.

This `test/` directory is legacy; the Makefile and CI use only `tests/`.
