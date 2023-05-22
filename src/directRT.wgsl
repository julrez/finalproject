struct Constants {
	width: u32,
	height: u32,
	fullWidth: u32,
	fullHeight: u32,
	rayPos: vec3f,
	vert0: vec3f,
	vert1: vec3f,
	vert2: vec3f,
	vert3: vec3f,
}

@group(0) @binding(0) var outputTexture: texture_storage_2d<rgba16float, write>;
@group(0) @binding(1) var<uniform> constants: Constants;
@group(0) @binding(2) var<storage, read> chunkBuffer: array<u32>;
@group(0) @binding(3) var<storage, read> accelerationBuffer: array<u32>;
@group(0) @binding(4) var depthTexture: texture_2d<f32>;
@group(0) @binding(5) var colorTexture: texture_2d<u32>;

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

fn generate_ray(inCoords: vec2<f32>) -> vec3<f32>
{
	let coords = (inCoords+vec2<f32>(0.5f, 0.5f))
		/ vec2f(f32(constants.fullWidth), f32(constants.fullHeight));

	let lower = mix(constants.vert0, constants.vert1, coords.x);
	let upper = mix(constants.vert2, constants.vert3, coords.x);
	return normalize(mix(upper, lower, coords.y))*32.0f;
}

fn get_depth(inCoord: vec2<u32>) -> f32
{
	return textureLoad(depthTexture, inCoord, 0).r;
}

fn cast_ray(pos: vec3<f32>, dir: vec3<f32>) -> bool
{
	var rayDir = dir;
	if (abs(rayDir.x) < 0.001) {
		rayDir.x = 0.001;
	}
	if (abs(rayDir.y) < 0.001) {
		rayDir.y = 0.001;
	}
	if (abs(rayDir.z) < 0.001) {
		rayDir.z = 0.001;
	}

	var origin = pos;
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
	
	var voxel = 0u;
	var chunkIndex: u32;
	var oldAccelerationIndex: u32 = 0xFFFFFFFFu;
	var i = 0;
	for (; i < 256; i++) {
		let accelerationIndex: u32 = (((gridPos.z >> 3u) << 16u)
			| ((gridPos.y >> 3u) << 8u) | (gridPos.x >> 3u));
		
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
		} else { // iterating over chunk
			voxel = chunkIndex;
			mask = ~7u;
			shift = 3u;
		}

		// exits if border or non-transparent voxel
		if ((voxel & 3221225472u) != 2147483648u) {
			// if i == 0 then we might be inside the voxel we come from
			//if (!((voxel & 3221225472u) == 0u && i < 1)) {
				break;
			//}
		}

		let box = (gridPos & vec3<u32>(mask, mask, mask)) - ((((vec3<u32>(voxel, voxel, voxel) >> (vec3<u32>(0u, 5u, 10u)+sign1or0*15u)) & vec3<u32>(31u, 31u, 31u)) ^ vec3<u32>(-vec3<i32>(sign1or0))) << vec3<u32>(shift, shift, shift));
		let t = (vec3<f32>(box) - origin) * invDir;
		
		let tmin = min(min(t.x, t.y), t.z);
		let newTmin = bitcast<f32>(bitcast<u32>(tmin)+1u);
		var newPos = origin+rayDir*newTmin;
		newPos.x = bitcast<f32>(bitcast<u32>(newPos.x)+addULP.x);
		newPos.y = bitcast<f32>(bitcast<u32>(newPos.y)+addULP.y);
		newPos.z = bitcast<f32>(bitcast<u32>(newPos.z)+addULP.z);
		gridPos = vec3<u32>(newPos);
	}
	return (voxel & 3221225472u) != 0u;
}

