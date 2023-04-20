export async function load({fetch, params})
{
	const vertexShaderSource = await fetch("voxelraycast.vert").then((response) => response.text());
	const fragmentShaderSource = await fetch("voxelraycast.frag").then((response) => response.text());
	return {
		vertexShaderSource,
		fragmentShaderSource
	};
}
