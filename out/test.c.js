// Support for growable heap + pthreads, where the buffer may change, so JS views
// must be updated.
function GROWABLE_HEAP_I8() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAP8;
}
function GROWABLE_HEAP_U8() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAPU8;
}
function GROWABLE_HEAP_I16() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAP16;
}
function GROWABLE_HEAP_U16() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAPU16;
}
function GROWABLE_HEAP_I32() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAP32;
}
function GROWABLE_HEAP_U32() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAPU32;
}
function GROWABLE_HEAP_F32() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAPF32;
}
function GROWABLE_HEAP_F64() {
  if (wasmMemory.buffer != HEAP8.buffer) {
    updateMemoryViews();
  }
  return HEAPF64;
}

var Module = typeof Module != "undefined" ? Module : {};

var moduleOverrides = Object.assign({}, Module);

var arguments_ = [];

var thisProgram = "./this.program";

var quit_ = (status, toThrow) => {
 throw toThrow;
};

var ENVIRONMENT_IS_WEB = typeof window == "object";

var ENVIRONMENT_IS_WORKER = typeof importScripts == "function";

var ENVIRONMENT_IS_NODE = typeof process == "object" && typeof process.versions == "object" && typeof process.versions.node == "string";

var ENVIRONMENT_IS_SHELL = !ENVIRONMENT_IS_WEB && !ENVIRONMENT_IS_NODE && !ENVIRONMENT_IS_WORKER;

if (Module["ENVIRONMENT"]) {
 throw new Error("Module.ENVIRONMENT has been deprecated. To force the environment, use the ENVIRONMENT compile-time option (for example, -sENVIRONMENT=web or -sENVIRONMENT=node)");
}

var ENVIRONMENT_IS_PTHREAD = Module["ENVIRONMENT_IS_PTHREAD"] || false;

var _scriptDir = typeof document != "undefined" && document.currentScript ? document.currentScript.src : undefined;

if (ENVIRONMENT_IS_WORKER) {
 _scriptDir = self.location.href;
} else if (ENVIRONMENT_IS_NODE) {
 _scriptDir = __filename;
}

var scriptDirectory = "";

function locateFile(path) {
 if (Module["locateFile"]) {
  return Module["locateFile"](path, scriptDirectory);
 }
 return scriptDirectory + path;
}

var read_, readAsync, readBinary, setWindowTitle;

