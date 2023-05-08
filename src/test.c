#include <stdio.h>

#include <stdint.h>
#include <stdlib.h>

#include <emscripten.h>
#include <pthread.h>
#include <emscripten/threading.h>
// NOTE: emscripten_thread_sleep sleeps

#include <wasm_simd128.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

enum JobType {
	JOB_GENERATE_NOISE,
	JOB_MOVE,
	JOB_GENERATE_CHUNK
} type;

struct Job {
	enum JobType type;
	void *jobInfo;
};

struct JobList {
	struct Job *array;
	uint32_t allocated;
	uint32_t count;
	uint32_t current;
	uint32_t done;
};

struct JobSystem {
	struct JobList *highPriority;
	struct JobList *lowPriority;
};

struct SharedWithJS {
	uint32_t shouldUpdate;
	uint8_t *updateAccelerationBufferData;
	uint8_t *updateChunkBufferData;
} sharedWithJS;


/*
 * useful things:
 *
 * to create a thread:
		pthread_t thread;
		pthread_create(&thread, NULL, (void*)test_thread, NULL);
		pthread_detach(thread);
 * to get the time in milliseconds (as a double)
		double time = emscripten_get_now();
 *
 */

uint16_t *heightMap;
uint32_t heightMapDimension;

uint32_t *accelerationBufferData;
uint32_t *chunkBufferData;
uint32_t maxChunkCount;
uint32_t chunkCount = 0;

struct Border {
	uint32_t xNeg;
	uint32_t yNeg;
	uint32_t zNeg;
	uint32_t xPos;
	uint32_t yPos;
	uint32_t zPos;
} border;


// from ken perlin
inline float perlin_fade_function(float t)
{
	return t * t * t * (t * (t * 6 - 15) + 10);
}

// from wikipedia
float lerp(float v0, float v1, float t) {
	return v0 + t * (v1 - v0);
}

void generate_heightmap(uint32_t dim)
{
	heightMapDimension = dim;
	heightMap = malloc(heightMapDimension*heightMapDimension*sizeof(uint16_t));
	
	uint32_t valueNoiseLen = 16;
	float *valueNoise = malloc((valueNoiseLen+1)*(valueNoiseLen+1)*4);

	// generate value noise
	for (uint32_t y = 0, i = 0; y < (valueNoiseLen+1); y++) {
	for (uint32_t x = 0; x < (valueNoiseLen+1); x++,i++) {
		valueNoise[i] = emscripten_random()*2.0f-1.0f;
	}}

	uint32_t width = heightMapDimension / valueNoiseLen;
	for (uint32_t y = 0, i = 0; y < heightMapDimension; y++) {
	for (uint32_t x = 0; x < heightMapDimension; x++,i++) {
		uint32_t gridX = x / width;
		uint32_t gridY = y / width;
		
		float cellX = ((x % width)+0.5) / width;
		float cellY = ((y % width)+0.5) / width;

		float n0 = valueNoise[((gridY)*valueNoiseLen)+(gridX)];
		float n1 = valueNoise[((gridY)*valueNoiseLen)+(gridX+1)];
		float n2 = valueNoise[((gridY+1)*valueNoiseLen)+(gridX)];
		float n3 = valueNoise[((gridY+1)*valueNoiseLen)+(gridX+1)];
		float finalNoise = lerp(
				lerp(n0, n1, perlin_fade_function(cellX)),
				lerp(n2, n3, perlin_fade_function(cellX)),
				perlin_fade_function(cellY));
		heightMap[i] = finalNoise*20+100;
	}}
}

__attribute__((used)) int init()
{
	generate_heightmap(2048);

	return 0;
}

uint16_t sat_get_element(
		uint16_t *sat,
		uint32_t dim,
		uint32_t x,
		uint32_t y,
		uint32_t z)
{
	return sat[(z*dim*dim)+(y*dim)+x];
}

v128_t sat_simd_get_element(
		v128_t *sat,
		uint16_t dim,
		uint16_t x,
		uint16_t y,
		uint16_t z)
{
	return sat[(z*dim*dim)+(y*dim)+x];
}

