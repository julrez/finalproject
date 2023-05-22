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

struct DebugMsgInfo {
	debugMsgBufferSize: atomic<u32>,
	debugMsgBuffer: array<u32>,
}

@group(0) @binding(0) var outputTexture: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var colorTexture: texture_2d<u32>;
@group(0) @binding(2) var directLightingTexture: texture_2d<f32>;
@group(0) @binding(3) var<uniform> constants: Constants;
//@group(0) @binding(4) var skyBoxTexture: texture_2d_array<f32>;
@group(0) @binding(4) var skyBoxTexture: texture_cube<f32>;
@group(0) @binding(5) var skyBoxSampler: sampler;
/*
@group(1) @binding(0) var<storage, read_write> debugInfo: DebugMsgInfo;

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
	return normalize(mix(upper, lower, coords.y));
}

@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>)
{
	if (globalID.x > constants.width || globalID.y > constants.height) {
		return;
	}
	let coord = (vec2<f32>(globalID.xy)+vec2<f32>(0.5f, 0.5f)) / 
		vec2<f32>(f32(constants.width), f32(constants.height));
	let inputColor = textureLoad(colorTexture, vec2(globalID.xy), 0);
	if (inputColor.r == 0u) {
		let rayDir = generate_ray(coord);

		let skyBoxTexel = textureSampleLevel(skyBoxTexture, skyBoxSampler, rayDir, 0f);
		textureStore(outputTexture, vec2(globalID.xy), skyBoxTexel);
		return;
	}
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

	var color = vec4(0f, f32(inputColor.r)/ 31f * 0.5f + 0.5f, 0f, 1f);

	color *= vec4(inputDirectLighting, inputDirectLighting, inputDirectLighting, 1f);
	textureStore(outputTexture, vec2(globalID.xy), color);
}
