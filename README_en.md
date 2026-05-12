[中文版](README.md) | English

# QTrace

QBDI-based Android arm64 tracing tool for real devices, built with Android Studio / Gradle.

## Features

- arm64 instruction tracing with memory read/write logging
- target function hook that starts QBDI tracing on hit
- optional JNI trace and libc trace
- runtime JSON configuration, no need to rebuild `libnativelib.so` for every target
- custom QBDI hooks, current built-in handler: `base64`

## Runtime Model

This version no longer relies on a hardcoded `config()` in `native_main.cpp`.

- `init_main()` only initializes the runtime and does not start tracing
- `libnativelib.so` exports 4 functions so you can control tracing from your own JS
- the bundled [inject.js](/Users/lym/Desktop/trace-tools/QTrace/inject.js) does not expose `rpc.exports`
- `inject.js` does the following automatically after the target library is loaded:
  - `dlopen("/data/local/tmp/libnativelib.so")`
  - call `qtrace_init()`
  - call `qtrace_apply_config(json)`
  - call `qtrace_start_trace()`

## Exported Functions

- `int qtrace_init()`
  - initialize shadowhook runtime
- `int qtrace_apply_config(const char* json)`
  - load runtime JSON config
- `int qtrace_start_trace()`
  - install the hook and wait for the target function to be hit
- `const char* qtrace_last_error()`
  - get the latest error message

Notes:

- call `qtrace_apply_config()` before `qtrace_start_trace()`
- once tracing has started, updating config in the same process is rejected
- `hook_argc` currently supports only `0` to `4`

## JSON Config

Example:

```json
{
  "target_lib": "libtiny.so",
  "target_offset": "0x173148",
  "hook_argc": 4,
  "buffer_size": "0x10000000",
  "debug_insn": false,
  "enable_jni_trace": true,
  "enable_libc_trace": true,
  "filter": {
    "arg_index": 2,
    "op": "eq",
    "value": "0x975dbf9a"
  },
  "custom_hooks": [
    {
      "offset": "0x71DD54",
      "handler": "base64"
    }
  ]
}
```

Fields:

- `target_lib`: required, target library name
- `target_offset`: required, function offset, decimal or `0x...`
- `hook_argc`: required, argument count, currently `0-4`
- `buffer_size`: optional, QBDI trace buffer size
- `debug_insn`: optional, enable more verbose instruction logging
- `enable_jni_trace`: optional, register built-in JNI trace hooks
- `enable_libc_trace`: optional, register built-in libc trace hooks
- `filter`: optional, trace only when argument condition matches
- `custom_hooks`: optional, extra QBDI hooks

`filter`:

- `arg_index`: zero-based argument index
- `op`: `eq` / `ne` / `gt` / `ge` / `lt` / `le`
- `value`: decimal or `0x...`

`custom_hooks`:

- `offset`: offset relative to `target_lib` base
- `handler`: currently only `base64`

## Build

1. Unzip `nativelib/src/main/cpp/qbdi-arm64/lib/libQBDI.zip` to get `libQBDI.a`

2. Put it at:

```text
nativelib/src/main/cpp/qbdi-arm64/lib/libQBDI.a
```

Or download the Android aarch64 build from:

[QBDI Releases](https://github.com/QBDI/QBDI/releases/)

3. Build:

```bash
./gradlew :nativelib:assembleDebug --no-daemon
```

Output:

```text
nativelib/build/intermediates/stripped_native_libs/debug/out/lib/arm64-v8a/libnativelib.so
```

## Usage

### 1. Prepare the device

Root is required. It is recommended to disable SELinux enforcement first:

```bash
adb shell 'su -c setenforce 0'
```

### 2. Push both the target library and QTrace library

The current implementation resolves target library info from `/data/local/tmp/`, so the target library must also be pushed there:

```bash
adb push libnativelib.so /data/local/tmp/libnativelib.so
adb push <target.so> /data/local/tmp/<target.so>
```

### 3. Edit `inject.js`

Update `DEFAULT_TRACE_CONFIG` at the top of [inject.js](/Users/lym/Desktop/trace-tools/QTrace/inject.js).

This script does not expose `rpc.exports`; it auto-initializes and starts tracing after the target library is detected.

### 4. Inject

You can use either Frida `spawn` or `attach`.

`spawn`:

```bash
frida -U -f <package> -l inject.js
```

`attach`:

```bash
frida -U -N <package> -l inject.js
```

You may also write your own JS and call the exported `qtrace_*` functions manually.

## Output Path

Trace logs are written to:

```text
/storage/emulated/0/Android/data/<package>/files/trace_logs/
```

Example file name:

```text
qbdi_20260512_125636_libtiny.so+0x45b6d0_1.txt
```

## Verified Example

Verified on a real device with:

- device: Xiaomi Mi 10 Pro
- package: `com.xingin.xhs`
- target library: `libtiny.so`
- target offset: `0x45B6D0`
- injection mode: `spawn`

Observed results:

- `libnativelib.so` was injected successfully
- shadowhook installed the target hook successfully
- on hit, logs showed `start trace`, `trace begin`, and `trace completed successfully`
- multiple trace files were generated under `trace_logs/`

Sample log:

```text
QTrace  : start trace:0x70d8c926d0
QTrace  : trace begin
QTrace  : trace completed successfully /storage/emulated/0/Android/data/com.xingin.xhs/files/trace_logs/qbdi_20260512_125636_libtiny.so+0x45b6d0_1.txt
```

Sample trace:

```text
0x45b6d0:  sub sp, sp, #0x60
0x45b6d4:  str x30, [sp, #0x20]
mem[w]: 0x6fe0b928f0 size: 8 value: 0x2a
```

Notes:

- this target kills itself on `attach`
- the verified stable path was `spawn + constructor patch`
- that patch is target-app-specific and is not enabled by default in the repository version of [inject.js](/Users/lym/Desktop/trace-tools/QTrace/inject.js)

During verification, the working approach was to watch `libmsaoaidsec.so` in the linker's `call_constructors` stage and patch `0x1BEC4` to `ret`. The core idea looks like this:

```js
if (soname.includes("libmsaoaidsec.so")) {
  const base = Module.findBaseAddress("libmsaoaidsec.so");
  const target = ptr(base.add(0x1BEC4));
  Memory.patchCode(target, 4, code => {
    const writer = new Arm64Writer(code, { pc: target });
    writer.putRet();
    writer.flush();
  });
}
```

## Troubleshooting

### `target so not found`

The current implementation reads target library metadata from `/data/local/tmp/<target_lib>`. Make sure the target library has been pushed there.

### `trace 已经启动，不能再更新配置`

Tracing has already started in the current process. Restart the target process before applying a new config.

### `hook target fail`

Check:

- whether `target_offset` is correct
- whether `hook_argc` matches the target function
- whether the pushed target library matches the real runtime version

### Process exits on `attach`

Try `spawn` first. Some targets trigger anti-debug or anti-Frida logic when attached to an already running process.