v128_t sat_simd_get_sum(v128_t *sat, uint16_t dim, v128_t box)
{
	uint16_t *uints = (uint16_t*)&box;
	uint16_t x1 = uints[1];
	uint16_t y1 = uints[2];
	uint16_t z1 = uints[3];
	uint16_t x2 = uints[4];
	uint16_t y2 = uints[5];
	uint16_t z2 = uints[6];
	v128_t sum = sat_simd_get_element(sat, dim, x2, y2, z2);
	if (z1 > 0) {
		sum = wasm_i16x8_sub(sum, sat_simd_get_element(sat, dim, x2, y2, z1-1));
	}
	if (y1 > 0) {
		sum = wasm_i16x8_sub(sum, sat_simd_get_element(sat, dim, x2, y1-1, z2));
	}
	if (x1 > 0) {
		sum = wasm_i16x8_sub(sum, sat_simd_get_element(sat, dim, x1-1, y2, z2));
	}
	if (x1 > 0 && y1 > 0) {
		sum = wasm_i16x8_add(sum, sat_simd_get_element(sat, dim, x1-1, y1-1, z2));
	}
	if (x1 > 0 && z1 > 0) {
		sum = wasm_i16x8_add(sum, sat_simd_get_element(sat, dim, x1-1, y2, z1-1));
	}
	if (y1 > 0 && z1 > 0) {
		sum = wasm_i16x8_add(sum, sat_simd_get_element(sat, dim, x2, y1-1, z1-1));
	}
	if (x1 > 0 && y1 > 0 && z1 > 0) {
		sum = wasm_i16x8_sub(sum, sat_simd_get_element(sat, dim, x1-1, y1-1, z1-1));
	}
	return sum;
}

uint16_t sat_get_sum(uint16_t *sat, uint16_t dim,
		uint16_t x1, uint16_t y1, uint16_t z1,
		uint16_t x2, uint16_t y2, uint16_t z2)
{
	uint16_t sum = sat_get_element(sat, dim, x2, y2, z2);
	if (z1 > 0) {
		sum -= sat_get_element(sat, dim, x2, y2, z1-1);
	}
	if (y1 > 0) {
		sum -= sat_get_element(sat, dim, x2, y1-1, z2);
	}
	if (x1 > 0) {
		sum -= sat_get_element(sat, dim, x1-1, y2, z2);
	}
	if (x1 > 0 && y1 > 0) {
		sum += sat_get_element(sat, dim, x1-1, y1-1, z2);
	}
	if (x1 > 0 && z1 > 0) {
		sum += sat_get_element(sat, dim, x1-1, y2, z1-1);
	}
	if (y1 > 0 && z1 > 0) {
		sum += sat_get_element(sat, dim, x2, y1-1, z1-1);
	}
	if (x1 > 0 && y1 > 0 && z1 > 0) {
		sum -= sat_get_element(sat, dim, x1-1, y1-1, z1-1);
	}
	return sum;
}


// creates summed area table for accelerationBuffer optimization
// trying out SIMD
// returns SIMD v128_t in u16x8 format
// u16[0] = normal summed area table value
// u16[1] = xNeg sat (area shifted once towards x-)
// u16[2] = yNeg sat (area shifted once towards y-)
// u16[3] = zNeg sat
// u16[4] = xPos sat
// u16[5] = yPos sat
// u16[6] = zPos sat
v128_t *sat_simd_create_acceleration(uint32_t dim, uint32_t *input)
{
	v128_t *sat;
	posix_memalign((void**)&sat, 16, dim*dim*dim*sizeof(v128_t));

	// begin by calculating normal summed area table
	// 2 has been added to dimension for no out of bounds
	uint16_t *preSAT = malloc(dim*dim*dim*sizeof(uint16_t));
	for (uint16_t z = 0, index = 0; z < dim; z++) {
	for (uint16_t y = 0; y < dim; y++) {
	for (uint16_t x = 0; x < dim; x++,index++) {
		uint16_t sum = (input[(z << 16)|(y << 8)|(x)] & (3 << 30)) == 0;

		if (x > 0 && y > 0 && z > 0) {
			sum += sat_get_element(preSAT, dim, x-1, y-1, z-1);
		}
		if (z > 0) {
			sum += sat_get_element(preSAT, dim, x, y, z-1);
		}
		if (y > 0) {
			sum += sat_get_element(preSAT, dim, x, y-1, z);
		}
		if (x > 0) {
			sum += sat_get_element(preSAT, dim, x-1, y, z);
		}
		if (x > 0 && y > 0) {
			sum -= sat_get_element(preSAT, dim, x-1, y-1, z);
		}
		if (y > 0 && z > 0) {
			sum -= sat_get_element(preSAT, dim, x, y-1, z-1);
		}
		if (x > 0 && z > 0) {
			sum -= sat_get_element(preSAT, dim, x-1, y, z-1);
		}

		preSAT[index] = sum;
	}}}
	for (uint16_t z = 0, index = 0; z < dim; z++) {
	for (uint16_t y = 0; y < dim; y++) {
	for (uint16_t x = 0; x < dim; x++,index++) {
		uint32_t sum = preSAT[index];
		uint16_t negX = x == 0 ? 0 : sat_get_element(preSAT, dim, x-1, y, z);
		uint16_t negY = y == 0 ? 0 : sat_get_element(preSAT, dim, x, y-1, z);
		uint16_t negZ = z == 0 ? 0 : sat_get_element(preSAT, dim, x, y, z-1);
		uint16_t posX = x == dim-1 ? sum : sat_get_element(preSAT, dim, x+1, y, z);
		uint16_t posY = y == dim-1 ? sum : sat_get_element(preSAT, dim, x, y+1, z);
		uint16_t posZ = z == dim-1 ? sum : sat_get_element(preSAT, dim, x, y, z+1);

		sat[index] = wasm_u16x8_make(sum, negX, negY, negZ, posX, posY, posZ, 0);
	}}}

	free(preSAT);
	return sat;
}

