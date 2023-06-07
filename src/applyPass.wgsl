// applies everything to the final texture
// this should probably be a fragment shader instead

struct Constants {
	width: u32,
	height: u32,
	padding0: u32,
	padding1: u32,
	rayPos: vec3<f32>,
	vert0: vec3f,
	vert1: vec3f,
	vert2: vec3f,
	vert3: vec3f,
}

@group(0) @binding(0) var outputTexture: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var colorTexture: texture_2d<u32>;
@group(0) @binding(2) var directLightingTexture: texture_2d<f32>;
@group(0) @binding(3) var<uniform> constants: Constants;
@group(0) @binding(4) var skyBoxTexture: texture_cube<f32>;
@group(0) @binding(5) var skyBoxSampler: sampler;
@group(0) @binding(6) var blockTexture: texture_2d<f32>;
@group(0) @binding(7) var depthTexture: texture_2d<f32>;
/*
@group(0) @binding(8) var textureAtlas: texture_2d<f32>;
@group(0) @binding(9) var<storage, read> chunkBuffer: array<u32>;
@group(0) @binding(10) var<storage, read> accelerationBuffer: array<u32>;
@group(0) @binding(11) var<storage, read> lightmapInfoBuffer: array<u32>;
*/

/*
@group(1) @binding(0) var<storage, read_write> debugInfo: DebugMsgInfo;

struct DebugMsgInfo {
	debugMsgBufferSize: atomic<u32>,
	debugMsgBuffer: array<u32>,
}

const DEBUG_TYPE_ASCII = 0;
const DEBUG_TYPE_F32 = 1;
const DEBUG_TYPE_U32 = 2;
const DEBUG_TYPE_START = 3;
const DEBUG_A = 97;
const DEBUG_B = 98;
const DEBUG_C = 99;
const DEBUG_D = 100;
const DEBUG_E = 101;
const DEBUG_F = 102;
const DEBUG_G = 103;
const DEBUG_H = 104;
const DEBUG_I = 105;
const DEBUG_J = 106;
const DEBUG_K = 107;
const DEBUG_L = 108;
const DEBUG_M = 109;
const DEBUG_N = 110;
const DEBUG_O = 111;
const DEBUG_P = 112;
const DEBUG_Q = 113;
const DEBUG_R = 114;
const DEBUG_S = 115;
const DEBUG_T = 116;
const DEBUG_U = 117;
const DEBUG_V = 118;
const DEBUG_W = 119;
const DEBUG_X = 120;
const DEBUG_Y = 121;
const DEBUG_Z = 122;
const DEBUG_SPACE = 32;
const DEBUG_COLON = 58;
const DEBUG_COMMA = 44;
fn debug_print(charType: u32, char: u32)
{
	let index = atomicAdd(&debugInfo.debugMsgBufferSize, 2);
	debugInfo.debugMsgBuffer[index] = charType;
	debugInfo.debugMsgBuffer[index+1] = char;
}
*/

// NOTE: this is NOT the same one as used in the other shaders!!!
fn generate_ray(coords: vec2f) -> vec3<f32>
{
	let lower = mix(constants.vert0, constants.vert1, coords.x);
	let upper = mix(constants.vert2, constants.vert3, coords.x);
	return normalize(mix(upper, lower, coords.y))*32.0f;
}

