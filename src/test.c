#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <pthread.h>

#include <emscripten.h>
#include <emscripten/threading.h>

#include <wasm_simd128.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// world coords of 0,0,0 in accelerationBufferData
uint32_t xWorld;
uint32_t yWorld;
uint32_t zWorld;
uint32_t seed;

uint32_t accelerationBufferSize;
uint32_t chunkBufferSize;

/*
 * useful things:
 *
 * to create a thread:
		pthread_t thread;
		pthread_create(&thread, NULL, (void*)test_thread, NULL);
		pthread_detach(thread);
 * to get the time in milliseconds (as a double)
		double time = emscripten_get_now();
 * NOTE: emscripten_thread_sleep sleeps
 *
 */

struct GameSettings {
	uint16_t loadDistance;
} gameSettings;

uint16_t *heightMap;
uint32_t heightMapDimension;

// byte 0: acceleration start
// byte 1: acceleration end
uint16_t *accelerationHeightMap;

uint32_t *accelerationBufferData;
uint32_t *chunkBufferData;
uint32_t maxChunkCount;
uint32_t chunkCount;

uint32_t targetLoadDistance = 30;

struct Border {
	uint32_t xNeg;
	uint32_t yNeg;
	uint32_t zNeg;
	uint32_t xPos;
	uint32_t yPos;
	uint32_t zPos;
} border;

struct SharedWithJS {
	float cameraPosX;
	float cameraPosY;
	float cameraPosZ;

	float cameraAxisX;
	float cameraAxisY;
	float cameraAxisZ;
} sharedWithJS;

// also shared with JS
struct UpdateStructure {
	// circular buffers
	volatile uint32_t *ucIndices;
	volatile uint32_t *uaIndices;
	
	// actual size subtracted by 1
	uint32_t ucIndicesCount;
	uint32_t uaIndicesCount;

	volatile uint32_t ucWriteIndex;
	volatile uint32_t ucReadIndex;
	volatile uint32_t uaWriteIndex;
	volatile uint32_t uaReadIndex;

	// 0 = normal
	// 1 = the called function is currently transform()
	// 2 = transform() has been called, waiting for JS
	uint32_t transformState;
	uint32_t copyChunkCount;
} updateStructure;

// octree pointing to chunks
struct Octree {
	uint32_t allocationCount;
	uint32_t count;
	// index to next node or 0
	uint32_t *indices;
	void *data;
};

struct Octree generationOctree;

// for debug. TODO: remove
uint32_t uaCount = 0;
uint32_t ucCount = 0;

uint32_t octree_generate_chunks_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize);
void octree_generate_chunks();
uint32_t octree_optimize_acceleration(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize);
__attribute__((used)) void *create_chunks(
		uint32_t accelerationBufferSize, uint32_t chunkBufferSize);
void ua_push_index(uint32_t accelerationIndex);
void uc_push_index(uint32_t accelerationIndex);
void uc_reset();
void ua_reset();
void update_structure_initialize();
float perlin_fade_function(float t);
float lerp(float v0, float v1, float t);
void generate_heightmap(uint32_t dim);
void chunk_create_from_2dnoise(
		uint32_t inax, uint32_t inay, uint32_t inaz);
void acceleration_area_create_from_2dnoise_version_2(
		uint32_t inax, uint32_t inaz);
void border_area_create_from_2dnoise(
		uint32_t inax, uint32_t inaz,
		struct Border newBorder,
		struct Border oldBorder);
void border_area_optimize_acceleration(
		uint32_t inax, uint32_t inaz,
		struct Border newBorder,
		struct Border oldBorder);
void load_new_chunks();
void job_thread();
uint16_t sat_get_element(
		uint16_t *sat,
		uint32_t dim,
		uint32_t x,
		uint32_t y,
		uint32_t z);
uint16_t sat_get_sum(uint16_t *sat, uint16_t dim,
		uint16_t x1, uint16_t y1, uint16_t z1,
		uint16_t x2, uint16_t y2, uint16_t z2);
uint16_t sat_2d_get_sum(
		uint16_t *sat, uint16_t dim,
		uint16_t x0, uint16_t y0,
		uint16_t x1, uint16_t y1);
void chunk_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz);
void acceleration_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz);
void chunk_create_empty(uint32_t inax, uint32_t inay, uint32_t inaz);

bool border_completely_inside_border(struct Border inside, struct Border outside);

void create_octree(
		struct Octree *octree,
		uint32_t allocationCount,
		uint32_t dataAllocationCount);