uint16_t sat_2d_get_sum(
		uint16_t *sat, uint16_t dim,
		uint16_t x0, uint16_t y0,
		uint16_t x1, uint16_t y1)
{
	uint16_t sum = sat[dim*y1+x1];
	if (x0 > 0) {
		sum -= sat[dim*y1+(x0-1)];
	}
	if (y0 > 0) {
		sum -= sat[dim*(y0-1)+(x1)];
	}
	if (x0 > 0 && y0 > 0) {
		sum += sat[dim*(y0-1)+(x0-1)];
	}
	return sum;
}

// optimizes a set of 4x4x4 chunks (32*32*32) voxels
void chunk_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz)
{
	// TODO: expand sat in 16? voxels in each side
	/*
	 * simdSAT makes sure only 8 loads are necessary in the main loop
	 * TODO: implement this?
	 * [0] = sat[x, y, z]
	 * [1] = sat[x, y, z-1]
	 * [2] = sat[x, y-1, z]
	 * [3] = sat[x, y-1, z-1]
	 * [4] = sat[x-1, y, z]
	 * [5] = sat[x-1, y, z-1]
	 * [6] = sat[x-1, y-1, z]
	 * [7] = sat[x-1, y-1, z-1]
	 */
	//v128_t *simdSAT;
	//posix_memalign((void**)&simdSAT, 16, 32*32*32*sizeof(v128_t));
	
	uint16_t *sat = malloc(40*40*40*sizeof(uint16_t));
	uint16_t maxDFIterationCount = 16;

	for (uint16_t z = 0, satIndex=0; z < 40; z++) {
	for (uint16_t y = 0; y < 40; y++) {
	for (uint16_t x = 0; x < 40; x++,satIndex++) {
		int realX = (inax<<3)+x-4;
		int realY = (inay<<3)+y-4;
		int realZ = (inaz<<3)+z-4;

		int cx = realX&7;
		int cy = realY&7;
		int cz = realZ&7;
		int accelerationX = realX>>3;
		int accelerationY = realY>>3;
		int accelerationZ = realZ>>3;

		uint16_t sum;
		if (accelerationX < 0 || accelerationY < 0 || accelerationZ < 0
				|| accelerationX > 255 || accelerationY > 255
				|| accelerationZ > 255) {
			sum = 1;
		} else {
			uint32_t accelerationIndex =
				(accelerationZ << 16) | (accelerationY << 8) | accelerationX;
			uint32_t chunkIndex = accelerationBufferData[accelerationIndex];

			switch ((chunkIndex & (3u << 30))) {
			case 0u:
				;
				uint32_t voxelIndex = chunkIndex | (cz<<6)|(cy<<3)|cx;
				uint32_t voxel = chunkBufferData[voxelIndex];

				sum = (voxel & (3u << 30)) == 0u;
				break;
			case 3u<<30:
				sum = 1;
				break;
			case 2u<<30:
				sum = 0;
				break;
			default: __builtin_unreachable();
			}
		}

		if (x > 0 && y > 0 && z > 0) {
			sum += sat_get_element(sat, 40, x-1, y-1, z-1);
		}
		if (z > 0) {
			sum += sat_get_element(sat, 40, x, y, z-1);
		}
		if (y > 0) {
			sum += sat_get_element(sat, 40, x, y-1, z);
		}
		if (x > 0) {
			sum += sat_get_element(sat, 40, x-1, y, z);
		}
		if (x > 0 && y > 0) {
			sum -= sat_get_element(sat, 40, x-1, y-1, z);
		}
		if (y > 0 && z > 0) {
			sum -= sat_get_element(sat, 40, x, y-1, z-1);
		}
		if (x > 0 && z > 0) {
			sum -= sat_get_element(sat, 40, x-1, y, z-1);
		}

		sat[satIndex] = sum;
	}}}

	for (uint16_t az = 0; az < 4; az++) {
	for (uint16_t ay = 0; ay < 4; ay++) {
	for (uint16_t ax = 0; ax < 4; ax++) {
		uint16_t accelerationX = inax+ax;
		uint16_t accelerationY = inay+ay;
		uint16_t accelerationZ = inaz+az;
		uint32_t accelerationIndex =
			(accelerationZ << 16) | (accelerationY << 8) | accelerationX;
		uint32_t chunkIndex = accelerationBufferData[accelerationIndex];

		uint8_t isChunk = (chunkIndex & (3u << 30)) == 0u;
		if (!isChunk) {
			continue;
		}
		for (uint16_t cz = 0, offset = 0; cz < 8; cz++) {
		for (uint16_t cy = 0; cy < 8; cy++) {
		for (uint16_t cx = 0; cx < 8; cx++, offset++) {
			int x = (ax << 3) | cx;
			int y = (ay << 3) | cy;
			int z = (az << 3) | cz;
			
			uint32_t voxelIndex = chunkIndex | cx | (cy << 3) | (cz << 6);
			uint32_t voxel = chunkBufferData[voxelIndex];

			if ((voxel & (3u << 30)) != (2u << 30)) {
				continue;
			}

			int satX = x+4;
			int satY = y+4;
			int satZ = z+4;

			int x0 = satX;
			int y0 = satY;
			int z0 = satZ;
			int x1 = satX;
			int y1 = satY;
			int z1 = satZ;

			uint8_t xNegExpand = 2;
			uint8_t yNegExpand = 2;
			uint8_t zNegExpand = 2;
			uint8_t xPosExpand = 2;
			uint8_t yPosExpand = 2;
			uint8_t zPosExpand = 2;
			
			for (uint16_t i = 0; i < maxDFIterationCount; i++) {
				if (x0 < 1) xNegExpand = 0;
				if (y0 < 1) yNegExpand = 0;
				if (z0 < 1) zNegExpand = 0;
				if (x1 > 38) xPosExpand = 0;
				if (y1 > 38) yPosExpand = 0;
				if (z1 > 38) zPosExpand = 0;

				uint16_t targetX0 = x0-(xNegExpand == 2);
				uint16_t targetY0 = y0-(yNegExpand == 2);
				uint16_t targetZ0 = z0-(zNegExpand == 2);
				uint16_t targetX1 = x1+(xPosExpand == 2);
				uint16_t targetY1 = y1+(yPosExpand == 2);
				uint16_t targetZ1 = z1+(zPosExpand == 2);

				if (xNegExpand) {
					if (0 == sat_get_sum(sat, 40,
								x0-1, targetY0, targetZ0,
								x0-1, targetY1, targetZ1)) {
						x0--;
						targetX0 = x0;
					} else {
						xNegExpand = 1;
					}
				}
				if (yNegExpand) {
					if (0 == sat_get_sum(sat, 40,
								targetX0, y0-1, targetZ0,
								targetX1, y0-1, targetZ1)) {
						y0--;
						targetY0 = y0;
					} else {
						yNegExpand = 1;
					}
				}
				if (zNegExpand) {
					if (0 == sat_get_sum(sat, 40,
								targetX0, targetY0, z0-1,
								targetX1, targetY1, z0-1)) {
						z0--;
						targetZ0 = z0;
					} else {
						zNegExpand = 1;
					}
				}
				if (xPosExpand) {
					if (0 == sat_get_sum(sat, 40,
								x1+1, targetY0, targetZ0,
								x1+1, targetY1, targetZ1)) {
						x1++;
						targetX1 = x1;
					} else {
						xPosExpand = 1;
					}
				}
				if (yPosExpand) {
					if (0 == sat_get_sum(sat, 40,
								targetX0, y1+1, targetZ0,
								targetX1, y1+1, targetZ1)) {
						y1++;
						targetY1 = y1;
					} else {
						yPosExpand = 1;
					}
				}
				if (zPosExpand) {
					if (0 == sat_get_sum(sat, 40,
								targetX0, targetY0, z1+1,
								targetX1, targetY1, z1+1)) {
						z1++;
						targetZ1 = z1;
					} else {
						zPosExpand = 1;
					}
				}
			}

			// construct final voxel
			voxel = (1 << 31)
				| (satX-x0) | ((satY-y0) << 5) | ((satZ-z0) << 10)
				| ((x1-satX) << 15) | ((y1-satY) << 20) | ((z1-satZ) << 25);
			chunkBufferData[voxelIndex] = voxel;
		}}}
	}}}

	free(sat);
}

