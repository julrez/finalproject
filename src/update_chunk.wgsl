@group(0) @binding(0) var<storage, read_write> chunkBuffer: array<u32>;
@group(0) @binding(1) var<storage, read> chunkData: array<u32>;
@group(0) @binding(2) var<storage, read> chunkIndices: array<u32>;

/*
 * NOTE:
 * for lower chunk: chunkIndex = index accelerationBuffer uses
 *      (index of 0,0,0 voxel in chunkBuffer)
 * for upper chunk: chunkIndex = accelerationBuffer index + 256
 */

/*
 * workgroups tile in x direction until 32768
 */
@compute @workgroup_size(8, 8, 4) // 8x8x4 chunk
fn main(
		@builtin(global_invocation_id) globalID: vec3<u32>,
		@builtin(workgroup_id) workgroupID: vec3<u32>,
		@builtin(local_invocation_id) localID: vec3<u32>)
{
	let workgroupIndex = chunkIndices[workgroupID.x];
	let writeIndex = workgroupIndex
		+ (localID.z << 6u)
		+ (localID.y << 3u)
		+ (localID.x);
	let readIndex = globalID.x;
	
	chunkBuffer[writeIndex] = chunkData[readIndex];
}