@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>)
{
	if (globalID.x > constants.width || globalID.y > constants.height) {
		return;
	}
	let screenCoord = (vec2<f32>(globalID.xy)+vec2<f32>(0.5f, 0.5f))
		/ vec2<f32>(f32(constants.width), f32(constants.height));
	let fetchCoord = vec2<u32>(screenCoord *
		vec2<f32>(f32(constants.fullWidth), f32(constants.fullHeight)));
	let inputDepth = get_depth(fetchCoord);
	let inputColor = textureLoad(colorTexture, fetchCoord, 0);
	if (inputDepth == 0f) {
		textureStore(outputTexture, vec2(globalID.xy), vec4(0f, 0f, 0f, 0f));
		return;
	}
	var cameraRayDir = generate_ray(vec2<f32>(fetchCoord));
	if (abs(cameraRayDir.x) < 0.001) {
		cameraRayDir.x = 0.001;
	}
	if (abs(cameraRayDir.y) < 0.001) {
		cameraRayDir.y = 0.001;
	}
	if (abs(cameraRayDir.z) < 0.001) {
		cameraRayDir.z = 0.001;
	}
	var invCameraRayDir = vec3<f32>(1.0f, 1.0f, 1.0f) / cameraRayDir;
	let cameraRayDirSign = vec3<u32>(
		u32(cameraRayDir.x>0.0),
		u32(cameraRayDir.y>0.0),
		u32(cameraRayDir.z>0.0)
	);

	let cameraAddULP = vec3<u32>(
		select(bitcast<u32>(-3), 3u, cameraRayDir.x>0.0),
		select(bitcast<u32>(-3), 3u, cameraRayDir.y>0.0),
		select(bitcast<u32>(-3), 3u, cameraRayDir.z>0.0),
	);
	cameraRayDir.x = bitcast<f32>(bitcast<u32>(cameraRayDir.x)+3u);
	cameraRayDir.y = bitcast<f32>(bitcast<u32>(cameraRayDir.y)+3u);
	cameraRayDir.z = bitcast<f32>(bitcast<u32>(cameraRayDir.z)+3u);
	
	var cameraPos = constants.rayPos;
	cameraPos.x = bitcast<f32>(bitcast<u32>(cameraPos.x)+cameraAddULP.x);
	cameraPos.y = bitcast<f32>(bitcast<u32>(cameraPos.y)+cameraAddULP.y);
	cameraPos.z = bitcast<f32>(bitcast<u32>(cameraPos.z)+cameraAddULP.z);

	var originPos = cameraPos+cameraRayDir*inputDepth;
	originPos.x = bitcast<f32>(bitcast<u32>(originPos.x)+cameraAddULP.x);
	originPos.y = bitcast<f32>(bitcast<u32>(originPos.y)+cameraAddULP.y);
	originPos.z = bitcast<f32>(bitcast<u32>(originPos.z)+cameraAddULP.z);
	let originGridPos = vec3<u32>(originPos);

	// calculate normal
	var normal: vec3<f32>;
	if (inputColor.g == 0u) { // x
		normal = vec3<f32>(-sign(cameraRayDir.x), 0f, 0f);
	} else if (inputColor.g == 1u) { // y
		normal = vec3<f32>(0f, -sign(cameraRayDir.y), 0f);
	} else { // z
		normal = vec3<f32>(0f, 0f, -sign(cameraRayDir.z));
	}

	// finally, calculate lighting
	let shininess = 128.0f;
	let specularStrength = 0.5f;

	//let lightPos = vec3<f32>(1000f, 2000f, 5000f);
	let lightPos = vec3<f32>(-6612.37984732f, 4207.50836314f, -6210.74118997f);
	let lightDir = normalize(lightPos-originPos);
	let viewDir = normalize(cameraPos-originPos);

	// TODO: do the precise thing again (bitcast)
	let hit = cast_ray(originPos+normal*0.005, lightDir*32.0f);

	// from learnopengl.com
	let reflectDir = reflect(-lightDir, normal);
	let specular = specularStrength
		* pow(max(dot(viewDir, reflectDir), 0.0f), shininess);

	let diffuse = max(dot(normal, lightDir), 0.0f);
	let ambient = 0.3f;
	var outputLighting = ambient;
	if (hit) {
		outputLighting += diffuse+specular;
	}
	// ambient

	textureStore(outputTexture, vec2(globalID.xy), vec4(outputLighting, 0f, 0f, 0f));
}