void acceleration_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz)
{
	// summed area table expanded by 4 voxels in each direction
	uint16_t *sat = malloc(40*40*40*sizeof(uint16_t));
	uint16_t maxDFIterationCount = 31;

	// generate SAT
	for (uint16_t z = 0,satIndex=0; z < 40; z++) {
	for (uint16_t y = 0; y < 40; y++) {
	for (uint16_t x = 0; x < 40; x++,satIndex++) {
		int accelerationX = inax+x-4;
		int accelerationY = inay+y-4;
		int accelerationZ = inaz+z-4;
		uint32_t accelerationIndex = (accelerationZ<<16)
			| (accelerationY<<8) | accelerationX;
		
		uint16_t sum;
		if (accelerationX < 0 || accelerationY < 0 || accelerationZ < 0
				|| accelerationX > 255 || accelerationY > 255
				|| accelerationZ > 255) {
			sum = 1;
		} else {
			uint32_t voxel = accelerationBufferData[accelerationIndex];
			switch (voxel & (3u << 30)) {
			case 0u:
				sum = 1;
				break;
			case 3u<<30:
				sum = 1;
				break;
			case 2u<<30:
				sum = 0;
				break;
			default: __builtin_unreachable();
			}
		}
		
		if (x > 0 && y > 0 && z > 0) {
			sum += sat_get_element(sat, 40, x-1, y-1, z-1);
		}
		if (z > 0) {
			sum += sat_get_element(sat, 40, x, y, z-1);
		}
		if (y > 0) {
			sum += sat_get_element(sat, 40, x, y-1, z);
		}
		if (x > 0) {
			sum += sat_get_element(sat, 40, x-1, y, z);
		}
		if (x > 0 && y > 0) {
			sum -= sat_get_element(sat, 40, x-1, y-1, z);
		}
		if (y > 0 && z > 0) {
			sum -= sat_get_element(sat, 40, x, y-1, z-1);
		}
		if (x > 0 && z > 0) {
			sum -= sat_get_element(sat, 40, x-1, y, z-1);
		}
		
		sat[satIndex] = sum;
	}}}

	for (uint16_t z = 0,satIndex=0; z < 32; z++) {
	for (uint16_t y = 0; y < 32; y++) {
	for (uint16_t x = 0; x < 32; x++,satIndex++) {
		uint16_t accelerationX = inax+x;
		uint16_t accelerationY = inay+y;
		uint16_t accelerationZ = inaz+z;
		
		uint32_t accelerationIndex = (accelerationZ<<16)
			| (accelerationY<<8) | accelerationX;
		uint32_t voxel = accelerationBufferData[accelerationIndex];
		if ((voxel & (3u << 30)) != (2u << 30)) {
			continue;
		}

		int satX = x+4;
		int satY = y+4;
		int satZ = z+4;

		int x0 = satX;
		int y0 = satY;
		int z0 = satZ;
		int x1 = satX;
		int y1 = satY;
		int z1 = satZ;

		// 2: only hits currently
		// 1: 1 intersection found
		// 0: can't
		uint8_t xNegExpand = 2;
		uint8_t yNegExpand = 2;
		uint8_t zNegExpand = 2;
		uint8_t xPosExpand = 2;
		uint8_t yPosExpand = 2;
		uint8_t zPosExpand = 2;
		
		// TODO: an optimization would be to compare
		for (uint16_t i = 0; i < maxDFIterationCount; i++) {
			if (x0 < 1) xNegExpand = 0;
			if (y0 < 1) yNegExpand = 0;
			if (z0 < 1) zNegExpand = 0;
			if (x1 > 38) xPosExpand = 0;
			if (y1 > 38) yPosExpand = 0;
			if (z1 > 38) zPosExpand = 0;

			uint16_t targetX0 = x0-(xNegExpand == 2);
			uint16_t targetY0 = y0-(yNegExpand == 2);
			uint16_t targetZ0 = z0-(zNegExpand == 2);
			uint16_t targetX1 = x1+(xPosExpand == 2);
			uint16_t targetY1 = y1+(yPosExpand == 2);
			uint16_t targetZ1 = z1+(zPosExpand == 2);

			if (xNegExpand) {
				if (0 == sat_get_sum(sat, 40,
							x0-1, targetY0, targetZ0,
							x0-1, targetY1, targetZ1)) {
					x0--;
					targetX0 = x0;
				} else {
					xNegExpand = 1;
				}
			}
			if (yNegExpand) {
				if (0 == sat_get_sum(sat, 40,
							targetX0, y0-1, targetZ0,
							targetX1, y0-1, targetZ1)) {
					y0--;
					targetY0 = y0;
				} else {
					yNegExpand = 1;
				}
			}
			if (zNegExpand) {
				if (0 == sat_get_sum(sat, 40,
							targetX0, targetY0, z0-1,
							targetX1, targetY1, z0-1)) {
					z0--;
					targetZ0 = z0;
				} else {
					zNegExpand = 1;
				}
			}
			if (xPosExpand) {
				if (0 == sat_get_sum(sat, 40,
							x1+1, targetY0, targetZ0,
							x1+1, targetY1, targetZ1)) {
					x1++;
					targetX1 = x1;
				} else {
					xPosExpand = 1;
				}
			}
			if (yPosExpand) {
				if (0 == sat_get_sum(sat, 40,
							targetX0, y1+1, targetZ0,
							targetX1, y1+1, targetZ1)) {
					y1++;
					targetY1 = y1;
				} else {
					yPosExpand = 1;
				}
			}
			if (zPosExpand) {
				if (0 == sat_get_sum(sat, 40,
							targetX0, targetY0, z1+1,
							targetX1, targetY1, z1+1)) {
					z1++;
					targetZ1 = z1;
				} else {
					zPosExpand = 1;
				}
			}
		}
		voxel = (1 << 31)
			| (satX-x0) | ((satY-y0) << 5) | ((satZ-z0) << 10)
			| ((x1-satX) << 15) | ((y1-satY) << 20) | ((z1-satZ) << 25);
		accelerationBufferData[accelerationIndex] = voxel;
	}}}
}

