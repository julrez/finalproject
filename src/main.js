let canvas;
var Module;

// for debug
/*
let gilInfoBufferAddress;
let abMemory, cbMemory;
function get_voxel(x, y, z)
{
	let accelerationIndex = ((z>>3)<<16) | ((y>>3)<<8) | (x>>3);
	let chunkIndex = abMemory[accelerationIndex];
	if ((chunkIndex & (3 << 30)) != 0) {
		return false;
	}
	let cx = x & 7;
	let cy = y & 7;
	let cz = z & 7;
	let voxelIndex = chunkIndex | (cz<<6)|(cy<<3)|cx;
	let voxel = cbMemory[voxelIndex];
	let address = voxel >> 5;
	console.log(Module.HEAP32[(gilPushData[0]>>2)+address*8+2]);
	console.log(Module.HEAP32[(gilPushData[0]>>2)+address*8+3]);
	console.log(Module.HEAP32[(gilPushData[0]>>2)+address*8+4]);
	console.log(Module.HEAP32[(gilPushData[0]>>2)+address*8+5]);
	console.log(Module.HEAP32[(gilPushData[0]>>2)+address*8+6]);
	console.log(Module.HEAP32[(gilPushData[0]>>2)+address*8+7]);
	return cbMemory[voxelIndex];
}
*/

let inMenu = true;
let menuType = "default";
let inMenuBecauseLastClick = true;

let lightingPercentage = 1; // from 0 to 1, 0 -> 100%

let lastPointerLockTime = undefined;

//let cameraPos = [1024, 1024, 1024];
let cameraAxis = [0, 0, -1];
let mousePitch = 0, mouseYaw = 270;

let shaderDebugEnabled = false;
if (shaderDebugEnabled) {
	var castMemory = new ArrayBuffer(4);
}

let keysPressed = {};

let sharedWithJSAddress = undefined;
let sharedWithJSMemory = undefined;

/*
let gilPushDataAddress;
let gilPushData;
*/

// 0 -> 8
let selectedByScroll = 0;
let lastKnownScrollPosition = window.scrollY;

function get_camera_pos()
{
	return [
		Module.HEAPF32[(sharedWithJSAddress>>2)+0],
		Module.HEAPF32[(sharedWithJSAddress>>2)+1],
		Module.HEAPF32[(sharedWithJSAddress>>2)+2]
	];
}

function set_camera_pos(cameraPos)
{
	Module.HEAPF32[(sharedWithJSAddress>>2)+0] = cameraPos[0];
	Module.HEAPF32[(sharedWithJSAddress>>2)+1] = cameraPos[1];
	Module.HEAPF32[(sharedWithJSAddress>>2)+2] = cameraPos[2];
}

function playOnClick()
{
	lock_cursor(canvas);

	inMenu = false;
	inMenuBecauseLastClick = true;
}

let selectedSetting = 0;
let totalSettingCount = 3;
let defaultSettings = [256, 50, 100];

function settingsOnClick()
{
	menuType = "settings";
	mouseOverSetting(0);

	// load settings
	for (let i = 0; i < totalSettingCount; i++) {
		let element = document.getElementById("setting"+i);
		let value = localStorage.getItem("setting"+i);
		if (value == null) {
			element.innerText = defaultSettings[i];
		} else {
			element.innerText = value;
		}
	}
}

function saveOnClick()
{
	var get_chunkcache = Module.cwrap("get_chunkcache", "number", []);
	let chunkCacheOffset = get_chunkcache();
	
	var get_seed = Module.cwrap("get_seed", "number", []);
	let seed = get_seed();

	const bitfieldsPtr = Module.HEAP32[chunkCacheOffset>>2];
	// recreated on load
	//const PSLPtr = Module.HEAP32[(chunkCacheOffset>>2)+1];
	//const count = Module.HEAP32[(chunkCacheOffset>>2)+2];
	const currentPrime = Module.HEAP32[(chunkCacheOffset>>2)+3];

	let primes = [
		53, 97, 193, 389, 769, 1543, 3079, 6151,
		12289, 24593, 49157, 98317, 196613, 393241, 786433,
		1572869, 3145739, 6291469,
	];
	let bitfieldsLen = primes[currentPrime];

	let bitfieldsArray = new Uint32Array(
		Module.HEAP8.buffer, bitfieldsPtr, bitfieldsLen*2
	);
	
	const stackIndicesPtr = Module.HEAP32[(chunkCacheOffset>>2)+4];
	const stackVoxelsPtr = Module.HEAP32[(chunkCacheOffset>>2)+5];
	const stackCount = Module.HEAP32[(chunkCacheOffset>>2)+6];
	const stackAllocationCount = Module.HEAP32[(chunkCacheOffset>>2)+7];
	//const stackFlags = Module.HEAP32[(chunkCacheOffset>>2)+8];

	let stackIndices = new Uint32Array(
		Module.HEAP8.buffer, stackIndicesPtr, stackAllocationCount
	);
	let stackVoxels = new Uint32Array(
		Module.HEAP8.buffer, stackVoxelsPtr, stackAllocationCount*8*8*8
	);

	// binary format:
	// 0: currentPrime
	// 1: stackCount
	// 2: stackAllocationCount
	// [bitfieldsLen*2]: bitfields
	// [stackAllocationCount]: stackIndices
	// [stackAllocationCount]: stackVoxels
	let data = new Uint32Array(3+bitfieldsLen*2+stackAllocationCount+stackAllocationCount*8*8*8);
	let cnt = 0;
	data[cnt++] = seed;
	data[cnt++] = currentPrime;
	data[cnt++] = stackCount;
	data[cnt++] = stackAllocationCount;
	for (let i = 0; i < bitfieldsLen*2; i++) {
		data[cnt++] = bitfieldsArray[i];
	}
	for (let i = 0; i < stackAllocationCount; i++) {
		data[cnt++] = stackIndices[i];
	}
	for (let i = 0; i < stackAllocationCount*8*8*8; i++) {
		data[cnt++] = stackVoxels[i];
	}

	let a = document.createElement("a");
    document.body.appendChild(a);
	let blob = new Blob([data], {type: "octet/stream"});
	let url = window.URL.createObjectURL(blob);
	a.href = url;
	a.download = "world.dat";
	a.click();
	setTimeout(() => {
		window.URL.revokeObjectURL(url);
	}, 0);
	
	var unpause_thread = Module.cwrap("unpause_thread", "number", []);
	unpause_thread();
}

function loadOnClick()
{
	let element = document.getElementById("loadFile");
	let file = element.files[0];
	if (file == undefined) {
		return;
	}

	file.arrayBuffer().then((buffer)=> {
		let array = new Uint32Array(buffer);
		let cnt = 0;
		let seed = array[cnt++];
		let currentPrime = array[cnt++];
		let stackCount = array[cnt++];
		let stackAllocationCount = array[cnt++];

		var set_world_before = Module.cwrap("set_world_before", "number",
			[
				"number", // currentPrime
				"number", // stackCount
				"number", // stackAllocationCount
				"number", // seed
			]
		);
		let chunkCacheOffset = set_world_before(
			currentPrime, stackCount, stackAllocationCount, seed);

		let primes = [
			53, 97, 193, 389, 769, 1543, 3079, 6151,
			12289, 24593, 49157, 98317, 196613, 393241, 786433,
			1572869, 3145739, 6291469,
		];
		let bitfieldsLen = primes[currentPrime];

		const bitfieldsPtr = Module.HEAP32[chunkCacheOffset>>2];
		const stackIndicesPtr = Module.HEAP32[(chunkCacheOffset>>2)+4];
		const stackVoxelsPtr = Module.HEAP32[(chunkCacheOffset>>2)+5];

		let bitfieldsArray = new Uint32Array(
			Module.HEAP8.buffer, bitfieldsPtr, bitfieldsLen*2
		);
		let stackIndices = new Uint32Array(
			Module.HEAP8.buffer, stackIndicesPtr, stackAllocationCount
		);
		let stackVoxels = new Uint32Array(
			Module.HEAP8.buffer, stackVoxelsPtr, stackAllocationCount*8*8*8
		);

		for (let i = 0; i < bitfieldsLen*2; i++) {
			bitfieldsArray[i] = array[cnt++];
		}
		for (let i = 0; i < stackAllocationCount; i++) {
			stackIndices[i] = array[cnt++];
		}
		for (let i = 0; i < stackAllocationCount*8*8*8; i++) {
			let voxel = array[cnt++];
			stackVoxels[i] = voxel;
		}
		
		var set_world_after = Module.cwrap("set_world_after", "number", []);
		set_world_after();
	});
}

let shouldReset = false;
function settingsDoneOnClick()
{
	let setting0 = document.getElementById("setting"+0);
	let setting0Value = Math.min(Math.max(parseFloat(setting0.innerText), 32), 1000);
	setting0.innerText = setting0Value;
	menuType = "default";
	
	let setting1 = document.getElementById("setting"+1);
	let setting1Value = Math.min(Math.max(parseFloat(setting1.innerText), 0), 1);
	setting1.innerText = setting1Value;
	let debugStringElement = document.getElementById("debugString");
	if (setting1Value == 1) {
		debugStringElement.style = "display: block;";
	} else {
		debugStringElement.style = "display: none;";
	}

	var set_load_distance = Module.cwrap("set_load_distance", "number", ["number"]);

	set_load_distance(setting0Value);
	
	let setting2 = document.getElementById("setting"+2);
	let setting2Value = Math.min(Math.max(parseFloat(setting2.innerText), 1), 100);
	setting2.innerText = setting2Value;

	lightingPercentage = setting2Value / 100;
	shouldReset = true; // force resize_canvas() so change comes immediately

	// store settings
	for (let i = 0; i < totalSettingCount; i++) {
		let element = document.getElementById("setting"+i);
		localStorage.setItem("setting"+i, element.innerText);
	}
}