// remakes accelerationBufferData with a new center
void transform();

struct ReturnedNode {
	uint32_t index;
	uint32_t voxelSize;
	uint16_t x;
	uint16_t y;
	uint16_t z;
};
struct ReturnedNode get_octree_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetLevel);
struct ReturnedNode force_octree_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetLevel);

// Dan bernstein hash function
uint32_t djb33_hash(const char* s, size_t len)
{
    uint32_t h = 5381;
    while (len--) {
        /* h = 33 * h ^ s[i]; */
        h += (h << 5);  
        h ^= *s++;
    }
    return h;
}
float distance2_point_to_cube(
		uint16_t cubeX,
		uint16_t cubeY,
		uint16_t cubeZ,
		uint16_t voxelSize,
		float pointX, float pointY, float pointZ)
{
	uint16_t centerX = cubeX+(voxelSize>>1);
	uint16_t centerY = cubeY+(voxelSize>>1);
	uint16_t centerZ = cubeZ+(voxelSize>>1);

	uint16_t halfVoxelSize = voxelSize>>1;

	float dx = MAX(fabsf(centerX-pointX) - (float)halfVoxelSize, 0.0f);
	float dy = MAX(fabsf(centerY-pointY) - (float)halfVoxelSize, 0.0f);
	float dz = MAX(fabsf(centerZ-pointZ) - (float)halfVoxelSize, 0.0f);

	return dx*dx + dy*dy + dz*dz;
}

uint32_t *tmp;
uint32_t tmpCount;

void octree_generate_chunks()
{
	if (tmp == 0) {
		tmp  = malloc(256*256*256*4);
	}
	float targetDistance2 = (float)gameSettings.loadDistance
		*(float)gameSettings.loadDistance;
	tmpCount = 0;
	// first 512 bits used as lookup used to know
	// if acceleration_area_calculate_df is necessary
	// 1 bit = 1 bool in 8x8x8 grid
	octree_generate_chunks_recursive(
			&generationOctree, targetDistance2, 0, 0, 0, 0, 1 << 10);
	// generate acceleration df
}

uint32_t octree_generate_chunks_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize)
{
	// 0 == nothing has been generated
	// 8 == all 8 2d leaves have been generated
	uint8_t *progress2DGeneration = &((uint8_t*)octree->data)[512];

	// sqrt(3)
	//const float largestDistanceMultiplier = 1.73205080757f;

	uint32_t progressChange = 0;
	for (uint8_t cube = 0; cube < 8; cube++) {
		uint32_t currentNode = currentSet+cube;
		uint16_t newX = x+(-(cube&1) & voxelSize);
		uint16_t newY = y+((-(cube&2)>>1) & voxelSize);
		uint16_t newZ = z+((-(cube&4)>>2) & voxelSize);

		float distance2 = distance2_point_to_cube(
				newX, newY, newZ, voxelSize,
				sharedWithJS.cameraPosX,
				sharedWithJS.cameraPosY,
				sharedWithJS.cameraPosZ);
		if (distance2 > targetDistance2) {
			continue;
		}
		if (progress2DGeneration[currentNode] > 7) {
			continue;
		}

		if (voxelSize == 8) {
			progress2DGeneration[currentNode] = 8;
			uint32_t accelerationIndex = ((newZ>>3) << 16)
				| ((newY>>3) << 8) | (newX>>3);
			uint32_t oldVal = accelerationBufferData[accelerationIndex];

			chunk_create_from_2dnoise(newX>>3, newY>>3, newZ>>3);
			if (oldVal != accelerationBufferData[accelerationIndex]) {
				// dont push index that has not gotten df generated yet
				//ua_push_index(accelerationIndex);
			}
			acceleration_area_calculate_df((newX>>3)&~31, (newY>>3)&~31, (newZ>>3)&~31);

			uint32_t index = ((newZ>>3)/32 *8*8)
				| ((newY>>3) / 32 *8) | ((newX>>3) / 32);
			((uint8_t*)generationOctree.data)[index] = 1;


			continue;
		}

		uint32_t newSet = octree->indices[currentNode];
		if (newSet == 0) {
			newSet = octree->count;
			octree->indices[currentNode] = newSet;
			// NOTE: octree.indices[octree.count] -> octree.count+7
			//       MUST be zero!
			octree->count += 8;
		}

		uint32_t newProgressChange = octree_generate_chunks_recursive(
			octree, targetDistance2,
			newSet,
			newX, newY, newZ,
			voxelSize>>1
		);
		progress2DGeneration[currentNode] += newProgressChange;
		if (newProgressChange == 8) {
			progressChange += 1;
		}
	}
	return progressChange;
}
// does not check for overflow! Should be done elsewhere
// aka. do not create chunks if almost overflowing
void ua_push_index(uint32_t accelerationIndex)
{
	uaCount++;
	/*
	emscripten_atomic_store_u32(
			(void*)&accelerationBufferData[accelerationIndex],
			accelerationBufferData[accelerationIndex]);
			*/
	// memory barrier
	// make sure acceleration item is written to first
	asm volatile("" : : : "memory");

	emscripten_atomic_store_u32(
			(void*)&updateStructure.uaIndices[updateStructure.uaWriteIndex],
			accelerationIndex);

	// make sure data is written to before showing the data to other threads
	// by adding to writeIndex
	asm volatile("" : : : "memory");

	/*
	updateStructure.uaWriteIndex = (updateStructure.uaWriteIndex+1)
		& (updateStructure.uaIndicesCount);
		*/
	emscripten_atomic_store_u32(
			(void*)&updateStructure.uaWriteIndex,
			(updateStructure.uaWriteIndex+1)
				% (updateStructure.uaIndicesCount));
}