// optimizes area in accelerationBuffer (32*32*32)
// input is location in accelerationBuffer of (x-, y-, z-) part of area
void acceleration_optimize_area(uint16_t xina, uint16_t yina, uint16_t zina)
{
	uint32_t satDimension = 32;
	v128_t *sat = sat_simd_create_acceleration(satDimension, accelerationBufferData);

	uint16_t maxDFIterationCount = 31;

	// sets the furthest you can expand in each direction
	// TODO: set this depending on in coords and border coords
	
	v128_t borderVector = wasm_i16x8_make(0,
			(xina == (border.xNeg)) ? 1 : 0,
			(yina == (border.yNeg)) ? 1 : 0,
			(zina == (border.zNeg)) ? 1 : 0,
			((xina+31) == (border.xPos)) ? 30 : 31,
			((yina+31) == (border.yPos)) ? 30 : 31,
			((zina+31) == (border.zPos)) ? 30 : 31,
			0);

	v128_t addVector = wasm_i16x8_make(0, -1, -1, -1, 1, 1, 1, 0);

	v128_t zeroVector = wasm_i16x8_make(0, 0, 0, 0, 0, 0, 0, 0);

	// this is inexact but that does not matter since this makes a good result
	// with good performance
	// An improvement would be to fallback to a more precise method if
	// the offsets are small
	// A future further optimization step can make sure the df is as good
	// as possible.
	// This can be optimized further by comparing to neighbours df
	for (uint16_t accelerationZ = 0; accelerationZ < 32; accelerationZ++) {
	for (uint16_t accelerationY = 0; accelerationY < 32; accelerationY++) {
	for (uint16_t accelerationX = 0; accelerationX < 32; accelerationX++) {
		uint32_t accelerationIndex =
			(accelerationZ << 16) | (accelerationY << 8) | accelerationX;

		// remove border and non air chunks
		if ((accelerationBufferData[accelerationIndex] & (3u << 30)) != (2u << 30)) {
			continue;
		}

		union PositionUnion {
			v128_t vector;
			uint16_t uints[8];
		} current;
		current.vector = wasm_u16x8_make(0,
				accelerationX, accelerationY, accelerationZ,
				accelerationX, accelerationY, accelerationZ,
				0);
		v128_t old;
			
		union {
			v128_t vector;
			uint16_t uints[8];
		} sums;
		sums.vector = zeroVector;

		uint32_t i = 0;
		for (; i < maxDFIterationCount; i++) {
			old = current.vector;
			v128_t mask1Vector = wasm_i16x8_ne(current.vector, borderVector);
			v128_t mask2Vector = wasm_i16x8_eq(sums.vector, zeroVector);
			v128_t finalMaskVector = wasm_v128_and(mask1Vector, mask2Vector);
			if (!wasm_v128_any_true(finalMaskVector)) { // if can't add: stop
				break;
			}

			v128_t postAddVector = wasm_v128_and(addVector, finalMaskVector);
			current.vector = wasm_i16x8_add(current.vector, postAddVector);

			sums.vector = sat_simd_get_sum(sat, satDimension, current.vector);
			if (sums.uints[0] != 0) {
				current.vector = old;
				//break;
				// TODO: change to a new approach here
			}
		}
		uint32_t voxel = (1 << 31)
			| ((accelerationX-current.uints[1]))
			| ((accelerationY-current.uints[2]) << 5)
			| ((accelerationZ-current.uints[3]) << 10)
			| ((current.uints[4]-accelerationX) << 15)
			| ((current.uints[5]-accelerationY) << 20)
			| ((current.uints[6]-accelerationZ) << 25);
		accelerationBufferData[accelerationIndex] = voxel;
	}}}
}