function showMenu()
{
	let HUDcontainer = document.getElementById("HUDContainer");
	HUDcontainer.style = "display: none";

	let UIcontainer = document.getElementById("UIContainer");
	let settingsContainer = document.getElementById("settingsContainer");
	if (menuType == "default") {
		UIcontainer.style = "display: block";
		settingsContainer.style = "display: none";
	} else {
		UIcontainer.style = "display: none";
		settingsContainer.style = "display: block";
	}
}

function showHUD()
{
	//canvas.exitPointerLock();
	let UIcontainer = document.getElementById("UIContainer");
	UIcontainer.style = "display: none";
	
	let HUDcontainer = document.getElementById("HUDContainer");
	HUDcontainer.style = "display: block";

	let settingsContainer = document.getElementById("settingsContainer");
	settingsContainer.style = "display: none";
}

function mouseOverSetting(settingID)
{
	selectedSetting = settingID;
	let id = "setting"+settingID;
	for (let i = 0; i < totalSettingCount; i++) {
		let curr = document.getElementById("setting"+i);
		curr.className="panelNumberInput";
	}
	let element = document.getElementById("setting"+settingID);
	element.className = "panelNumberInput movingUnderscore";
}

function settingsHandleKey(keyEvent)
{
	let key = keyEvent.key;
	let element = document.getElementById("setting"+selectedSetting);
	if (key == "Backspace") {
		element.innerText = element.innerText.slice(0, -1);
	}
	if (!isNaN(key) && element.innerText.length < 5) {
		element.innerText = element.innerText+key;
	}
}

window.addEventListener('load', main);
async function main()
{
	console.log("currently it might take a while for everything to load!");
	canvas = document.getElementById("gameCanvas");
	canvas.width = window.innerWidth;
	canvas.height = window.innerHeight;
	
	Module = {
		//wasmMemory:
		onRuntimeInitialized: function () {
			let init = Module.cwrap("init", "number", []);
			sharedWithJSAddress = init();
			let sharedWithJSOffset = sharedWithJSAddress/4;
			//set_camera_pos([1024.0, 1024.0, 1024.0]);
			set_camera_pos([1024.0, 1024.0, 1024.0]);

			let loadDistance = localStorage.getItem("setting"+0);
			if (loadDistance == null) {
				loadDistance = defaultSettings[0];
			}
			var set_load_distance =
				Module.cwrap("set_load_distance", "number", ["number"]);

			set_load_distance(loadDistance);

			let newLightingPercentage = localStorage.getItem("setting"+2);
			if (newLightingPercentage == null) {
				newLightingPercentage = defaultSettings[2];
			}
			lightingPercentage = newLightingPercentage / 100;

			/*
			let gilPercentage = localStorage.getItem("setting"+1);
			if (gilPercentage == null) {
				gilPercentage = defaultSettings[0];
			}
			*/
			/*
			var set_gil_budget =
				Module.cwrap("set_gil_budget", "number", ["number"]);
			set_gil_budget(gilPercentage);
			*/

			var get_spawn_y =
				Module.cwrap("get_spawn_y", "number", []);
			let spawnY = get_spawn_y();
			set_camera_pos([1024, spawnY+8, 1024]);

			/*
			var get_gil_gpupush_structure =
				Module.cwrap("get_gil_gpupush_structure", "number", []);
			gilPushDataAddress = get_gil_gpupush_structure();
			gilPushData = new Uint32Array(Module.HEAP32.buffer, gilPushDataAddress, 9);
			*/
	

			compute_test();

			let send_click_request = Module.cwrap(
				"get_click_request",
				"number", 
				[
					"number",
					"number",
					"number",
					"number",
					"number",
					"number",
					"number",
					"number",
				]
			);

			addEventListener("click", (event) => {
				if (inMenuBecauseLastClick) {
					inMenuBecauseLastClick = false;
					return;
				}
				if (!inMenu) {
					lock_cursor(canvas);
					send_click_request(
						event.button,
						selectedByScroll,
						get_camera_pos()[0],
						get_camera_pos()[1],
						get_camera_pos()[2],
						cameraAxis[0],
						cameraAxis[1],
						cameraAxis[2],
					);
				}
			});
			document.addEventListener("pointerlockchange", (event) => {
				inMenu = !(canvas === document.pointerLockElement);
				if (!(canvas === document.pointerLockElement)) {
					lastPointerLockTime = performance.now();
				}
			});
		}
	};
	const mainWasmScript = document.createElement('script');
	mainWasmScript.async = true;
	mainWasmScript.src = "out/test.c.js";
	document.head.appendChild(mainWasmScript);

	addEventListener("mousemove", (event) => {
		if (!(canvas === document.pointerLockElement)) {
			return;
		}
		let xOffset = event.movementX;
		let yOffset = -event.movementY;

		let m_yaw = 0.022; // to get same sensitivity as the Source engine

		let sensitivity = 3.0;

		let incrementX = xOffset * sensitivity * m_yaw;
		let incrementY = yOffset * sensitivity * m_yaw;

		mouseYaw += incrementX;
		mousePitch += incrementY;
		
		if (mousePitch > 89) {
			mousePitch = 89;
		} else if (mousePitch < -89) {
			mousePitch = -89;
		}

		cameraAxis[0] = Math.cos(mouseYaw*Math.PI/180)
			* Math.cos(mousePitch*Math.PI/180);
		cameraAxis[1] = Math.sin(mousePitch*Math.PI/180);
		cameraAxis[2] = Math.sin(mouseYaw*Math.PI/180)
			* Math.cos(mousePitch*Math.PI/180);
	});
	addEventListener("keydown", (event) => {
		if (inMenu && menuType == "settings") {
			settingsHandleKey(event);
		}
		keysPressed[event.code] = true;
	});
	addEventListener("keyup", (event) => {
		keysPressed[event.code] = false;
	});

	lastScroll = performance.now();
	window.scrollTo(0, 1);
	addEventListener("scroll", (event) => {
		if (keysPressed.Space || performance.now() - lastScroll < 10) {
			return;
		}
		let diff = window.scrollY-1;
		if (diff > 0) {
			selectedByScroll = Math.max(0, selectedByScroll-1);
		} else {
			selectedByScroll = Math.min(8, selectedByScroll+1);
		}
		lastScroll = performance.now();
		window.scrollTo(0, 1);
	});
	
	//renderer_setup();
}