void uc_push_index(uint32_t accelerationIndex)
{
	ucCount++;

	emscripten_atomic_store_u32(
			(void*)&updateStructure.ucIndices[updateStructure.ucWriteIndex],
			accelerationIndex);
	
	asm volatile("" : : : "memory");
	/*
	updateStructure.ucWriteIndex = (updateStructure.ucWriteIndex+1)
		& (updateStructure.ucIndicesCount);
		*/
	emscripten_atomic_store_u32(
		(void*)&updateStructure.ucWriteIndex,
		(updateStructure.ucWriteIndex+1)
			% (updateStructure.ucIndicesCount));
}

void uc_reset()
{
	ucCount = 0;

	updateStructure.ucWriteIndex = 0;
	updateStructure.ucReadIndex = 0;
}

void ua_reset()
{
	uaCount = 0;

	updateStructure.uaWriteIndex = 0;
	updateStructure.uaReadIndex = 0;
}

void update_structure_initialize()
{
	// big values just to be safe. MUST be a power of 2 with 1 subtracted!
	//updateStructure.ucIndicesCount = (1 << 24) - 1;
	updateStructure.ucIndicesCount = (1 << 24)*2; // times 2 to be safe

	// 32768 (max number of workgroups), 64 (shader workgroup size), 2 (index+data)
	// force into power of 2
	// stored as actual size -1 since all fetches subtract 1 anyway
	//updateStructure.uaIndicesCount = (1u << (32u-__builtin_clzl(32768u*64*2-1)))-1u;
	updateStructure.uaIndicesCount = 32768u*64*2*2; // times 2 to be safe

	updateStructure.ucIndices =
		malloc((updateStructure.ucIndicesCount+1)*sizeof(uint32_t));
	updateStructure.uaIndices =
		malloc((updateStructure.uaIndicesCount+1)*sizeof(uint32_t));
	// does not do anything currently
	uc_reset();
	ua_reset();
}

// from ken perlin
float perlin_fade_function(float t)
{
	return t * t * t * (t * (t * 6 - 15) + 10);
}

// from wikipedia
float lerp(float v0, float v1, float t)
{
	return v0 + t * (v1 - v0);
}

float generate_random_from_2d(uint32_t x, uint32_t y)
{
	uint32_t width = 128;
	uint32_t xPos = width*x;
	uint32_t yPos = width*y;
	uint32_t tmpSeed = ((xPos&0xFFFF) + (yPos<<16)) ^ seed;
	srand(tmpSeed);
	return (rand()%10000) / 10000.0f * 2.0f-1.0f;
}

