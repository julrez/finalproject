struct Constants {
	outputWidth: u32,
	outputHeight: u32,
	inputWidth: u32,
	inputHeight: u32,
	rayPos: vec3f,
	vert0: vec3f,
	vert1: vec3f,
	vert2: vec3f,
	vert3: vec3f,
}

@group(0) @binding(0) var outputTexture: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var<uniform> constants: Constants;
@group(0) @binding(2) var inputTexture: texture_storage_2d<rgba8unorm, read>;

@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>)
{
	outputTexture[global_invocation_id] = inputTexture[global_invocation_id];
}
