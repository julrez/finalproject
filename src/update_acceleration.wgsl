struct UpdateData {
	count: u32,
	padding: u32,
	data: array<u32>,
}

@group(0) @binding(0) var<storage, read_write> accelerationBuffer: array<u32>;
@group(0) @binding(1) var<storage, read> updateData: UpdateData;

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

/*
 * workgroups tile in x direction until 32768
 */
@compute @workgroup_size(64, 1, 1)
fn main(
		@builtin(global_invocation_id) globalID: vec3<u32>,
		@builtin(workgroup_id) workgroupID: vec3<u32>)
{
	if (globalID.x >= (updateData.count>>1u)) {
		return;
	}

	let index = updateData.data[globalID.x<<1u];
	let data = updateData.data[(globalID.x<<1u) | 1u];
	
	accelerationBuffer[index] = data;
}