// you should maybe not make a 2048x2048 texture when you load the site...
void generate_heightmap(uint32_t dim)
{
	heightMapDimension = dim;
	heightMap = malloc(heightMapDimension*heightMapDimension*sizeof(uint16_t));
	
	uint32_t width = 128;
	for (uint32_t y = 0, i = 0; y < heightMapDimension; y++) {
	for (uint32_t x = 0; x < heightMapDimension; x++,i++) {
		uint32_t gridX = (x+xWorld) / width;
		uint32_t gridY = (y+yWorld) / width;
		
		float cellX = (((x+xWorld) % width)+0.5) / width;
		float cellY = (((y+yWorld) % width)+0.5) / width;

		float n0 = generate_random_from_2d(gridY, gridX);
		float n1 = generate_random_from_2d(gridY, gridX+1);
		float n2 = generate_random_from_2d(gridY+1, gridX);
		float n3 = generate_random_from_2d(gridY+1, gridX+1);
		float finalNoise = lerp(
				lerp(n0, n1, perlin_fade_function(cellX)),
				lerp(n2, n3, perlin_fade_function(cellX)),
				perlin_fade_function(cellY));
		heightMap[i] = finalNoise*40+1000;
	}}
	
	// to accelerate chunk generation
	accelerationHeightMap = malloc(256*256*256*sizeof(uint16_t));
	for (uint32_t accelerationZ = 0, i = 0; accelerationZ < 256; accelerationZ++) {
	for (uint32_t accelerationX = 0; accelerationX < 256; accelerationX++,i++) {
		uint32_t minDepth = 0xFFFFFFFF;
		uint32_t maxDepth = 0;
		for (uint32_t z = 0; z < 10; z++) {
		for (uint32_t x = 0; x < 10; x++) {
			uint32_t realX = (accelerationX<<3)+x-1;
			uint32_t realZ = (accelerationZ<<3)+z-1;
			if (realX > 2047u || realZ > 2047u) {
				continue;
			}
			if (((realZ*heightMapDimension)|realX) >= 2048*2048) {
				printf("???\n");
				continue;
			}
			uint32_t depth = heightMap[(realZ*heightMapDimension)|realX];
			minDepth = MIN(minDepth, depth);
			maxDepth = MAX(maxDepth, depth);
		}}
		uint8_t minAccelerationY = minDepth>>3u;
		uint8_t maxAccelerationY = (maxDepth+7u)>>3u;
		accelerationHeightMap[i] = (maxAccelerationY<<8u) | minAccelerationY;
	}}
}

// NOTE: make sure the chunk is not a border first!!!
// tries to generate a chunk, if chunk empty-> does not generate one
// does not ua_push_index!
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

	uint16_t accelerationValue = accelerationHeightMap[(accelerationZ<<8)+accelerationX];
	uint8_t minValue = accelerationValue & 0xFF;
	uint8_t maxValue = accelerationValue >> 8;

	if (inay >= minValue && inay <= maxValue) {
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
	}
	if (voxelCountInChunk == 0) {
		accelerationBufferData[accelerationIndex] = 1 << 31;
	} else {
		chunkCount++;
		// do not push index that has not gotten df optimized yet
		//uc_push_index(accelerationIndex);
	}
}