if (ENVIRONMENT_IS_NODE) {
 if (typeof process == "undefined" || !process.release || process.release.name !== "node") throw new Error("not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)");
 var nodeVersion = process.versions.node;
 var numericVersion = nodeVersion.split(".").slice(0, 3);
 numericVersion = numericVersion[0] * 1e4 + numericVersion[1] * 100 + numericVersion[2].split("-")[0] * 1;
 var minVersion = 101900;
 if (numericVersion < 101900) {
  throw new Error("This emscripten-generated code requires node v10.19.19.0 (detected v" + nodeVersion + ")");
 }
 var fs = require("fs");
 var nodePath = require("path");
 if (ENVIRONMENT_IS_WORKER) {
  scriptDirectory = nodePath.dirname(scriptDirectory) + "/";
 } else {
  scriptDirectory = __dirname + "/";
 }
 read_ = (filename, binary) => {
  filename = isFileURI(filename) ? new URL(filename) : nodePath.normalize(filename);
  return fs.readFileSync(filename, binary ? undefined : "utf8");
 };
 readBinary = filename => {
  var ret = read_(filename, true);
  if (!ret.buffer) {
   ret = new Uint8Array(ret);
  }
  assert(ret.buffer);
  return ret;
 };
 readAsync = (filename, onload, onerror, binary = true) => {
  filename = isFileURI(filename) ? new URL(filename) : nodePath.normalize(filename);
  fs.readFile(filename, binary ? undefined : "utf8", (err, data) => {
   if (err) onerror(err); else onload(binary ? data.buffer : data);
  });
 };
 if (!Module["thisProgram"] && process.argv.length > 1) {
  thisProgram = process.argv[1].replace(/\\/g, "/");
 }
 arguments_ = process.argv.slice(2);
 if (typeof module != "undefined") {
  module["exports"] = Module;
 }
 process.on("uncaughtException", ex => {
  if (ex !== "unwind" && !(ex instanceof ExitStatus) && !(ex.context instanceof ExitStatus)) {
   throw ex;
  }
 });
 var nodeMajor = process.versions.node.split(".")[0];
 if (nodeMajor < 15) {
  process.on("unhandledRejection", reason => {
   throw reason;
  });
 }
 quit_ = (status, toThrow) => {
  process.exitCode = status;
  throw toThrow;
 };
 Module["inspect"] = () => "[Emscripten Module object]";
 let nodeWorkerThreads;
 try {
  nodeWorkerThreads = require("worker_threads");
 } catch (e) {
  console.error('The "worker_threads" module is not supported in this node.js build - perhaps a newer version is needed?');
  throw e;
 }
 global.Worker = nodeWorkerThreads.Worker;
} else if (ENVIRONMENT_IS_SHELL) {
 if (typeof process == "object" && typeof require === "function" || typeof window == "object" || typeof importScripts == "function") throw new Error("not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)");
 if (typeof read != "undefined") {
  read_ = f => {
   return read(f);
  };
 }
 readBinary = f => {
  let data;
  if (typeof readbuffer == "function") {
   return new Uint8Array(readbuffer(f));
  }
  data = read(f, "binary");
  assert(typeof data == "object");
  return data;
 };
 readAsync = (f, onload, onerror) => {
  setTimeout(() => onload(readBinary(f)), 0);
 };
 if (typeof clearTimeout == "undefined") {
  globalThis.clearTimeout = id => {};
 }
 if (typeof scriptArgs != "undefined") {
  arguments_ = scriptArgs;
 } else if (typeof arguments != "undefined") {
  arguments_ = arguments;
 }
 if (typeof quit == "function") {
  quit_ = (status, toThrow) => {
   setTimeout(() => {
    if (!(toThrow instanceof ExitStatus)) {
     let toLog = toThrow;
     if (toThrow && typeof toThrow == "object" && toThrow.stack) {
      toLog = [ toThrow, toThrow.stack ];
     }
     err(`exiting due to exception: ${toLog}`);
    }
    quit(status);
   });
   throw toThrow;
  };
 }
 if (typeof print != "undefined") {
  if (typeof console == "undefined") console = {};
  console.log = print;
  console.warn = console.error = typeof printErr != "undefined" ? printErr : print;
 }
} else if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
 if (ENVIRONMENT_IS_WORKER) {
  scriptDirectory = self.location.href;
 } else if (typeof document != "undefined" && document.currentScript) {
  scriptDirectory = document.currentScript.src;
 }
 if (scriptDirectory.indexOf("blob:") !== 0) {
  scriptDirectory = scriptDirectory.substr(0, scriptDirectory.replace(/[?#].*/, "").lastIndexOf("/") + 1);
 } else {
  scriptDirectory = "";
 }
 if (!(typeof window == "object" || typeof importScripts == "function")) throw new Error("not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)");
 if (!ENVIRONMENT_IS_NODE) {
  read_ = url => {
   var xhr = new XMLHttpRequest();
   xhr.open("GET", url, false);
   xhr.send(null);
   return xhr.responseText;
  };
  if (ENVIRONMENT_IS_WORKER) {
   readBinary = url => {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", url, false);
    xhr.responseType = "arraybuffer";
    xhr.send(null);
    return new Uint8Array(xhr.response);
   };
  }
  readAsync = (url, onload, onerror) => {
   var xhr = new XMLHttpRequest();
   xhr.open("GET", url, true);
   xhr.responseType = "arraybuffer";
   xhr.onload = () => {
    if (xhr.status == 200 || xhr.status == 0 && xhr.response) {
     onload(xhr.response);
     return;
    }
    onerror();
   };
   xhr.onerror = onerror;
   xhr.send(null);
  };
 }
 setWindowTitle = title => document.title = title;
} else {
 throw new Error("environment detection error");
}

if (ENVIRONMENT_IS_NODE) {
 if (typeof performance == "undefined") {
  global.performance = require("perf_hooks").performance;
 }
}

var defaultPrint = console.log.bind(console);

var defaultPrintErr = console.error.bind(console);

if (ENVIRONMENT_IS_NODE) {
 defaultPrint = (...args) => fs.writeSync(1, args.join(" ") + "\n");
 defaultPrintErr = (...args) => fs.writeSync(2, args.join(" ") + "\n");
}

var out = Module["print"] || defaultPrint;

var err = Module["printErr"] || defaultPrintErr;

Object.assign(Module, moduleOverrides);

moduleOverrides = null;

checkIncomingModuleAPI();

if (Module["arguments"]) arguments_ = Module["arguments"];

legacyModuleProp("arguments", "arguments_");

if (Module["thisProgram"]) thisProgram = Module["thisProgram"];

legacyModuleProp("thisProgram", "thisProgram");

if (Module["quit"]) quit_ = Module["quit"];

legacyModuleProp("quit", "quit_");

assert(typeof Module["memoryInitializerPrefixURL"] == "undefined", "Module.memoryInitializerPrefixURL option was removed, use Module.locateFile instead");

assert(typeof Module["pthreadMainPrefixURL"] == "undefined", "Module.pthreadMainPrefixURL option was removed, use Module.locateFile instead");

assert(typeof Module["cdInitializerPrefixURL"] == "undefined", "Module.cdInitializerPrefixURL option was removed, use Module.locateFile instead");

assert(typeof Module["filePackagePrefixURL"] == "undefined", "Module.filePackagePrefixURL option was removed, use Module.locateFile instead");

assert(typeof Module["read"] == "undefined", "Module.read option was removed (modify read_ in JS)");

assert(typeof Module["readAsync"] == "undefined", "Module.readAsync option was removed (modify readAsync in JS)");

assert(typeof Module["readBinary"] == "undefined", "Module.readBinary option was removed (modify readBinary in JS)");

assert(typeof Module["setWindowTitle"] == "undefined", "Module.setWindowTitle option was removed (modify setWindowTitle in JS)");

assert(typeof Module["TOTAL_MEMORY"] == "undefined", "Module.TOTAL_MEMORY has been renamed Module.INITIAL_MEMORY");

legacyModuleProp("read", "read_");

legacyModuleProp("readAsync", "readAsync");

legacyModuleProp("readBinary", "readBinary");

legacyModuleProp("setWindowTitle", "setWindowTitle");

var IDBFS = "IDBFS is no longer included by default; build with -lidbfs.js";

var PROXYFS = "PROXYFS is no longer included by default; build with -lproxyfs.js";

var WORKERFS = "WORKERFS is no longer included by default; build with -lworkerfs.js";

var NODEFS = "NODEFS is no longer included by default; build with -lnodefs.js";

assert(ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER || ENVIRONMENT_IS_NODE, "Pthreads do not work in this environment yet (need Web Workers, or an alternative to them)");

assert(!ENVIRONMENT_IS_SHELL, "shell environment detected but not enabled at build time.  Add 'shell' to `-sENVIRONMENT` to enable.");

var wasmBinary;

if (Module["wasmBinary"]) wasmBinary = Module["wasmBinary"];

legacyModuleProp("wasmBinary", "wasmBinary");

var noExitRuntime = Module["noExitRuntime"] || true;

legacyModuleProp("noExitRuntime", "noExitRuntime");

if (typeof WebAssembly != "object") {
 abort("no native wasm support detected");
}

var wasmMemory;

var wasmModule;

var ABORT = false;

var EXITSTATUS;

function assert(condition, text) {
 if (!condition) {
  abort("Assertion failed" + (text ? ": " + text : ""));
 }
}

function _free() {
 abort("free() called but not included in the build - add '_free' to EXPORTED_FUNCTIONS");
}

var HEAP, HEAP8, HEAPU8, HEAP16, HEAPU16, HEAP32, HEAPU32, HEAPF32, HEAPF64;

function updateMemoryViews() {
 var b = wasmMemory.buffer;
 Module["HEAP8"] = HEAP8 = new Int8Array(b);
 Module["HEAP16"] = HEAP16 = new Int16Array(b);
 Module["HEAP32"] = HEAP32 = new Int32Array(b);
 Module["HEAPU8"] = HEAPU8 = new Uint8Array(b);
 Module["HEAPU16"] = HEAPU16 = new Uint16Array(b);
 Module["HEAPU32"] = HEAPU32 = new Uint32Array(b);
 Module["HEAPF32"] = HEAPF32 = new Float32Array(b);
 Module["HEAPF64"] = HEAPF64 = new Float64Array(b);
}

assert(!Module["STACK_SIZE"], "STACK_SIZE can no longer be set at runtime.  Use -sSTACK_SIZE at link time");

assert(typeof Int32Array != "undefined" && typeof Float64Array !== "undefined" && Int32Array.prototype.subarray != undefined && Int32Array.prototype.set != undefined, "JS engine does not provide full typed array support");

var INITIAL_MEMORY = Module["INITIAL_MEMORY"] || 134217728;

legacyModuleProp("INITIAL_MEMORY", "INITIAL_MEMORY");

assert(INITIAL_MEMORY >= 5242880, "INITIAL_MEMORY should be larger than STACK_SIZE, was " + INITIAL_MEMORY + "! (STACK_SIZE=" + 5242880 + ")");

if (ENVIRONMENT_IS_PTHREAD) {
 wasmMemory = Module["wasmMemory"];
} else {
 if (Module["wasmMemory"]) {
  wasmMemory = Module["wasmMemory"];
 } else {
  wasmMemory = new WebAssembly.Memory({
   "initial": INITIAL_MEMORY / 65536,
   "maximum": 2147483648 / 65536,
   "shared": true
  });
  if (!(wasmMemory.buffer instanceof SharedArrayBuffer)) {
   err("requested a shared WebAssembly.Memory but the returned buffer is not a SharedArrayBuffer, indicating that while the browser has SharedArrayBuffer it does not have WebAssembly threads support - you may need to set a flag");
   if (ENVIRONMENT_IS_NODE) {
    err("(on node you may need: --experimental-wasm-threads --experimental-wasm-bulk-memory and/or recent version)");
   }
   throw Error("bad memory");
  }
 }
}

updateMemoryViews();

INITIAL_MEMORY = wasmMemory.buffer.byteLength;

assert(INITIAL_MEMORY % 65536 === 0);

var wasmTable;

function writeStackCookie() {
 var max = _emscripten_stack_get_end();
 assert((max & 3) == 0);
 if (max == 0) {
  max += 4;
 }
 GROWABLE_HEAP_U32()[max >> 2] = 34821223;
 GROWABLE_HEAP_U32()[max + 4 >> 2] = 2310721022;
 GROWABLE_HEAP_U32()[0] = 1668509029;
}

function checkStackCookie() {
 if (ABORT) return;
 var max = _emscripten_stack_get_end();
 if (max == 0) {
  max += 4;
 }
 var cookie1 = GROWABLE_HEAP_U32()[max >> 2];
 var cookie2 = GROWABLE_HEAP_U32()[max + 4 >> 2];
 if (cookie1 != 34821223 || cookie2 != 2310721022) {
  abort("Stack overflow! Stack cookie has been overwritten at " + ptrToString(max) + ", expected hex dwords 0x89BACDFE and 0x2135467, but received " + ptrToString(cookie2) + " " + ptrToString(cookie1));
 }
 if (GROWABLE_HEAP_U32()[0] !== 1668509029) {
  abort("Runtime error: The application has corrupted its heap memory area (address zero)!");
 }
}

(function() {
 var h16 = new Int16Array(1);
 var h8 = new Int8Array(h16.buffer);
 h16[0] = 25459;
 if (h8[0] !== 115 || h8[1] !== 99) throw "Runtime error: expected the system to be little-endian! (Run with -sSUPPORT_BIG_ENDIAN to bypass)";
})();

var __ATPRERUN__ = [];

var __ATINIT__ = [];

var __ATEXIT__ = [];

var __ATPOSTRUN__ = [];

var runtimeInitialized = false;

var runtimeKeepaliveCounter = 0;

function keepRuntimeAlive() {
 return noExitRuntime || runtimeKeepaliveCounter > 0;
}

function preRun() {
 assert(!ENVIRONMENT_IS_PTHREAD);
 if (Module["preRun"]) {
  if (typeof Module["preRun"] == "function") Module["preRun"] = [ Module["preRun"] ];
  while (Module["preRun"].length) {
   addOnPreRun(Module["preRun"].shift());
  }
 }
 callRuntimeCallbacks(__ATPRERUN__);
}

function initRuntime() {
 assert(!runtimeInitialized);
 runtimeInitialized = true;
 if (ENVIRONMENT_IS_PTHREAD) return;
 checkStackCookie();
 callRuntimeCallbacks(__ATINIT__);
}

function postRun() {
 checkStackCookie();
 if (ENVIRONMENT_IS_PTHREAD) return;
 if (Module["postRun"]) {
  if (typeof Module["postRun"] == "function") Module["postRun"] = [ Module["postRun"] ];
  while (Module["postRun"].length) {
   addOnPostRun(Module["postRun"].shift());
  }
 }
 callRuntimeCallbacks(__ATPOSTRUN__);
}

function addOnPreRun(cb) {
 __ATPRERUN__.unshift(cb);
}

function addOnInit(cb) {
 __ATINIT__.unshift(cb);
}

function addOnExit(cb) {}

function addOnPostRun(cb) {
 __ATPOSTRUN__.unshift(cb);
}

assert(Math.imul, "This browser does not support Math.imul(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill");

assert(Math.fround, "This browser does not support Math.fround(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill");

assert(Math.clz32, "This browser does not support Math.clz32(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill");

assert(Math.trunc, "This browser does not support Math.trunc(), build with LEGACY_VM_SUPPORT or POLYFILL_OLD_MATH_FUNCTIONS to add in a polyfill");

var runDependencies = 0;

var runDependencyWatcher = null;

var dependenciesFulfilled = null;

var runDependencyTracking = {};

function getUniqueRunDependency(id) {
 var orig = id;
 while (1) {
  if (!runDependencyTracking[id]) return id;
  id = orig + Math.random();
 }
}

function addRunDependency(id) {
 runDependencies++;
 if (Module["monitorRunDependencies"]) {
  Module["monitorRunDependencies"](runDependencies);
 }
 if (id) {
  assert(!runDependencyTracking[id]);
  runDependencyTracking[id] = 1;
  if (runDependencyWatcher === null && typeof setInterval != "undefined") {
   runDependencyWatcher = setInterval(() => {
    if (ABORT) {
     clearInterval(runDependencyWatcher);
     runDependencyWatcher = null;
     return;
    }
    var shown = false;
    for (var dep in runDependencyTracking) {
     if (!shown) {
      shown = true;
      err("still waiting on run dependencies:");
     }
     err("dependency: " + dep);
    }
    if (shown) {
     err("(end of list)");
    }
   }, 1e4);
  }
 } else {
  err("warning: run dependency added without ID");
 }
}

function removeRunDependency(id) {
 runDependencies--;
 if (Module["monitorRunDependencies"]) {
  Module["monitorRunDependencies"](runDependencies);
 }
 if (id) {
  assert(runDependencyTracking[id]);
  delete runDependencyTracking[id];
 } else {
  err("warning: run dependency removed without ID");
 }
 if (runDependencies == 0) {
  if (runDependencyWatcher !== null) {
   clearInterval(runDependencyWatcher);
   runDependencyWatcher = null;
  }
  if (dependenciesFulfilled) {
   var callback = dependenciesFulfilled;
   dependenciesFulfilled = null;
   callback();
  }
 }
}

function abort(what) {
 if (Module["onAbort"]) {
  Module["onAbort"](what);
 }
 what = "Aborted(" + what + ")";
 err(what);
 ABORT = true;
 EXITSTATUS = 1;
 var e = new WebAssembly.RuntimeError(what);
 throw e;
}

var FS = {
 error: function() {
  abort("Filesystem support (FS) was not included. The problem is that you are using files from JS, but files were not used from C/C++, so filesystem support was not auto-included. You can force-include filesystem support with -sFORCE_FILESYSTEM");
 },
 init: function() {
  FS.error();
 },
 createDataFile: function() {
  FS.error();
 },
 createPreloadedFile: function() {
  FS.error();
 },
 createLazyFile: function() {
  FS.error();
 },
 open: function() {
  FS.error();
 },
 mkdev: function() {
  FS.error();
 },
 registerDevice: function() {
  FS.error();
 },
 analyzePath: function() {
  FS.error();
 },
 ErrnoError: function ErrnoError() {
  FS.error();
 }
};

Module["FS_createDataFile"] = FS.createDataFile;

Module["FS_createPreloadedFile"] = FS.createPreloadedFile;

var dataURIPrefix = "data:application/octet-stream;base64,";

function isDataURI(filename) {
 return filename.startsWith(dataURIPrefix);
}

function isFileURI(filename) {
 return filename.startsWith("file://");
}

function createExportWrapper(name, fixedasm) {
 return function() {
  var displayName = name;
  var asm = fixedasm;
  if (!fixedasm) {
   asm = Module["asm"];
  }
  assert(runtimeInitialized, "native function `" + displayName + "` called before runtime initialization");
  if (!asm[name]) {
   assert(asm[name], "exported native function `" + displayName + "` not found");
  }
  return asm[name].apply(null, arguments);
 };
}

var wasmBinaryFile;

wasmBinaryFile = "test.c.wasm";

if (!isDataURI(wasmBinaryFile)) {
 wasmBinaryFile = locateFile(wasmBinaryFile);
}

function getBinary(file) {
 try {
  if (file == wasmBinaryFile && wasmBinary) {
   return new Uint8Array(wasmBinary);
  }
  if (readBinary) {
   return readBinary(file);
  }
  throw "both async and sync fetching of the wasm failed";
 } catch (err) {
  abort(err);
 }
}

function getBinaryPromise(binaryFile) {
 if (!wasmBinary && (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER)) {
  if (typeof fetch == "function" && !isFileURI(binaryFile)) {
   return fetch(binaryFile, {
    credentials: "same-origin"
   }).then(response => {
    if (!response["ok"]) {
     throw "failed to load wasm binary file at '" + binaryFile + "'";
    }
    return response["arrayBuffer"]();
   }).catch(() => getBinary(binaryFile));
  } else {
   if (readAsync) {
    return new Promise((resolve, reject) => {
     readAsync(binaryFile, response => resolve(new Uint8Array(response)), reject);
    });
   }
  }
 }
 return Promise.resolve().then(() => getBinary(binaryFile));
}

function instantiateArrayBuffer(binaryFile, imports, receiver) {
 return getBinaryPromise(binaryFile).then(binary => {
  return WebAssembly.instantiate(binary, imports);
 }).then(instance => {
  return instance;
 }).then(receiver, reason => {
  err("failed to asynchronously prepare wasm: " + reason);
  if (isFileURI(wasmBinaryFile)) {
   err("warning: Loading from a file URI (" + wasmBinaryFile + ") is not supported in most browsers. See https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-run-a-local-webserver-for-testing-why-does-my-program-stall-in-downloading-or-preparing");
  }
  abort(reason);
 });
}

function instantiateAsync(binary, binaryFile, imports, callback) {
 if (!binary && typeof WebAssembly.instantiateStreaming == "function" && !isDataURI(binaryFile) && !isFileURI(binaryFile) && !ENVIRONMENT_IS_NODE && typeof fetch == "function") {
  return fetch(binaryFile, {
   credentials: "same-origin"
  }).then(response => {
   var result = WebAssembly.instantiateStreaming(response, imports);
   return result.then(callback, function(reason) {
    err("wasm streaming compile failed: " + reason);
    err("falling back to ArrayBuffer instantiation");
    return instantiateArrayBuffer(binaryFile, imports, callback);
   });
  });
 } else {
  return instantiateArrayBuffer(binaryFile, imports, callback);
 }
}

function createWasm() {
 var info = {
  "env": wasmImports,
  "wasi_snapshot_preview1": wasmImports
 };
 function receiveInstance(instance, module) {
  var exports = instance.exports;
  Module["asm"] = exports;
  registerTLSInit(Module["asm"]["_emscripten_tls_init"]);
  wasmTable = Module["asm"]["__indirect_function_table"];
  assert(wasmTable, "table not found in wasm exports");
  addOnInit(Module["asm"]["__wasm_call_ctors"]);
  wasmModule = module;
  PThread.loadWasmModuleToAllWorkers(() => removeRunDependency("wasm-instantiate"));
  return exports;
 }
 addRunDependency("wasm-instantiate");
 var trueModule = Module;
 function receiveInstantiationResult(result) {
  assert(Module === trueModule, "the Module object should not be replaced during async compilation - perhaps the order of HTML elements is wrong?");
  trueModule = null;
  receiveInstance(result["instance"], result["module"]);
 }
 if (Module["instantiateWasm"]) {
  try {
   return Module["instantiateWasm"](info, receiveInstance);
  } catch (e) {
   err("Module.instantiateWasm callback failed with error: " + e);
   return false;
  }
 }
 instantiateAsync(wasmBinary, wasmBinaryFile, info, receiveInstantiationResult);
 return {};
}

var tempDouble;

var tempI64;

function legacyModuleProp(prop, newName) {
 if (!Object.getOwnPropertyDescriptor(Module, prop)) {
  Object.defineProperty(Module, prop, {
   configurable: true,
   get: function() {
    abort("Module." + prop + " has been replaced with plain " + newName + " (the initial value can be provided on Module, but after startup the value is only looked for on a local variable of that name)");
   }
  });
 }
}

function ignoredModuleProp(prop) {
 if (Object.getOwnPropertyDescriptor(Module, prop)) {
  abort("`Module." + prop + "` was supplied but `" + prop + "` not included in INCOMING_MODULE_JS_API");
 }
}

function isExportedByForceFilesystem(name) {
 return name === "FS_createPath" || name === "FS_createDataFile" || name === "FS_createPreloadedFile" || name === "FS_unlink" || name === "addRunDependency" || name === "FS_createLazyFile" || name === "FS_createDevice" || name === "removeRunDependency";
}

function missingGlobal(sym, msg) {
 if (typeof globalThis !== "undefined") {
  Object.defineProperty(globalThis, sym, {
   configurable: true,
   get: function() {
    warnOnce("`" + sym + "` is not longer defined by emscripten. " + msg);
    return undefined;
   }
  });
 }
}

missingGlobal("buffer", "Please use HEAP8.buffer or wasmMemory.buffer");

function missingLibrarySymbol(sym) {
 if (typeof globalThis !== "undefined" && !Object.getOwnPropertyDescriptor(globalThis, sym)) {
  Object.defineProperty(globalThis, sym, {
   configurable: true,
   get: function() {
    var msg = "`" + sym + "` is a library symbol and not included by default; add it to your library.js __deps or to DEFAULT_LIBRARY_FUNCS_TO_INCLUDE on the command line";
    var librarySymbol = sym;
    if (!librarySymbol.startsWith("_")) {
     librarySymbol = "$" + sym;
    }
    msg += " (e.g. -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=" + librarySymbol + ")";
    if (isExportedByForceFilesystem(sym)) {
     msg += ". Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you";
    }
    warnOnce(msg);
    return undefined;
   }
  });
 }
 unexportedRuntimeSymbol(sym);
}

function unexportedRuntimeSymbol(sym) {
 if (!Object.getOwnPropertyDescriptor(Module, sym)) {
  Object.defineProperty(Module, sym, {
   configurable: true,
   get: function() {
    var msg = "'" + sym + "' was not exported. add it to EXPORTED_RUNTIME_METHODS (see the FAQ)";
    if (isExportedByForceFilesystem(sym)) {
     msg += ". Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you";
    }
    abort(msg);
   }
  });
 }
}

function dbg(text) {
 if (ENVIRONMENT_IS_NODE) {
  fs.writeSync(2, Array.from(arguments).join(" ") + "\n");
 } else console.warn.apply(console, arguments);
}

function ExitStatus(status) {
 this.name = "ExitStatus";
 this.message = "Program terminated with exit(" + status + ")";
 this.status = status;
}

function terminateWorker(worker) {
 worker.terminate();
 worker.onmessage = e => {
  var cmd = e["data"]["cmd"];
  err('received "' + cmd + '" command from terminated worker: ' + worker.workerID);
 };
}

function killThread(pthread_ptr) {
 assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! killThread() can only ever be called from main application thread!");
 assert(pthread_ptr, "Internal Error! Null pthread_ptr in killThread!");
 var worker = PThread.pthreads[pthread_ptr];
 delete PThread.pthreads[pthread_ptr];
 terminateWorker(worker);
 __emscripten_thread_free_data(pthread_ptr);
 PThread.runningWorkers.splice(PThread.runningWorkers.indexOf(worker), 1);
 worker.pthread_ptr = 0;
}

function cancelThread(pthread_ptr) {
 assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! cancelThread() can only ever be called from main application thread!");
 assert(pthread_ptr, "Internal Error! Null pthread_ptr in cancelThread!");
 var worker = PThread.pthreads[pthread_ptr];
 worker.postMessage({
  "cmd": "cancel"
 });
}

function cleanupThread(pthread_ptr) {
 assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! cleanupThread() can only ever be called from main application thread!");
 assert(pthread_ptr, "Internal Error! Null pthread_ptr in cleanupThread!");
 var worker = PThread.pthreads[pthread_ptr];
 assert(worker);
 PThread.returnWorkerToPool(worker);
}

function zeroMemory(address, size) {
 GROWABLE_HEAP_U8().fill(0, address, address + size);
 return address;
}

function spawnThread(threadParams) {
 assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! spawnThread() can only ever be called from main application thread!");
 assert(threadParams.pthread_ptr, "Internal error, no pthread ptr!");
 var worker = PThread.getNewWorker();
 if (!worker) {
  return 6;
 }
 assert(!worker.pthread_ptr, "Internal error!");
 PThread.runningWorkers.push(worker);
 PThread.pthreads[threadParams.pthread_ptr] = worker;
 worker.pthread_ptr = threadParams.pthread_ptr;
 var msg = {
  "cmd": "run",
  "start_routine": threadParams.startRoutine,
  "arg": threadParams.arg,
  "pthread_ptr": threadParams.pthread_ptr
 };
 if (ENVIRONMENT_IS_NODE) {
  worker.unref();
 }
 worker.postMessage(msg, threadParams.transferList);
 return 0;
}

var UTF8Decoder = typeof TextDecoder != "undefined" ? new TextDecoder("utf8") : undefined;

function UTF8ArrayToString(heapOrArray, idx, maxBytesToRead) {
 var endIdx = idx + maxBytesToRead;
 var endPtr = idx;
 while (heapOrArray[endPtr] && !(endPtr >= endIdx)) ++endPtr;
 if (endPtr - idx > 16 && heapOrArray.buffer && UTF8Decoder) {
  return UTF8Decoder.decode(heapOrArray.buffer instanceof SharedArrayBuffer ? heapOrArray.slice(idx, endPtr) : heapOrArray.subarray(idx, endPtr));
 }
 var str = "";
 while (idx < endPtr) {
  var u0 = heapOrArray[idx++];
  if (!(u0 & 128)) {
   str += String.fromCharCode(u0);
   continue;
  }
  var u1 = heapOrArray[idx++] & 63;
  if ((u0 & 224) == 192) {
   str += String.fromCharCode((u0 & 31) << 6 | u1);
   continue;
  }
  var u2 = heapOrArray[idx++] & 63;
  if ((u0 & 240) == 224) {
   u0 = (u0 & 15) << 12 | u1 << 6 | u2;
  } else {
   if ((u0 & 248) != 240) warnOnce("Invalid UTF-8 leading byte " + ptrToString(u0) + " encountered when deserializing a UTF-8 string in wasm memory to a JS string!");
   u0 = (u0 & 7) << 18 | u1 << 12 | u2 << 6 | heapOrArray[idx++] & 63;
  }
  if (u0 < 65536) {
   str += String.fromCharCode(u0);
  } else {
   var ch = u0 - 65536;
   str += String.fromCharCode(55296 | ch >> 10, 56320 | ch & 1023);
  }
 }
 return str;
}

function UTF8ToString(ptr, maxBytesToRead) {
 assert(typeof ptr == "number");
 return ptr ? UTF8ArrayToString(GROWABLE_HEAP_U8(), ptr, maxBytesToRead) : "";
}

var SYSCALLS = {
 varargs: undefined,
 get: function() {
  assert(SYSCALLS.varargs != undefined);
  SYSCALLS.varargs += 4;
  var ret = GROWABLE_HEAP_I32()[SYSCALLS.varargs - 4 >> 2];
  return ret;
 },
 getStr: function(ptr) {
  var ret = UTF8ToString(ptr);
  return ret;
 }
};

function _proc_exit(code) {
 if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(1, 1, code);
 EXITSTATUS = code;
 if (!keepRuntimeAlive()) {
  PThread.terminateAllThreads();
  if (Module["onExit"]) Module["onExit"](code);
  ABORT = true;
 }
 quit_(code, new ExitStatus(code));
}

function exitJS(status, implicit) {
 EXITSTATUS = status;
 checkUnflushedContent();
 if (ENVIRONMENT_IS_PTHREAD) {
  assert(!implicit);
  exitOnMainThread(status);
  throw "unwind";
 }
 if (keepRuntimeAlive() && !implicit) {
  var msg = `program exited (with status: ${status}), but keepRuntimeAlive() is set (counter=${runtimeKeepaliveCounter}) due to an async operation, so halting execution but not exiting the runtime or preventing further async execution (you can use emscripten_force_exit, if you want to force a true shutdown)`;
  err(msg);
 }
 _proc_exit(status);
}

var _exit = exitJS;

function ptrToString(ptr) {
 assert(typeof ptr === "number");
 return "0x" + ptr.toString(16).padStart(8, "0");
}

function handleException(e) {
 if (e instanceof ExitStatus || e == "unwind") {
  return EXITSTATUS;
 }
 checkStackCookie();
 if (e instanceof WebAssembly.RuntimeError) {
  if (_emscripten_stack_get_current() <= 0) {
   err("Stack overflow detected.  You can try increasing -sSTACK_SIZE (currently set to 5242880)");
  }
 }
 quit_(1, e);
}

var PThread = {
 unusedWorkers: [],
 runningWorkers: [],
 tlsInitFunctions: [],
 pthreads: {},
 nextWorkerID: 1,
 debugInit: function() {
  function pthreadLogPrefix() {
   var t = 0;
   if (runtimeInitialized && typeof _pthread_self != "undefined") {
    t = _pthread_self();
   }
   return "w:" + (Module["workerID"] || 0) + ",t:" + ptrToString(t) + ": ";
  }
  var origDbg = dbg;
  dbg = message => origDbg(pthreadLogPrefix() + message);
 },
 init: function() {
  PThread.debugInit();
  if (ENVIRONMENT_IS_PTHREAD) {
   PThread.initWorker();
  } else {
   PThread.initMainThread();
  }
 },
 initMainThread: function() {
  var pthreadPoolSize = navigator.hardwareConcurrency;
  while (pthreadPoolSize--) {
   PThread.allocateUnusedWorker();
  }
 },
 initWorker: function() {
  noExitRuntime = false;
 },
 setExitStatus: function(status) {
  EXITSTATUS = status;
 },
 terminateAllThreads__deps: [ "$terminateWorker" ],
 terminateAllThreads: function() {
  assert(!ENVIRONMENT_IS_PTHREAD, "Internal Error! terminateAllThreads() can only ever be called from main application thread!");
  for (var worker of PThread.runningWorkers) {
   terminateWorker(worker);
  }
  for (var worker of PThread.unusedWorkers) {
   terminateWorker(worker);
  }
  PThread.unusedWorkers = [];
  PThread.runningWorkers = [];
  PThread.pthreads = [];
 },
 returnWorkerToPool: function(worker) {
  var pthread_ptr = worker.pthread_ptr;
  delete PThread.pthreads[pthread_ptr];
  PThread.unusedWorkers.push(worker);
  PThread.runningWorkers.splice(PThread.runningWorkers.indexOf(worker), 1);
  worker.pthread_ptr = 0;
  __emscripten_thread_free_data(pthread_ptr);
 },
 receiveObjectTransfer: function(data) {},
 threadInitTLS: function() {
  PThread.tlsInitFunctions.forEach(f => f());
 },
 loadWasmModuleToWorker: worker => new Promise(onFinishedLoading => {
  worker.onmessage = e => {
   var d = e["data"];
   var cmd = d["cmd"];
   if (worker.pthread_ptr) PThread.currentProxiedOperationCallerThread = worker.pthread_ptr;
   if (d["targetThread"] && d["targetThread"] != _pthread_self()) {
    var targetWorker = PThread.pthreads[d.targetThread];
    if (targetWorker) {
     targetWorker.postMessage(d, d["transferList"]);
    } else {
     err('Internal error! Worker sent a message "' + cmd + '" to target pthread ' + d["targetThread"] + ", but that thread no longer exists!");
    }
    PThread.currentProxiedOperationCallerThread = undefined;
    return;
   }
   if (cmd === "checkMailbox") {
    checkMailbox();
   } else if (cmd === "spawnThread") {
    spawnThread(d);
   } else if (cmd === "cleanupThread") {
    cleanupThread(d["thread"]);
   } else if (cmd === "killThread") {
    killThread(d["thread"]);
   } else if (cmd === "cancelThread") {
    cancelThread(d["thread"]);
   } else if (cmd === "loaded") {
    worker.loaded = true;
    if (ENVIRONMENT_IS_NODE && !worker.pthread_ptr) {
     worker.unref();
    }
    onFinishedLoading(worker);
   } else if (cmd === "print") {
    out("Thread " + d["threadId"] + ": " + d["text"]);
   } else if (cmd === "printErr") {
    err("Thread " + d["threadId"] + ": " + d["text"]);
   } else if (cmd === "alert") {
    alert("Thread " + d["threadId"] + ": " + d["text"]);
   } else if (d.target === "setimmediate") {
    worker.postMessage(d);
   } else if (cmd === "callHandler") {
    Module[d["handler"]](...d["args"]);
   } else if (cmd) {
    err("worker sent an unknown command " + cmd);
   }
   PThread.currentProxiedOperationCallerThread = undefined;
  };
  worker.onerror = e => {
   var message = "worker sent an error!";
   if (worker.pthread_ptr) {
    message = "Pthread " + ptrToString(worker.pthread_ptr) + " sent an error!";
   }
   err(message + " " + e.filename + ":" + e.lineno + ": " + e.message);
   throw e;
  };
  if (ENVIRONMENT_IS_NODE) {
   worker.on("message", function(data) {
    worker.onmessage({
     data: data
    });
   });
   worker.on("error", function(e) {
    worker.onerror(e);
   });
  }
  assert(wasmMemory instanceof WebAssembly.Memory, "WebAssembly memory should have been loaded by now!");
  assert(wasmModule instanceof WebAssembly.Module, "WebAssembly Module should have been loaded by now!");
  var handlers = [];
  var knownHandlers = [ "onExit", "onAbort", "print", "printErr" ];
  for (var handler of knownHandlers) {
   if (Module.hasOwnProperty(handler)) {
    handlers.push(handler);
   }
  }
  worker.workerID = PThread.nextWorkerID++;
  worker.postMessage({
   "cmd": "load",
   "handlers": handlers,
   "urlOrBlob": Module["mainScriptUrlOrBlob"] || _scriptDir,
   "wasmMemory": wasmMemory,
   "wasmModule": wasmModule,
   "workerID": worker.workerID
  });
 }),
 loadWasmModuleToAllWorkers: function(onMaybeReady) {
  if (ENVIRONMENT_IS_PTHREAD) {
   return onMaybeReady();
  }
  let pthreadPoolReady = Promise.all(PThread.unusedWorkers.map(PThread.loadWasmModuleToWorker));
  pthreadPoolReady.then(onMaybeReady);
 },
 allocateUnusedWorker: function() {
  var worker;
  var pthreadMainJs = locateFile("test.c.worker.js");
  worker = new Worker(pthreadMainJs);
  PThread.unusedWorkers.push(worker);
 },
 getNewWorker: function() {
  if (PThread.unusedWorkers.length == 0) {
   if (!ENVIRONMENT_IS_NODE) {
    err("Tried to spawn a new thread, but the thread pool is exhausted.\n" + "This might result in a deadlock unless some threads eventually exit or the code explicitly breaks out to the event loop.\n" + "If you want to increase the pool size, use setting `-sPTHREAD_POOL_SIZE=...`." + "\nIf you want to throw an explicit error instead of the risk of deadlocking in those cases, use setting `-sPTHREAD_POOL_SIZE_STRICT=2`.");
   }
   PThread.allocateUnusedWorker();
   PThread.loadWasmModuleToWorker(PThread.unusedWorkers[0]);
  }
  return PThread.unusedWorkers.pop();
 }
};

Module["PThread"] = PThread;

function callRuntimeCallbacks(callbacks) {
 while (callbacks.length > 0) {
  callbacks.shift()(Module);
 }
}

function establishStackSpace() {
 var pthread_ptr = _pthread_self();
 var stackTop = GROWABLE_HEAP_I32()[pthread_ptr + 52 >> 2];
 var stackSize = GROWABLE_HEAP_I32()[pthread_ptr + 56 >> 2];
 var stackMax = stackTop - stackSize;
 assert(stackTop != 0);
 assert(stackMax != 0);
 assert(stackTop > stackMax, "stackTop must be higher then stackMax");
 _emscripten_stack_set_limits(stackTop, stackMax);
 stackRestore(stackTop);
 writeStackCookie();
}

Module["establishStackSpace"] = establishStackSpace;

function exitOnMainThread(returnCode) {
 if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(2, 0, returnCode);
 _exit(returnCode);
}

function getValue(ptr, type = "i8") {
 if (type.endsWith("*")) type = "*";
 switch (type) {
 case "i1":
  return GROWABLE_HEAP_I8()[ptr >> 0];

 case "i8":
  return GROWABLE_HEAP_I8()[ptr >> 0];

 case "i16":
  return GROWABLE_HEAP_I16()[ptr >> 1];

 case "i32":
  return GROWABLE_HEAP_I32()[ptr >> 2];

 case "i64":
  return GROWABLE_HEAP_I32()[ptr >> 2];

 case "float":
  return GROWABLE_HEAP_F32()[ptr >> 2];

 case "double":
  return GROWABLE_HEAP_F64()[ptr >> 3];

 case "*":
  return GROWABLE_HEAP_U32()[ptr >> 2];

 default:
  abort(`invalid type for getValue: ${type}`);
 }
}

var wasmTableMirror = [];

function getWasmTableEntry(funcPtr) {
 var func = wasmTableMirror[funcPtr];
 if (!func) {
  if (funcPtr >= wasmTableMirror.length) wasmTableMirror.length = funcPtr + 1;
  wasmTableMirror[funcPtr] = func = wasmTable.get(funcPtr);
 }
 assert(wasmTable.get(funcPtr) == func, "JavaScript-side Wasm function table mirror is out of date!");
 return func;
}

function invokeEntryPoint(ptr, arg) {
 var result = getWasmTableEntry(ptr)(arg);
 checkStackCookie();
 if (keepRuntimeAlive()) {
  PThread.setExitStatus(result);
 } else {
  __emscripten_thread_exit(result);
 }
}

Module["invokeEntryPoint"] = invokeEntryPoint;

function registerTLSInit(tlsInitFunc) {
 PThread.tlsInitFunctions.push(tlsInitFunc);
}

function setValue(ptr, value, type = "i8") {
 if (type.endsWith("*")) type = "*";
 switch (type) {
 case "i1":
  GROWABLE_HEAP_I8()[ptr >> 0] = value;
  break;

 case "i8":
  GROWABLE_HEAP_I8()[ptr >> 0] = value;
  break;

 case "i16":
  GROWABLE_HEAP_I16()[ptr >> 1] = value;
  break;

 case "i32":
  GROWABLE_HEAP_I32()[ptr >> 2] = value;
  break;

 case "i64":
  tempI64 = [ value >>> 0, (tempDouble = value, +Math.abs(tempDouble) >= 1 ? tempDouble > 0 ? +Math.floor(tempDouble / 4294967296) >>> 0 : ~~+Math.ceil((tempDouble - +(~~tempDouble >>> 0)) / 4294967296) >>> 0 : 0) ], 
  GROWABLE_HEAP_I32()[ptr >> 2] = tempI64[0], GROWABLE_HEAP_I32()[ptr + 4 >> 2] = tempI64[1];
  break;

 case "float":
  GROWABLE_HEAP_F32()[ptr >> 2] = value;
  break;

 case "double":
  GROWABLE_HEAP_F64()[ptr >> 3] = value;
  break;

 case "*":
  GROWABLE_HEAP_U32()[ptr >> 2] = value;
  break;

 default:
  abort(`invalid type for setValue: ${type}`);
 }
}

function warnOnce(text) {
 if (!warnOnce.shown) warnOnce.shown = {};
 if (!warnOnce.shown[text]) {
  warnOnce.shown[text] = 1;
  if (ENVIRONMENT_IS_NODE) text = "warning: " + text;
  err(text);
 }
}

function ___assert_fail(condition, filename, line, func) {
 abort(`Assertion failed: ${UTF8ToString(condition)}, at: ` + [ filename ? UTF8ToString(filename) : "unknown filename", line, func ? UTF8ToString(func) : "unknown function" ]);
}

function ___emscripten_init_main_thread_js(tb) {
 __emscripten_thread_init(tb, !ENVIRONMENT_IS_WORKER, 1, !ENVIRONMENT_IS_WEB, 5242880);
 PThread.threadInitTLS();
}

function ___emscripten_thread_cleanup(thread) {
 if (!ENVIRONMENT_IS_PTHREAD) cleanupThread(thread); else postMessage({
  "cmd": "cleanupThread",
  "thread": thread
 });
}

function pthreadCreateProxied(pthread_ptr, attr, startRoutine, arg) {
 if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(3, 1, pthread_ptr, attr, startRoutine, arg);
 return ___pthread_create_js(pthread_ptr, attr, startRoutine, arg);
}

function ___pthread_create_js(pthread_ptr, attr, startRoutine, arg) {
 if (typeof SharedArrayBuffer == "undefined") {
  err("Current environment does not support SharedArrayBuffer, pthreads are not available!");
  return 6;
 }
 var transferList = [];
 var error = 0;
 if (ENVIRONMENT_IS_PTHREAD && (transferList.length === 0 || error)) {
  return pthreadCreateProxied(pthread_ptr, attr, startRoutine, arg);
 }
 if (error) return error;
 var threadParams = {
  startRoutine: startRoutine,
  pthread_ptr: pthread_ptr,
  arg: arg,
  transferList: transferList
 };
 if (ENVIRONMENT_IS_PTHREAD) {
  threadParams.cmd = "spawnThread";
  postMessage(threadParams, transferList);
  return 0;
 }
 return spawnThread(threadParams);
}

var nowIsMonotonic = true;

function __emscripten_get_now_is_monotonic() {
 return nowIsMonotonic;
}

function maybeExit() {
 if (!keepRuntimeAlive()) {
  try {
   if (ENVIRONMENT_IS_PTHREAD) __emscripten_thread_exit(EXITSTATUS); else _exit(EXITSTATUS);
  } catch (e) {
   handleException(e);
  }
 }
}

function callUserCallback(func) {
 if (ABORT) {
  err("user callback triggered after runtime exited or application aborted.  Ignoring.");
  return;
 }
 try {
  func();
  maybeExit();
 } catch (e) {
  handleException(e);
 }
}

function __emscripten_thread_mailbox_await(pthread_ptr) {
 if (typeof Atomics.waitAsync === "function") {
  var wait = Atomics.waitAsync(GROWABLE_HEAP_I32(), pthread_ptr >> 2, pthread_ptr);
  assert(wait.async);
  wait.value.then(checkMailbox);
  var waitingAsync = pthread_ptr + 128;
  Atomics.store(GROWABLE_HEAP_I32(), waitingAsync >> 2, 1);
 }
}

Module["__emscripten_thread_mailbox_await"] = __emscripten_thread_mailbox_await;

function checkMailbox() {
 var pthread_ptr = _pthread_self();
 if (pthread_ptr) {
  __emscripten_thread_mailbox_await(pthread_ptr);
  callUserCallback(() => __emscripten_check_mailbox());
 }
}

Module["checkMailbox"] = checkMailbox;

function __emscripten_notify_mailbox_postmessage(targetThreadId, currThreadId, mainThreadId) {
 if (targetThreadId == currThreadId) {
  setTimeout(() => checkMailbox());
 } else if (ENVIRONMENT_IS_PTHREAD) {
  postMessage({
   "targetThread": targetThreadId,
   "cmd": "checkMailbox"
  });
 } else {
  var worker = PThread.pthreads[targetThreadId];
  if (!worker) {
   err("Cannot send message to thread with ID " + targetThreadId + ", unknown thread ID!");
   return;
  }
  worker.postMessage({
   "cmd": "checkMailbox"
  });
 }
}

function __emscripten_set_offscreencanvas_size(target, width, height) {
 err("emscripten_set_offscreencanvas_size: Build with -sOFFSCREENCANVAS_SUPPORT=1 to enable transferring canvases to pthreads.");
 return -1;
}

function __emscripten_thread_set_strongref(thread) {
 if (ENVIRONMENT_IS_NODE) {
  PThread.pthreads[thread].ref();
 }
}

function _emscripten_check_blocking_allowed() {
 if (ENVIRONMENT_IS_NODE) return;
 if (ENVIRONMENT_IS_WORKER) return;
 warnOnce("Blocking on the main thread is very dangerous, see https://emscripten.org/docs/porting/pthreads.html#blocking-on-the-main-browser-thread");
}

function _emscripten_date_now() {
 return Date.now();
}

function runtimeKeepalivePush() {
 runtimeKeepaliveCounter += 1;
}

function _emscripten_exit_with_live_runtime() {
 runtimeKeepalivePush();
 throw "unwind";
}

var _emscripten_get_now;

if (ENVIRONMENT_IS_NODE) {
 _emscripten_get_now = () => {
  var t = process.hrtime();
  return t[0] * 1e3 + t[1] / 1e6;
 };
} else _emscripten_get_now = () => performance.timeOrigin + performance.now();

function _emscripten_random() {
 return Math.random();
}

function withStackSave(f) {
 var stack = stackSave();
 var ret = f();
 stackRestore(stack);
 return ret;
}

function proxyToMainThread(index, sync) {
 var numCallArgs = arguments.length - 2;
 var outerArgs = arguments;
 var maxArgs = 19;
 if (numCallArgs > maxArgs) {
  throw "proxyToMainThread: Too many arguments " + numCallArgs + " to proxied function idx=" + index + ", maximum supported is " + maxArgs;
 }
 return withStackSave(() => {
  var serializedNumCallArgs = numCallArgs;
  var args = stackAlloc(serializedNumCallArgs * 8);
  var b = args >> 3;
  for (var i = 0; i < numCallArgs; i++) {
   var arg = outerArgs[2 + i];
   GROWABLE_HEAP_F64()[b + i] = arg;
  }
  return __emscripten_run_in_main_runtime_thread_js(index, serializedNumCallArgs, args, sync);
 });
}

var emscripten_receive_on_main_thread_js_callArgs = [];

function _emscripten_receive_on_main_thread_js(index, numCallArgs, args) {
 emscripten_receive_on_main_thread_js_callArgs.length = numCallArgs;
 var b = args >> 3;
 for (var i = 0; i < numCallArgs; i++) {
  emscripten_receive_on_main_thread_js_callArgs[i] = GROWABLE_HEAP_F64()[b + i];
 }
 var func = proxiedFunctionTable[index];
 assert(func.length == numCallArgs, "Call args mismatch in emscripten_receive_on_main_thread_js");
 return func.apply(null, emscripten_receive_on_main_thread_js_callArgs);
}

function getHeapMax() {
 return 2147483648;
}

function abortOnCannotGrowMemory(requestedSize) {
 abort(`Cannot enlarge memory arrays to size ${requestedSize} bytes (OOM). If you want malloc to return NULL (0) instead of this abort, do not link with -sABORTING_MALLOC (that is, the default when growth is enabled is to not abort, but you have overridden that)`);
}

function emscripten_realloc_buffer(size) {
 var b = wasmMemory.buffer;
 try {
  wasmMemory.grow(size - b.byteLength + 65535 >>> 16);
  updateMemoryViews();
  return 1;
 } catch (e) {
  err(`emscripten_realloc_buffer: Attempted to grow heap from ${b.byteLength} bytes to ${size} bytes, but got error: ${e}`);
 }
}

function _emscripten_resize_heap(requestedSize) {
 var oldSize = GROWABLE_HEAP_U8().length;
 requestedSize = requestedSize >>> 0;
 if (requestedSize <= oldSize) {
  return false;
 }
 var maxHeapSize = getHeapMax();
 if (requestedSize > maxHeapSize) {
  err(`Cannot enlarge memory, asked to go up to ${requestedSize} bytes, but the limit is ${maxHeapSize} bytes!`);
  abortOnCannotGrowMemory(requestedSize);
 }
 var alignUp = (x, multiple) => x + (multiple - x % multiple) % multiple;
 for (var cutDown = 1; cutDown <= 4; cutDown *= 2) {
  var overGrownHeapSize = oldSize * (1 + .2 / cutDown);
  overGrownHeapSize = Math.min(overGrownHeapSize, requestedSize + 100663296);
  var newSize = Math.min(maxHeapSize, alignUp(Math.max(requestedSize, overGrownHeapSize), 65536));
  var replacement = emscripten_realloc_buffer(newSize);
  if (replacement) {
   return true;
  }
 }
 err(`Failed to grow the heap from ${oldSize} bytes to ${newSize} bytes, not enough memory!`);
 abortOnCannotGrowMemory(requestedSize);
}

var printCharBuffers = [ null, [], [] ];

function printChar(stream, curr) {
 var buffer = printCharBuffers[stream];
 assert(buffer);
 if (curr === 0 || curr === 10) {
  (stream === 1 ? out : err)(UTF8ArrayToString(buffer, 0));
  buffer.length = 0;
 } else {
  buffer.push(curr);
 }
}

function flush_NO_FILESYSTEM() {
 _fflush(0);
 if (printCharBuffers[1].length) printChar(1, 10);
 if (printCharBuffers[2].length) printChar(2, 10);
}

function _fd_write(fd, iov, iovcnt, pnum) {
 if (ENVIRONMENT_IS_PTHREAD) return proxyToMainThread(4, 1, fd, iov, iovcnt, pnum);
 var num = 0;
 for (var i = 0; i < iovcnt; i++) {
  var ptr = GROWABLE_HEAP_U32()[iov >> 2];
  var len = GROWABLE_HEAP_U32()[iov + 4 >> 2];
  iov += 8;
  for (var j = 0; j < len; j++) {
   printChar(fd, GROWABLE_HEAP_U8()[ptr + j]);
  }
  num += len;
 }
 GROWABLE_HEAP_U32()[pnum >> 2] = num;
 return 0;
}

function getCFunc(ident) {
 var func = Module["_" + ident];
 assert(func, "Cannot call unknown function " + ident + ", make sure it is exported");
 return func;
}

function writeArrayToMemory(array, buffer) {
 assert(array.length >= 0, "writeArrayToMemory array must have a length (should be an array or typed array)");
 GROWABLE_HEAP_I8().set(array, buffer);
}

function lengthBytesUTF8(str) {
 var len = 0;
 for (var i = 0; i < str.length; ++i) {
  var c = str.charCodeAt(i);
  if (c <= 127) {
   len++;
  } else if (c <= 2047) {
   len += 2;
  } else if (c >= 55296 && c <= 57343) {
   len += 4;
   ++i;
  } else {
   len += 3;
  }
 }
 return len;
}

function stringToUTF8Array(str, heap, outIdx, maxBytesToWrite) {
 assert(typeof str === "string");
 if (!(maxBytesToWrite > 0)) return 0;
 var startIdx = outIdx;
 var endIdx = outIdx + maxBytesToWrite - 1;
 for (var i = 0; i < str.length; ++i) {
  var u = str.charCodeAt(i);
  if (u >= 55296 && u <= 57343) {
   var u1 = str.charCodeAt(++i);
   u = 65536 + ((u & 1023) << 10) | u1 & 1023;
  }
  if (u <= 127) {
   if (outIdx >= endIdx) break;
   heap[outIdx++] = u;
  } else if (u <= 2047) {
   if (outIdx + 1 >= endIdx) break;
   heap[outIdx++] = 192 | u >> 6;
   heap[outIdx++] = 128 | u & 63;
  } else if (u <= 65535) {
   if (outIdx + 2 >= endIdx) break;
   heap[outIdx++] = 224 | u >> 12;
   heap[outIdx++] = 128 | u >> 6 & 63;
   heap[outIdx++] = 128 | u & 63;
  } else {
   if (outIdx + 3 >= endIdx) break;
   if (u > 1114111) warnOnce("Invalid Unicode code point " + ptrToString(u) + " encountered when serializing a JS string to a UTF-8 string in wasm memory! (Valid unicode code points should be in range 0-0x10FFFF).");
   heap[outIdx++] = 240 | u >> 18;
   heap[outIdx++] = 128 | u >> 12 & 63;
   heap[outIdx++] = 128 | u >> 6 & 63;
   heap[outIdx++] = 128 | u & 63;
  }
 }
 heap[outIdx] = 0;
 return outIdx - startIdx;
}

function stringToUTF8(str, outPtr, maxBytesToWrite) {
 assert(typeof maxBytesToWrite == "number", "stringToUTF8(str, outPtr, maxBytesToWrite) is missing the third parameter that specifies the length of the output buffer!");
 return stringToUTF8Array(str, GROWABLE_HEAP_U8(), outPtr, maxBytesToWrite);
}

function stringToUTF8OnStack(str) {
 var size = lengthBytesUTF8(str) + 1;
 var ret = stackAlloc(size);
 stringToUTF8(str, ret, size);
 return ret;
}

function ccall(ident, returnType, argTypes, args, opts) {
 var toC = {
  "string": str => {
   var ret = 0;
   if (str !== null && str !== undefined && str !== 0) {
    ret = stringToUTF8OnStack(str);
   }
   return ret;
  },
  "array": arr => {
   var ret = stackAlloc(arr.length);
   writeArrayToMemory(arr, ret);
   return ret;
  }
 };
 function convertReturnValue(ret) {
  if (returnType === "string") {
   return UTF8ToString(ret);
  }
  if (returnType === "boolean") return Boolean(ret);
  return ret;
 }
 var func = getCFunc(ident);
 var cArgs = [];
 var stack = 0;
 assert(returnType !== "array", 'Return type should not be "array".');
 if (args) {
  for (var i = 0; i < args.length; i++) {
   var converter = toC[argTypes[i]];
   if (converter) {
    if (stack === 0) stack = stackSave();
    cArgs[i] = converter(args[i]);
   } else {
    cArgs[i] = args[i];
   }
  }
 }
 var ret = func.apply(null, cArgs);
 function onDone(ret) {
  if (stack !== 0) stackRestore(stack);
  return convertReturnValue(ret);
 }
 ret = onDone(ret);
 return ret;
}

function cwrap(ident, returnType, argTypes, opts) {
 return function() {
  return ccall(ident, returnType, argTypes, arguments, opts);
 };
}

PThread.init();

var proxiedFunctionTable = [ null, _proc_exit, exitOnMainThread, pthreadCreateProxied, _fd_write ];

function checkIncomingModuleAPI() {
 ignoredModuleProp("fetchSettings");
}

var wasmImports = {
 "__assert_fail": ___assert_fail,
 "__emscripten_init_main_thread_js": ___emscripten_init_main_thread_js,
 "__emscripten_thread_cleanup": ___emscripten_thread_cleanup,
 "__pthread_create_js": ___pthread_create_js,
 "_emscripten_get_now_is_monotonic": __emscripten_get_now_is_monotonic,
 "_emscripten_notify_mailbox_postmessage": __emscripten_notify_mailbox_postmessage,
 "_emscripten_set_offscreencanvas_size": __emscripten_set_offscreencanvas_size,
 "_emscripten_thread_mailbox_await": __emscripten_thread_mailbox_await,
 "_emscripten_thread_set_strongref": __emscripten_thread_set_strongref,
 "emscripten_check_blocking_allowed": _emscripten_check_blocking_allowed,
 "emscripten_date_now": _emscripten_date_now,
 "emscripten_exit_with_live_runtime": _emscripten_exit_with_live_runtime,
 "emscripten_get_now": _emscripten_get_now,
 "emscripten_random": _emscripten_random,
 "emscripten_receive_on_main_thread_js": _emscripten_receive_on_main_thread_js,
 "emscripten_resize_heap": _emscripten_resize_heap,
 "exit": _exit,
 "fd_write": _fd_write,
 "memory": wasmMemory
};

var asm = createWasm();

var ___wasm_call_ctors = createExportWrapper("__wasm_call_ctors");

var _malloc = Module["_malloc"] = createExportWrapper("malloc");

var _jobs_setup = Module["_jobs_setup"] = createExportWrapper("jobs_setup");

var _set_load_distance = Module["_set_load_distance"] = createExportWrapper("set_load_distance");

var _get_spawn_y = Module["_get_spawn_y"] = createExportWrapper("get_spawn_y");

var _unpause_thread = Module["_unpause_thread"] = createExportWrapper("unpause_thread");

var _get_seed = Module["_get_seed"] = createExportWrapper("get_seed");

var _get_chunkcache = Module["_get_chunkcache"] = createExportWrapper("get_chunkcache");

var _set_world_before = Module["_set_world_before"] = createExportWrapper("set_world_before");

var _set_world_after = Module["_set_world_after"] = createExportWrapper("set_world_after");

var _get_click_request = Module["_get_click_request"] = createExportWrapper("get_click_request");

var _init = Module["_init"] = createExportWrapper("init");

var _create_chunks = Module["_create_chunks"] = createExportWrapper("create_chunks");

var __emscripten_tls_init = Module["__emscripten_tls_init"] = createExportWrapper("_emscripten_tls_init");

var _pthread_self = Module["_pthread_self"] = function() {
 return (_pthread_self = Module["_pthread_self"] = Module["asm"]["pthread_self"]).apply(null, arguments);
};

var ___errno_location = createExportWrapper("__errno_location");

var __emscripten_thread_init = Module["__emscripten_thread_init"] = createExportWrapper("_emscripten_thread_init");

var __emscripten_thread_crashed = Module["__emscripten_thread_crashed"] = createExportWrapper("_emscripten_thread_crashed");

var _fflush = Module["_fflush"] = createExportWrapper("fflush");

var _emscripten_main_thread_process_queued_calls = createExportWrapper("emscripten_main_thread_process_queued_calls");

var _emscripten_main_runtime_thread_id = createExportWrapper("emscripten_main_runtime_thread_id");

var __emscripten_run_in_main_runtime_thread_js = createExportWrapper("_emscripten_run_in_main_runtime_thread_js");

var _emscripten_dispatch_to_thread_ = createExportWrapper("emscripten_dispatch_to_thread_");

var _emscripten_stack_get_base = function() {
 return (_emscripten_stack_get_base = Module["asm"]["emscripten_stack_get_base"]).apply(null, arguments);
};

var _emscripten_stack_get_end = function() {
 return (_emscripten_stack_get_end = Module["asm"]["emscripten_stack_get_end"]).apply(null, arguments);
};

var __emscripten_thread_free_data = createExportWrapper("_emscripten_thread_free_data");

var __emscripten_thread_exit = Module["__emscripten_thread_exit"] = createExportWrapper("_emscripten_thread_exit");

var __emscripten_check_mailbox = Module["__emscripten_check_mailbox"] = createExportWrapper("_emscripten_check_mailbox");

var _emscripten_stack_init = function() {
 return (_emscripten_stack_init = Module["asm"]["emscripten_stack_init"]).apply(null, arguments);
};

var _emscripten_stack_set_limits = function() {
 return (_emscripten_stack_set_limits = Module["asm"]["emscripten_stack_set_limits"]).apply(null, arguments);
};

var _emscripten_stack_get_free = function() {
 return (_emscripten_stack_get_free = Module["asm"]["emscripten_stack_get_free"]).apply(null, arguments);
};

var stackSave = createExportWrapper("stackSave");

var stackRestore = createExportWrapper("stackRestore");

var stackAlloc = createExportWrapper("stackAlloc");

var _emscripten_stack_get_current = function() {
 return (_emscripten_stack_get_current = Module["asm"]["emscripten_stack_get_current"]).apply(null, arguments);
};

var dynCall_jiji = Module["dynCall_jiji"] = createExportWrapper("dynCall_jiji");

Module["keepRuntimeAlive"] = keepRuntimeAlive;

Module["wasmMemory"] = wasmMemory;

Module["ccall"] = ccall;

Module["cwrap"] = cwrap;

Module["ExitStatus"] = ExitStatus;

var missingLibrarySymbols = [ "isLeapYear", "ydayFromDate", "arraySum", "addDays", "setErrNo", "inetPton4", "inetNtop4", "inetPton6", "inetNtop6", "readSockaddr", "writeSockaddr", "getHostByName", "initRandomFill", "randomFill", "traverseStack", "getCallstack", "emscriptenLog", "convertPCtoSourceLocation", "readEmAsmArgs", "jstoi_q", "jstoi_s", "getExecutableName", "listenOnce", "autoResumeAudioContext", "dynCallLegacy", "getDynCaller", "dynCall", "runtimeKeepalivePop", "safeSetTimeout", "asmjsMangle", "asyncLoad", "alignMemory", "mmapAlloc", "HandleAllocator", "getNativeTypeSize", "STACK_SIZE", "STACK_ALIGN", "POINTER_SIZE", "ASSERTIONS", "writeI53ToI64", "writeI53ToI64Clamped", "writeI53ToI64Signaling", "writeI53ToU64Clamped", "writeI53ToU64Signaling", "readI53FromI64", "readI53FromU64", "convertI32PairToI53", "convertI32PairToI53Checked", "convertU32PairToI53", "uleb128Encode", "sigToWasmTypes", "generateFuncType", "convertJsFunctionToWasm", "getEmptyTableSlot", "updateTableMap", "getFunctionAddress", "addFunction", "removeFunction", "reallyNegative", "unSign", "strLen", "reSign", "formatString", "intArrayFromString", "intArrayToString", "AsciiToString", "stringToAscii", "UTF16ToString", "stringToUTF16", "lengthBytesUTF16", "UTF32ToString", "stringToUTF32", "lengthBytesUTF32", "stringToNewUTF8", "getSocketFromFD", "getSocketAddress", "registerKeyEventCallback", "maybeCStringToJsString", "findEventTarget", "findCanvasEventTarget", "getBoundingClientRect", "fillMouseEventData", "registerMouseEventCallback", "registerWheelEventCallback", "registerUiEventCallback", "registerFocusEventCallback", "fillDeviceOrientationEventData", "registerDeviceOrientationEventCallback", "fillDeviceMotionEventData", "registerDeviceMotionEventCallback", "screenOrientation", "fillOrientationChangeEventData", "registerOrientationChangeEventCallback", "fillFullscreenChangeEventData", "registerFullscreenChangeEventCallback", "JSEvents_requestFullscreen", "JSEvents_resizeCanvasForFullscreen", "registerRestoreOldStyle", "hideEverythingExceptGivenElement", "restoreHiddenElements", "setLetterbox", "softFullscreenResizeWebGLRenderTarget", "doRequestFullscreen", "fillPointerlockChangeEventData", "registerPointerlockChangeEventCallback", "registerPointerlockErrorEventCallback", "requestPointerLock", "fillVisibilityChangeEventData", "registerVisibilityChangeEventCallback", "registerTouchEventCallback", "fillGamepadEventData", "registerGamepadEventCallback", "registerBeforeUnloadEventCallback", "fillBatteryEventData", "battery", "registerBatteryEventCallback", "setCanvasElementSizeCallingThread", "setCanvasElementSizeMainThread", "setCanvasElementSize", "getCanvasSizeCallingThread", "getCanvasSizeMainThread", "getCanvasElementSize", "demangle", "demangleAll", "jsStackTrace", "stackTrace", "getEnvStrings", "checkWasiClock", "wasiRightsToMuslOFlags", "wasiOFlagsToMuslOFlags", "createDyncallWrapper", "setImmediateWrapped", "clearImmediateWrapped", "polyfillSetImmediate", "getPromise", "makePromise", "idsToPromises", "makePromiseCallback", "ExceptionInfo", "setMainLoop", "FS_createPreloadedFile", "FS_modeStringToFlags", "FS_getMode", "_setNetworkCallback", "heapObjectForWebGLType", "heapAccessShiftForWebGLHeap", "webgl_enable_ANGLE_instanced_arrays", "webgl_enable_OES_vertex_array_object", "webgl_enable_WEBGL_draw_buffers", "webgl_enable_WEBGL_multi_draw", "emscriptenWebGLGet", "computeUnpackAlignedImageSize", "colorChannelsInGlTextureFormat", "emscriptenWebGLGetTexPixelData", "__glGenObject", "emscriptenWebGLGetUniform", "webglGetUniformLocation", "webglPrepareUniformLocationsBeforeFirstUse", "webglGetLeftBracePos", "emscriptenWebGLGetVertexAttrib", "__glGetActiveAttribOrUniform", "writeGLArray", "emscripten_webgl_destroy_context_before_on_calling_thread", "registerWebGlEventCallback", "runAndAbortIfError", "SDL_unicode", "SDL_ttfContext", "SDL_audio", "GLFW_Window", "ALLOC_NORMAL", "ALLOC_STACK", "allocate", "writeStringToMemory", "writeAsciiToMemory" ];

missingLibrarySymbols.forEach(missingLibrarySymbol);

var unexportedSymbols = [ "run", "addOnPreRun", "addOnInit", "addOnPreMain", "addOnExit", "addOnPostRun", "addRunDependency", "removeRunDependency", "FS_createFolder", "FS_createPath", "FS_createDataFile", "FS_createLazyFile", "FS_createLink", "FS_createDevice", "FS_unlink", "out", "err", "callMain", "abort", "stackAlloc", "stackSave", "stackRestore", "getTempRet0", "setTempRet0", "GROWABLE_HEAP_I8", "GROWABLE_HEAP_U8", "GROWABLE_HEAP_I16", "GROWABLE_HEAP_U16", "GROWABLE_HEAP_I32", "GROWABLE_HEAP_U32", "GROWABLE_HEAP_F32", "GROWABLE_HEAP_F64", "writeStackCookie", "checkStackCookie", "ptrToString", "zeroMemory", "exitJS", "getHeapMax", "abortOnCannotGrowMemory", "emscripten_realloc_buffer", "ENV", "MONTH_DAYS_REGULAR", "MONTH_DAYS_LEAP", "MONTH_DAYS_REGULAR_CUMULATIVE", "MONTH_DAYS_LEAP_CUMULATIVE", "ERRNO_CODES", "ERRNO_MESSAGES", "DNS", "Protocols", "Sockets", "timers", "warnOnce", "UNWIND_CACHE", "readEmAsmArgsArray", "handleException", "runtimeKeepalivePush", "callUserCallback", "maybeExit", "getCFunc", "freeTableIndexes", "functionsInTableMap", "setValue", "getValue", "PATH", "PATH_FS", "UTF8Decoder", "UTF8ArrayToString", "UTF8ToString", "stringToUTF8Array", "stringToUTF8", "lengthBytesUTF8", "UTF16Decoder", "stringToUTF8OnStack", "writeArrayToMemory", "SYSCALLS", "JSEvents", "specialHTMLTargets", "currentFullscreenStrategy", "restoreOldWindowedStyle", "flush_NO_FILESYSTEM", "dlopenMissingError", "promiseMap", "uncaughtExceptionCount", "exceptionLast", "exceptionCaught", "Browser", "wget", "preloadPlugins", "FS", "MEMFS", "TTY", "PIPEFS", "SOCKFS", "tempFixedLengthArray", "miniTempWebGLFloatBuffers", "miniTempWebGLIntBuffers", "GL", "emscripten_webgl_power_preferences", "AL", "GLUT", "EGL", "GLEW", "IDBStore", "SDL", "SDL_gfx", "GLFW", "allocateUTF8", "allocateUTF8OnStack", "PThread", "terminateWorker", "killThread", "cleanupThread", "registerTLSInit", "cancelThread", "spawnThread", "exitOnMainThread", "proxyToMainThread", "emscripten_receive_on_main_thread_js_callArgs", "invokeEntryPoint", "checkMailbox" ];

unexportedSymbols.forEach(unexportedRuntimeSymbol);

var calledRun;

dependenciesFulfilled = function runCaller() {
 if (!calledRun) run();
 if (!calledRun) dependenciesFulfilled = runCaller;
};

function stackCheckInit() {
 assert(!ENVIRONMENT_IS_PTHREAD);
 _emscripten_stack_init();
 writeStackCookie();
}

function run() {
 if (runDependencies > 0) {
  return;
 }
 if (!ENVIRONMENT_IS_PTHREAD) stackCheckInit();
 if (ENVIRONMENT_IS_PTHREAD) {
  initRuntime();
  startWorker(Module);
  return;
 }
 preRun();
 if (runDependencies > 0) {
  return;
 }
 function doRun() {
  if (calledRun) return;
  calledRun = true;
  Module["calledRun"] = true;
  if (ABORT) return;
  initRuntime();
  if (Module["onRuntimeInitialized"]) Module["onRuntimeInitialized"]();
  assert(!Module["_main"], 'compiled without a main, but one is present. if you added it from JS, use Module["onRuntimeInitialized"]');
  postRun();
 }
 if (Module["setStatus"]) {
  Module["setStatus"]("Running...");
  setTimeout(function() {
   setTimeout(function() {
    Module["setStatus"]("");
   }, 1);
   doRun();
  }, 1);
 } else {
  doRun();
 }
 checkStackCookie();
}

function checkUnflushedContent() {
 var oldOut = out;
 var oldErr = err;
 var has = false;
 out = err = x => {
  has = true;
 };
 try {
  flush_NO_FILESYSTEM();
 } catch (e) {}
 out = oldOut;
 err = oldErr;
 if (has) {
  warnOnce("stdio streams had content in them that was not flushed. you should set EXIT_RUNTIME to 1 (see the FAQ), or make sure to emit a newline when you printf etc.");
  warnOnce("(this may also be due to not including full filesystem support - try building with -sFORCE_FILESYSTEM)");
 }
}

if (Module["preInit"]) {
 if (typeof Module["preInit"] == "function") Module["preInit"] = [ Module["preInit"] ];
 while (Module["preInit"].length > 0) {
  Module["preInit"].pop()();
 }
}

run();
