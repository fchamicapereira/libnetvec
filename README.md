# Libnetvec

## Dependencies

- cmake
- g++ >= 13
- [Ninja](https://ninja-build.org/)

## Building

Use any of the following scripts to automatically build the `build/` directory:

- `build-address-sanitizer.sh`: For building with asan, for debugging purposes. This will generate the slowest performing binary.
- `build-debug.sh`: For debug builds. Useful to pair with a debugger (e.g. gdb).
- `build-release.sh`: Most performant binaries.
- `build-profiling.sh`: Useful to profile the binary.

All generated binaries, both from `tests/` and `bench/` will be located in `build/bin`.

Example of a command to run the tests and benchmarks for the map data structure:

```
build/> ninja && ./bin/test-mapvec && ./bin/bench-map
```