void acceleration_area_create_from_2dnoise_version_2(uint32_t inax, uint32_t inaz)
{
	// get lowest and highest point. can be optimized by SAT yet again
	uint32_t minDepth = 0xFFFFFFFF;
	uint32_t maxDepth = 0;
	// check 1 outside to be sure
	for (uint32_t z = 0; z < 10; z++) {
	for (uint32_t x = 0; x < 10; x++) {
		// inaxyz can not be 0 or 255, no need to check for overflow or underflow
		uint32_t realX = (inax<<3)+x-1;
		uint32_t realZ = (inaz<<3)+z-1;
		uint32_t depth = heightMap[(realZ*heightMapDimension)|realX];
		minDepth = MIN(minDepth, depth);
		maxDepth = MAX(maxDepth, depth);
	}}
	maxDepth += 1;
	for (uint32_t inay = border.yNeg; inay <= border.yPos; inay++) {
		uint32_t accelerationIndex = (inaz << 16)
			| (inay << 8) | inax;
		uint32_t oldValue = accelerationBufferData[accelerationIndex];
		if (inax >= border.xPos || inay >= border.yPos
				|| inaz >= border.zPos || inax <= border.xNeg
				|| inay <= border.yNeg || inaz <= border.zNeg) {
			accelerationBufferData[accelerationIndex] = 3 << 30;
		} else if (((inay<<3)+7) >= minDepth && (inay<<3) <= maxDepth) {
			chunk_create_from_2dnoise(inax, inay, inaz);
		} else {
			accelerationBufferData[accelerationIndex] = 1 << 31;
		}
		if (accelerationBufferData[accelerationIndex] != oldValue) {
			ua_push_index(accelerationIndex);
		}
	}
}
void border_area_create_from_2dnoise(
		uint32_t inax, uint32_t inaz,
		struct Border newBorder,
		struct Border oldBorder)
{
	// get lowest and highest point. can be optimized by SAT yet again
	uint32_t minDepth = 0xFFFFFFFF;
	uint32_t maxDepth = 0;
	// NOTE: if accelerationIndex.x|y|z == 0 or 255 a crash can happen here
	for (uint32_t z = 0; z < 10; z++) {
	for (uint32_t x = 0; x < 10; x++) {
		uint32_t realX = (inax<<3)+x-1;
		uint32_t realZ = (inaz<<3)+z-1;
		uint32_t depth = heightMap[(realZ*heightMapDimension)|realX];
		minDepth = MIN(minDepth, depth);
		maxDepth = MAX(maxDepth, depth);
	}}
	maxDepth += 1;
	for (uint32_t inay = border.yNeg; inay <= border.yPos; inay++) {
		uint32_t accelerationIndex = (inaz << 16)
			| (inay << 8) | inax;
		uint32_t oldValue = accelerationBufferData[accelerationIndex];
		if (!(
				   inax >= oldBorder.xPos || inay >= oldBorder.yPos
				|| inaz >= oldBorder.zPos || inax <= oldBorder.xNeg
				|| inay <= oldBorder.yNeg || inaz <= oldBorder.zNeg)) {
			inay = oldBorder.yPos-1;
			continue;
		} else if (inax >= newBorder.xPos || inay >= newBorder.yPos
				|| inaz >= newBorder.zPos || inax <= newBorder.xNeg
				|| inay <= newBorder.yNeg || inaz <= newBorder.zNeg) {
			accelerationBufferData[accelerationIndex] = 3 << 30;
		} else if (((inay<<3)+7) >= minDepth && (inay<<3) <= maxDepth) {
			chunk_create_from_2dnoise(inax, inay, inaz);
		} else {
			accelerationBufferData[accelerationIndex] = 1 << 31;
		}
		if (accelerationBufferData[accelerationIndex] != oldValue) {
			// do not push since it has not gotten df optimized yet
			if ((accelerationBufferData[accelerationIndex] & (3u << 30)) == (3u << 30)) {
				ua_push_index(accelerationIndex);
			}
		}
	}
}

void border_area_optimize_acceleration(
		uint32_t inax, uint32_t inaz,
		struct Border newBorder,
		struct Border oldBorder)
{
	uint32_t yStart = border.yNeg & ~31;
	uint32_t yEnd = MIN((border.yPos+31) & ~31, 255 & ~31);
	for (uint32_t inay = yStart; inay <= yEnd; inay+=32) {
		struct Border currentBorder = {
			.xNeg = inax,
			.yNeg = inay,
			.zNeg = inaz,
			.xPos = inax+32,
			.yPos = inay+32,
			.zPos = inaz+32,
		};
		if (border_completely_inside_border(currentBorder, oldBorder)) {
			continue;
		}
		
		acceleration_area_calculate_df(inax, inay, inaz);
	}
}

void border_area_optimize_chunk(
		uint32_t inax, uint32_t inaz,
		struct Border newBorder,
		struct Border oldBorder)
{
	uint32_t yStart = border.yNeg & ~4;
	uint32_t yEnd = (border.yPos+4) & ~4;
	for (uint32_t inay = yStart; inay <= yEnd; inay+=4) {

		struct Border currentBorder = {
			.xNeg = inax,
			.yNeg = inay,
			.zNeg = inaz,
			.xPos = inax+4,
			.yPos = inay+4,
			.zPos = inaz+4,
		};
		if (border_completely_inside_border(currentBorder, oldBorder)) {
			continue;
		}
		
		chunk_area_calculate_df(inax, inay, inaz);
	}
}

