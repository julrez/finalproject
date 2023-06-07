// TODO: implement some simple #ifdef preprocessor

struct Constants {
	width: u32,
	height: u32,
	padding0: u32,
	padding1: u32,
	rayPos: vec3f,
	vert0: vec3f,
	vert1: vec3f,
	vert2: vec3f,
	vert3: vec3f,
}

struct DebugMsgInfo {
	debugMsgBufferSize: atomic<u32>,
	debugMsgBuffer: array<u32>,
}

@group(0) @binding(0) var outputTexture: texture_storage_2d<r32uint, write>;
@group(0) @binding(1) var<uniform> constants: Constants;
@group(0) @binding(2) var<storage, read> chunkBuffer: array<u32>;
@group(0) @binding(3) var<storage, read> accelerationBuffer: array<u32>;

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

fn generate_ray(inCoords: vec2f) -> vec3<f32>
{
	let coords = (inCoords+vec2<f32>(0.5f, 0.5f))
		/ vec2f(f32(constants.width), f32(constants.height));

	let lower = mix(constants.vert0, constants.vert1, coords.x);
	let upper = mix(constants.vert2, constants.vert3, coords.x);
	return normalize(mix(upper, lower, coords.y))*32.0f;
}

@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>)
{
	if (globalID.x*3u > constants.width || globalID.y*3u > constants.height) {
		return;
	}
	// middle of 3x3 area
	let midIndex = vec2<u32>(globalID.x*3u+1u, globalID.y*3u+1u);
	
	var rayDir = generate_ray(vec2f(midIndex));
	//rayDir.x = 0.01; // cool starwars like effect
	if (abs(rayDir.x) < 0.001) {
		rayDir.x = 0.001;
	}
	if (abs(rayDir.y) < 0.001) {
		rayDir.y = 0.001;
	}
	if (abs(rayDir.z) < 0.001) {
		rayDir.z = 0.001;
	}

	var origin = constants.rayPos;
	var invDir = vec3<f32>(1.0f, 1.0f, 1.0f) / rayDir;
	let sign1or0 = vec3<u32>(u32(rayDir.x>0.0), u32(rayDir.y>0.0), u32(rayDir.z>0.0));
	var gridPos = vec3<u32>(origin);

	let addULP = vec3<u32>(
		select(bitcast<u32>(-1), 1u, rayDir.x>0.0),
		select(bitcast<u32>(-1), 1u, rayDir.y>0.0),
		select(bitcast<u32>(-1), 1u, rayDir.z>0.0),
	);
	rayDir.x = bitcast<f32>(bitcast<u32>(rayDir.x)+1u);
	rayDir.y = bitcast<f32>(bitcast<u32>(rayDir.y)+1u);
	rayDir.z = bitcast<f32>(bitcast<u32>(rayDir.z)+1u);
	
	origin.x = bitcast<f32>(bitcast<u32>(origin.x)+addULP.x);
	origin.y = bitcast<f32>(bitcast<u32>(origin.y)+addULP.y);
	origin.z = bitcast<f32>(bitcast<u32>(origin.z)+addULP.z);

	/*
	// generate four extremes, could be used to speed up
	let extreme0Dir = generate_ray(vec2f(vec2u(midIndex.x - 1u, midIndex.y-1u)));
	let extreme1Dir = generate_ray(vec2f(vec2u(midIndex.x + 1u, midIndex.y-1u)));
	let extreme2Dir = generate_ray(vec2f(vec2u(midIndex.x - 1u, midIndex.y+1u)));
	let extreme3Dir = generate_ray(vec2f(vec2u(midIndex.x + 1u, midIndex.y+1u)));
	//the plan: if you get difference in x, y and z at t of 1
	//-> you can just multiply with tmin to get diff
	*/

	var endVoxelIndices = array<u32, 9>(0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
	
	// 11: x, 10: y, 11: z
	var voxel = 0u;
	var chunkIndex: u32;
	var oldAccelerationIndex: u32 = 0xFFFFFFFFu;
	var i = 0;
	var tmin: f32;
	var newPos: vec3<f32>;
	for (; i < 512; i++) {
		let accelerationIndex: u32 = (((gridPos.z >> 3u) << 16u) | ((gridPos.y >> 3u) << 8u) | (gridPos.x >> 3u));
		
		if (oldAccelerationIndex != accelerationIndex) {
			chunkIndex = accelerationBuffer[accelerationIndex];
		}
		oldAccelerationIndex = accelerationIndex;
		
		var mask = 0xFFFFFFFFu;
		var shift = 0u;

		if ((chunkIndex & 2147483648u) == 0u) { // iterating over voxel
			let voxelIndex: u32 = chunkIndex
				| ((gridPos.x & 7u))
				| ((gridPos.y & 7u) << 3u)
				| ((gridPos.z & 7u) << 6u);
			voxel = chunkBuffer[voxelIndex];
			// voxel in area! check for intersection
			if ((voxel & 31u) == 0u
					|| ((voxel & 31u)<<5u) == 0u
					|| ((voxel & 31u)<<10u) == 0u
					|| ((voxel & 31u)<<15u) == 0u
					|| ((voxel & 31u)<<20u) == 0u) {
				break;
			}
		} else { // iterating over chunk
			voxel = chunkIndex;
			mask = ~7u;
			shift = 3u;
		}

		// exits if border or non-transparent voxel
		if ((voxel & 3221225472u) != 2147483648u) {
			break;
		}

		let box = (gridPos & vec3<u32>(mask, mask, mask)) - ((((vec3<u32>(voxel, voxel, voxel) >> (vec3<u32>(0u, 5u, 10u)+sign1or0*15u)) & vec3<u32>(31u, 31u, 31u)) ^ vec3<u32>(-vec3<i32>(sign1or0))) << vec3<u32>(shift, shift, shift));
		let t = (vec3<f32>(box) - origin) * invDir;
		
		tmin = min(min(t.x, t.y), t.z);
		//let newTmin = tmin+0.01;
		let newTmin = bitcast<f32>(bitcast<u32>(tmin)+1u);
		newPos = origin+rayDir*newTmin;
		newPos.x = bitcast<f32>(bitcast<u32>(newPos.x)+addULP.x);
		newPos.y = bitcast<f32>(bitcast<u32>(newPos.y)+addULP.y);
		newPos.z = bitcast<f32>(bitcast<u32>(newPos.z)+addULP.z);
		gridPos = vec3<u32>(newPos);
	}
	// THIS IS WRONG BTW! TODO: send coords instead so pass2 can calculate tmin
	// also: use only 1 textureStore
	var color: vec4<u32>;
	let depth = tmin*0.0f;
	//let depth = 0f;
	color = vec4<u32>(bitcast<u32>(depth), 0u, 0u, 0u);
	// these many stores are not necessary
	textureStore(outputTexture, vec2(globalID.x*3u, globalID.y*3u), color);
	textureStore(outputTexture, vec2(globalID.x*3u, globalID.y*3u+1u), color);
	textureStore(outputTexture, vec2(globalID.x*3u, globalID.y*3u+2u), color);
	textureStore(outputTexture, vec2(globalID.x*3u+1u, globalID.y*3u), color);
	textureStore(outputTexture, vec2(globalID.x*3u+1u, globalID.y*3u+1u), color);
	textureStore(outputTexture, vec2(globalID.x*3u+1u, globalID.y*3u+2u), color);
	textureStore(outputTexture, vec2(globalID.x*3u+2u, globalID.y*3u), color);
	textureStore(outputTexture, vec2(globalID.x*3u+2u, globalID.y*3u+1u), color);
	textureStore(outputTexture, vec2(globalID.x*3u+2u, globalID.y*3u+2u), color);
}
