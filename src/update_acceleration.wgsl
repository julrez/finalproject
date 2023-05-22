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

const DEBUG_TYPE_ASCII = 0u;
const DEBUG_TYPE_F32 = 1u;
const DEBUG_TYPE_U32 = 2u;
const DEBUG_TYPE_START = 3u;
const DEBUG_A = 97u;
const DEBUG_B = 98u;
const DEBUG_C = 99u;
const DEBUG_D = 100u;
const DEBUG_E = 101u;
const DEBUG_F = 102u;
const DEBUG_G = 103u;
const DEBUG_H = 104u;
const DEBUG_I = 105u;
const DEBUG_J = 106u;
const DEBUG_K = 107u;
const DEBUG_L = 108u;
const DEBUG_M = 109u;
const DEBUG_N = 110u;
const DEBUG_O = 111u;
const DEBUG_P = 112u;
const DEBUG_Q = 113u;
const DEBUG_R = 114u;
const DEBUG_S = 115u;
const DEBUG_T = 116u;
const DEBUG_U = 117u;
const DEBUG_V = 118u;
const DEBUG_W = 119u;
const DEBUG_X = 120u;
const DEBUG_Y = 121u;
const DEBUG_Z = 122u;
const DEBUG_SPACE = 32u;
const DEBUG_COLON = 58u;
const DEBUG_COMMA = 44u;
fn debug_print(charType: u32, char: u32)
{
	let index = atomicAdd(&debugInfo.debugMsgBufferSize, 2u);
	debugInfo.debugMsgBuffer[index] = charType;
	debugInfo.debugMsgBuffer[index+1u] = char;
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