async function compute_test()
{
	const adapter = await navigator.gpu.requestAdapter({
		powerPreference: "high-performance",
	});
	let maxStorageBufferBindingSize = 268435456;
	const device = await adapter.requestDevice({
		requiredLimits: {
			maxStorageBufferBindingSize: maxStorageBufferBindingSize,
			/*maxBufferSize: maxStorageBufferBindingSize*/
		}
	});
	const queue = device.queue;
	
	let context = canvas.getContext('webgpu');

	context.configure({
		device: device,
		//format: navigator.gpu.getPreferredCanvasFormat(),
		format: "rgba8unorm",
		usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC,
		alphaMode: 'opaque'
	});

	let blockImage = new Image();
	blockImage.src = "static/"+"textures.png";
	await blockImage.decode();
	let blockBitmap = await createImageBitmap(blockImage);
	let blockTexture = device.createTexture({
		size: [
			blockBitmap.width,
			blockBitmap.height,
		],
		dimension: "2d",
		format: "rgba8unorm",
		usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
			| GPUTextureUsage.RENDER_ATTACHMENT,
		viewFormats: [],
	});
	let blockTextureView = blockTexture.createView();
	device.queue.copyExternalImageToTexture(
		{
			source: blockBitmap
		},
		{
			texture: blockTexture,
			origin: [0, 0, 0]
		},
		{ 
			width: blockTexture.width,
			height: blockTexture.height
		}
	);

	console.log(device.limits);

	let skyBoxImageNames = ["px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"];
	let skyBoxImages = [];
	let skyBoxBitmaps = [];
	for (let i = 0; i < 6; i++) {
		skyBoxImages[i] = new Image();
		skyBoxImages[i].src = "static/"+skyBoxImageNames[i];
	}
	for (let i = 0; i < 6; i++) {
		await skyBoxImages[i].decode();
		skyBoxBitmaps[i] = await createImageBitmap(skyBoxImages[i]);
	}
	let skyBoxTexture = device.createTexture({
		size: [
			skyBoxBitmaps[0].width,
			skyBoxBitmaps[0].height,
			6
		],
		dimension: "2d",
		format: "rgba8unorm",
		usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
			| GPUTextureUsage.RENDER_ATTACHMENT,
		viewFormats: [],
	});
	let skyBoxTextureView = skyBoxTexture.createView({
		dimension: "cube"
	});
	for (let i = 0; i < 6; i++) {
		device.queue.copyExternalImageToTexture(
			{
				source: skyBoxBitmaps[i]
			},
			{
				texture: skyBoxTexture,
				origin: [0, 0, i]
			},
			{ 
				width: skyBoxBitmaps[0].width,
				height: skyBoxBitmaps[0].height
			}
		);
	}

	let skyBoxSampler = device.createSampler({
		magFilter: "linear",
		minFilter: "linear",
		mipmapFilter: "linear",
	});
	
	let pass2Texture = device.createTexture({
		size: {
			width: window.screen.width,
			height: window.screen.height,
		},
		dimension: "2d",
		format: "rgba8uint",
		//new: usage: GPUTextureUsage.STORAGE_BINDING | TEXTURE_BINDING,
		usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
		viewFormats: [],
	});
	let pass2TextureView = pass2Texture.createView();
	
	let pass1Texture = device.createTexture({
		size: { /* make sure no out of bounds happens */
			width: Math.ceil(screen.width/3)*3,
			height: Math.ceil(screen.height/3)*3,
		},
		dimension: "2d",
		format: "r32uint",
		usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
		viewFormats: [],
	});
	let pass1TextureView = pass1Texture.createView();
	
	let depthTexture = device.createTexture({
		size: { /* make sure no out of bounds happens */
			width: Math.ceil(screen.width),
			height: Math.ceil(screen.height),
		},
		dimension: "2d",
		format: "r32float", // depth32float does not support STORAGE_BINDING ):
		usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
		viewFormats: [],
	});
	let depthTextureView = depthTexture.createView();
	
	let pass2ConstantsBuffer = device.createBuffer({
		size: 96,
		usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
	});
	let pass2ConstantsBufferMemory = new ArrayBuffer(96);
	let pass2ConstantsBufferUint32 = new Uint32Array(pass2ConstantsBufferMemory);
	let pass2ConstantsBufferFloat32 = new Float32Array(pass2ConstantsBufferMemory);
	
	let pass1ConstantsBuffer = device.createBuffer({
		size: 96,
		usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
	});
	let pass1ConstantsBufferMemory = new ArrayBuffer(96);
	let pass1ConstantsBufferUint32 = new Uint32Array(pass1ConstantsBufferMemory);
	let pass1ConstantsBufferFloat32 = new Float32Array(pass1ConstantsBufferMemory);


	let chunkBufferSize = maxStorageBufferBindingSize-16;
	let chunkBuffer = device.createBuffer({
		size: chunkBufferSize,
		usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
	});

	let accelerationBufferSize = 256*256*256*4;
	let accelerationBuffer = device.createBuffer({
		size: accelerationBufferSize,
		usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
	});
	
	var create_chunks = Module.cwrap("create_chunks", "number", ["number", "number"]);
	let returnedDataPtr = create_chunks(accelerationBufferSize, chunkBufferSize);
	let returnedDataArray = new Uint32Array(Module.HEAP32.buffer, returnedDataPtr, 2);
	abMemory = new Uint32Array(Module.HEAP32.buffer, returnedDataArray[0], accelerationBufferSize/4);
	cbMemory = new Uint32Array(Module.HEAP32.buffer, returnedDataArray[1], chunkBufferSize/4);

	// webgpu does not support SharedArrayBuffer :(
	let copiedAbMemory = abMemory.slice().buffer;
	let copiedCbMemory = cbMemory.slice().buffer;

	// setup all threads and workers
	var jobs_setup_function = Module.cwrap("jobs_setup", "number", []);
	let updateStructureAddress = jobs_setup_function();
	let updateStructureMemory = new Uint32Array(
		Module.HEAP32.buffer, updateStructureAddress, 10*4
	);
	
	{
		var uaBufferData = new Uint32Array(32768*64*2*4 + 4*2);
		var ucVoxelBufferData = new Uint32Array(32768*(8*8*4)*4);
		var ucIndexBufferData = new Uint32Array(16384*4);
		var ucChunkCount = 0;
	}
	// temporary code to test if everything is done correctly

	queue.writeBuffer(
		chunkBuffer,
		0, // bufferOffset
		copiedCbMemory,
		0, // dataOffset
		chunkBufferSize // size
	);
	
	queue.writeBuffer(
		accelerationBuffer,
		0, // bufferOffset
		copiedAbMemory,
		0, // dataOffset
		accelerationBufferSize // size
	);

	if (shaderDebugEnabled) {
		var debugMsgBuffer = device.createBuffer({
			size: 65536,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
			mappedAtCreation: false,
		});
		var mappedDebugMsgBuffer = device.createBuffer({
			size: 65536,
			usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
			mappedAtCreation: false,
		});
	}

	let pass2BindGroupLayout = device.createBindGroupLayout({
		entries: [
			{
				binding: 0,
				visibility: GPUShaderStage.COMPUTE,
				storageTexture: {
					access: "write-only",
					format: "rgba8uint",
					viewDimension: "2d"
				}
			},
			{
				binding: 1,
				visibility: GPUShaderStage.COMPUTE,
				buffer: { }
			},
			{
				binding: 2,
				visibility: GPUShaderStage.COMPUTE,
				buffer: {
					type: "read-only-storage",
				}
			},
			{
				binding: 3,
				visibility: GPUShaderStage.COMPUTE,
				buffer: {
					type: "read-only-storage",
				}
			},
			{
				binding: 4,
				visibility: GPUShaderStage.COMPUTE,
				texture: {
					multisampled: false,
					sampleType: "uint",
					viewDimension: "2d"
				}
			},
			{
				binding: 5,
				visibility: GPUShaderStage.COMPUTE,
				storageTexture: {
					access: "write-only",
					format: "r32float",
					viewDimension: "2d"
				}
			}
		]
	});
	
	let pass2BindGroup = device.createBindGroup({
		layout: pass2BindGroupLayout,
		entries: [
			{
				binding: 0,
				resource: pass2TextureView,
			},
			{
				binding: 1,
				resource: {
					buffer: pass2ConstantsBuffer,
				}
			},
			{
				binding: 2,
				resource: {
					buffer: chunkBuffer,
				}
			},
			{
				binding: 3,
				resource: {
					buffer: accelerationBuffer,
				}
			},
			{
				binding: 4,
				resource: pass1TextureView,
			},
			{
				binding: 5,
				resource: depthTextureView,
			}
		]
	});

	let pass1BindGroupLayout = device.createBindGroupLayout({
		entries: [
			{
				binding: 0,
				visibility: GPUShaderStage.COMPUTE,
				storageTexture: {
					access: "write-only",
					//format: navigator.gpu.getPreferredCanvasFormat(),
					format: "r32uint",
					viewDimension: "2d"
				}
			},
			{
				binding: 1,
				visibility: GPUShaderStage.COMPUTE,
				buffer: { }
			},
			{
				binding: 2,
				visibility: GPUShaderStage.COMPUTE,
				buffer: {
					type: "read-only-storage",
				}
			},
			{
				binding: 3,
				visibility: GPUShaderStage.COMPUTE,
				buffer: {
					type: "read-only-storage",
				}
			}
		]
	});

	let pass1BindGroup = device.createBindGroup({
		layout: pass1BindGroupLayout,
		entries: [
			{
				binding: 0,
				resource: pass1TextureView,
			},
			{
				binding: 1,
				resource: {
					buffer: pass1ConstantsBuffer,
				}
			},
			{
				binding: 2,
				resource: {
					buffer: chunkBuffer,
				}
			},
			{
				binding: 3,
				resource: {
					buffer: accelerationBuffer,
				}
			}
		]
	});

	if (shaderDebugEnabled) {
		var debugBindGroupLayout = device.createBindGroupLayout({
			entries: [
				{
					binding: 0,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "storage",
					}
				}
			]
		});
		var debugBindGroup = device.createBindGroup({
			layout: debugBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: {
						buffer: debugMsgBuffer,
					}
				}
			]
		});
	}
	
	{
		let pass2PipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: shaderDebugEnabled
					? [pass2BindGroupLayout, debugBindGroupLayout]
					: [pass2BindGroupLayout],
		});

		let pass2Source = await (await fetch("src/pass2.wgsl")).text();
		let pass2Module = device.createShaderModule({
			code: pass2Source
		});
		if (typeof(pass2Module.compilationInfo) == "function") {
			pass2Module.compilationInfo().then((info) => {
				for (let i = 0; i < info.messages.length; i++) {
					console.log("src/pass2.wgsl: " + info.messages[i].type +
						": " + info.messages[i].message);
				}
			});
		}
		var pass2Pipeline = device.createComputePipeline({
			layout: pass2PipelineLayout,
			compute: {
				module: pass2Module,
				entryPoint: "main",
			}
		});
	}

	{
		let pass1PipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: shaderDebugEnabled
					? [pass1BindGroupLayout, debugBindGroupLayout]
					: [pass1BindGroupLayout],
		});
		let pass1Source = await (await fetch("src/pass1.wgsl")).text();
		let pass1Module = device.createShaderModule({
			code: pass1Source
		});
		if (typeof(pass1Module.compilationInfo) == "function") {
			pass1Module.compilationInfo().then((info) => {
				for (let i = 0; i < info.messages.length; i++) {
					console.log("src/pass1.wgsl: " + info.messages[i].type +
						": " + info.messages[i].message);
				}
			});
		}
		var pass1Pipeline = device.createComputePipeline({
			layout: pass1PipelineLayout,
			compute: {
				module: pass1Module,
				entryPoint: "main",
			}
		});
	}

	{ // direct lighting (dl) pass, does some lightweight ray-tracing
		// rt in 2x lower resolution
		dlPassWidth = Math.ceil(Math.sqrt(lightingPercentage)*canvas.width);
		dlPassHeight = Math.ceil(Math.sqrt(lightingPercentage)*canvas.height);

		var dlPassConstantsBuffer = device.createBuffer({
			size: 96,
			usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
		});
		var dlPassConstantsBufferMemory = new ArrayBuffer(96);
		var dlPassConstantsBufferUint32 = new Uint32Array(dlPassConstantsBufferMemory);
		var dlPassConstantsBufferFloat32 = new Float32Array(dlPassConstantsBufferMemory);

		// not used :(
		var dlPassLightBuffer = device.createBuffer({
			size: 96,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
		});

		var dlPassTexture = device.createTexture({
			size: {
				width: Math.ceil(dlPassWidth),
				height: Math.ceil(dlPassHeight),
			},
			dimension: "2d",
			format: "rgba16float",
			usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
			viewFormats:[],
		});
		var dlPassTextureView = dlPassTexture.createView();

		var dlPassBindGroupLayout = device.createBindGroupLayout({
			entries: [
				{
					binding: 0,
					visibility: GPUShaderStage.COMPUTE,
					storageTexture: {
						access: "write-only",
						format: "rgba16float",
						viewDimension: "2d"
					},
				},
				{
					binding: 1,
					visibility: GPUShaderStage.COMPUTE,
					buffer: { }
				},
				{
					binding: 2,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 3,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 4,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "unfilterable-float",
						viewDimension: "2d",
					}
				},
				{
					binding: 5,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "uint",
						viewDimension: "2d",
					}
				},
				{
					binding: 6,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				}
			],
		});

		var dlPassBindGroup = device.createBindGroup({
			layout: dlPassBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: dlPassTextureView
				},
				{
					binding: 1,
					resource: {
						buffer: dlPassConstantsBuffer,
					}
				},
				{
					binding: 2,
					resource: {
						buffer: chunkBuffer
					}
				},
				{
					binding: 3,
					resource: {
						buffer: accelerationBuffer
					}
				},
				{
					binding: 4,
					resource: depthTextureView,
				},
				{
					binding: 5,
					resource: pass2TextureView,
				},
				{
					binding: 6,
					resource: {
						buffer: dlPassLightBuffer,
					}
				}
			]
		});

		var dlPipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: shaderDebugEnabled
					? [dlPassBindGroupLayout, debugBindGroupLayout]
					: [dlPassBindGroupLayout],
		});
		let dlPassComputeShaderSource = await (
			await fetch("src/directRT.wgsl")
		).text();
		let dlPassComputeShaderModule = device.createShaderModule({
			code: dlPassComputeShaderSource
		});
		if (typeof(dlPassComputeShaderModule.compilationInfo) == "function") {
			dlPassComputeShaderModule.compilationInfo().then((info) => {
				for (let i = 0; i < info.messages.length; i++) {
					console.log("src/directRT.wgsl: " + info.messages[i].type +
						": " + info.messages[i].message);
				}
			});
		}

		var dlPassComputePipeline = device.createComputePipeline({
			layout: dlPipelineLayout,
			compute: {
				module: dlPassComputeShaderModule,
				entryPoint: "main",
			}
		});
	}