uint32_t first = 1;
void load_new_chunks()
{
	struct Border oldBorder = border;
	struct Border newBorder;

	/*
	uint32_t borderAddCount = 4;
	newBorder.xNeg = MAX((int)oldBorder.xNeg-borderAddCount, 0);
	newBorder.yNeg = MAX((int)oldBorder.yNeg-borderAddCount, 0);
	newBorder.zNeg = MAX((int)oldBorder.zNeg-borderAddCount, 0);
	newBorder.xPos = MIN((int)oldBorder.xPos+borderAddCount, 255);
	newBorder.yPos = MIN((int)oldBorder.yPos+borderAddCount, 255);
	newBorder.zPos = MIN((int)oldBorder.zPos+borderAddCount, 255);

	// TODO: remove this check
	uint16_t currentLoadDistance = MAX(
			MAX(
				oldBorder.xPos-oldBorder.xNeg,
				oldBorder.yPos-oldBorder.yNeg
			), oldBorder.zPos-oldBorder.zNeg);
			*/

	/*
	if (currentLoadDistance >= targetLoadDistance-1) {
		return;
	}
	*/
	int centerX = (int)sharedWithJS.cameraPosX >> 3;
	int centerY = (int)sharedWithJS.cameraPosY >> 3;
	int centerZ = (int)sharedWithJS.cameraPosZ >> 3;
	
	newBorder.xNeg = MAX(centerX-targetLoadDistance, 0);
	newBorder.yNeg = MAX(centerY-targetLoadDistance, 0);
	newBorder.zNeg = MAX(centerZ-targetLoadDistance, 0);
	newBorder.xPos = MIN(centerX+targetLoadDistance, 255);
	newBorder.yPos = MIN(centerY+targetLoadDistance, 255);
	newBorder.zPos = MIN(centerZ+targetLoadDistance, 255);

	int minNeg = MIN(MIN(newBorder.xNeg, newBorder.yNeg), newBorder.zNeg);
	int maxPos = MAX(MAX(newBorder.xPos, newBorder.yPos), newBorder.zPos);
	if (minNeg < 32 || maxPos > (256-32) || chunkCount > 80000) {
		transform();
		while ((volatile uint32_t)updateStructure.transformState == 2) {
			emscripten_thread_sleep(1);
		}
		return;
	}
	
	oldBorder.xNeg = MAX(oldBorder.xNeg, newBorder.xNeg);
	oldBorder.yNeg = MAX(oldBorder.yNeg, newBorder.yNeg);
	oldBorder.zNeg = MAX(oldBorder.zNeg, newBorder.zNeg);
	oldBorder.xPos = MIN(oldBorder.xPos, newBorder.xPos);
	oldBorder.yPos = MIN(oldBorder.yPos, newBorder.yPos);
	oldBorder.zPos = MIN(oldBorder.zPos, newBorder.zPos);
	
	border = newBorder;

	for (uint32_t accelerationZ = border.zNeg; accelerationZ <= border.zPos; accelerationZ++) {
	for (uint32_t accelerationX = border.xNeg; accelerationX <= border.xPos; accelerationX++) {
		border_area_create_from_2dnoise(
				accelerationX, accelerationZ,
				newBorder, oldBorder);
	}}

	uint32_t aZStart = border.zNeg & ~31;
	uint32_t aZEnd = MIN((border.zPos+31) & ~31, 255 & ~31);
	uint32_t aXStart = border.xNeg & ~31;
	uint32_t aXEnd = MIN((border.xPos+31) & ~31, 255 & ~31);
	for (uint32_t accelerationZ = aZStart; accelerationZ <= aZEnd; accelerationZ+=32) {
	for (uint32_t accelerationX = aXStart; accelerationX <= aXEnd; accelerationX+=32) {
		border_area_optimize_acceleration(accelerationX, accelerationZ,
				newBorder, oldBorder);
	}}

	uint32_t cZStart = border.zNeg & ~3;
	uint32_t cZEnd = (border.zPos+3) & ~3;
	uint32_t cXStart = border.xNeg & ~3;
	uint32_t cXEnd = (border.xPos+3) & ~3;
	for (uint32_t accelerationZ = cZStart; accelerationZ <= cZEnd; accelerationZ+=4) {
	for (uint32_t accelerationX = cXStart; accelerationX <= cXEnd; accelerationX+=4) {
		border_area_optimize_chunk(accelerationX, accelerationZ,
				newBorder, oldBorder);
	}}
}

void job_thread()
{
	while (true) {
		load_new_chunks();
	}
}
__attribute__((used)) struct UpdateStructure *jobs_setup()
{
	pthread_t thread;
	pthread_create(&thread, NULL, (void*)job_thread, NULL);
	pthread_detach(thread);
	
	return &updateStructure;
}

__attribute__((used)) struct SharedWithJS *init()
{
	xWorld = 65536;
	yWorld = 65536;
	zWorld = 65536;
	seed = 50;
	
	generate_heightmap(2048);

	update_structure_initialize();

	gameSettings.loadDistance = 32;

	return &sharedWithJS;
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
		if (accelerationX <= (int)border.xNeg
				|| accelerationY <= (int)border.yNeg
				|| accelerationZ <= (int)border.zNeg
				|| accelerationX >= (int)border.xPos
				|| accelerationY >= (int)border.yPos
				|| accelerationZ >= (int)border.zPos) {
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
			emscripten_atomic_store_u32(&chunkBufferData[voxelIndex], voxel);
		}}}

		uc_push_index(accelerationIndex);

		// since df has been generated -> give it to the GPU
		ua_push_index(accelerationIndex);
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
		if (accelerationX <= (int)border.xNeg
				|| accelerationY <= (int)border.yNeg
				|| accelerationZ <= (int)border.zNeg
				|| accelerationX >= (int)border.xPos
				|| accelerationY >= (int)border.yPos
				|| accelerationZ >= (int)border.zPos) {
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
		uint32_t oldValue = voxel;

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

		if (voxel != oldValue) {
			ua_push_index(accelerationIndex);
		}
	}}}
	free(sat);
}