void acceleration_area_create_from_2dnoise(
		uint32_t inax, uint32_t inay, uint32_t inaz)
{
	// TODO: rename acceleration... to ina...
	uint32_t accelerationX = inax;
	uint32_t accelerationY = inay;
	uint32_t accelerationZ = inaz;

	uint32_t chunkIndex = chunkCount << 9;
	uint32_t accelerationIndex = (accelerationZ << 16) | (accelerationY << 8) | accelerationX;
	
	if (accelerationX > (border.xPos-2) || accelerationY > (border.yPos-2)
			|| accelerationZ > (border.zPos-2) || accelerationX == 0
			|| accelerationY == 0 || accelerationZ == 0) {
		accelerationBufferData[accelerationIndex] = 3 << 30;
		return;
	} else {
		accelerationBufferData[accelerationIndex] = chunkIndex;
	}
	// TODO: calculate min and max heightmap value in chunk and only generated required chunks
	uint32_t voxelCountInChunk = 0;
	for (uint32_t chunkZ = 0; chunkZ < 8; chunkZ++) {
	for (uint32_t chunkY = 0; chunkY < 8; chunkY++) {
	for (uint32_t chunkX = 0; chunkX < 8; chunkX++) {
		uint32_t realX = (accelerationX<<3)+chunkX;
		uint32_t realY = (accelerationY<<3)+chunkY;
		uint32_t realZ = (accelerationZ<<3)+chunkZ;
		
		uint32_t voxel = (uint32_t)(emscripten_random()*32) & 31u;
		uint32_t depth = heightMap[(realZ*heightMapDimension)|realX];
		if (realY > depth) {
			voxel = 0;
		}
		if (voxel != 0) {
			voxelCountInChunk++;
		} else {
			voxel = 1 << 31;
		}
		
		uint32_t chunkOffset = (chunkZ << 6) | (chunkY << 3) | chunkX;
		uint32_t index = chunkIndex | chunkOffset;
		chunkBufferData[index] = voxel;
	}}}
	if (voxelCountInChunk == 0) {
		accelerationBufferData[accelerationIndex] = 1 << 31;
	} else {
		chunkCount++;
	}
}