@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>)
{
	if (globalID.x > constants.width || globalID.y > constants.height) {
		return;
	}
	let coord = (vec2<f32>(globalID.xy)+vec2<f32>(0.5f, 0.5f)) / 
		vec2<f32>(f32(constants.width), f32(constants.height));

	var rayDir = generate_ray(coord);
	if (abs(rayDir.x) < 0.001) {
		rayDir.x = 0.001;
	}
	if (abs(rayDir.y) < 0.001) {
		rayDir.y = 0.001;
	}
	if (abs(rayDir.z) < 0.001) {
		rayDir.z = 0.001;
	}
	rayDir.x = bitcast<f32>(bitcast<u32>(rayDir.x)+1u);
	rayDir.y = bitcast<f32>(bitcast<u32>(rayDir.y)+1u);
	rayDir.z = bitcast<f32>(bitcast<u32>(rayDir.z)+1u);
	var origin = constants.rayPos;
	let addULP = vec3<u32>(
		select(bitcast<u32>(-1), 1u, rayDir.x>0.0),
		select(bitcast<u32>(-1), 1u, rayDir.y>0.0),
		select(bitcast<u32>(-1), 1u, rayDir.z>0.0),
	);
	origin.x = bitcast<f32>(bitcast<u32>(origin.x)+addULP.x);
	origin.y = bitcast<f32>(bitcast<u32>(origin.y)+addULP.y);
	origin.z = bitcast<f32>(bitcast<u32>(origin.z)+addULP.z);

	let inputColor = textureLoad(colorTexture, vec2(globalID.xy), 0);
	if (inputColor.r == 255u) {
		let skyBoxTexel = textureSampleLevel(skyBoxTexture, skyBoxSampler, rayDir, 0f);
		textureStore(outputTexture, vec2(globalID.xy), skyBoxTexel);
		return;
	}

	let depth = textureLoad(depthTexture, vec2(globalID.xy), 0).r;
	var hitPos = vec3<f32>(origin+rayDir*bitcast<f32>(bitcast<u32>(depth)+7u));
	hitPos.x = bitcast<f32>(bitcast<u32>(hitPos.x)+addULP.x);
	hitPos.y = bitcast<f32>(bitcast<u32>(hitPos.y)+addULP.y);
	hitPos.z = bitcast<f32>(bitcast<u32>(hitPos.z)+addULP.z);

	var blockTexCoord: vec2<f32>;
	var face = inputColor.g<<1;
	blockTexCoord.x = fract(hitPos.x);
	blockTexCoord.y = fract(hitPos.y);
	if (inputColor.g == 0u) { // x normal
		blockTexCoord.x = fract(hitPos.y);
		blockTexCoord.y = fract(hitPos.z);
		face += u32(rayDir.x<0.0);
	} else if (inputColor.g == 1u) { // y normal
		blockTexCoord.x = fract(hitPos.x);
		blockTexCoord.y = fract(hitPos.z);
		face += u32(rayDir.y<0.0);
	} else { // z normal
		blockTexCoord.x = fract(hitPos.x);
		blockTexCoord.y = fract(hitPos.y);
		face += u32(rayDir.z<0.0);
	}
	var integerBlockTexCoord = vec2<u32>(blockTexCoord*vec2<f32>(16f, 16f));
	integerBlockTexCoord.x += face<<4u;
	integerBlockTexCoord.y += inputColor.r<<4u;
	//let actualColor = textureSampleLevel(blockTexture, skyBoxSampler, blockTexCoord, 0.0);
	//let actualColor = textureLoad(blockTexture, vec2<u32>(0, 0), 0);
	let actualColor = textureLoad(blockTexture, integerBlockTexCoord, 0);
	
	/*
	// get lightmap info
	let gridPos = vec3u(hitPos);
	let accelerationIndex: u32 = (((gridPos.z >> 3u) << 16u) | ((gridPos.y >> 3u) << 8u) | (gridPos.x >> 3u));
	let chunkIndex = accelerationBuffer[accelerationIndex];
	let voxelIndex: u32 = chunkIndex
		| ((gridPos.x & 7u))
		| ((gridPos.y & 7u) << 3u)
		| ((gridPos.z & 7u) << 6u);
	let hitVoxel = chunkBuffer[voxelIndex];
	let lightmapInfoAddress = ((hitVoxel >> 5) << 3) + 2 + face;
	let lightmapInfo = lightmapInfoBuffer[lightmapInfoAddress];
	let lightmapType = lightmapInfo & 7;
	let lightmapCoord = vec2<u32>(
		(lightmapInfo >> 3) & 2047,
		lightmapInfo >> 14
	);
	var lightMapColor = textureLoad(textureAtlas, lightmapCoord, 0);
	if (globalID.x == constants.width/2 && globalID.y == constants.height/2) {
		debug_print(DEBUG_TYPE_START, DEBUG_A);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_COLON);
		debug_print(DEBUG_TYPE_U32, gridPos.x);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, gridPos.y);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, gridPos.z);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, lightmapInfo);
		//debug_print(DEBUG_TYPE_F32, bitcast<u32>(lightMapColor.r));
	}
	*/
	
	/*
	let inputDirectLighting = textureLoad(directLightingTexture,
				vec2(globalID.x/2u, globalID.y), 0).r;
				*/
	// should probably not use skyboxsampler...
	let inputDirectLighting = textureSampleLevel(
		directLightingTexture,
		skyBoxSampler,
		coord,
		0f).r;

	//var color =vec4(0f, f32(inputColor.r)/ 31f * 0.5f + 0.5f, 0f, 1f);
	var color = vec4(actualColor.rgb, 1.0);

	color *= vec4(inputDirectLighting, inputDirectLighting, inputDirectLighting, 1f);
	textureStore(outputTexture, vec2(globalID.xy), color);
}
