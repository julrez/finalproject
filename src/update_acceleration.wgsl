@group(0) @binding(0) var<storage, read_write> accelerationBuffer: array<u32>;
@group(0) @binding(1) var<storage, read> chunkData: array<u32>;
@group(0) @binding(2) var<storage, read> chunkIndices: array<u32>;

/*
 * workgroups tile in x direction until 32768
 */
@compute @workgroup_size(4, 4, 4)
fn main(
		@builtin(global_invocation_id) globalID: vec3<u32>,
		@builtin(workgroup_id) workgroupID: vec3<u32>,
		@builtin(local_invocation_id) localID: vec3<u32>)
{
	let workgroupIndex = chunkIndices[workgroupID.x];
	let writeIndex = workgroupIndex
		+ (localID.z << 16u)
		+ (localID.y << 8u)
		+ (localID.x);
	let readIndex = globalID.x;
	
	accelerationBuffer[writeIndex] = chunkData[readIndex];
}
