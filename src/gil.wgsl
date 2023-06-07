
struct Constants {
	listCount: u32,
	paddingM1: u32,
	padding0: u32,
	padding1: u32,
	rayPos: vec3<f32>,
	vert0: vec3f,
	vert1: vec3f,
	vert2: vec3f,
	vert3: vec3f,
}
// texture atlas
@group(0) @binding(0) var outputTexture: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var<uniform> constants: Constants;
@group(0) @binding(2) var<storage, read> chunkBuffer: array<u32>;
@group(0) @binding(3) var<storage, read> accelerationBuffer: array<u32>;
@group(0) @binding(4) var<storage, read> lightmapInfoBuffer: array<u32>;
// 1 entry = 2 uints
// uint 0 = (gridX) | (gridY << 16)
// uint 1 = (gridZ) | (offset << 16) | (normal << 24)
// constants.listCount number of entries
@group(0) @binding(5) var<storage, read> list: array<u32>;
// TODO: block texture, block sampler

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

@compute @workgroup_size(64, 1)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>)
{
	let id = globalID.x + (globalID.y*32768*64);
	if (id >= constants.listCount) {
		return;
	}
	let uint0 = list[(id<<2)];
	let uint1 = list[(id<<2)+1];

	let gridPos = vec3<u32>(uint0 & 0xFFFF, uint0 >> 16, uint1 & 0xFFFF);
	let offset1D = (uint1 >> 16) & 0xFF;
	let offset2D = vec2<u32>(offset1D&0xF, offset1D>>4);

	let face = (uint1 >> 24);

	let accelerationIndex: u32 = (((gridPos.z >> 3u) << 16u) | ((gridPos.y >> 3u) << 8u) | (gridPos.x >> 3u));
	let chunkIndex = accelerationBuffer[accelerationIndex];
	let voxelIndex: u32 = chunkIndex
		| ((gridPos.x & 7u))
		| ((gridPos.y & 7u) << 3u)
		| ((gridPos.z & 7u) << 6u);
	let hitVoxel = chunkBuffer[voxelIndex];
	let lightmapInfoAddress = ((hitVoxel >> 5) << 3) + face;
	let lightmapInfo = lightmapInfoBuffer[lightmapInfoAddress];
	let lightmapType = lightmapInfo & 7;
	let lightmapCoord = vec2<u32>(
		(lightmapInfo >> 3) & 2047,
		lightmapInfo >> 14
	);

	if (id == 1) {
		debug_print(DEBUG_TYPE_START, DEBUG_X);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_COLON);
		debug_print(DEBUG_TYPE_U32, gridPos.x);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, gridPos.y);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, gridPos.z);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_COLON);
		debug_print(DEBUG_TYPE_U32, hitVoxel >> 5);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, lightmapInfoBuffer[lightmapInfoAddress]);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, lightmapInfoBuffer[lightmapInfoAddress+1]);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, lightmapInfoBuffer[lightmapInfoAddress+2]);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, lightmapInfoBuffer[lightmapInfoAddress+3]);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, lightmapInfoBuffer[lightmapInfoAddress+4]);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
		debug_print(DEBUG_TYPE_U32, lightmapInfoBuffer[lightmapInfoAddress+5]);
		debug_print(DEBUG_TYPE_ASCII, DEBUG_SPACE);
	}
}