// NOTE: make sure the chunk is not a border first!!!
// tries to generate a chunk, if chunk empty-> does not generate one
void chunk_create_from_2dnoise(uint32_t inax, uint32_t inay, uint32_t inaz)
{
	uint32_t accelerationX = inax;
	uint32_t accelerationY = inay;
	uint32_t accelerationZ = inaz;

	uint32_t chunkIndex = chunkCount << 9;
	uint32_t accelerationIndex = (accelerationZ << 16)
		| (accelerationY << 8) | accelerationX;
	
	accelerationBufferData[accelerationIndex] = chunkIndex;
	uint32_t voxelCountInChunk = 0;
	for (uint32_t chunkZ = 0; chunkZ < 8; chunkZ++) {
	for (uint32_t chunkY = 0; chunkY < 8; chunkY++) {
	for (uint32_t chunkX = 0; chunkX < 8; chunkX++) {
		uint32_t realX = (accelerationX<<3)+chunkX;
		uint32_t realY = (accelerationY<<3)+chunkY;
		uint32_t realZ = (accelerationZ<<3)+chunkZ;
		
		uint32_t voxel = (uint32_t)(emscripten_random()*32) & 31u;
		voxel += 1;
		uint32_t depth = heightMap[(realZ*heightMapDimension)|realX];
		if (realY > depth) {
			voxel = 0;
		}
		if (voxel != 0) {
			voxelCountInChunk++;
		} else {
			voxel = 1 << 31;
		}
		
		uint32_t chunkOffset = (chunkZ << 6) | (chunkY << 3) | chunkX;
		uint32_t index = chunkIndex | chunkOffset;
		chunkBufferData[index] = voxel;
	}}}
	if (voxelCountInChunk == 0) {
		accelerationBufferData[accelerationIndex] = 1 << 31;
	} else {
		chunkCount++;
	}
}

