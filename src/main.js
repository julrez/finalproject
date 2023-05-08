let canvas;
var Module;

// TODO: meta tag for github pages hosting!!!

let cameraAxis = [0, 0, -1];
let cameraPos = [500, 120.234890, 500];
let mousePitch = 0, mouseYaw = 270;

let shaderDebugEnabled = false;
if (shaderDebugEnabled) {
	var castMemory = new ArrayBuffer(4);
}

let keysPressed = {};

let skybox;
function load_skybox_texture(input)
{
}

window.addEventListener('load', main);
async function main()
{
	console.log("currently it might take a while for everything to load!");
	canvas = document.getElementById("gameCanvas");
	canvas.width = window.innerWidth;
	canvas.height = window.innerHeight;
	
	compute_test();
	
	Module = {
		//wasmMemory:
		onRuntimeInitialized: function () {
			let init = Module.cwrap("init", "number", []);
			init();
			load_skybox_texture({
				callback: compute_test
			});
		}
	};
	const mainWasmScript = document.createElement('script');
	mainWasmScript.async = true;
	mainWasmScript.src = "out/test.c.js";
	document.head.appendChild(mainWasmScript);

	addEventListener("click", (event) => {
		lock_cursor(canvas);
	});
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
		keysPressed[event.code] = true;
	});
	addEventListener("keyup", (event) => {
		keysPressed[event.code] = false;
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
	
	let nearestSampler = device.createSampler({
		magFilter: "nearest",
		minFilter: "nearest",
		mipmapFilter: "nearest",
	});
	
	let pass2Texture = device.createTexture({
		size: {
			width: canvas.width,
			height: canvas.height,
		},
		dimension: "2d",
		format: "rgba8unorm", // TODO: uint
		//new: usage: GPUTextureUsage.STORAGE_BINDING | TEXTURE_BINDING,
		usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
		viewFormats: [],
	});
	let pass2TextureView = pass2Texture.createView();
	
	let pass1Texture = device.createTexture({
		size: { /* make sure no out of bounds happens */
			width: Math.ceil(canvas.width/3)*3,
			height: Math.ceil(canvas.height/3)*3,
		},
		dimension: "2d",
		format: "r32uint",
		usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
		viewFormats: [],
	});
	let pass1TextureView = pass1Texture.createView();
	
	let depthTexture = device.createTexture({
		size: { /* make sure no out of bounds happens */
			width: Math.ceil(canvas.width),
			height: Math.ceil(canvas.height),
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
	let abMemory = new Uint32Array(Module.HEAP32.buffer, returnedDataArray[0], accelerationBufferSize/4);
	let cbMemory = new Uint32Array(Module.HEAP32.buffer, returnedDataArray[1], chunkBufferSize/4);

	// webgpu does not support SharedArrayBuffer :(
	accelerationBufferMemory = abMemory.slice().buffer;
	chunkBufferMemory = cbMemory.slice().buffer;

	queue.writeBuffer(
		chunkBuffer,
		0, // bufferOffset
		chunkBufferMemory,
		0, // dataOffset
		chunkBufferSize // size
	);
	
	queue.writeBuffer(
		accelerationBuffer,
		0, // bufferOffset
		accelerationBufferMemory,
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
					format: "rgba8unorm",
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
		var dlPassWidth = Math.ceil(Math.sqrt(1/2)*canvas.width);
		var dlPassHeight = Math.ceil(Math.sqrt(1/2)*canvas.height);

		var dlPassConstantsBuffer = device.createBuffer({
			size: 96,
			usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
		});
		var dlPassConstantsBufferMemory = new ArrayBuffer(96);
		var dlPassConstantsBufferUint32 = new Uint32Array(dlPassConstantsBufferMemory);
		var dlPassConstantsBufferFloat32 = new Float32Array(dlPassConstantsBufferMemory);

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
						sampleType: "float",
						viewDimension: "2d",
					}
				},
				{
					binding: 6,
					visibility: GPUShaderStage.COMPUTE,
					sampler: { }
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
					resource: nearestSampler,
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
				width: canvas.width,
				height: canvas.height,
			},
			dimension: "2d",
			format: "rgba8unorm",
			usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC
				| GPUTextureUsage.RENDER_ATTACHMENT
				| GPUTextureUsage.TEXTURE_BINDING,
			viewFormats: [],
		});
		let applyPassTextureView = applyPassTexture.createView();

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
						sampleType: "float",
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
				}
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
				}
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
	
		var uaMaxChunks = 32768;
		var uaChunkDataBufferSize = 32768*(4*4*4)*4;
		var uaChunkDataBuffer = device.createBuffer({
			size: uaChunkDataBufferSize,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
			mappedAtCreation: false
		});
		var uaChunkIndicesBufferSize = 32768*4;
		// written to by using queue.writeBuffer
		var uaChunkIndicesBuffer = device.createBuffer({
			size: uaChunkIndicesBufferSize,
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
				{
					binding: 2,
					visibility: GPUShaderStage.COMPUTE,
					buffer: {
						type: "read-only-storage"
					}
				}
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
						buffer: uaChunkDataBuffer,
					}
				},
				{
					binding: 2,
					resource: {
						buffer: uaChunkIndicesBuffer,
					}
				},
			]
		});

		var uaPipelineLayout = device.createPipelineLayout({
			bindGroupLayouts: [uaBindGroupLayout]
		});

		let uaComputeShaderSource = await (
			await fetch("src/update_acceleration.wgsl")
		).text();
		let uaComputeShaderModule = device.createShaderModule({
			code: uaComputeShaderSource
		});

		var uaComputePipeline = device.createComputePipeline({
			layout: uaPipelineLayout,
			compute: {
				module: uaComputeShaderModule,
				entryPoint: "main",
			}
		});
	}

	{ // update chunkBuffer (uc) compute pipeline creation
		var ucMaxChunks = 32768;
		var ucChunkDataBufferSize = 32768*(8*8*8)*4;
		var ucChunkDataBuffer = device.createBuffer({
			size: ucChunkDataBufferSize,
			usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
			mappedAtCreation: false
		});
		var ucChunkIndicesBufferSize = 32768*4;
		var ucChunkIndicesBuffer = device.createBuffer({
			size: ucChunkIndicesBufferSize,
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
						buffer: ucChunkDataBuffer,
					}
				},
				{
					binding: 2,
					resource: {
						buffer: ucChunkIndicesBuffer,
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
		
		var ucComputePipeline = device.createComputePipeline({
			layout: ucPipelineLayout,
			compute: {
				module: ucComputeShaderModule,
				entryPoint: "main",
			}
		});
	}

	let commandEncoder;
	
	let debugStringElement = document.getElementById("debugString");
	debugStringElement.style = "display: none;";

	let frames = [];

	let oldTime = performance.now()/1000;
	const render_frame = async () => {
		let newTime = performance.now()/1000;
		let deltaTime = newTime-oldTime;
		oldTime = newTime;
		
		while (frames.length > 0 && (newTime) > (frames[0]+1)) {
			frames.shift();
		}
		frames.push(newTime);

		let chunkStr = "\nChunk Info:";
		{
			let debugVoxelIndex = (Math.floor(cameraPos[0] / 8))
				| ((Math.floor(cameraPos[1] / 8))<<8)
				| ((Math.floor(cameraPos[2] / 8))<<16);
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
				let vx = Math.floor(cameraPos[0]) % 8;
				let vy = Math.floor(cameraPos[1]) % 8;
				let vz = Math.floor(cameraPos[2]) % 8;
				
				let vcx = Math.floor(cameraPos[0]) % 32;
				let vcy = Math.floor(cameraPos[1]) % 32;
				let vcz = Math.floor(cameraPos[2]) % 32;

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
			"\ncamera pos x: " + cameraPos[0] + 
			"\ncamera pos y: " + cameraPos[1] + 
			"\ncamera pos z: " + cameraPos[2] + 
			"\ncamera dir x: " + cameraAxis[0] + 
			"\ncamera dir y: " + cameraAxis[1] + 
			"\ncamera dir z: " + cameraAxis[2] + 
			"\nacceleration: " +
			Math.floor(cameraPos[0] / 8) + ", " +
			Math.floor(cameraPos[1] / 8) + ", " +
			Math.floor(cameraPos[2] / 8) +
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
		pass2ConstantsBufferFloat32[4] = cameraPos[0];
		pass2ConstantsBufferFloat32[5] = cameraPos[1];
		pass2ConstantsBufferFloat32[6] = cameraPos[2];
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
		pass1ConstantsBufferFloat32[4] = cameraPos[0];
		pass1ConstantsBufferFloat32[5] = cameraPos[1];
		pass1ConstantsBufferFloat32[6] = cameraPos[2];
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
		dlPassConstantsBufferFloat32[4] = cameraPos[0];
		dlPassConstantsBufferFloat32[5] = cameraPos[1];
		dlPassConstantsBufferFloat32[6] = cameraPos[2];
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

		applyPassConstantsBufferUint32.set(pass2ConstantsBufferUint32);
		queue.writeBuffer(
			applyPassConstantsBuffer,
			0,
			applyPassConstantsBufferMemory,
			0,
			96
		);

		commandEncoder = device.createCommandEncoder();
		if (shaderDebugEnabled) {
			commandEncoder.clearBuffer(debugMsgBuffer, 0, 65536);
		}

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

		/*
		device.queue.onSubmittedWorkDone().then(()=> {
		});
		*/

		if (shaderDebugEnabled) {
			device.queue.onSubmittedWorkDone().then(()=> {
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
			});
		} else {
			requestAnimationFrame(render_frame);
		}
	};

	render_frame();
}

function calculate_movement(deltaTime)
{
	let speed = 64.0 * deltaTime;

	if (keysPressed.ShiftRight) {
		speed = speed * 0.5;
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
		cameraPos = add_vec3(cameraPos, scaledUpVector);
	}
	if (keysPressed.ShiftLeft) {
		cameraPos = sub_vec3(cameraPos, scaledUpVector);
	}
	if (keysPressed.KeyW) {
		cameraPos = add_vec3(cameraPos, verticalVector);
	}
	if (keysPressed.KeyS) {
		cameraPos = sub_vec3(cameraPos, verticalVector);
	}
	if (keysPressed.KeyA) {
		cameraPos = sub_vec3(cameraPos, horizontalVector);
	}
	if (keysPressed.KeyD) {
		cameraPos = add_vec3(cameraPos, horizontalVector);
	}
}

function lock_cursor(element)
{
	if (!(element === document.pointerLockElement)) {
		element.requestPointerLock();
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