/*
	{
		var gilCountX = 0;
		var gilCountY = 0;
		var gilListCount = 0;

		var gilPassTextureAtlas = device.createTexture({
			size: [
				2048,
				2048
			],
			dimension: "2d",
			format: "rgba16float",
			usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
				| GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.STORAGE_BINDING,
			viewFormats: [],
		});
		var gilPassTextureAtlasView = gilPassTextureAtlas.createView();

		var gilPassInfoBuffer = device.createBuffer({
			size: 1000000*8 * 4,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
		});

		var gilPassListBufferData = new Uint32Array(2048*2048*2);

		var gilPassListBuffer = device.createBuffer({
			size: 2048*2048 * 2 * 4,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
		});

		var gilPassConstantsBuffer = device.createBuffer({
			size: 96,
			usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
		});
		var gilPassConstantsBufferMemory = new ArrayBuffer(96);
		var gilPassConstantsBufferUint32 = new Uint32Array(gilPassConstantsBufferMemory);
		var gilPassConstantsBufferFloat32 = new Float32Array(gilPassConstantsBufferMemory);
		
		var gilPassBindGroupLayout = device.createBindGroupLayout({
			entries: [
				{
					binding: 0,
					visibility: GPUShaderStage.COMPUTE,
					storageTexture: {
						access: "write-only",
						format: "rgba16float",
						viewDimension: "2d"
					}
				},
				{
					binding: 1,
					visibility: GPUShaderStage.COMPUTE,
					buffer: { }
				},
				{
					binding: 2,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 3,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 4,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 5,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
			]
		});
		var gilPassBindGroup = device.createBindGroup({
			layout: gilPassBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: gilPassTextureAtlasView,
				},
				{
					binding: 1,
					resource: {
						buffer: gilPassConstantsBuffer
					}
				},
				{
					binding: 2,
					resource: {
						buffer: chunkBuffer
					}
				},
				{
					binding: 3,
					resource: {
						buffer: accelerationBuffer
					}
				},
				{
					binding: 4,
					resource: {
						buffer: gilPassInfoBuffer
					}
				},
				{
					binding: 5,
					resource: {
						buffer: gilPassListBuffer
					}
				}
			],
		});

		var gilPassPipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: shaderDebugEnabled
					? [gilPassBindGroupLayout, debugBindGroupLayout]
					: [gilPassBindGroupLayout],
		});
		let gilPassComputeShaderSource = await (
			await fetch("src/gil.wgsl")
		).text();
		let gilPassComputeShaderModule = device.createShaderModule({
			code: gilPassComputeShaderSource
		});
		if (typeof(gilPassComputeShaderModule.compilationInfo) == "function") {
			gilPassComputeShaderModule.compilationInfo().then((info) => {
				for (let i = 0; i < info.messages.length; i++) {
					console.log("src/directRT.wgsl: " + info.messages[i].type +
						": " + info.messages[i].message);
				}
			});
		}

		var gilPassComputePipeline = device.createComputePipeline({
			layout: gilPassPipelineLayout,
			compute: {
				module: gilPassComputeShaderModule,
				entryPoint: "main",
			}
		});
	}
	*/

	// this should probably be a fragment shader instead of a compute shader!
	{ // applyPass, applies lighting and color to the final texture
		var applyPassConstantsBuffer = device.createBuffer({
			size: 96,
			usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
		});
		var applyPassConstantsBufferMemory = new ArrayBuffer(96);
		var applyPassConstantsBufferUint32 = new Uint32Array(applyPassConstantsBufferMemory);
		var applyPassConstantsBufferFloat32 = new Float32Array(applyPassConstantsBufferMemory);

		var applyPassTexture = device.createTexture({
			size: {
				width: screen.width,
				height: screen.height,
			},
			dimension: "2d",
			format: "rgba8unorm",
			usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC
				| GPUTextureUsage.RENDER_ATTACHMENT
				| GPUTextureUsage.TEXTURE_BINDING,
			viewFormats: [],
		});
		var applyPassTextureView = applyPassTexture.createView();

		var applyPassBindGroupLayout = device.createBindGroupLayout({
			entries: [
				{
					binding: 0,
					visibility: GPUShaderStage.COMPUTE,
					storageTexture: {
						access: "write-only",
						format: "rgba8unorm",
						viewDimension: "2d"
					}
				},
				{
					binding: 1,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "uint",
						viewDimension: "2d"
					}
				},
				{
					binding: 2,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "float",
						viewDimension: "2d"
					}
				},
				{
					binding: 3,
					visibility: GPUShaderStage.COMPUTE,
					buffer: { }
				},
				{
					binding: 4,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "float",
						viewDimension: "cube"
					}
				},
				{
					binding: 5,
					visibility: GPUShaderStage.COMPUTE,
					sampler: { },
				},
				{
					binding: 6,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "float",
						viewDimension: "2d",
					}
				},
				{
					binding: 7,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "unfilterable-float",
						viewDimension: "2d",
					}
				},
				/*
				{
					binding: 8,
					visibility: GPUShaderStage.COMPUTE,
					texture: {
						multisampled: false,
						sampleType: "unfilterable-float",
						viewDimension: "2d",
					}
				},
				{
					binding: 9,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 10,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 11,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				*/
			]
		});

		var applyPassBindGroup = device.createBindGroup({
			layout: applyPassBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: applyPassTextureView,
				},
				{
					binding: 1,
					resource: pass2TextureView,
				},
				{
					binding: 2,
					resource: dlPassTextureView,
				},
				{
					binding: 3,
					resource: {
						buffer: applyPassConstantsBuffer
					}
				},
				{
					binding: 4,
					resource: skyBoxTextureView
				},
				{
					binding: 5,
					resource: skyBoxSampler,
				},
				{
					binding: 6,
					resource: blockTextureView,
				},
				{
					binding: 7,
					resource: depthTextureView,
				},
				/*
				{
					binding: 8,
					resource: gilPassTextureAtlasView,
				},
				{
					binding: 9,
					resource: {
						buffer: chunkBuffer
					}
				},
				{
					binding: 10,
					resource: {
						buffer: accelerationBuffer
					}
				},
				{
					binding: 11,
					resource: {
						buffer: gilPassInfoBuffer
					}
				},
				*/
			]
		});

		var applyPassPipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: shaderDebugEnabled
					? [applyPassBindGroupLayout, debugBindGroupLayout]
					: [applyPassBindGroupLayout],
		});

		let applyPassShaderSource = await (
			await fetch("src/applyPass.wgsl")
		).text();
		let applyPassShaderModule = device.createShaderModule({
			code: applyPassShaderSource
		});
		if (typeof(applyPassShaderModule.compilationInfo) == "function") {
			applyPassShaderModule.compilationInfo().then((info) => {
				for (let i = 0; i < info.messages.length; i++) {
					console.log("src/applyPass.wgsl: " + info.messages[i].type +
						": " + info.messages[i].message);
				}
			});
		}
		var applyPassPipeline = device.createComputePipeline({
			layout: applyPassPipelineLayout,
			compute: {
				module: applyPassShaderModule,
				entryPoint: "main",
			}
		});
	}

	{ // update accelerationBuffer (ua) compute pipeline creation
		// NOTE: this is only used for smaller writes!
		// for entire writes queue.writeBuffer is used instead
	
		// (val+3) & ~3 to force divisible by 4
		var uaBufferSize = (32768*64*2*4 + 4*2+15) & ~15;
		var uaBuffer = device.createBuffer({
			size: uaBufferSize,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
			mappedAtCreation: false
		});

		var uaBindGroupLayout = device.createBindGroupLayout({
			entries: [
				{
					binding: 0,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "storage",
					}
				},
				{
					binding: 1,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
			]
		});

		var uaBindGroup = device.createBindGroup({
			layout: uaBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: {
						buffer: accelerationBuffer,
					}
				},
				{
					binding: 1,
					resource: {
						buffer: uaBuffer,
					}
				},
			]
		});

		var uaPipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: shaderDebugEnabled
				? [uaBindGroupLayout, debugBindGroupLayout]
				: [uaBindGroupLayout]
		});

		let uaComputeShaderSource = await (
			await fetch("src/update_acceleration.wgsl")
		).text();
		let uaComputeShaderModule = device.createShaderModule({
			code: uaComputeShaderSource
		});
		if (typeof(uaComputeShaderModule.compilationInfo) == "function") {
			uaComputeShaderModule.compilationInfo().then((info) => {
				for (let i = 0; i < info.messages.length; i++) {
					console.log("src/update_acceleration.wgsl: "
						+ info.messages[i].type +
						": " + info.messages[i].message);
				}
			});
		}

		var uaComputePipeline = device.createComputePipeline({
			layout: uaPipelineLayout,
			compute: {
				module: uaComputeShaderModule,
				entryPoint: "main",
			}
		});
	}

	{ // update chunkBuffer (uc) compute pipeline creation
		// max workgroup count (x): 32768
		// max chunks/frame = 32768/2 = 16384
		var ucVoxelBufferSize = (32768*(8*8*4)*4 + 15) & ~15; var ucVoxelBuffer = device.createBuffer({ size: ucVoxelBufferSize,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
			mappedAtCreation: false
		});
		var ucIndexBufferSize = (16384*4 + 15) & ~15;
		var ucIndexBuffer = device.createBuffer({
			size: ucIndexBufferSize,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
			mappedAtCreation: false
		});

		var ucBindGroupLayout = device.createBindGroupLayout({
			entries: [
				{
					binding: 0,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "storage",
					}
				},
				{
					binding: 1,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage",
					}
				},
				{
					binding: 2,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage"
					}
				}
			]
		});

		var ucBindGroup = device.createBindGroup({
			layout: ucBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: {
						buffer: chunkBuffer,
					}
				},
				{
					binding: 1,
					resource: {
						buffer: ucVoxelBuffer,
					}
				},
				{
					binding: 2,
					resource: {
						buffer: ucIndexBuffer,
					}
				}
			]
		});

		var ucPipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: [ucBindGroupLayout]
		});

		let ucComputeShaderSource = await (
			await fetch("src/update_chunk.wgsl")
		).text();
		let ucComputeShaderModule = device.createShaderModule({
			code: ucComputeShaderSource
		});
		if (typeof(ucComputeShaderModule.compilationInfo) == "function") {
			ucComputeShaderModule.compilationInfo().then((info) => {
				for (let i = 0; i < info.messages.length; i++) {
					console.log("src/update_chunk.wgsl: " + info.messages[i].type +
						": " + info.messages[i].message);
				}
			});
		}
		
		var ucComputePipeline = device.createComputePipeline({
			layout: ucPipelineLayout,
			compute: {
				module: ucComputeShaderModule,
				entryPoint: "main",
			}
		});
	}
	/*
	{
		var ugilBufferData = new Uint32Array(32768*64*2*4 + 4*2);
		var ugilBufferSize = uaBufferSize;
		var ugilBuffer = device.createBuffer({
			size: ugilBufferSize,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
			mappedAtCreation: false,
		});
		// im just gonna reuse ua since I can not be bothered to write
		// another shader
		var ugilBindGroup = device.createBindGroup({
			layout: uaBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: {
						buffer: gilPassInfoBuffer,
					}
				},
				{
					binding: 1,
					resource: {
						buffer: ugilBuffer,
					}
				},
			]
		});
		var ugilComputePipeline = uaComputePipeline;
	}
	*/
	let commandEncoder;
	
	let debugStringElement = document.getElementById("debugString");
	if (!shaderDebugEnabled) {
		debugStringElement.style = "display: none;";
	}

	let frames = [];

	function resize_canvas()
	{
		canvas.width = window.innerWidth;
		canvas.height = window.innerHeight;

		dlPassWidth = Math.ceil(Math.sqrt(lightingPercentage)*canvas.width);
		dlPassHeight = Math.ceil(Math.sqrt(lightingPercentage)*canvas.height);
		dlPassTexture = device.createTexture({
			size: {
				width: Math.ceil(dlPassWidth),
				height: Math.ceil(dlPassHeight),
			},
			dimension: "2d",
			format: "rgba16float",
			usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
			viewFormats:[],
		});
		dlPassTextureView = dlPassTexture.createView();

		// TODO: make bind group creation into functions
		dlPassBindGroup = device.createBindGroup({
			layout: dlPassBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: dlPassTextureView
				},
				{
					binding: 1,
					resource: {
						buffer: dlPassConstantsBuffer,
					}
				},
				{
					binding: 2,
					resource: {
						buffer: chunkBuffer
					}
				},
				{
					binding: 3,
					resource: {
						buffer: accelerationBuffer
					}
				},
				{
					binding: 4,
					resource: depthTextureView,
				},
				{
					binding: 5,
					resource: pass2TextureView,
				},
				{
					binding: 6,
					resource: {
						buffer: dlPassLightBuffer,
					}
				}
			]
		});
		applyPassBindGroup = device.createBindGroup({
			layout: applyPassBindGroupLayout,
			entries: [
				{
					binding: 0,
					resource: applyPassTextureView,
				},
				{
					binding: 1,
					resource: pass2TextureView,
				},
				{
					binding: 2,
					resource: dlPassTextureView,
				},
				{
					binding: 3,
					resource: {
						buffer: applyPassConstantsBuffer
					}
				},
				{
					binding: 4,
					resource: skyBoxTextureView
				},
				{
					binding: 5,
					resource: skyBoxSampler,
				},
				{
					binding: 6,
					resource: blockTextureView,
				},
				{
					binding: 7,
					resource: depthTextureView,
				},
				/*
				{
					binding: 8,
					resource: gilPassTextureAtlasView,
				},
				{
					binding: 9,
					resource: {
						buffer: chunkBuffer
					}
				},
				{
					binding: 10,
					resource: {
						buffer: accelerationBuffer
					}
				},
				{
					binding: 11,
					resource: {
						buffer: gilPassInfoBuffer
					}
				},
				*/

			]
		});
	}
	
	let oldSelectedByScroll = selectedByScroll;
	let currentHotbarItem = document.getElementById("inv"+selectedByScroll);
	currentHotbarItem.style = "border: 2px solid white";

	let dlPassLightBufferData = new Uint32Array(96/4);

	let oldTime = performance.now()/1000;
	async function render_frame() {
		if (window.innerWidth != canvas.width || window.innerHeight != canvas.height
				|| shouldReset) {
			shouldReset = false;
			resize_canvas();
		}
		let newTime = performance.now()/1000;
		let deltaTime = newTime-oldTime;
		oldTime = newTime;

		Module.HEAPF32[(sharedWithJSAddress>>2)+3] = cameraAxis[0];
		Module.HEAPF32[(sharedWithJSAddress>>2)+4] = cameraAxis[1];
		Module.HEAPF32[(sharedWithJSAddress>>2)+5] = cameraAxis[2];

		if (inMenu) {
			showMenu();
		} else {
			showHUD();
		}
		
		if (inMenu) {
			mouseYaw += deltaTime;

			cameraAxis[0] = Math.cos(mouseYaw*Math.PI/180)
				* Math.cos(mousePitch*Math.PI/180);
			cameraAxis[1] = Math.sin(mousePitch*Math.PI/180);
			cameraAxis[2] = Math.sin(mouseYaw*Math.PI/180)
				* Math.cos(mousePitch*Math.PI/180);
		}

		if (oldSelectedByScroll != selectedByScroll) {
			let oldItem = document.getElementById("inv"+oldSelectedByScroll);
			oldItem.style = "border: 2px solid black";
			currentHotbarItem = document.getElementById("inv"+selectedByScroll);
			currentHotbarItem.style = "border: 2px solid white";
			oldSelectedByScroll = selectedByScroll;
		}
		
		while (frames.length > 0 && (newTime) > (frames[0]+1)) {
			frames.shift();
		}
		frames.push(newTime);


		let chunkStr = "\nChunk Info:";
		{
			let debugVoxelIndex = (Math.floor(get_camera_pos()[0] / 8))
				| ((Math.floor(get_camera_pos()[1] / 8))<<8)
				| ((Math.floor(get_camera_pos()[2] / 8))<<16);
			chunkStr += "\n\tacceleration index: " + debugVoxelIndex;
			let debugVoxel = abMemory[debugVoxelIndex];
			chunkStr += "\n\tRaw data: " + debugVoxel;
			switch ((3 << 30) & debugVoxel) {
				case 0:
					chunkStr += "\n\tType: voxel";
					break;
				case 1<<30:
					chunkStr += "\n\tType: should be unreachable! How did this happen?";
					break;
				case 2<<30:
					chunkStr += "\n\tType: air";
					break;
				case 3<<30:
					chunkStr += "\n\tType: miss/border";
					break;
			}
			if ((debugVoxel & (3 << 30)) == 2<<30) {
				chunkStr += "\n\txNeg: " + ((debugVoxel) & 0b11111);
				chunkStr += "\n\tyNeg: " + ((debugVoxel >> 5) & 0b11111);
				chunkStr += "\n\tzNeg: " + ((debugVoxel >> 10) & 0b11111);
				chunkStr += "\n\txPos: " + ((debugVoxel >> 15) & 0b11111);
				chunkStr += "\n\tyPos: " + ((debugVoxel >> 20) & 0b11111);
				chunkStr += "\n\tzPos: " + ((debugVoxel >> 25) & 0b11111);
			}
			if ((debugVoxel & (3 << 30)) == 0) {
				let vx = Math.floor(get_camera_pos()[0]) % 8;
				let vy = Math.floor(get_camera_pos()[1]) % 8;
				let vz = Math.floor(get_camera_pos()[2]) % 8;
				
				let vcx = Math.floor(get_camera_pos()[0]) % 32;
				let vcy = Math.floor(get_camera_pos()[1]) % 32;
				let vcz = Math.floor(get_camera_pos()[2]) % 32;

				let voxel = cbMemory[debugVoxel|vx|(vy<<3)|(vz<<6)];
				chunkStr += "\nVoxel Info: (" +vx + "," +vy+","+vz+")" +
					", (" + vcx + ", " + vcy + ", " + vcz + ")"
					+ "\n\tRaw data: " + voxel;
				chunkStr += "\n\txNeg: " + ((voxel) & 0b11111);
				chunkStr += "\n\tyNeg: " + ((voxel >> 5) & 0b11111);
				chunkStr += "\n\tzNeg: " + ((voxel >> 10) & 0b11111);
				chunkStr += "\n\txPos: " + ((voxel >> 15) & 0b11111);
				chunkStr += "\n\tyPos: " + ((voxel >> 20) & 0b11111);
				chunkStr += "\n\tzPos: " + ((voxel >> 25) & 0b11111);
			}
		}

		let debugString =
			/*"FPS: " + frames.length +*/
			"debug:" +
			"\ncamera pos x: " + get_camera_pos()[0] + 
			"\ncamera pos y: " + get_camera_pos()[1] + 
			"\ncamera pos z: " + get_camera_pos()[2] + 
			"\ncamera dir x: " + cameraAxis[0] + 
			"\ncamera dir y: " + cameraAxis[1] + 
			"\ncamera dir z: " + cameraAxis[2] + 
			"\nacceleration: " +
			Math.floor(get_camera_pos()[0] / 8) + ", " +
			Math.floor(get_camera_pos()[1] / 8) + ", " +
			Math.floor(get_camera_pos()[2] / 8) +
			"\nshaderDebugEnabled: " + shaderDebugEnabled +
			"\nAPI: webgpu" + chunkStr;
		debugStringElement.innerHTML = debugString;
		
		calculate_movement(deltaTime);

		let swapTexture = context.getCurrentTexture();
		let swapTextureView = swapTexture.createView();

		/*
		 * create raydir helpers
		 * could be replaced with projection matrix
		 */
		let up = [0.0, 1.0, 0.0];

		let fov = (Math.PI/180.0)*90.0;
		let invAspectRatio = canvas.height / canvas.width;
		let focalLength = 1.0;
		let width = focalLength * Math.tan(fov*0.5) * 2.0;
		let height = width * invAspectRatio;

		// needs to be normalized! (which it hopefully is)
		let axis = [...cameraAxis];

		let toHorizontalSide = scale_vec3_by_constant(
			normalize_vec3(cross_vec3(axis, up)),
			width*0.5
		);
		let toVerticalSide = scale_vec3_by_constant(
			normalize_vec3(cross_vec3(axis, toHorizontalSide)),
			height*0.5
		);
		let scaledAxis = scale_vec3_by_constant(axis, focalLength);

		let cameraPosition = [0, 0, 0]
		let faceCenter = add_vec3(scaledAxis, cameraPosition);
		
		let vert0 = [0, 0, 0];
		let vert1 = [0, 0, 0];
		let vert2 = [0, 0, 0];
		let vert3 = [0, 0, 0];

		vert0 = add_vec3(
			sub_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		vert1 = add_vec3(
			add_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		vert2 = sub_vec3(
			sub_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		vert3 = sub_vec3(
			add_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		
		pass2ConstantsBufferUint32[0] = canvas.width;
		pass2ConstantsBufferUint32[1] = canvas.height;
		// 2-3 = padding, cameraPos:
		pass2ConstantsBufferFloat32[4] = get_camera_pos()[0];
		pass2ConstantsBufferFloat32[5] = get_camera_pos()[1];
		pass2ConstantsBufferFloat32[6] = get_camera_pos()[2];
		// vert0:
		pass2ConstantsBufferFloat32[8] = vert0[0];
		pass2ConstantsBufferFloat32[9] = vert0[1];
		pass2ConstantsBufferFloat32[10] = vert0[2];
		// vert1:
		pass2ConstantsBufferFloat32[12] = vert1[0];
		pass2ConstantsBufferFloat32[13] = vert1[1];
		pass2ConstantsBufferFloat32[14] = vert1[2];
		// vert2:
		pass2ConstantsBufferFloat32[16] = vert2[0];
		pass2ConstantsBufferFloat32[17] = vert2[1];
		pass2ConstantsBufferFloat32[18] = vert2[2];
		// vert3:
		pass2ConstantsBufferFloat32[20] = vert3[0];
		pass2ConstantsBufferFloat32[21] = vert3[1];
		pass2ConstantsBufferFloat32[22] = vert3[2];
		queue.writeBuffer(
			pass2ConstantsBuffer,
			0, // bufferOffset
			pass2ConstantsBufferMemory,
			0, // dataOffset
			96 // size
		);

		pass1ConstantsBufferUint32[0] = canvas.width;
		pass1ConstantsBufferUint32[1] = canvas.height;
		pass1ConstantsBufferUint32[2] = 0;
		pass1ConstantsBufferUint32[3] = 0;
		pass1ConstantsBufferFloat32[4] = get_camera_pos()[0];
		pass1ConstantsBufferFloat32[5] = get_camera_pos()[1];
		pass1ConstantsBufferFloat32[6] = get_camera_pos()[2];
		pass1ConstantsBufferFloat32[8] = vert0[0];
		pass1ConstantsBufferFloat32[9] = vert0[1];
		pass1ConstantsBufferFloat32[10] = vert0[2];
		pass1ConstantsBufferFloat32[12] = vert1[0];
		pass1ConstantsBufferFloat32[13] = vert1[1];
		pass1ConstantsBufferFloat32[14] = vert1[2];
		pass1ConstantsBufferFloat32[16] = vert2[0];
		pass1ConstantsBufferFloat32[17] = vert2[1];
		pass1ConstantsBufferFloat32[18] = vert2[2];
		pass1ConstantsBufferFloat32[20] = vert3[0];
		pass1ConstantsBufferFloat32[21] = vert3[1];
		pass1ConstantsBufferFloat32[22] = vert3[2];
		queue.writeBuffer(
			pass1ConstantsBuffer,
			0, // bufferOffset
			pass1ConstantsBufferMemory,
			0, // dataOffset
			96 // size
		);

		dlPassConstantsBufferUint32[0] = dlPassWidth;
		dlPassConstantsBufferUint32[1] = dlPassHeight;
		dlPassConstantsBufferUint32[2] = canvas.width;
		dlPassConstantsBufferUint32[3] = canvas.height;
		dlPassConstantsBufferFloat32[4] = get_camera_pos()[0];
		dlPassConstantsBufferFloat32[5] = get_camera_pos()[1];
		dlPassConstantsBufferFloat32[6] = get_camera_pos()[2];
		dlPassConstantsBufferFloat32[8] = vert0[0];
		dlPassConstantsBufferFloat32[9] = vert0[1];
		dlPassConstantsBufferFloat32[10] = vert0[2];
		dlPassConstantsBufferFloat32[12] = vert1[0];
		dlPassConstantsBufferFloat32[13] = vert1[1];
		dlPassConstantsBufferFloat32[14] = vert1[2];
		dlPassConstantsBufferFloat32[16] = vert2[0];
		dlPassConstantsBufferFloat32[17] = vert2[1];
		dlPassConstantsBufferFloat32[18] = vert2[2];
		dlPassConstantsBufferFloat32[20] = vert3[0];
		dlPassConstantsBufferFloat32[21] = vert3[1];
		dlPassConstantsBufferFloat32[22] = vert3[2];
		queue.writeBuffer(
			dlPassConstantsBuffer,
			0,
			dlPassConstantsBufferMemory,
			0,
			96
		);
		
		/*
		gilPassConstantsBufferUint32[0] = gilListCount;
		gilPassConstantsBufferUint32[1] = 0;
		gilPassConstantsBufferUint32[2] = 0;
		gilPassConstantsBufferUint32[3] = 0;
		gilPassConstantsBufferFloat32[4] = get_camera_pos()[0];
		gilPassConstantsBufferFloat32[5] = get_camera_pos()[1];
		gilPassConstantsBufferFloat32[6] = get_camera_pos()[2];
		gilPassConstantsBufferFloat32[8] = vert0[0];
		gilPassConstantsBufferFloat32[9] = vert0[1];
		gilPassConstantsBufferFloat32[10] = vert0[2];
		gilPassConstantsBufferFloat32[12] = vert1[0];
		gilPassConstantsBufferFloat32[13] = vert1[1];
		gilPassConstantsBufferFloat32[14] = vert1[2];
		gilPassConstantsBufferFloat32[16] = vert2[0];
		gilPassConstantsBufferFloat32[17] = vert2[1];
		gilPassConstantsBufferFloat32[18] = vert2[2];
		gilPassConstantsBufferFloat32[20] = vert3[0];
		gilPassConstantsBufferFloat32[21] = vert3[1];
		gilPassConstantsBufferFloat32[22] = vert3[2];
		queue.writeBuffer(
			gilPassConstantsBuffer,
			0,
			gilPassConstantsBufferMemory,
			0,
			96
		);
		*/

		applyPassConstantsBufferUint32.set(pass2ConstantsBufferUint32);
		queue.writeBuffer(
			applyPassConstantsBuffer,
			0,
			applyPassConstantsBufferMemory,
			0,
			96
		);

		// count
		dlPassLightBufferData[0] = 0;
		// array. first 16 bits = x. 16 bits = y. 32 bits = z
		//dlPassLightBufferData[1] = (1023 << 16) | 1023;
		//dlPassLightBufferData[2] = 1023;
		queue.writeBuffer(
			dlPassLightBuffer,
			0,
			dlPassLightBufferData.buffer,
			0,
			96
		);
		
		commandEncoder = device.createCommandEncoder();
		if (shaderDebugEnabled) {
			if (typeof(commandEncoder.clearBuffer) == "function") {
				commandEncoder.clearBuffer(debugMsgBuffer, 0, 65536);
			}
		}

		/*
		let pass1PassEncoder = commandEncoder.beginComputePass();
		pass1PassEncoder.setPipeline(pass1Pipeline);
		pass1PassEncoder.setBindGroup(0, pass1BindGroup);
		if (shaderDebugEnabled) {
			pass1PassEncoder.setBindGroup(1, debugBindGroup);
		}
		pass1PassEncoder.dispatchWorkgroups(
			Math.ceil(canvas.width/8/3),
			Math.ceil(canvas.height/8/3)
		);
		if (pass1PassEncoder.end == undefined) {
			pass1PassEncoder.endPass();
		} else {
			pass1PassEncoder.end();
		}
		*/

		let pass2PassEncoder = commandEncoder.beginComputePass();
		pass2PassEncoder.setPipeline(pass2Pipeline);
		pass2PassEncoder.setBindGroup(0, pass2BindGroup);
		if (shaderDebugEnabled) {
			pass2PassEncoder.setBindGroup(1, debugBindGroup);
		}
		pass2PassEncoder.dispatchWorkgroups(
			Math.ceil(canvas.width/8),
			Math.ceil(canvas.height/8)
		);
		if (pass2PassEncoder.end == undefined) {
			pass2PassEncoder.endPass();
		} else {
			pass2PassEncoder.end();
		}

		/*
		if (gilListCount != 0) {
			let gilPassEncoder = commandEncoder.beginComputePass();
			gilPassEncoder.setPipeline(gilPassComputePipeline);
			gilPassEncoder.setBindGroup(0, gilPassBindGroup);
			if (shaderDebugEnabled) {
				gilPassEncoder.setBindGroup(1, debugBindGroup);
			}
			gilPassEncoder.dispatchWorkgroups(
				gilCountX,
				gilCountY
			);
			if (gilPassEncoder.end == undefined) {
				gilPassEncoder.endPass();
			} else {
				gilPassEncoder.end();
			}
		}
		*/

		let dlPassEncoder = commandEncoder.beginComputePass();
		{
			dlPassEncoder.setPipeline(dlPassComputePipeline);
			dlPassEncoder.setBindGroup(0, dlPassBindGroup);
			if (shaderDebugEnabled) {
				dlPassEncoder.setBindGroup(1, debugBindGroup);
			}
			dlPassEncoder.dispatchWorkgroups(
				Math.ceil(dlPassWidth/8),
				Math.ceil(dlPassHeight/8)
			);
			if (dlPassEncoder.end == undefined) {
				dlPassEncoder.endPass();
			} else {
				dlPassEncoder.end();
			}
		}

		let applyPassEncoder = commandEncoder.beginComputePass();
		{
			applyPassEncoder.setPipeline(applyPassPipeline);
			applyPassEncoder.setBindGroup(0, applyPassBindGroup);
			if (shaderDebugEnabled) {
				applyPassEncoder.setBindGroup(1, debugBindGroup);
			}
			applyPassEncoder.dispatchWorkgroups(
				Math.ceil(canvas.width/8),
				Math.ceil(canvas.height/8)
			);
			if (applyPassEncoder.end == undefined) {
				applyPassEncoder.endPass();
			} else {
				applyPassEncoder.end();
			}
		}

		commandEncoder.copyTextureToTexture(
			{ // source
				texture: applyPassTexture
			},
			{ // destination
				texture: swapTexture,
			},
			{
				width: canvas.width,
				height: canvas.height,
			}
		);
		if (shaderDebugEnabled) {
			commandEncoder.copyBufferToBuffer(debugMsgBuffer, 0, mappedDebugMsgBuffer, 0, 65536);
		}
		device.queue.submit([commandEncoder.finish()]);

		if (shaderDebugEnabled) {
			//device.queue.onSubmittedWorkDone().then(()=> {
				mappedDebugMsgBuffer.mapAsync(GPUMapMode.READ).then(() => {
					let memory = mappedDebugMsgBuffer.getMappedRange();
					let array = new Uint32Array(memory);
					if (array[0] != 0) {
						let str = "";
						for (let i = 1; i < array[0]+1; i+=2) {
							let type = array[i];
							let char = array[i+1];
							
							let strToAdd;
							if (type == 0) {
								strToAdd = String.fromCharCode(char);
							} else if (type == 1) {
								(new Uint32Array(castMemory))[0] = char;
								strToAdd = (new Float32Array(castMemory))[0];
							} else if (type == 2) {
								strToAdd = char;
							} else {
								strToAdd = "\ndebugInfo: " + String.fromCharCode(char);
							}
							str += strToAdd;
						}
						console.log(str);
					}
					mappedDebugMsgBuffer.unmap();
					
					requestAnimationFrame(render_frame);
				});
			//});
		} else {
			requestAnimationFrame(render_frame);
		}
		let debugMap = {}
		// update voxels here
		{
			ucChunkCount = 0;
			uaBufferData[0] = 0;

			// used to not sent duplicates to the GPU during update
			// only send the most recently updated value to the GPU
			var uaDuplicateFinderMap = { };
			var ucDuplicateFinderMap = { };

			let updateStructure = {};
			let names = [
				"ucIndices",
				"uaIndices",
				
				"ucIndicesCount",
				"uaIndicesCount",

				"ucWriteIndex",
				"ucReadIndex",
				
				"uaWriteIndex",
				"uaReadIndex",
				"transformState",
			]
			for (let i = 0; i < names.length; i++) {
				updateStructure[names[i]] = Atomics.load(updateStructureMemory, i);
			}
			//console.log(updateStructure.transformState);
			/*
			if (updateStructure.transformState == 2) {
				let cameraPos = get_camera_pos();
				cameraPos[0] = cameraPos[0]%8 + 1024;
				cameraPos[1] = cameraPos[1]%8 + 1024;
				cameraPos[2] = cameraPos[2]%8 + 1024;
				set_camera_pos(cameraPos);

				copiedAbMemory = abMemory.slice().buffer;
				
				let result = queue.writeBuffer(
					accelerationBuffer,
					0, // bufferOffset
					copiedAbMemory,
					0, // dataOffset
					accelerationBufferSize // size
				);
				// always the case but
				// ensure that this is called after queue.writeBuffer
				if (result == undefined) {
					console.log("?");
					Atomics.store(updateStructureMemory, 8, 0);
					updateStructure.uaReadIndex = updateStructure.uaWriteIndex;
				}
			}
			*/
			while (true) {
				let writeIndex = updateStructure.ucWriteIndex;
				if (updateStructure.ucReadIndex == writeIndex) {
					break;
				}
				if ((ucChunkCount<<1) >= 32768-1) {
					break;
				}
				let accelerationIndex = Atomics.load(Module.HEAP32,
					(updateStructure.ucIndices>>2)+updateStructure.ucReadIndex
				);
				updateStructure.ucReadIndex = (updateStructure.ucReadIndex+1)
					& updateStructure.ucIndicesCount;
				Atomics.store(updateStructureMemory, 5, updateStructure.ucReadIndex);
				let chunkOffset = Atomics.load(abMemory, accelerationIndex);
				// has already been freed
				if ((chunkOffset & (3 << 30)) != 0) {
					continue;
				}
				ucIndexBufferData[ucChunkCount] = chunkOffset;
				
				let duplicateAddress = ucDuplicateFinderMap[accelerationIndex];
				let location;
				if (duplicateAddress == undefined) {
					location = ucChunkCount*512;
					ucChunkCount++;
					ucDuplicateFinderMap[accelerationIndex] = location;
				} else {
					location = duplicateAddress;
				}
				for (let i = 0; i < 512; i++) {
					ucVoxelBufferData[location+i] = Atomics.load(cbMemory, chunkOffset+i);
				}
				// finally push to ua buffer too
				uaBufferData[2+uaBufferData[0]++] = accelerationIndex;
				uaBufferData[2+uaBufferData[0]++] = chunkOffset;
			}

			while (true) {
				if (updateStructure.uaReadIndex == updateStructure.uaWriteIndex) {
					break;
				}
				if (Math.ceil(uaBufferData[0]/2/64) >= 32768-1) {
					break;
				}
				let accelerationIndex = Atomics.load(Module.HEAP32,
					(updateStructure.uaIndices>>2)+updateStructure.uaReadIndex
				);
				updateStructure.uaReadIndex = (updateStructure.uaReadIndex+1)
					% updateStructure.uaIndicesCount;
				Atomics.store(updateStructureMemory, 7, updateStructure.uaReadIndex);

				let duplicateAddress = uaDuplicateFinderMap[accelerationIndex];

				let mem = Atomics.load(abMemory, accelerationIndex);
				if (duplicateAddress == undefined) {
					let location = uaBufferData[0];
					uaBufferData[2+uaBufferData[0]++] = accelerationIndex;
					uaBufferData[2+uaBufferData[0]++] = mem;
					uaDuplicateFinderMap[accelerationIndex] = location;
				} else {
					uaBufferData[2+duplicateAddress] = accelerationIndex;
					uaBufferData[2+duplicateAddress+1] = mem;
				}
			}
		}
		/*
		{
			ugilBufferData[0] = 0; // count = 0
			while (true) {
				let writeIndex = Atomics.load(gilPushData, 2);
				let readIndex = gilPushData[3];
				if (writeIndex == readIndex) {
					break;
				}
				if (Math.ceil(ugilBufferData[0]/2/64) >= 32768-1) {
					console.log("How the fuck did this happen? This should not be possible");
					break;
				}
				let index = Atomics.load(Module.HEAP32,
					(gilPushData[1]>>2)+readIndex,
				);
				let mem = Atomics.load(Module.HEAP32,
					(gilPushData[0]>>2)+index
				);
				// remake mem for easier use for the gpu
				// 1D coord -> 2d coord
				const LM_NONE = 0;
				const LM_16x16 = 1;
				const LM_8x8 = 2;
				const LM_4x4 = 3;
				const LM_2x2 = 4;
				const LM_1x1 = 5;
				let type = mem & 7;
				let address = mem >> 3;
				let coordX;
				let coordY;
				if (type == LM_1x1) {
					coordX = Math.floor(address%2048);
					coordY = 1792 + Math.floor(address/2048);
				}
				let newMem = type | coordX << 3 | (coordY << 14);
				// reorder faces from xyzNeg xyzPos -> xNegPos yNegPos zNegPos
				let reorder = [0, 1, 2, 4, 6, 3, 5, 7]
				let face = index & 7;
				index &= ~7;
				index |= reorder[face];

				gilPushData[3] = (readIndex + 1) % gilPushData[4];
				ugilBufferData[2+ugilBufferData[0]++] = index;
				ugilBufferData[2+ugilBufferData[0]++] = newMem;
				console.log("js: " + index + ": " + newMem+ ", " + mem + ", " + address);
			}
		}
		*/
		/*
		{
			gilInfoBufferAddress = gilPushData[0];
			let lightmapState = gilPushData[8];
			if (lightmapState == 1) {
				gilListCount = gilPushData[6];
				let count = 0;

				for (let i = 0; i < gilListCount*2; i++) {
					gilPassListBufferData[i] = Module.HEAP32[(gilPushData[5]>>2)+i];
					count += gilPassListBufferData[i];
				}
				if (gilListCount != 0) {
					// best memory barrier
					if (count != 0) {
						gilPushData[8] = 0;
					} else {
						console.log("literally impossible. If you are seeing this then I messed up");
					}
					let gilCountTotal = Math.ceil(gilListCount/64);
					gilCountX = gilCountTotal % 32768;
					gilCountY = Math.ceil(gilCountTotal / 32768);
					//console.log("x: "+gilCountX + ", y: " + gilCountY);
					queue.writeBuffer(
						gilPassListBuffer,
						0,
						gilPassListBufferData,
						0,
						((gilListCount*2)+3)&~3
					);
				} else {
					gilPushData[8] = 0;
					let gilCountTotal = 0;
					gilCountX = gilCountTotal % 32768;
					gilCountY = Math.ceil(gilCountTotal / 32768);
				}
			}
		}
		*/
		if (uaBufferData[0] != 0) {
			queue.writeBuffer(
				uaBuffer,
				0,
				uaBufferData,
				0,
				((uaBufferData[0]+2)+3)&~3
			);
			let testCommandEncoder = device.createCommandEncoder();
			let testPassEncoder = testCommandEncoder.beginComputePass();
			testPassEncoder.setPipeline(uaComputePipeline);
			testPassEncoder.setBindGroup(0, uaBindGroup);
			if (shaderDebugEnabled) {
				// does not work since the map call already has been called
				testPassEncoder.setBindGroup(1, debugBindGroup);
			}
			testPassEncoder.dispatchWorkgroups(Math.ceil(uaBufferData[0]/2/64));
			if (testPassEncoder.end == undefined) {
				testPassEncoder.endPass();
			} else {
				testPassEncoder.end();
			}
			device.queue.submit([testCommandEncoder.finish()]);
		}
		if (ucChunkCount != 0) {
			queue.writeBuffer(
				ucVoxelBuffer,
				0,
				ucVoxelBufferData,
				0,
				((512*ucChunkCount)+3)&~3
			);
			queue.writeBuffer(
				ucIndexBuffer,
				0,
				ucIndexBufferData,
				0,
				(ucChunkCount+3)&~3
			);
			let testCommandEncoder = device.createCommandEncoder();
			let testPassEncoder = testCommandEncoder.beginComputePass();
			testPassEncoder.setPipeline(ucComputePipeline);
			testPassEncoder.setBindGroup(0, ucBindGroup);
			testPassEncoder.dispatchWorkgroups(ucChunkCount<<1);
			if (testPassEncoder.end == undefined) {
				testPassEncoder.endPass();
			} else {
				testPassEncoder.end();
			}
			device.queue.submit([testCommandEncoder.finish()]);
		}
		/*
		if (ugilBufferData[0] != 0) {
			queue.writeBuffer(
				ugilBuffer,
				0,
				ugilBufferData,
				0,
				((ugilBufferData[0]+2)+3)&~3
			);
			let testCommandEncoder = device.createCommandEncoder();
			let testPassEncoder = testCommandEncoder.beginComputePass();
			testPassEncoder.setPipeline(ugilComputePipeline);
			testPassEncoder.setBindGroup(0, ugilBindGroup);
			if (shaderDebugEnabled) {
				// does not work since the map call already has been called
				testPassEncoder.setBindGroup(1, debugBindGroup);
			}
			testPassEncoder.dispatchWorkgroups(Math.ceil(ugilBufferData[0]/2/64));
			if (testPassEncoder.end == undefined) {
				testPassEncoder.endPass();
			} else {
				testPassEncoder.end();
			}
			device.queue.submit([testCommandEncoder.finish()]);
		}
		*/
	};

	render_frame();
}

function calculate_movement(deltaTime)
{
	if (inMenu) {
		return;
	}
	let speed = 8.0 * deltaTime;

	if (keysPressed.ShiftRight) {
		speed = speed * 10;
	}

	let up = [0.0, 1.0, 0.0];

	let newAxis = normalize_vec3([cameraAxis[0], 0.0, cameraAxis[2]]);

	let verticalVector = scale_vec3_by_constant(newAxis, speed);
	let horizontalVector = scale_vec3_by_constant(
		normalize_vec3(cross_vec3(newAxis, up)),
		speed
	);
	let scaledUpVector = [0.0, speed, 0.0];

	if (keysPressed.Space) {
		set_camera_pos(add_vec3(get_camera_pos(), scaledUpVector));
	}
	if (keysPressed.ShiftLeft) {
		set_camera_pos(sub_vec3(get_camera_pos(), scaledUpVector));
	}
	if (keysPressed.KeyW) {
		set_camera_pos(add_vec3(get_camera_pos(), verticalVector));
	}
	if (keysPressed.KeyS) {
		set_camera_pos(sub_vec3(get_camera_pos(), verticalVector));
	}
	if (keysPressed.KeyA) {
		set_camera_pos(sub_vec3(get_camera_pos(), horizontalVector));
	}
	if (keysPressed.KeyD) {
		set_camera_pos(add_vec3(get_camera_pos(), horizontalVector));
	}
}

function lock_cursor(element)
{
	if (!(element === document.pointerLockElement)) {
		element.requestPointerLock();
		return true;
	} else {
		return false;
	}
}

function normalize_vec3(input)
{
	let lengthINV = 1/Math.sqrt(input[0]*input[0]+input[1]*input[1]+input[2]*input[2]);
	return [input[0]*lengthINV, input[1]*lengthINV, input[2]*lengthINV];
}

function cross_vec3(a, b)
{
	return [
		a[1] * b[2] - a[2] * b[1],
		a[2] * b[0] - a[0] * b[2],
		a[0] * b[1] - a[1] * b[0]
	];
}

function dot_vec3(a, b)
{
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

function scale_vec3_by_constant(a, c)
{
	return [
		a[0] * c,
		a[1] * c,
		a[2] * c,
	];
}

function add_vec3(a, b)
{
	return [
		a[0]+b[0],
		a[1]+b[1],
		a[2]+b[2],
	];
}

function sub_vec3(a, b)
{
	return [
		a[0]-b[0],
		a[1]-b[1],
		a[2]-b[2],
	];
}

function apply_transformation()
{
	console.log("working?");
}