void chunk_create_empty(uint32_t inax, uint32_t inay, uint32_t inaz)
{
	uint32_t accelerationX = inax;
	uint32_t accelerationY = inay;
	uint32_t accelerationZ = inaz;

	uint32_t chunkIndex = chunkCount << 9;
	uint32_t accelerationIndex = (accelerationZ << 16)
		| (accelerationY << 8) | accelerationX;
	
	accelerationBufferData[accelerationIndex] = chunkIndex;
	for (uint32_t chunkZ = 0; chunkZ < 8; chunkZ++) {
	for (uint32_t chunkY = 0; chunkY < 8; chunkY++) {
	for (uint32_t chunkX = 0; chunkX < 8; chunkX++) {
		uint32_t voxel = 1 << 31;
		
		uint32_t chunkOffset = (chunkZ << 6) | (chunkY << 3) | chunkX;
		uint32_t index = chunkIndex | chunkOffset;
		chunkBufferData[index] = voxel;
	}}}

	chunkCount++;
}

// TODO: check for out of memory instead of crashing...
void acceleration_area_create_from_2dnoise_version_2(uint32_t inax, uint32_t inaz)
{
	// TODO: move this border creation step elsewhere
	if (inax > (border.xPos-2) || inaz > (border.zPos-2)
			|| inax == 0 || inaz == 0) {
		for (uint32_t inay = 0; inay < 31; inay++) {
			chunk_create_from_2dnoise(inax, inay, inaz);
		}
	}
	// get lowest and highest point. can be optimized by SAT yet again
	uint32_t minDepth = 0xFFFFFFFF;
	uint32_t maxDepth = 0;
	for (uint32_t z = 0; z < 8; z++) {
	for (uint32_t x = 0; x < 8; x++) {
		uint32_t realX = (inax<<3)+x;
		uint32_t realZ = (inaz<<3)+z;
		uint32_t depth = heightMap[(realZ*heightMapDimension)|realX];
		minDepth = MIN(minDepth, depth);
		maxDepth = MAX(maxDepth, depth);
	}}
	// TODO: do something better
	// this is a temporary solution to
	// make sure all opaque voxels are surrounded
	// by distance fields so rendering is correct
	maxDepth += 1;
	for (uint32_t inay = border.yNeg; inay <= border.yPos; inay++) {
		uint32_t accelerationIndex = (inaz << 16)
			| (inay << 8) | inax;
		if (inax > (border.xPos-2) || inay > (border.yPos-2)
				|| inaz > (border.zPos-2) || inax == border.xNeg
				|| inay == border.yNeg || inaz == border.zNeg) {
			accelerationBufferData[accelerationIndex] = 3 << 30;
		} else if (((inay<<3)+7) >= minDepth && (inay<<3) <= maxDepth) {
			chunk_create_from_2dnoise(inax, inay, inaz);
		} else {
			accelerationBufferData[accelerationIndex] = 1 << 31;
		}
	}
}

__attribute__((used)) void *create_chunks(uint32_t accelerationBufferSize, uint32_t chunkBufferSize)
{
	accelerationBufferData = malloc(accelerationBufferSize);
	chunkBufferData = malloc(chunkBufferSize);

	maxChunkCount = chunkBufferSize / (512*sizeof(uint32_t));

	uint32_t xMax = 128;
	uint32_t yMax = 64;
	uint32_t zMax = 128;
	
	border.xNeg = 0;
	border.yNeg = 0;
	border.zNeg = 0;
	border.xPos = xMax-1;
	border.yPos = yMax-1;
	border.zPos = zMax-1;

	for (uint32_t accelerationZ = border.zNeg; accelerationZ <= border.zPos; accelerationZ++) {
	for (uint32_t accelerationX = border.xNeg; accelerationX <= border.xPos; accelerationX++) {
		acceleration_area_create_from_2dnoise_version_2(
				accelerationX, accelerationZ);
	}}

	// TODO: make cuboid 1 smaller, so to not include border
	for (uint32_t accelerationZ = border.zNeg; accelerationZ <= border.zPos; accelerationZ+=32) {
	for (uint32_t accelerationY = border.yNeg; accelerationY <= border.yPos; accelerationY+=32) {
	for (uint32_t accelerationX = border.xNeg; accelerationX <= border.xPos; accelerationX+=32) {
		acceleration_area_calculate_df(
				accelerationX, accelerationY, accelerationZ);
	}}}
	for (uint32_t z = 0; z < zMax; z+=4) {
	for (uint32_t y = 0; y < yMax; y+=4) {
	for (uint32_t x = 0; x < xMax; x+=4) {
		chunk_area_calculate_df(x, y, z);
	}}}

	printf("%d / %d\n", chunkCount, maxChunkCount);

	uint32_t *returnedData = malloc(64);
	returnedData[0] = (uint32_t)accelerationBufferData;
	returnedData[1] = (uint32_t)chunkBufferData;
	return returnedData;
}
