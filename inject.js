const DEFAULT_TRACE_CONFIG = {
  target_lib: "libtiny.so",
  target_offset: "0x173148",
  hook_argc: 4,
  buffer_size: "0x10000000",
  debug_insn: false,
  enable_jni_trace: true,
  enable_libc_trace: true,
  filter: {
    arg_index: 2,
    op: "eq",
    value: "0x975dbf9a"
  },
  custom_hooks: [
    {
      offset: "0x71DD54",
      handler: "base64"
    }
  ]
};

const TRACE_MODULE_NAME = "libnativelib.so";
let pendingTraceConfig = JSON.parse(JSON.stringify(DEFAULT_TRACE_CONFIG));
let traceModuleInjected = false;
let traceStarted = false;

function getTraceExport(name, retType, argTypes) {
  const address = Module.findExportByName(TRACE_MODULE_NAME, name);
  if (address === null) {
    throw new Error(`trace export not found: ${name}`);
  }
  return new NativeFunction(address, retType, argTypes);
}

function getLastTraceError() {
  const getLastError = getTraceExport("qtrace_last_error", "pointer", []);
  const errorPointer = getLastError();
  return errorPointer.isNull() ? "unknown error" : errorPointer.readCString();
}

function callTraceExport(name, retType, argTypes, args) {
  const fn = getTraceExport(name, retType, argTypes);
  const result = fn.apply(null, args);
  if (retType === "int" && result !== 0) {
    throw new Error(`${name} failed: ${getLastTraceError()}`);
  }
  return result;
}

function isTraceModuleLoaded() {
  return Process.findModuleByName(TRACE_MODULE_NAME) !== null;
}

function setPendingTraceConfig(configOrJson) {
  pendingTraceConfig = typeof configOrJson === "string"
    ? JSON.parse(configOrJson)
    : JSON.parse(JSON.stringify(configOrJson));
  console.log(`pending trace config updated for ${pendingTraceConfig.target_lib}`);
}

function applyTraceConfig(configOrJson) {
  if (!isTraceModuleLoaded()) {
    throw new Error(`${TRACE_MODULE_NAME} is not loaded yet`);
  }
  if (configOrJson !== undefined) {
    setPendingTraceConfig(configOrJson);
  }
  const json = JSON.stringify(pendingTraceConfig);
  const jsonPointer = Memory.allocUtf8String(json);
  callTraceExport("qtrace_apply_config", "int", ["pointer"], [jsonPointer]);
  console.log(`trace config applied: ${json}`);
}

function startTrace() {
  if (!isTraceModuleLoaded()) {
    throw new Error(`${TRACE_MODULE_NAME} is not loaded yet`);
  }
  callTraceExport("qtrace_start_trace", "int", [], []);
  console.log("trace started");
}

function configureAndStartTrace(configOrJson) {
  if (traceStarted) {
    console.log("trace already started");
    return;
  }
  applyTraceConfig(configOrJson);
  startTrace();
  traceStarted = true;
}

function initializeTraceRuntime() {
  if (!isTraceModuleLoaded()) {
    throw new Error(`${TRACE_MODULE_NAME} is not loaded yet`);
  }
  callTraceExport("qtrace_init", "int", [], []);
  console.log("trace runtime initialized");
}

function inject() {
  if (traceModuleInjected && isTraceModuleLoaded()) {
    return;
  }
  const dlopenPointer = Module.findExportByName(null, "dlopen");
  const dlopen = new NativeFunction(dlopenPointer, "pointer", ["pointer", "int"]);
  const soPath = "/data/local/tmp/libnativelib.so";
  const soPathPointer = Memory.allocUtf8String(soPath);
  console.log("inject libnativelib.so");
  const handle = dlopen(soPathPointer, 2);
  if (handle.isNull()) {
    throw new Error("dlopen libnativelib.so failed");
  }
  traceModuleInjected = true;
}

function injectAndStartTrace() {
  inject();
  initializeTraceRuntime();
  configureAndStartTrace();
}

function matchesTargetLib(soname) {
  if (!soname || !pendingTraceConfig.target_lib) {
    return false;
  }
  return soname.indexOf(pendingTraceConfig.target_lib) !== -1;
}

function attachDlopenHook(symbolName) {
  const symbol = Module.findExportByName(null, symbolName);
  if (symbol === null) {
    return;
  }
  Interceptor.attach(symbol, {
    onEnter(args) {
      this.soname = args[0].readCString();
      this.shouldInject = matchesTargetLib(this.soname);
    },
    onLeave() {
      if (this.shouldInject) {
        injectAndStartTrace();
      }
    }
  });
}

function hookLoadLibrary() {
  attachDlopenHook("__loader_dlopen");
  attachDlopenHook("android_dlopen_ext");

  if (Process.findModuleByName(pendingTraceConfig.target_lib) !== null) {
    injectAndStartTrace();
  }
}

setImmediate(hookLoadLibrary);