// NOT YET UPDATED. DO NOT USE
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

__attribute__((used)) void *create_chunks(
		uint32_t inAccelerationBufferSize, uint32_t inChunkBufferSize)
{
	accelerationBufferSize = inAccelerationBufferSize;
	chunkBufferSize = inChunkBufferSize;

	chunkCount = 0;
	posix_memalign((void**)&accelerationBufferData, 16, accelerationBufferSize);
	posix_memalign((void**)&chunkBufferData, 16, chunkBufferSize);

	uint32_t *returnedData = malloc(64);
	returnedData[0] = (uint32_t)accelerationBufferData;
	returnedData[1] = (uint32_t)chunkBufferData;
	
	for (uint32_t i = 0; i < accelerationBufferSize/4; i++) {
		accelerationBufferData[i] = 3u << 30;
	}
	
	maxChunkCount = chunkBufferSize / (512*sizeof(uint32_t));

	int initialLoadDistance = 5;

	int centerX = 128;
	int centerY = 128;
	int centerZ = 128;
	
	border.xNeg = MAX(centerX-initialLoadDistance, 0);
	border.yNeg = MAX(centerY-initialLoadDistance, 0);
	border.zNeg = MAX(centerZ-initialLoadDistance, 0);
	border.xPos = MIN(centerX+initialLoadDistance, 255);
	border.yPos = MIN(centerY+initialLoadDistance, 255);
	border.zPos = MIN(centerZ+initialLoadDistance, 255);

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
	for (uint32_t z = border.zNeg; z <= border.zPos; z+=4) {
	for (uint32_t y = border.yNeg; y <= border.yPos; y+=4) {
	for (uint32_t x = border.xNeg; x <= border.xPos; x+=4) {
		chunk_area_calculate_df(x, y, z);
	}}}

	printf("chunkBufferCount: %d / %d\n", chunkCount, maxChunkCount);

	// returned accelerationBufferData and chunkBufferData fills instead
	uc_reset();
	ua_reset();
	return returnedData;
}

bool border_completely_inside_border(struct Border inside, struct Border outside)
{
	return (inside.xNeg > outside.xNeg && inside.xPos < outside.xPos
			&& inside.yNeg > outside.yNeg && inside.yPos < outside.yPos
			&& inside.zNeg > outside.zNeg && inside.zPos < outside.zPos);
}

void create_octree(
		struct Octree *octree,
		uint32_t allocationCount,
		uint32_t dataAllocationCount)
{
	octree->allocationCount = allocationCount;
	octree->indices = calloc(allocationCount, sizeof(uint32_t));
	if (dataAllocationCount != 0) {
		octree->data = calloc(dataAllocationCount, 1);
	}
	
	// already initialized to 0
	octree->count = 8;
}

