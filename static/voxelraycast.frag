#version 300 es

// flags

#define PREFER_ACCURACY
#define EPSILON 0.001
#define ITERATION_COUNT 256
//#define VISUALIZE_ITERATIONS
//#define VISUALIZE_HIGH_ITERATION
//#define TEST_IF_STUCK

precision highp float;
precision mediump usampler2D;

in vec2 UV;
in vec3 cameraPosition;

/*
 * voxel format: (32 bit unsigned integer)
 * top 2 bits = type
 *   - 00 = voxel
 *   - 11 = border/miss
 *   - 10 = air
 */
uniform usampler2D voxelTexture;
uniform usampler2D accelerationTexture;

out vec4 outColor;

uniform vec3 vert0;
uniform vec3 vert1;
uniform vec3 vert2;
uniform vec3 vert3;

uniform vec2 invResolution;

vec3 generate_ray()
{
	vec2 coords = gl_FragCoord.xy*invResolution;

	vec3 lower = mix(vert0, vert1, coords.x);
	vec3 upper = mix(vert2, vert3, coords.x);
	// multiplied by 16 to not create high iteration count holes
	return normalize(mix(lower, upper, coords.y));
}

void main()
{
	vec3 origin = cameraPosition;
	//vec3 rayDir = vec3(UV.x, UV.y, -1);
	vec3 rayDir = generate_ray();
	if (abs(rayDir.x) < EPSILON) {
		rayDir.x = EPSILON;
	}
	if (abs(rayDir.y) < EPSILON) {
		rayDir.y = EPSILON;
	}
	if (abs(rayDir.z) < EPSILON) {
		rayDir.z = EPSILON;
	}
	vec3 invDir = vec3(1.0f, 1.0f, 1.0f) / rayDir;
	ivec3 gridPos = ivec3(origin);
	ivec3 sign1or0 = ivec3(rayDir.x>0.0, rayDir.y>0.0, rayDir.z>0.0);

	uint voxel = uint(3221225472); // miss
	int oldAccelerationIndex = -1;
	uint chunkIndex;

	// TODO: introduce LOD
	uint i = uint(0);
	for (; i < uint(ITERATION_COUNT); i++) {
		// for debugging
#ifdef VISUALIZE_HIGH_ITERATION
		if (i >= uint(ITERATION_COUNT-1)) {
			outColor = vec4(0, 0, 255, 255);
			return;
		}
#endif
		int accelerationIndex =
			  (((gridPos.x & 0xFFF8) >> 3))
			| (((gridPos.y & 0xFFF8) >> 3) << 8)
			| (((gridPos.z & 0xFFF8) >> 3) << 16);
		if (accelerationIndex != oldAccelerationIndex) {
			chunkIndex = texelFetch(
					accelerationTexture,
					ivec2(accelerationIndex&4095, accelerationIndex>>12),
					0).r;
		}

		int mask = -1;
		int shift = 0;
		if ((chunkIndex & uint(2147483648)) == uint(0)) {
			int voxelIndex = int(chunkIndex<<9)
					| (gridPos.x & 7)
					| ((gridPos.y & 7) << 3)
					| ((gridPos.z & 7) << 6);
			voxel = texelFetch(
					voxelTexture,
					ivec2(voxelIndex&8191, voxelIndex>>13),
					0).r;
		} else {
			voxel = chunkIndex;
			mask = -8;
			shift = 3;
		}
		// TODO: calculate with fractions for less inaccuracy
		ivec3 box = (gridPos & mask) - (((ivec3(voxel) >> ((ivec3(0, 5, 10)+sign1or0*15)) & 31) ^ -sign1or0) << shift);
		vec3 t = (vec3(box) - origin) * invDir;
		
		float tmin = min(min(t.x, t.y), t.z);
		gridPos = ivec3(origin+rayDir*(tmin+0.01));
#ifdef TEST_IF_STUCK
		ivec3 oldPos = gridPos;
#endif

		// TODO: add to ULP instead
#ifdef PREFER_ACCURACY
		//float increment = 0.0;
		if (tmin == t.x) {
			gridPos.x = box.x-(1-sign1or0.x);
		} else if (tmin == t.y) {
			gridPos.y = box.y-(1-sign1or0.y);
		} else if (tmin == t.z) {
			gridPos.z = box.z-(1-sign1or0.z);
		}
#endif

#ifdef TEST_IF_STUCK
		if (oldPos == gridPos) {
			outColor = vec4(0, 255, 0, 255);
			return;
		}
#endif
		// exits if border or non-transparent voxel
		if ((voxel & uint(3221225472)) != uint(2147483648)) {
			break;
		}
	}
#ifdef VISUALIZE_ITERATIONS
	float color = float(i)/float(ITERATION_COUNT);
	outColor = vec4(color, 0.0, 0.0, 1.0);
	return;
#endif

	vec4 finalColor;
	if ((voxel & uint(3221225472)) == uint(0)) { // hit
		finalColor = vec4(float(voxel)/255.0, 0.0, 0.0, 1.0);
	} else { // miss
		finalColor = vec4(0, 0, 0, 255);
	}
	
	outColor = finalColor;
}
