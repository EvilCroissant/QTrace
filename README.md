中文 | [English](README_en.md)

# QTrace

基于 QBDI 的 Android arm64 真机 trace 工具，使用 Android Studio / Gradle 构建。

## Features

- arm64 指令 trace，支持内存读写记录
- 目标函数入口 hook + 命中后启动 QBDI trace
- 可选的 JNI trace 与 libc trace
- 运行时 JSON 配置，不需要每次改目标都重新编译 `libnativelib.so`
- 自定义 QBDI hook，当前内置 handler: `base64`

## 当前运行模型

当前版本已经不再依赖 `native_main.cpp` 里的硬编码 `config()`。

- `init_main()` 只做初始化，不自动开始 trace
- `libnativelib.so` 导出 4 个函数，供你在自己的 JS 里自由调用
- 项目自带的 [inject.js](/Users/lym/Desktop/trace-tools/QTrace/inject.js) 不导出 `rpc.exports`
- `inject.js` 的行为是：
  - 监听目标 so 加载
  - `dlopen("/data/local/tmp/libnativelib.so")`
  - 调用 `qtrace_init()`
  - 调用 `qtrace_apply_config(json)`
  - 调用 `qtrace_start_trace()`

## 导出函数

`libnativelib.so` 当前导出以下接口：

- `int qtrace_init()`
  - 初始化 shadowhook 运行时
- `int qtrace_apply_config(const char* json)`
  - 加载运行时 JSON 配置
- `int qtrace_start_trace()`
  - 按当前配置安装 hook 并等待命中后开始 trace
- `const char* qtrace_last_error()`
  - 获取最近一次错误信息

注意：

- `qtrace_apply_config()` 必须在 `qtrace_start_trace()` 之前调用
- trace 启动后再次更新配置会返回错误
- 当前 `hook_argc` 仅支持 `0` 到 `4`

## JSON 配置格式

示例：

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

字段说明：

- `target_lib`
  - 必填，目标 so 文件名
- `target_offset`
  - 必填，目标函数偏移，支持十进制或 `0x...`
- `hook_argc`
  - 必填，目标函数参数个数，当前支持 `0-4`
- `buffer_size`
  - 可选，QBDI trace 缓冲区大小
- `debug_insn`
  - 可选，是否开启更详细的指令调试输出
- `enable_jni_trace`
  - 可选，是否注册内置 JNI trace
- `enable_libc_trace`
  - 可选，是否注册内置 libc trace
- `filter`
  - 可选，参数过滤器，仅参数满足条件时才进入 trace
- `custom_hooks`
  - 可选，额外的 QBDI hook 列表

`filter` 说明：

- `arg_index`：从 `0` 开始计数
- `op`：当前支持 `eq` / `ne` / `gt` / `ge` / `lt` / `le`
- `value`：支持十进制或 `0x...`

`custom_hooks` 说明：

- `offset`：相对 `target_lib` 基址的偏移
- `handler`：当前仅内置 `base64`

## 构建

1. 解压 `nativelib/src/main/cpp/qbdi-arm64/lib/libQBDI.zip`，得到 `libQBDI.a`

2. 将 `libQBDI.a` 放在：

```text
nativelib/src/main/cpp/qbdi-arm64/lib/libQBDI.a
```

也可以从 QBDI 官方发布页下载 Android aarch64 版本：

[QBDI Releases](https://github.com/QBDI/QBDI/releases/)

3. 构建：

```bash
./gradlew :nativelib:assembleDebug --no-daemon
```

产物路径：

```text
nativelib/build/intermediates/stripped_native_libs/debug/out/lib/arm64-v8a/libnativelib.so
```

## 使用方式

### 1. 设备准备

需要 root，并建议先关闭 SELinux 拦截：

```bash
adb shell 'su -c setenforce 0'
```

### 2. 推送 QTrace so

当前版本只需要把 `libnativelib.so` 推到 `/data/local/tmp/`。目标 so 不需要再额外复制一份到 tmp，QTrace 会直接从进程里已加载模块的地址范围定位目标库。

```bash
adb push libnativelib.so /data/local/tmp/libnativelib.so
```

### 3. 配置 `inject.js`

修改 [inject.js](/Users/lym/Desktop/trace-tools/QTrace/inject.js) 顶部的 `DEFAULT_TRACE_CONFIG`。

这个脚本不暴露 `rpc.exports`，默认就是“目标 so 加载后自动 init + apply_config + start_trace”。

### 4. 注入

可以用 Frida 的 `spawn` 或 `attach`。

`spawn` 示例：

```bash
frida -U -f <package> -l inject.js
```

`attach` 示例：

```bash
frida -U -N <package> -l inject.js
```

如果你不想使用项目自带脚本，也可以在你自己的 JS 中手动调用 `qtrace_*` 导出函数。

## 输出位置

trace 日志默认输出到应用私有目录：

```text
/storage/emulated/0/Android/data/<package>/files/trace_logs/
```

文件名示例：

```text
qbdi_20260512_125636_libtiny.so+0x45b6d0_1.txt
```

## 已验证示例

已在真机上验证以下场景：

- 设备：Xiaomi Mi 10 Pro
- 包名：`com.xingin.xhs`
- 目标 so：`libtiny.so`
- 目标偏移：`0x45B6D0`
- 注入方式：`spawn`

关键结果：

- `libnativelib.so` 正常注入
- shadowhook 正常安装目标 hook
- 命中后出现 `start trace`、`trace begin`、`trace completed successfully`
- `trace_logs/` 下成功生成多份 trace 文件

示例日志片段：

```text
QTrace  : start trace:0x70d8c926d0
QTrace  : trace begin
QTrace  : trace completed successfully /storage/emulated/0/Android/data/com.xingin.xhs/files/trace_logs/qbdi_20260512_125636_libtiny.so+0x45b6d0_1.txt
```

示例 trace 内容：

```text
0x45b6d0:  sub sp, sp, #0x60
0x45b6d4:  str x30, [sp, #0x20]
mem[w]: 0x6fe0b928f0 size: 8 value: 0x2a
```

说明：

- 这个目标在 `attach` 路径下会触发进程自杀
- 对该目标，实际验证通过的是 `spawn + constructor patch` 方案
- 该 patch 属于目标应用相关逻辑，不在仓库正式版 [inject.js](/Users/lym/Desktop/trace-tools/QTrace/inject.js) 中默认启用

验证时使用的是“在 linker 的 `call_constructors` 阶段监听 `libmsaoaidsec.so`，然后对 `0x1BEC4` 下 `ret` patch” 这类目标相关逻辑，核心思路如下：

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

## 常见问题

### `未找到目标 so`

当前实现要求目标 so 已经被目标进程加载。请确认：

- `target_lib` 名称和运行时已加载模块名一致
- 目标函数所在 so 的确已经在当前时刻完成加载

### `trace 已经启动，不能再更新配置`

说明当前进程里已经执行过 `qtrace_start_trace()`。如果要换配置，请重启目标进程。

### `hook target fail`

通常需要检查：

- `target_offset` 是否正确
- `hook_argc` 是否匹配目标函数
- 目标 so 是否和运行时实际版本一致

### `attach` 时进程退出

优先尝试 `spawn`。部分应用会在 attach 时触发反调试或反 Frida 逻辑，需要额外做目标应用相关补丁。