struct ReturnedNode get_octree_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetLevel)
{
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint16_t currentSet = 0;
	uint16_t voxelSize = 1 << 10;
	uint16_t currentNode = 0;
	uint16_t targetVoxelSize = 1 << targetLevel;
	for (;;) {
		uint16_t midX = x + voxelSize;
		uint16_t midY = y + voxelSize;
		uint16_t midZ = z + voxelSize;
		
		uint16_t cube = (targetX >= midX)
			+ ((targetY >= midY) << 1)
			+ ((targetZ >= midZ) << 2);
		currentNode = currentSet + cube;

		x += voxelSize & -(targetX >= midX);
		y += voxelSize & -(targetY >= midY);
		z += voxelSize & -(targetZ >= midZ);

		if (voxelSize == targetVoxelSize) {
			break;
		}
		
		currentSet = octree->indices[currentNode];
		if (currentSet == 0) {
			break;
		}

		voxelSize >>= 1;
	}
	return (struct ReturnedNode) {
		.index = currentNode,
		.voxelSize = voxelSize,
		.x = x,
		.y = y,
		.z = z,
	};
}
struct ReturnedNode force_octree_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetLevel)
{
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint16_t currentSet = 0;
	uint16_t voxelSize = 1 << 10;
	uint16_t currentNode = 0;
	uint16_t targetVoxelSize = 1 << targetLevel;
	for (;;) {
		uint16_t midX = x + voxelSize;
		uint16_t midY = y + voxelSize;
		uint16_t midZ = z + voxelSize;
		
		uint16_t cube = (targetX >= midX)
			+ ((targetY >= midY) << 1)
			+ ((targetZ >= midZ) << 2);
		currentNode = currentSet + cube;

		x += voxelSize & -(targetX >= midX);
		y += voxelSize & -(targetY >= midY);
		z += voxelSize & -(targetZ >= midZ);

		if (voxelSize == targetVoxelSize) {
			break;
		}
		
		currentSet = octree->indices[currentNode];
		if (currentSet == 0) {
			// allocate new nodes
			currentSet = octree->count;
			octree->indices[currentNode] = currentSet;
			// NOTE: octree.indices[octree.count] -> octree.count+7
			//       MUST be zero!
			octree->count += 8;
		}

		voxelSize >>= 1;
	}
	return (struct ReturnedNode) {
		.index = currentNode,
		.voxelSize = voxelSize,
		.x = x,
		.y = y,
		.z = z,
	};
}

void transform()
{
	//emscripten_thread_sleep(16000);

	updateStructure.transformState = 1;
	
	uint32_t lenX = border.xPos-border.xNeg;
	uint32_t lenY = border.yPos-border.yNeg;
	uint32_t lenZ = border.zPos-border.zNeg;
	uint32_t midX = border.xNeg+(lenX/2);
	uint32_t midY = border.yNeg+(lenY/2);
	uint32_t midZ = border.zNeg+(lenZ/2);
	uint32_t transformX = 128-midX;
	uint32_t transformY = 128-midY;
	uint32_t transformZ = 128-midZ;

	struct Border oldBorder = border;
	struct Border newBorder; 
	newBorder.xNeg = MAX(border.xNeg+transformX, 0);
	newBorder.yNeg = MAX(border.yNeg+transformY, 0);
	newBorder.zNeg = MAX(border.zNeg+transformZ, 0);
	newBorder.xPos = MIN(border.xPos+transformX, 255);
	newBorder.yPos = MIN(border.yPos+transformY, 255);
	newBorder.zPos = MIN(border.zPos+transformZ, 255);

	printf("oldBorder: %d, %d, %d, %d, %d, %d\n",
			border.xNeg, border.yNeg, border.zNeg,
			border.xPos, border.yPos, border.zPos);
	printf("newBorder: %d, %d, %d, %d, %d, %d\n",
			newBorder.xNeg, newBorder.yNeg, newBorder.zNeg,
			newBorder.xPos, newBorder.yPos, newBorder.zPos);
	printf("change: %d, %d, %d\n",
			transformX, transformY, transformZ);

	for (uint32_t z = newBorder.zNeg, i = 0; z < newBorder.zPos; z++) {
	for (uint32_t y = newBorder.yNeg; y < newBorder.yPos; y++) {
	for (uint32_t x = newBorder.xNeg; x < newBorder.xPos; x++,i++) {
		uint32_t oldX = x-transformX;
		uint32_t oldY = y-transformY;
		uint32_t oldZ = z-transformZ;
		uint32_t oldIndex = (oldZ<<16) | (oldY<<8) | oldX;
		if (x == newBorder.xNeg || x == newBorder.xPos
				|| y == newBorder.yNeg || y == newBorder.yPos
				|| z == newBorder.zNeg || z == newBorder.zPos) {
			accelerationBufferData[(z<<16)|(y<<8)|x] = 3u << 30;
		} else {
			accelerationBufferData[(z<<16)|(y<<8)|x] = accelerationBufferData[oldIndex];
		}
	}}}

	border = newBorder;
	
	printf("old: %d\n", oldBorder.xNeg*8+xWorld);

	// WHY DOES THIS NOT WORK????
	xWorld -= transformX*8;
	yWorld -= transformY*8;
	zWorld -= transformZ*8;
	
	printf("new: %d\n", newBorder.xNeg*8+xWorld);

	free(heightMap);
	free(accelerationHeightMap);
	generate_heightmap(heightMapDimension);
	
	
	//chunkCount = 0;

	asm volatile("" : : : "memory");
	updateStructure.transformState = 2;
}
