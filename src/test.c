#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <pthread.h>

#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/stack.h>

#include <wasm_simd128.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define SWAP(x, y) do { typeof(x) SWAP = x; x = y; y = SWAP; } while (0)
#define IGNORE_UNUSED_WARNING(VARIABLE) (void)VARIABLE;

v128_t *cachedSIMDSAT;
uint16_t *cachedSAT;

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

// modified chunks are stored here
struct GameSettings {
	volatile uint16_t loadDistance;
} gameSettings;

uint16_t *heightMap;
uint32_t heightMapDimension;

// byte 0: acceleration start
// byte 1: acceleration end
uint16_t *accelerationHeightMap;

uint32_t *accelerationBufferData;
volatile uint32_t *chunkBufferData;
uint32_t maxChunkCount;

// locks to make sure only one thread writes to a chunk at a time
/*
pthread_mutex_t *chunkLocks;
void chunk_lock(uint32_t accelerationIndex);
void chunk_unlock(uint32_t accelerationIndex);
*/

uint32_t targetLoadDistance = 30;

struct ReturnedChunkCreationBitfield {
	uint8_t generated:1;
	// 1 if that direction is not blocked by voxels
	// (approximately, only sides of chunk is checked)
	uint8_t xNeg: 1;
	uint8_t yNeg: 1;
	uint8_t zNeg: 1;
	uint8_t xPos: 1;
	uint8_t yPos: 1;
	uint8_t zPos: 1;
};

struct SharedWithJS {
	volatile float cameraPosX;
	volatile float cameraPosY;
	volatile float cameraPosZ;

	volatile float cameraAxisX;
	volatile float cameraAxisY;
	volatile float cameraAxisZ;

	uint32_t *lightsToShareLocation;
	uint32_t *lightsToShareCountLocation;
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
	
	// since writeIndex must be added to after setting ucIndices data,
	// this variable is used as the value ucWriteIndex is before
	volatile uint32_t ucPreWriteIndex;
} updateStructure;

struct Octree {
	uint32_t allocationCount;
	uint32_t count;
	// index to next node or 0
	uint32_t *indices;
	void *data;
	enum OctreeType {
		OCTREE_TYPE_DEFAULT,
		OCTREE_TYPE_LIGHTMAP
	} type;
};

// TODO: remove all GIL stuff, it would take too much time to implement correctly
/*
// thread creates the texture atlas for lightmaps
struct GILStructure {
#define GIL_OPERATION_CHUNK_MODIFIED (1u << 31)
#define GIL_OPERATION_VOXEL_DELETED (1u << 30)
	// operation = accelerationIndex | GIL_OPERATION_CHUNK_MODIFIED
	// or operation = address | GIL_OPERATION_VOXEL_DELETED
	volatile uint32_t * operations;
	volatile uint32_t writeIndex; // used by job_thread
	volatile uint32_t doneWriteIndex; // used by job_thread upon generation completion
	volatile uint32_t readIndex; // used by gil_thread
	uint32_t maxOperationCount;

	float budget;
	// if texture atlas budget is 2048x2048
	// 4096 16x16 lightmaps
	// 16384 8x8 lightmaps
	// 65536 4x4 lightmaps
	// 131072 2x2 lightmaps
	// 524288 1x1 lightmaps
#define LM_NONE 0
#define LM_16x16 1
#define LM_8x8 2
#define LM_4x4 3
#define LM_2x2 4
#define LM_1x1 5
	uint32_t count[5];
	uint32_t maxCount[5];
	uint32_t *freeIndices[5];
	uint32_t freeCount[5];
#define GILINFO_ADDRESS_MASK 0b00111111111111111111111111100000
	// 
	// array of 8 uint32_t
	// 0: gridX | (gridY << 16)
	// 1: gridZ
	// 2: xNeg lightmap info
	// 3: yNeg lightmap info
	// 4: zNeg lightmap info
	// 5: xPos lightmap info
	// 6: yPos lightmap info
	// 7: zPos lightmap info
	// later sent to the GPU
	// lightmap info:
	//     3 bits: lightmap type
	//     11 bits: texture atlas x
	//     11 bits: texture atlas y
	// 
	volatile uint32_t *gilInfoBuffer;
	// indices of empty elements in gilInfoBuffer
	uint32_t *gilInfoBufferFreeIndices;
	// TODO: rename to gilInfoBufferUsedCount or something better
	uint32_t gilInfoBufferFreeCount;
	uint32_t gilInfoBufferMaxCount;

	//
	// used to construct lightmapList
	// keeps the position of all lightmaps
	// octree->data:
	// uint32_t nodeStack[octree->allocationCount];
	// uint32_t nodeStackCount;
	// uint32_t lightmapCount[octree->allocationCount];
	// lightmapCount also counts LM_NONE
	// if (voxelSize == 8) -> octree->indices = gilInfoBuffer index
	//
	struct Octree lightmapOctree;

	// shared with JS
	struct GILAndJSPushData {
		volatile uint32_t *gilInfoBuffer;
		volatile uint32_t *indices;
		volatile uint32_t writeIndex;
		volatile uint32_t readIndex;
		uint32_t maxCount;

		// passed on to gil.wgsl
		// 1 entry = 2 uints
		// uint 0 = (gridX) | (gridY << 16)
		// uint 1 = (gridZ) | (offset << 16) | (normal << 24)
		// lightmapListCount number of entries
		uint32_t *lightmapList;
		// side of list is maxCount*2!!
		uint32_t lightmapListCount;
		uint32_t lightmapListMaxCount;
#define LIGHTMAP_STATE_WAITING_ON_C 0
#define LIGHTMAP_STATE_WAITING_ON_JS 1
		volatile uint32_t lightmapState;
	} gpuPush;
} gil;
*/

/*
void gil_push(uint32_t accelerationIndex);
uint32_t gil_pop();
void gil_setup();
uint32_t gil_info_buffer_allocate();
void gil_info_buffer_free(uint32_t index);
uint32_t gil_allocate_lightmap(uint32_t target );
void gil_free_lightmap(uint32_t lightmap);
void gil_push_to_gpu(uint32_t index);
*/

/*
void lightmapoctree_add_to_count(
		struct Octree *octree,
		uint16_t targetX,
		uint16_t targetY,
		uint16_t targetZ,
		uint32_t change);
		*/

// generationOctree.data:
// uint8_t progress2DGeneration[octree.allocationCount];
// uint32_t nodeGeneratedCount[octree.allocationCount];
// uint32_t nodeStack[octree.allocationCount]
// uint8_t shouldUpdate[octree.allocationCount];
// uint32_t nodeStackCount
// uin32_t floodFillEntryCount[octree.allocationCount];

// index goes from 0->maxChunkCount
struct Octree generationOctree;

struct RaycastResult {
	uint16_t x;
	uint16_t y;
	uint16_t z;
	uint8_t success;
	uint8_t normal;
};
struct RaycastResult raycast(
		float originX, float originY, float originZ,
		float dirX, float dirY, float dirZ);

struct ChunkStack {
	uint32_t *indices;
	uint32_t *voxels; // if CHUNKSTACK_FLAG_ALLOCATE_VOXELS
	uint32_t count;
	uint32_t allocationCount;
#define CHUNKSTACK_FLAG_RESIZABLE 1
#define CHUNKSTACK_FLAG_ALLOCATE_VOXELS 2
	uint32_t flags;
} chunkStack;

// keeps modified chunks, uses robin hood hashing
// ChunkCache is probably a bad name. should probably be called ChunkStorage
struct ChunkCache {
	uint64_t *bitfields; // accelerationIndex | flags | chunkStackIndex<<32
#define CHUNKCACHE_FLAG_OCCUPIED (1u<<24)
#define CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT (1u<<25)
// TODO: #define CHUNKCACHE_FLAG_CHUNK_EMPTY (1u<<26)
	// if (bitfield & CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT) == 0
	// then chunk is stored in chunkBufferData, else chunkCache.chunkData
	uint16_t *PSL; // probe sequence length
	uint32_t count;
	uint32_t currentPrime;

	struct ChunkStack stack;
} chunkCache;

// primes from https://planetmath.org/goodhashtableprimes
uint32_t primes[] = {
	53, 97, 193, 389, 769, 1543, 3079, 6151,
	12289, 24593, 49157, 98317, 196613, 393241, 786433,
	1572869, 3145739, 6291469, // thats enough of primes
};

void octree_create_lightmap_list_recursive(
		struct Octree *octree,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize);

void chunkcache_create();
uint32_t chunkcache_insert(uint8_t inax, uint8_t inay, uint8_t inaz);
uint32_t chunkcache_get(uint8_t inax, uint8_t inay, uint8_t inaz);

void chunkstack_create(
		struct ChunkStack *stack, uint32_t allocationCount,
		bool resizable, bool allocateVoxels);
uint32_t chunkstack_allocate(struct ChunkStack *chunkStack);
void chunkstack_free(struct ChunkStack *chunkStack, uint32_t index);

void nodestack_create(struct Octree *octree);
uint32_t nodestack_allocate(struct Octree *octree);
void nodestack_free(struct Octree *octree, uint32_t index);

float distance2_point_to_cube(
		uint16_t cubeX,
		uint16_t cubeY,
		uint16_t cubeZ,
		uint16_t voxelSize,
		float pointX, float pointY, float pointZ);
float furthest_distance2_point_to_cube(
		uint16_t cubeX,
		uint16_t cubeY,
		uint16_t cubeZ,
		uint16_t voxelSize,
		float pointX, float pointY, float pointZ);
uint32_t octree_generate_chunks_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize);
uint32_t octree_generate_chunks_flood_fill(
		struct Octree *octree,
		float targetDistance2,
		uint8_t inax, uint8_t inay, uint8_t inaz);
uint32_t octree_free_chunks_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize);
void octree_update_cache_recursive(
		struct Octree *octree,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize);

float octreeGenerateChunksCurrentDistance;
void octree_generate_chunks();
uint32_t octree_optimize_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize);
struct OctreeMoveStack {
	uint32_t count;
	uint32_t allocatedCount;
	uint32_t *indices;
	uint32_t *voxels;
};
struct ReturnedNode {
	uint32_t index;
	uint32_t voxelSize;
	uint16_t x;
	uint16_t y;
	uint16_t z;
};
struct ReturnedNode octree_get_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize);
struct ReturnedNode octree_force_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize);

// sets progress2DGeneration[node.index] to 8 and backpropagates to upper nodes
// returns false if node not found
bool generationoctree_set_node_as_generated(
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize);
bool generationoctree_add_to_floodfillentrycount(
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize, uint32_t numberToAdd,
		bool onlyAddIfZero, bool force);

bool is_chunk_optimized(uint8_t inax, uint8_t inay, uint8_t inaz);

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
void free_heightmap();
// TODO: 8 bit inax, inay, inaz!!!
struct ReturnedChunkCreationBitfield chunk_create_from_2dnoise(
		uint32_t inax, uint32_t inay, uint32_t inaz);
// do the necessary stuff after updating a chunk
void chunk_after_update(uint8_t inax, uint8_t inay, uint8_t inaz);
void chunk_destroy(uint8_t inax, uint8_t inay, uint8_t inaz);
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
void chunk_area_create(uint8_t inax, uint8_t inay, uint8_t inaz);
//bool quickHack = false;
void chunk_area_optimize_and_push(uint8_t inax, uint8_t inay, uint8_t inaz);
void chunk_area_destroy(uint8_t inax, uint8_t inay, uint8_t inaz);
void chunk_area_update_cache(uint8_t inax, uint8_t inay, uint8_t inaz);
void chunk_area_push_indices(uint8_t inax, uint8_t inay, uint8_t inaz);
bool shouldLockChunks = false;
void chunk_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz);
void acceleration_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz);
void chunk_create_empty(uint32_t inax, uint32_t inay, uint32_t inaz);

void octree_create(
		struct Octree *octree,
		uint32_t allocationCount,
		uint32_t dataAllocationCount,
		enum OctreeType type);

uint32_t get_voxel(uint32_t x, uint32_t y, uint32_t z);

struct RaycastResult raycast(
		float originX, float originY, float originZ,
		float dirX, float dirY, float dirZ)
{
	float invDirX = 1.0f / dirX;
	float invDirY = 1.0f / dirY;
	float invDirZ = 1.0f / dirZ;

	uint8_t sign1or0X = dirX > 0 ? 1 : 0;
	uint8_t sign1or0Y = dirY > 0 ? 1 : 0;
	uint8_t sign1or0Z = dirZ > 0 ? 1 : 0;

	uint32_t gridX = originX;
	uint32_t gridY = originY;
	uint32_t gridZ = originZ;

	float tx, ty, tz, tmin;

	uint32_t voxel;
	for (uint32_t i = 0; i < 1024; i++) {
		uint32_t ax = gridX>>3;
		uint32_t ay = gridY>>3;
		uint32_t az = gridZ>>3;
		uint32_t accelerationIndex = ((az) << 16) | ((ay) << 8) | (ax);
		voxel = accelerationBufferData[accelerationIndex];
		if ((voxel & (3u << 30)) == 0u) {
			uint32_t cx = gridX & 7;
			uint32_t cy = gridY & 7;
			uint32_t cz = gridZ & 7;
			uint32_t voxelIndex = voxel | (cz<<6)|(cy<<3)|cx;
			voxel = chunkBufferData[voxelIndex];
		}
		if ((voxel & (3u << 30)) != (2u << 30)) {
			break;
		}
		uint32_t boxX = gridX+sign1or0X;
		uint32_t boxY = gridY+sign1or0Y;
		uint32_t boxZ = gridZ+sign1or0Z;
		tx = (boxX-originX) * invDirX;
		ty = (boxY-originY) * invDirY;
		tz = (boxZ-originZ) * invDirZ;

		tmin = MIN(MIN(tx, ty), tz);
		if (tx == tmin) {
			gridX += sign1or0X*2-1;
		} else if (ty == tmin) {
			gridY += sign1or0Y*2-1;
		} else /*if (tz == tmin)*/ {
			gridZ += sign1or0Z*2-1;
		}
	}
	uint8_t normal;
	if (tx == tmin) {
		normal = sign1or0X;
	} else if (ty == tmin) {
		normal = 2+sign1or0Y;
	} else /*if (tz == tmin)*/ {
		normal = 4+sign1or0Z;
	}
	return (struct RaycastResult) {
		.x = gridX,
		.y = gridY,
		.z = gridZ,
		.success = (voxel & (3u << 30)) == 0u,
		.normal = normal,
	};
}

uint32_t hash3d(uint32_t x, uint32_t y, uint32_t z, uint32_t n)
{
	return ((x*73856093) ^ (y*19349663) ^ (z*83492791)) % n;
}

void chunkcache_create()
{
	chunkCache.currentPrime = 0;
	chunkCache.count = 0;
	uint32_t allocatedCount = primes[chunkCache.currentPrime]+16;
	chunkCache.bitfields = malloc(sizeof(uint64_t)*allocatedCount);
	chunkCache.PSL = malloc(sizeof(uint16_t)*allocatedCount);
	for (uint32_t i = 0; i < allocatedCount; i++) {
		chunkCache.bitfields[i] = 0;
	}
	chunkstack_create(&chunkCache.stack, 32, true, true);
}

void chunkcache_resize(bool onlyUpdatePSL)
{
	uint64_t *oldBitfields = chunkCache.bitfields;
	uint16_t *oldPSL = chunkCache.PSL;
	uint32_t oldAllocatedCount = primes[chunkCache.currentPrime]+16;

	chunkCache.count = 0;
	if (onlyUpdatePSL == false) {
		chunkCache.currentPrime += 1;
	}
	uint32_t allocatedCount = primes[chunkCache.currentPrime]+16;
	chunkCache.bitfields = malloc(sizeof(uint64_t)*allocatedCount);
	chunkCache.PSL = malloc(sizeof(uint16_t)*allocatedCount);

	for (uint32_t i = 0; i < allocatedCount; i++) {
		chunkCache.bitfields[i] = 0;
	}
	for (uint32_t i = 0; i < oldAllocatedCount; i++) {
		uint32_t bitfield = oldBitfields[i];
		if (bitfield & CHUNKCACHE_FLAG_OCCUPIED) {
			uint8_t x = bitfield&0xFF;
			uint8_t y = (bitfield>>8)&0xFF;
			uint8_t z = (bitfield>>16)&0xFF;
			uint32_t index = chunkcache_insert(x, y, z);
			chunkCache.bitfields[index] = oldBitfields[i];
		}
	}
	free(oldBitfields);
	free(oldPSL);
}

// will exit if chunk already inserted
// returns index
uint32_t chunkcache_insert(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	uint32_t accelerationIndex = ((inaz) << 16) | ((inay) << 8) | (inax);
	uint32_t allocatedCount = primes[chunkCache.currentPrime]+16;
	uint32_t hash = hash3d(inax, inay, inaz, primes[chunkCache.currentPrime]);
	chunkCache.count += 1;

	uint32_t PSL = 0;
	uint32_t bitfield = accelerationIndex | CHUNKCACHE_FLAG_OCCUPIED;
	
	if (chunkCache.count*1.5f > (float)allocatedCount) {
		goto resize;
	}

	uint32_t index = 0xFFFFFFFF;
	for (uint32_t i = hash; i < allocatedCount; i++, PSL++) {
		if (chunkCache.bitfields[i] & CHUNKCACHE_FLAG_OCCUPIED) {
			if ((chunkCache.bitfields[i] & 0xFFFFFF) == accelerationIndex) {
				index = i;
				return index; // already set
			}
			uint32_t readPSL = chunkCache.PSL[i];
			if (PSL > readPSL) {
				if (index == 0xFFFFFFFF) {
					index = i;
				}
				SWAP(chunkCache.PSL[i], PSL);
				SWAP(chunkCache.bitfields[i], bitfield);
			}
		} else {
			if (index == 0xFFFFFFFF) {
				index = i;
			}
			SWAP(chunkCache.PSL[i], PSL);
			SWAP(chunkCache.bitfields[i], bitfield);
			return index;
		}
	}
resize:

	chunkcache_resize(false);
	return chunkcache_insert(
			bitfield&0xFF,
			(bitfield>>8)&0xFF,
			(bitfield>>16)&0xFF);
}

uint32_t chunkcache_get(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	uint32_t accelerationIndex = ((inaz) << 16) | ((inay) << 8) | (inax);
	uint32_t allocatedCount = primes[chunkCache.currentPrime]+16;
	uint32_t hash = hash3d(inax, inay, inaz, primes[chunkCache.currentPrime]);

	uint32_t PSL = 0;
	for (uint32_t i = hash; i < allocatedCount; i++) {
		if (chunkCache.bitfields[i] & CHUNKCACHE_FLAG_OCCUPIED) {
			if ((chunkCache.bitfields[i] & 0xFFFFFF) == accelerationIndex) {
				return i;
			}
			uint32_t readPSL = chunkCache.PSL[i];
			if (PSL > readPSL) {
				break;
			}
		} else {
			break;
		}
		PSL++;
	}
	return 0xFFFFFFFF;
}

// if fixedSize == 0 -> stack becomes resized
void chunkstack_create(
		struct ChunkStack *stack, uint32_t allocationCount,
		bool resizable, bool allocateVoxels)
{
	stack->flags = 0;
	if (resizable) {
		stack->flags |= CHUNKSTACK_FLAG_RESIZABLE;
	}
	if (allocateVoxels) {
		stack->flags |= CHUNKSTACK_FLAG_ALLOCATE_VOXELS;
	}
	stack->allocationCount = allocationCount;
	stack->count = 0;
	stack->indices = malloc(stack->allocationCount*sizeof(uint32_t));
	if (allocateVoxels) {
		stack->voxels = malloc(8*8*8*stack->allocationCount*sizeof(uint32_t));
	}
	for (uint32_t i = 0; i < stack->allocationCount; i++) {
		stack->indices[i] = i;
	}
}

uint32_t chunkstack_allocate(struct ChunkStack *stack)
{
	if (stack->count >= stack->allocationCount) {
		if (stack->flags & CHUNKSTACK_FLAG_RESIZABLE) {
			stack->indices = realloc(
					stack->indices, stack->allocationCount*sizeof(uint32_t));
			if (stack->flags & CHUNKSTACK_FLAG_ALLOCATE_VOXELS) {
				stack->voxels = realloc(
						stack->voxels,
						8*8*8*stack->allocationCount*sizeof(uint32_t));
			}
			for (uint32_t i = stack->count; i < stack->allocationCount; i++) {
				stack->indices[i] = i;
			}
		} else {
			printf("HOLY JESUS. OUT OF CHUNK MEMORY\n");
		}
	}
	uint32_t index = stack->indices[stack->count++];
	return index;
}

void chunkstack_free(struct ChunkStack *stack, uint32_t chunkBufferIndex)
{
	uint32_t chunkStackIndex = --stack->count;
	stack->indices[chunkStackIndex] = chunkBufferIndex;
}

void nodestack_create(struct Octree *octree)
{
	uint32_t *nodeStackCount, *nodeStack;
	if (octree->type == OCTREE_TYPE_DEFAULT) {
		uint8_t *progress2DGeneration = octree->data;
		uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
		nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
		uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
		nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	} else { // lightmap
		nodeStack = octree->data;
		nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
	}

	*nodeStackCount = 0;
	for (uint32_t i = 0; i < octree->allocationCount; i++) {
		nodeStack[i] = i;
	}
}

uint32_t nodestack_allocate(struct Octree *octree)
{
	uint32_t *nodeStackCount, *nodeStack;
	if (octree->type == OCTREE_TYPE_DEFAULT) {
		uint8_t *progress2DGeneration = octree->data;
		uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
		nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
		uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
		nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	} else { // lightmap
		nodeStack = octree->data;
		nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
	}
	
	if (*nodeStackCount >= octree->allocationCount) {
		printf("HOLY JESUS. OUT OF NODE MEMORY\n");
	}
	uint32_t index = nodeStack[*nodeStackCount];
	for (uint32_t i = 0; i < 8; i++) {
		octree->indices[index+i] = 0;

		// this is it, code does not get more beautiful than this
		if (octree->type == OCTREE_TYPE_DEFAULT) {
			uint8_t *progress2DGeneration = octree->data;
			uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
			nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
			uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
			nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
			uint32_t *floodFillEntryCount = (uint32_t*)(&nodeStackCount[1]);

			nodeGeneratedCount[index+i] = 0;
			progress2DGeneration[index+i] = 0;
			shouldUpdateCount[index+i] = 0;
			floodFillEntryCount[index+i] = 0;
		} else { // lightmap
			nodeStack = octree->data;
			nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
			uint32_t *lightmapCount = (uint32_t*)(&nodeStackCount[1]);
			lightmapCount[index+i] = 0;
		}
	}
	*nodeStackCount += 8;
	return index;
}

void nodestack_free(struct Octree *octree, uint32_t set)
{
	uint32_t *nodeStackCount, *nodeStack;
	if (octree->type == OCTREE_TYPE_DEFAULT) {
		uint8_t *progress2DGeneration = octree->data;
		uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
		nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
		uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
		nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	} else { // lightmap
		nodeStack = octree->data;
		nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
	}

	*nodeStackCount -= 8;
	nodeStack[*nodeStackCount] = set;
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

// compute the furthest distance from a point and a cube
float furthest_distance2_point_to_cube(
		uint16_t cubeX,
		uint16_t cubeY,
		uint16_t cubeZ,
		uint16_t voxelSize,
		float pointX, float pointY, float pointZ)
{
	// one of the 8 corners will always be the point with the furthest distance
	float corners[8][3] = {
		{cubeX, cubeY, cubeZ},
		{cubeX, cubeY, cubeZ+voxelSize},
		{cubeX, cubeY+voxelSize, cubeZ},
		{cubeX, cubeY+voxelSize, cubeZ+voxelSize},
		{cubeX+voxelSize, cubeY, cubeZ},
		{cubeX+voxelSize, cubeY, cubeZ+voxelSize},
		{cubeX+voxelSize, cubeY+voxelSize, cubeZ},
		{cubeX+voxelSize, cubeY+voxelSize, cubeZ+voxelSize},
	};

	float maxDistance = 0.0f;
	for (uint8_t i = 0; i < 8; i++) {
		float pre[3] = {
			fabsf(corners[i][0]-pointX),
			fabsf(corners[i][1]-pointY),
			fabsf(corners[i][2]-pointZ)
		};
		float distance = pre[0]*pre[0]+pre[1]*pre[1]+pre[2]*pre[2];
		maxDistance = MAX(maxDistance, distance);
	}

	return maxDistance;
	//return dx*dx+dy*dy+dz*dz;
}
float octreeGenerateChunksPos[3];
// 1 bool for every 32x32x32 acceleration area
bool octreeGenerateAccelerationDf[8][8][8];
void octree_generate_chunks()
{
	float targetDistance2 = octreeGenerateChunksCurrentDistance
		*octreeGenerateChunksCurrentDistance;
	octreeGenerateChunksCurrentDistance = MIN(
		octreeGenerateChunksCurrentDistance+8,
		gameSettings.loadDistance
	);

	octreeGenerateChunksPos[0] = sharedWithJS.cameraPosX;
	octreeGenerateChunksPos[1] = sharedWithJS.cameraPosY;
	octreeGenerateChunksPos[2] = sharedWithJS.cameraPosZ;
	// first 512 bits used as lookup used to know
	// if acceleration_area_calculate_df is necessary
	// 1 bit = 1 bool in 8x8x8 grid
	for (uint32_t z = 0; z < 8; z++) {
	for (uint32_t y = 0; y < 8; y++) {
	for (uint32_t x = 0; x < 8; x++) {
		octreeGenerateAccelerationDf[z][y][x] = 0;
	}}}

	uint8_t xMid = (uint32_t)octreeGenerateChunksPos[0] >> 3;
	uint8_t yMid = (uint32_t)octreeGenerateChunksPos[1] >> 3;
	uint8_t zMid = (uint32_t)octreeGenerateChunksPos[2] >> 3;
	octree_generate_chunks_flood_fill(
			&generationOctree, targetDistance2,
			xMid, yMid, zMid);
	octree_free_chunks_recursive(
			&generationOctree, targetDistance2, 0, 0, 0, 0, 1 << 10);
	octree_optimize_recursive(
			&generationOctree, targetDistance2, 0, 0, 0, 0, 1 << 10);

	/*
	asm volatile("" : : : "memory");
	//gil.doneWriteIndex = gil.writeIndex;
	*/
}

struct FloodFillStack {
	uint8_t *array;
	uint32_t count;
	uint32_t maxCount;
} floodFillStack;

uint32_t octree_update_floodfillstack_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize)
{
	// 0 = nothing has been generated
	// 8 = all 8 leaves have been generated
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	uint32_t *floodFillEntryCount = (uint32_t*)(&nodeStackCount[1]);

	uint32_t countChange = 0;
	for (uint8_t cube = 0; cube < 8; cube++) {
		uint32_t currentNode = currentSet+cube;
		uint16_t newX = x+(-(cube&1) & voxelSize);
		uint16_t newY = y+((-(cube&2)>>1) & voxelSize);
		uint16_t newZ = z+((-(cube&4)>>2) & voxelSize);

		if (newX > 2048 || newY > 2048 || newZ > 2048) {
			printf("hopefully this is unreachable\n");
		}
		
		float distance2 = distance2_point_to_cube(
				newX, newY, newZ, voxelSize,
				octreeGenerateChunksPos[0],
				octreeGenerateChunksPos[1],
				octreeGenerateChunksPos[2]);
		if (distance2 > targetDistance2) {
			continue;
		}
		if (floodFillEntryCount[currentNode] == 0) {
			continue;
		}

		uint32_t newCountChange;
		if (voxelSize == 8) {
			floodFillStack.array[floodFillStack.count++] = newX>>3;
			floodFillStack.array[floodFillStack.count++] = newY>>3;
			floodFillStack.array[floodFillStack.count++] = newZ>>3;
			newCountChange = 1;
		} else {
			uint32_t newSet = octree->indices[currentNode];
			if (newSet == 0) {
				newCountChange = 0;
			} else {
				newCountChange = octree_update_floodfillstack_recursive(
					octree, targetDistance2,
					newSet,
					newX, newY, newZ,
					voxelSize>>1
				);
			}
		}
		floodFillEntryCount[currentNode] -= newCountChange;
		countChange += newCountChange;
	}
	return countChange;
}

// generates chunks bottom up since we then can use
// flood fill to only generate visible chunks
uint32_t octree_generate_chunks_flood_fill(
		struct Octree *octree,
		float targetDistance2,
		uint8_t initialX, uint8_t initialY, uint8_t initialZ)
{
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	// probably a bad name, count of how many chunks failed the distance check
	// for every node during the flood fill. This makes it possible to then later
	// continue from where the last flood fill exited.
	uint32_t *floodFillEntryCount = (uint32_t*)(&nodeStackCount[1]);

	floodFillStack.count = 0;
	floodFillStack.maxCount = 2024000;
	floodFillStack.array = malloc(floodFillStack.maxCount*sizeof(uint8_t)*3);

	floodFillStack.array[floodFillStack.count++] = initialX;
	floodFillStack.array[floodFillStack.count++] = initialY;
	floodFillStack.array[floodFillStack.count++] = initialZ;
	
	octree_update_floodfillstack_recursive(
			&generationOctree, targetDistance2, 0, 0, 0, 0, 1 << 10);
	// flood fill
	while (floodFillStack.count > 0) {
		if (floodFillStack.count > floodFillStack.maxCount) {
			printf("memory error: %d\n", floodFillStack.count);
		}
		uint8_t inaz = floodFillStack.array[--floodFillStack.count];
		uint8_t inay = floodFillStack.array[--floodFillStack.count];
		uint8_t inax = floodFillStack.array[--floodFillStack.count];
		float distance2 = distance2_point_to_cube(
				inax<<3, inay<<3, inaz<<3, 8,
				octreeGenerateChunksPos[0],
				octreeGenerateChunksPos[1],
				octreeGenerateChunksPos[2]);
		if (inax == 0 || inay == 0 || inaz == 0
				|| inax == 255 || inay == 255 || inaz == 255) {
			continue;
		}
		
		struct ReturnedNode node = octree_force_node(octree, inax<<3, inay<<3, inaz<<3, 8);
		if (distance2 > targetDistance2) {
			// save for a later floodfill
			// TODO: should octree_free_chunks_recursive also do this?
			if (floodFillEntryCount[node.index] == 0) {
				generationoctree_add_to_floodfillentrycount(
						inax<<3, inay<<3, inaz<<3, 8, 1, false, false);
			}
			continue;
		}
		if (progress2DGeneration[node.index] == 8) {
			continue;
		} else {
			struct ReturnedChunkCreationBitfield bitfield =
				chunk_create_from_2dnoise(inax, inay, inaz);
			generationoctree_set_node_as_generated(inax<<3, inay<<3, inaz<<3, 8);
		
			if (bitfield.zPos) {
				floodFillStack.array[floodFillStack.count++] = inax;
				floodFillStack.array[floodFillStack.count++] = inay;
				floodFillStack.array[floodFillStack.count++] = inaz+1;
			}
			if (bitfield.zNeg) {
				floodFillStack.array[floodFillStack.count++] = inax;
				floodFillStack.array[floodFillStack.count++] = inay;
				floodFillStack.array[floodFillStack.count++] = inaz-1;
			}
			if (bitfield.yPos)  {
				floodFillStack.array[floodFillStack.count++] = inax;
				floodFillStack.array[floodFillStack.count++] = inay+1;
				floodFillStack.array[floodFillStack.count++] = inaz;
			}
			if (bitfield.yNeg) {
				floodFillStack.array[floodFillStack.count++] = inax;
				floodFillStack.array[floodFillStack.count++] = inay-1;
				floodFillStack.array[floodFillStack.count++] = inaz;
			}
			if (bitfield.xPos) {
				floodFillStack.array[floodFillStack.count++] = inax+1;
				floodFillStack.array[floodFillStack.count++] = inay;
				floodFillStack.array[floodFillStack.count++] = inaz;
			}
			if (bitfield.xNeg) {
				floodFillStack.array[floodFillStack.count++] = inax-1;
				floodFillStack.array[floodFillStack.count++] = inay;
				floodFillStack.array[floodFillStack.count++] = inaz;
			}
		}
	}

	free(floodFillStack.array);

	return 0;
}

// NOT USED ANYMROE!!! TODO: remove
// returned value:
// lowest 4 bits: change in progress2DGeneration
// higher 28 bits: change in nodeGeneratedCount
uint32_t octree_generate_chunks_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize)
{
	// 0 = nothing has been generated
	// 8 = all 8 leaves have been generated
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	IGNORE_UNUSED_WARNING(nodeStackCount);

	uint8_t progressChange = 0;
	uint32_t nodeGeneratedCountChange = 0;
	for (uint8_t cube = 0; cube < 8; cube++) {
		uint32_t currentNode = currentSet+cube;
		uint16_t newX = x+(-(cube&1) & voxelSize);
		uint16_t newY = y+((-(cube&2)>>1) & voxelSize);
		uint16_t newZ = z+((-(cube&4)>>2) & voxelSize);

		if (newX > 2048 || newY > 2048 || newZ > 2048) {
			printf("why is this not unreachable?? This has never happened before\n");
		}
		
		float distance2 = distance2_point_to_cube(
				newX, newY, newZ, voxelSize,
				octreeGenerateChunksPos[0],
				octreeGenerateChunksPos[1],
				octreeGenerateChunksPos[2]);
		if (distance2 > targetDistance2) {
			continue;
		}
		if (progress2DGeneration[currentNode] > 7) {
			continue;
		}

		uint32_t newProgressChange;
		uint32_t newNodeGeneratedCountChange;
		if (voxelSize == 32) {
			chunk_area_create(newX>>3, newY>>3, newZ>>3);
			newProgressChange = 8;
			newNodeGeneratedCountChange = 1;
		} else {
			uint32_t newSet = octree->indices[currentNode];
			if (newSet == 0) {
				newSet = nodestack_allocate(octree);
				octree->indices[currentNode] = newSet;
			}

			uint32_t returnedBitfield = octree_generate_chunks_recursive(
				octree, targetDistance2,
				newSet,
				newX, newY, newZ,
				voxelSize>>1
			);
			newProgressChange = returnedBitfield & 0b1111;
			newNodeGeneratedCountChange = returnedBitfield>>4;
		}
		progress2DGeneration[currentNode] += newProgressChange;
		nodeGeneratedCount[currentNode] += newNodeGeneratedCountChange;
		if (newProgressChange > 0u && progress2DGeneration[currentNode] == 8u) {
			progressChange += 1;
		}
		nodeGeneratedCountChange += newNodeGeneratedCountChange;
	}
	return progressChange + (nodeGeneratedCountChange<<4);
}

uint32_t octree_optimize_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize)
{
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	uint32_t *floodFillEntryCount = (uint32_t*)(&nodeStackCount[1]);
	IGNORE_UNUSED_WARNING(floodFillEntryCount);

	for (uint8_t cube = 0; cube < 8; cube++) {
		uint32_t currentNode = currentSet+cube;
		uint16_t newX = x+(-(cube&1) & voxelSize);
		uint16_t newY = y+((-(cube&2)>>1) & voxelSize);
		uint16_t newZ = z+((-(cube&4)>>2) & voxelSize);

		if (newX > 2048 || newY > 2048 || newZ > 2048) {
			printf("why is this not unreachable??\n");
		}
		
		float distance2 = distance2_point_to_cube(
				newX, newY, newZ, voxelSize,
				octreeGenerateChunksPos[0],
				octreeGenerateChunksPos[1],
				octreeGenerateChunksPos[2]);
		if (distance2 > targetDistance2) {
			continue;
		}
		if (shouldUpdateCount[currentNode] == 0) {
			continue;
		}

		if (voxelSize == 8*32) {
			acceleration_area_calculate_df(newX>>3, newY>>3, newZ>>3);
		}
		if (voxelSize == 32) {
			chunk_area_optimize_and_push(newX>>3, newY>>3, newZ>>3);
		} else {
			uint32_t newSet = octree->indices[currentNode];
			if (newSet == 0) {
			} else {
				octree_optimize_recursive(
					octree, targetDistance2,
					newSet,
					newX, newY, newZ,
					voxelSize>>1
				);
			}
		}

		shouldUpdateCount[currentNode] = 0;
	}
	return 0;
}

// NOTE: sometimes the execution gets stuck here :(
// (but of course not when I start debugging)
// does it still happen???
// TODO: DEBUG!
uint32_t octree_free_chunks_recursive(
		struct Octree *octree,
		float targetDistance2,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize)
{
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);

	uint32_t nodeGeneratedCountChange = 0;
	uint32_t progressChange = 0;
	for (uint8_t cube = 0; cube < 8; cube++) {
		uint32_t currentNode = currentSet+cube;
		uint16_t newX = x+(-(cube&1) & voxelSize);
		uint16_t newY = y+((-(cube&2)>>1) & voxelSize);
		uint16_t newZ = z+((-(cube&4)>>2) & voxelSize);
		
		float distance2 = furthest_distance2_point_to_cube(
				newX, newY, newZ, voxelSize,
				octreeGenerateChunksPos[0],
				octreeGenerateChunksPos[1],
				octreeGenerateChunksPos[2]);
		if (distance2 < targetDistance2) {
			continue;
		}
		if (nodeGeneratedCount[currentNode] == 0) {
			continue;
		}
		uint32_t newNodeGeneratedCountChange;
		uint8_t newProgressChange;
		if (voxelSize == 8) {
			distance2 = distance2_point_to_cube(
					newX, newY, newZ, voxelSize,
					octreeGenerateChunksPos[0],
					octreeGenerateChunksPos[1],
					octreeGenerateChunksPos[2]);
			if (!(distance2 > targetDistance2)) {
				continue;
			}
			chunk_destroy(newX>>3, newY>>3, newZ>>3);
			// TODO: actually support it instead of whatever this is
			generationoctree_add_to_floodfillentrycount(
					newX, newY, newZ, 8, 1, true, false);
			newNodeGeneratedCountChange = 0u-1u;
			newProgressChange = 8;
		} else {
			uint32_t newSet = octree->indices[currentNode];
			if (newSet == 0) {
				newNodeGeneratedCountChange = 0;
				newProgressChange = 0;
			} else {
				uint32_t bitfield = octree_free_chunks_recursive(
					octree, targetDistance2,
					newSet,
					newX, newY, newZ,
					voxelSize>>1
				);
				newProgressChange = bitfield & 0b1111;
				newNodeGeneratedCountChange = bitfield >> 4;
			}
		}
		uint8_t oldWasFull = progress2DGeneration[currentNode] == 8u;
		progress2DGeneration[currentNode] -= newProgressChange;
		nodeGeneratedCount[currentNode] += newNodeGeneratedCountChange;
		if (newProgressChange > 0u && oldWasFull) {
			progressChange += 1;
		}
		nodeGeneratedCountChange += newNodeGeneratedCountChange;
	}
	return progressChange + (nodeGeneratedCountChange << 4);
}

void octree_update_cache_recursive(
		struct Octree *octree,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize)
{
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);

	for (uint8_t cube = 0; cube < 8; cube++) {
		uint32_t currentNode = currentSet+cube;
		uint16_t newX = x+(-(cube&1) & voxelSize);
		uint16_t newY = y+((-(cube&2)>>1) & voxelSize);
		uint16_t newZ = z+((-(cube&4)>>2) & voxelSize);
		
		if (nodeGeneratedCount[currentNode] == 0) {
			continue;
		}
		if (voxelSize == 32) {
			chunk_area_update_cache(newX>>3, newY>>3, newZ>>3);
		} else {
			uint32_t newSet = octree->indices[currentNode];
			if (newSet != 0) {
				octree_update_cache_recursive(
					octree,
					newSet,
					newX, newY, newZ,
					voxelSize>>1
				);
			}
		}
	}
}

// does not check for overflow! Should be done elsewhere
// aka. do not create chunks if almost overflowing
void ua_push_index(uint32_t accelerationIndex)
{
	/*
	emscripten_atomic_store_u32(
			(void*)&accelerationBufferData[accelerationIndex],
			accelerationBufferData[accelerationIndex]);
			*/

	emscripten_atomic_store_u32(
			(void*)&updateStructure.uaIndices[updateStructure.uaWriteIndex],
			accelerationIndex);

	// memory barrier
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

// atomic, can be called by both job_thread and gil_thread
void uc_push_index(uint32_t accelerationIndex)
{
	uint32_t writeIndex = emscripten_atomic_add_u32(
			(void*)&updateStructure.ucPreWriteIndex,
			1);
	writeIndex &= updateStructure.ucIndicesCount;
	emscripten_atomic_store_u32(
			(void*)&updateStructure.ucIndices[writeIndex],
			accelerationIndex);
	asm volatile("" : : : "memory");
	emscripten_atomic_store_u32(
			(void*)&updateStructure.ucWriteIndex,
			writeIndex);
}

void uc_reset()
{
	updateStructure.ucWriteIndex = 0;
	updateStructure.ucPreWriteIndex = 0;
	updateStructure.ucReadIndex = 0;
}

void ua_reset()
{
	updateStructure.uaWriteIndex = 0;
	updateStructure.uaReadIndex = 0;
}

void update_structure_initialize()
{
	// -1 so that you can use & instead of % which simplifies atomics
	updateStructure.ucIndicesCount = (1 << 24)*2 - 1; // times 2 to be safe

	// 32768 (max number of workgroups), 64 (shader workgroup size), 2 (index+data)
	// force into power of 2
	// stored as actual size -1 since all fetches subtract 1 anyway
	//updateStructure.uaIndicesCount = (1u << (32u-__builtin_clzl(32768u*64*2-1)))-1u;
	updateStructure.uaIndicesCount = 32768u*64*2*2; // times 2 to be safe

	// add with 1 since subtracted by 1 before
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
		uint32_t gridY = (y+zWorld) / width;
		
		float cellX = (((x+xWorld) % width)+0.5) / width;
		float cellY = (((y+zWorld) % width)+0.5) / width;

		float n0 = generate_random_from_2d(gridY, gridX);
		float n1 = generate_random_from_2d(gridY, gridX+1);
		float n2 = generate_random_from_2d(gridY+1, gridX);
		float n3 = generate_random_from_2d(gridY+1, gridX+1);
		float finalNoise = lerp(
				lerp(n0, n1, perlin_fade_function(cellX)),
				lerp(n2, n3, perlin_fade_function(cellX)),
				perlin_fade_function(cellY));
		heightMap[i] = finalNoise*40+1000+yWorld-65536;
	}}
}

void free_heightmap()
{
	free(heightMap);
}

// NOTE: make sure the chunk is not a border first!!!
// tries to generate a chunk, if chunk empty-> does not generate one
// does not ua_push_index!
struct ReturnedChunkCreationBitfield chunk_create_from_2dnoise(
		uint32_t inax, uint32_t inay, uint32_t inaz)
{
	struct ReturnedChunkCreationBitfield returnVal;

	uint32_t accelerationX = inax;
	uint32_t accelerationY = inay;
	uint32_t accelerationZ = inaz;

	uint32_t chunkIndex = chunkstack_allocate(&chunkStack) << 9;
	uint32_t accelerationIndex = (accelerationZ << 16)
		| (accelerationY << 8) | accelerationX;
	
	accelerationBufferData[accelerationIndex] = chunkIndex;
	uint32_t voxelCountInChunk = 0;

	/*
	uint16_t accelerationValue = accelerationHeightMap[(accelerationZ<<8)+accelerationX];
	uint8_t minValue = accelerationValue & 0xFF;
	uint8_t maxValue = accelerationValue >> 8;
	bool shouldGenerate = inay >= minValue && inay <= maxValue;
	*/
	bool shouldGenerate = true;
	
	uint32_t chunkCacheIndex = chunkcache_get(inax, inay, inaz);
	if (chunkCacheIndex != 0xFFFFFFFF) {
		if (chunkCache.bitfields[chunkCacheIndex]
				& CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT) {
			uint32_t stackIndex = chunkCache.bitfields[chunkCacheIndex] >> 32;
			memcpy(
					(void*)&chunkBufferData[chunkIndex],
					&chunkCache.stack.voxels[stackIndex<<9],
					8*8*8*sizeof(uint32_t));
			voxelCountInChunk = 1;
			shouldGenerate = false;
		} else {
			//printf("this should be unreachable???\n");
			__builtin_unreachable();
		}
	}

	if (shouldGenerate) {
		returnVal = (struct ReturnedChunkCreationBitfield) {
			.generated = 1,
			.xNeg = 0,
			.yNeg = 0,
			.zNeg = 0,
			.xPos = 0,
			.yPos = 0,
			.zPos = 0,
		};
		for (uint32_t chunkZ = 0; chunkZ < 8; chunkZ++) {
		for (uint32_t chunkY = 0; chunkY < 8; chunkY++) {
		for (uint32_t chunkX = 0; chunkX < 8; chunkX++) {
			uint32_t realX = (accelerationX<<3)+chunkX;
			uint32_t realY = (accelerationY<<3)+chunkY;
			uint32_t realZ = (accelerationZ<<3)+chunkZ;
			
			//uint32_t voxel = ((uint32_t)(emscripten_random()*16) & 15u) + 1;
			uint32_t voxel = 1;
			uint32_t depth = heightMap[(realZ*heightMapDimension)|realX];
			if (realY == depth) {
				voxel = 1;
			} else if (realY < depth-5) {
				voxel = 3;
			} else {
				voxel = 2;
			}
			if (realY > depth) {
				voxel = 0;
			}
			if (voxel != 0) {
				voxelCountInChunk++;
			} else {
				if (chunkX == 0) {
					returnVal.xNeg = 1;
				} else if (chunkX == 7) {
					returnVal.xPos = 1;
				}
				if (chunkY == 0) {
					returnVal.yNeg = 1;
				} else if (chunkY == 7) {
					returnVal.yPos = 1;
				}
				if (chunkZ == 0) {
					returnVal.zNeg = 1;
				} else if (chunkZ == 7) {
					returnVal.zPos = 1;
				}
				voxel = 1 << 31;
			}
			
			uint32_t chunkOffset = (chunkZ << 6) | (chunkY << 3) | chunkX;
			uint32_t index = chunkIndex | chunkOffset;
			chunkBufferData[index] = voxel;
		}}}
	}
	if (voxelCountInChunk == 0) {
		accelerationBufferData[accelerationIndex] = 1 << 31;
		chunkstack_free(&chunkStack, chunkIndex>>9);
	} else {
		returnVal.generated = 0;
		asm volatile("" : : : "memory");
		//gil_push(accelerationIndex | GIL_OPERATION_CHUNK_MODIFIED);
	}
	return returnVal;
}

// btw you have to make sure that a chunk is stored at the
// location before calling
void chunk_after_update(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	// first of all, make sure chunk is cached
	chunkcache_insert(inax, inay, inaz);

	// then, make sure to add chunks to flood fill algo if necessary
	struct ReturnedChunkCreationBitfield bitfield = {
		.generated = 1,
		.xNeg = 0,
		.yNeg = 0,
		.zNeg = 0,
		.xPos = 0,
		.yPos = 0,
		.zPos = 0,
	};
	uint32_t accelerationIndex = (inaz<<16)|(inay<<8)|inax;
	for (uint32_t chunkZ = 0; chunkZ < 8; chunkZ++) {
	for (uint32_t chunkY = 0; chunkY < 8; chunkY++) {
	for (uint32_t chunkX = 0; chunkX < 8; chunkX++) {
		uint32_t chunkOffset = (chunkZ << 6) | (chunkY << 3) | chunkX;
		uint32_t index = accelerationBufferData[accelerationIndex]
			| chunkOffset;
		uint32_t voxel = chunkBufferData[index];
		if ((voxel & (3 << 30)) != 0) {
			if (chunkX == 0) {
				bitfield.xNeg = 1;
			} else if (chunkX == 7) {
				bitfield.xPos = 1;
			}
			if (chunkY == 0) {
				bitfield.yNeg = 1;
			} else if (chunkY == 7) {
				bitfield.yPos = 1;
			}
			if (chunkZ == 0) {
				bitfield.zNeg = 1;
			} else if (chunkZ == 7) {
				bitfield.zPos = 1;
			}
		}
	}}}
	if (bitfield.zPos) {
		generationoctree_add_to_floodfillentrycount(
				inax<<3, inay<<3, (inaz+1)<<3, 8, 1,
				true, true);
	}
	if (bitfield.zNeg) {
		generationoctree_add_to_floodfillentrycount(
				inax<<3, inay<<3, (inaz-1)<<3, 8, 1,
				true, true);
	}
	if (bitfield.yPos) {
		generationoctree_add_to_floodfillentrycount(
				inax<<3, (inay+1)<<3, inaz<<3, 8, 1,
				true, true);
	}
	if (bitfield.yNeg) {
		generationoctree_add_to_floodfillentrycount(
				inax<<3, (inay-1)<<3, inaz<<3, 8, 1,
				true, true);
	}
	if (bitfield.xPos) {
		generationoctree_add_to_floodfillentrycount(
				(inax+1)<<3, inay<<3, inaz<<3, 8, 1,
				true, true);
	}
	if (bitfield.xNeg) {
		generationoctree_add_to_floodfillentrycount(
				(inax-1)<<3, inay<<3, inaz<<3, 8, 1,
				true, true);
	}
	//gil_push(accelerationIndex | GIL_OPERATION_CHUNK_MODIFIED);
}

void chunk_destroy(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	uint32_t accelerationIndex = ((inaz) << 16)
		| ((inay) << 8) | (inax);
	uint32_t oldVal = accelerationBufferData[accelerationIndex];

	if ((accelerationBufferData[accelerationIndex] & (3 << 30)) == 0u) {
		//chunk_lock(accelerationIndex);
		/*
		asm volatile("" : : : "memory");
		for (uint8_t chunkZ = 0; chunkZ < 8; chunkZ++) {
		for (uint8_t chunkY = 0; chunkY < 8; chunkY++) {
		for (uint8_t chunkX = 0; chunkX < 8; chunkX++) {
			uint32_t chunkOffset = (chunkZ << 6) | (chunkY << 3) | chunkX;
			uint32_t index = accelerationBufferData[accelerationIndex]
				| chunkOffset;
			uint32_t voxel = chunkBufferData[index];
			if ((voxel & (3u << 30)) == 0u && (voxel & GILINFO_ADDRESS_MASK)) {
				gil_push(((voxel & GILINFO_ADDRESS_MASK) >> 5)
						| GIL_OPERATION_VOXEL_DELETED);
			}
		}}}
	*/
		// make sure to copy to cache before deleting
		uint32_t chunkCacheIndex = chunkcache_get(inax, inay, inaz);
		if (chunkCacheIndex != 0xFFFFFFFF) {
			uint32_t stackIndex;
			if (chunkCache.bitfields[chunkCacheIndex]
					& CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT) {
				stackIndex = chunkCache.bitfields[chunkCacheIndex] >> 32;
			} else {
				stackIndex = chunkstack_allocate(&chunkCache.stack);
				chunkCache.bitfields[chunkCacheIndex] |=
					CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT
					| (((uint64_t)stackIndex) << 32);
			}
			memcpy(
					&chunkCache.stack.voxels[stackIndex<<9],
					(void*)&chunkBufferData[accelerationBufferData[accelerationIndex]],
					8*8*8*sizeof(uint32_t));
		}
		chunkstack_free(&chunkStack, accelerationBufferData[accelerationIndex]>>9);
		asm volatile("" : : : "memory");
		//chunk_unlock(accelerationIndex);
	}
	accelerationBufferData[accelerationIndex] = 3u << 30;

	// TODO: VERY IMPORTANT: add to octreeGenerateAccelerationDf[z][y][x]
	// (to be replaced with shouldUpdate)

	if (oldVal != accelerationBufferData[accelerationIndex]) {
		ua_push_index(accelerationIndex);
	}
}

struct ClickRequest {
	uint32_t type;
	uint32_t selectedVoxel;
	float originX;
	float originY;
	float originZ;
	float dirX;
	float dirY;
	float dirZ;
} clickRequests[64];

uint32_t lightsToShare[64][4];
volatile uint32_t lightsToShareCount;

volatile bool threadShouldStop = false;
volatile bool threadHasStopped = false;

uint32_t clickRequestCount = 0;
void job_thread()
{
	lightsToShareCount = 0;
	int i = 0;
	while (true) {
		if (threadShouldStop) {
			emscripten_thread_sleep(1);
			threadHasStopped = true;
			continue;
		}
		if (i++ % 100 == 0) {
			//printf("chunkCount: %d\n", chunkStack.count);
		}
		octree_generate_chunks();
		for (uint32_t i = 0; i < clickRequestCount; i++) {
			struct ClickRequest request = clickRequests[i];
			if (request.type == 1) {
				continue;
			}
			struct RaycastResult result = raycast(
					request.originX, request.originY, request.originZ,
					request.dirX, request.dirY, request.dirZ);
			if (request.type == 2) {
				if (result.normal == 0) {
					result.x += 1;
				} else if (result.normal == 1) {
					result.x -= 1;
				} else if (result.normal == 2) {
					result.y += 1;
				} else if (result.normal == 3) {
					result.y -= 1;
				} else if (result.normal == 4) {
					result.z += 1;
				} else if (result.normal == 5) {
					result.z -= 1;
				}
			}
			if (result.success) {
				uint32_t ax = result.x>>3;
				uint32_t ay = result.y>>3;
				uint32_t az = result.z>>3;
				uint32_t accelerationIndex = (az<<16)|(ay<<8)|ax;
				bool hasAllocated = false;
				if (accelerationBufferData[accelerationIndex] & (1 << 31)) {
					hasAllocated = true;
					if (request.type == 0) {
						continue;
					}
					chunk_create_empty(ax, ay, az);
				}
				//chunk_lock(accelerationIndex);
				uint32_t cx = result.x & 7;
				uint32_t cy = result.y & 7;
				uint32_t cz = result.z & 7;
				uint32_t voxelIndex = accelerationBufferData[accelerationIndex]
					| (cz<<6) | (cy<<3) | cx;
				if (request.type == 0) { // left click, destroy block
					chunkBufferData[voxelIndex] = 1 << 31;
					/*
					uint32_t voxel = chunkBufferData[voxelIndex];
					if (voxel & GILINFO_ADDRESS_MASK) {
						gil_push((voxel >> 5)
								| GIL_OPERATION_VOXEL_DELETED);
					}
					*/
					// TODO: check if completely empty
				} else {
					chunkBufferData[voxelIndex] = request.selectedVoxel;
				}
				chunk_after_update(ax, ay, az);

				if (request.selectedVoxel == 8) { // light voxel
					uint32_t index = emscripten_atomic_add_u32(
							(void*)&lightsToShareCount, 1);
					lightsToShare[index][0] = result.x;
					lightsToShare[index][1] = result.y;
					lightsToShare[index][2] = result.z;
					lightsToShare[index][3] = request.type;
				}
				//chunk_unlock(accelerationIndex);

				uint32_t caStartX = ax & ~3;
				uint32_t caStartY = ay & ~3;
				uint32_t caStartZ = az & ~3;
				uint32_t startX = caStartX;
				uint32_t startY = caStartY;
				uint32_t startZ = caStartZ;
				uint32_t endX = caStartX;
				uint32_t endY = caStartY;
				uint32_t endZ = caStartZ;
				// since a 40x40x40 area is covered instead of 32x32x32
				if ((result.x & 31) < 4) startX = MAX((int)caStartX-4, 0);
				if ((result.y & 31) < 4) startY = MAX((int)caStartY-4, 0);
				if ((result.z & 31) < 4) startZ = MAX((int)caStartZ-4, 0);
				if ((result.x & 31) > 27) endX = MIN((int)caStartX+4, 252);
				if ((result.y & 31) > 27) endY = MIN((int)caStartY+4, 252);
				if ((result.z & 31) > 27) endZ = MIN((int)caStartZ+4, 252);
				for (uint32_t z = startZ; z <= endZ; z+=4) {
				for (uint32_t y = startY; y <= endY; y+=4) {
				for (uint32_t x = startX; x <= endX; x+=4) {
					/*
					chunk_area_calculate_df(x, y, z);
					chunk_area_push_indices(x, y, z);
					*/
					//quickHack = true;
					chunk_area_optimize_and_push(x, y, z);
					//quickHack = false;
				}}}
				if (hasAllocated) {
					caStartX = ax & ~31;
					caStartY = ay & ~31;
					caStartZ = az & ~31;
					startX = caStartX;
					startY = caStartY;
					startZ = caStartZ;
					endX = caStartX;
					endY = caStartY;
					endZ = caStartZ;
					// since a 40x40x40 area is covered instead of 32x32x32
					if ((ax & 31) < 4) startX = MAX((int)caStartX-32, 0);
					if ((ay & 31) < 4) startY = MAX((int)caStartY-32, 0);
					if ((az & 31) < 4) startZ = MAX((int)caStartZ-32, 0);
					if ((ax & 31) > 27) endX = MIN((int)caStartX+32, 224);
					if ((ay & 31) > 27) endY = MIN((int)caStartY+32, 224);
					if ((az & 31) > 27) endZ = MIN((int)caStartZ+32, 224);
					for (uint32_t z = startZ; z <= endZ; z+=32) {
					for (uint32_t y = startY; y <= endY; y+=32) {
					for (uint32_t x = startX; x <= endX; x+=32) {
						acceleration_area_calculate_df(x, y, z);
					}}}
				}
			}
		}
		clickRequestCount = 0;

		emscripten_thread_sleep(1);
	}
}

/*
void gil_thread()
{
	uint32_t debugCount = 0;
	uint32_t k = 0;
	IGNORE_UNUSED_WARNING(debugCount);
	while (true) {
		if (k++ % 100 == 0) {
			struct Octree *octree = &gil.lightmapOctree;
			uint32_t *nodeStack = octree->data;
			uint32_t *nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
			uint32_t *lightmapCount = (uint32_t*)(&nodeStackCount[1]);
			uint32_t totalLightmapCount = 0;
			for (uint32_t i = 0; i < 8; i++) totalLightmapCount += lightmapCount[i];
			printf("gilInfoBufferCount: %d. lms: %d, %d, %d, %d, %d. %d\n",
					gil.gilInfoBufferFreeCount,
					gil.freeCount[0],
					gil.freeCount[1],
					gil.freeCount[2],
					gil.freeCount[3],
					gil.freeCount[4],
					totalLightmapCount
					);
		}
		while (gil.readIndex != gil.doneWriteIndex) {
			uint32_t operation = gil_pop();
			if (operation & GIL_OPERATION_VOXEL_DELETED) {
				uint32_t address = operation & ~GIL_OPERATION_VOXEL_DELETED;
				if (address != 0) {
					for (uint32_t i = 2; i < 8; i++) {
						if (gil.gilInfoBuffer[address*8 + i] != LM_NONE) {
							gil_free_lightmap(gil.gilInfoBuffer[address*8 + i]);
							uint32_t gridX = gil.gilInfoBuffer[address*8 + 0] & 0xFFFF;
							uint32_t gridY = gil.gilInfoBuffer[address*8 + 0] >> 16;
							uint32_t gridZ = gil.gilInfoBuffer[address*8 + 1];
							lightmapoctree_add_to_count(
									&gil.lightmapOctree,
									gridX, gridY, gridZ,
									0-1);

						}
					}
					gil_info_buffer_free(address);

					// gil_push_to_gpu not needed! since the shaders
					// only care about visible stuff
				}
			}
			if (operation & GIL_OPERATION_CHUNK_MODIFIED) {
				uint32_t accelerationIndex = operation & 0xFFFFFF;
				uint8_t ax = accelerationIndex & 0xFF;
				uint8_t ay = (accelerationIndex>>8) & 0xFF;
				uint8_t az = (accelerationIndex>>16) & 0xFF;
				//chunk_lock(accelerationIndex);
				asm volatile("" : : : "memory");
				for (uint32_t chunkZ = 0; chunkZ < 8; chunkZ++) {
				for (uint32_t chunkY = 0; chunkY < 8; chunkY++) {
				for (uint32_t chunkX = 0; chunkX < 8; chunkX++) {
					uint32_t chunkOffset = (chunkZ << 6) | (chunkY << 3) | chunkX;
					uint32_t index = accelerationBufferData[accelerationIndex] | chunkOffset;
					uint32_t voxel = chunkBufferData[index];
					uint32_t address = (voxel & GILINFO_ADDRESS_MASK) >> 5;
					if ((voxel & (3u << 30)) == 0u) {
						uint32_t gridX = (ax<<3) + chunkX;
						uint32_t gridY = (ay<<3) + chunkY;
						uint32_t gridZ = (az<<3) + chunkZ;
						// chunks can not be at borders so we do not need to
						// check for over or underflow from 2048x2048x2048 region
						uint8_t sides = 0;
						sides |= (get_voxel(gridX-1, gridY, gridZ) & (3u << 30)) == (1u << 31);
						sides |= ((get_voxel(gridX, gridY-1, gridZ) & (3u << 30)) == (1u << 31))<<1;
						sides |= ((get_voxel(gridX, gridY, gridZ-1) & (3u << 30)) == (1u << 31))<<2;
						sides |= ((get_voxel(gridX+1, gridY, gridZ) & (3u << 30)) == (1u << 31))<<3;
						sides |= ((get_voxel(gridX, gridY+1, gridZ) & (3u << 30)) == (1u << 31))<<4;
						sides |= ((get_voxel(gridX, gridY, gridZ+1) & (3u << 30)) == (1u << 31))<<5;
						// not visible from any direction
						if (sides == 0) {
							if (address) {
								for (uint32_t i = 2; i < 8; i++) {
									if (gil.gilInfoBuffer[address*8 + i] != LM_NONE) {
										gil_free_lightmap(gil.gilInfoBuffer[address*8 + i]);
										lightmapoctree_add_to_count(
												&gil.lightmapOctree,
												gridX, gridY, gridZ,
												0-1);
									}
								}
								gil_info_buffer_free(address);
								chunkBufferData[index] &= ~GILINFO_ADDRESS_MASK;

								// gil_push_to_gpu not needed, invisible
							}
							continue;
						} else {
							if (!address) {
								address = gil_info_buffer_allocate();
								chunkBufferData[index] &= ~GILINFO_ADDRESS_MASK;
								chunkBufferData[index] |= address << 5;
								gil.gilInfoBuffer[address*8 + 0] = gridX | (gridY << 16);
								gil.gilInfoBuffer[address*8 + 1] = gridZ;
								gil.gilInfoBuffer[address*8 + 2] = 0;
								gil.gilInfoBuffer[address*8 + 3] = 0;
								gil.gilInfoBuffer[address*8 + 4] = 0;
								gil.gilInfoBuffer[address*8 + 5] = 0;
								gil.gilInfoBuffer[address*8 + 6] = 0;
								gil.gilInfoBuffer[address*8 + 7] = 0;
							}
							for (uint32_t i = 2; i < 8; i++) {
								// if side visible but not allocated -> allocate
								if (gil.gilInfoBuffer[address*8 + i] == 0
										&& ((sides >> (i-2)) & 1)) {
									// TODO: allocate LM_NONE here and allocate the rest
									// using lightmapOctree
									gil.gilInfoBuffer[address*8 + i] =
										gil_allocate_lightmap(LM_1x1);
									lightmapoctree_add_to_count(
											&gil.lightmapOctree,
											gridX, gridY, gridZ,
											1);
									gil_push_to_gpu(address*8 + i);
									

								// if side not visible but allocated -> free!
								} else if (gil.gilInfoBuffer[address*8 + i] != 0
										&& !((sides >> (i-2)) & 1)) {
									gil_free_lightmap(gil.gilInfoBuffer[address*8 + i]);
									lightmapoctree_add_to_count(
											&gil.lightmapOctree,
											gridX, gridY, gridZ,
											0-1);
									gil.gilInfoBuffer[address*8 + i] = LM_NONE;
									// gill_push_to_gpu not needed since not visible
								}
							}
						}
					}
				}}}
				uc_push_index(accelerationIndex);
				asm volatile("" : : : "memory");
				chunk_unlock(accelerationIndex);
			}
		}
		if (gil.gpuPush.lightmapState == LIGHTMAP_STATE_WAITING_ON_C) {
			gil.gpuPush.lightmapListCount = 0;
			octree_create_lightmap_list_recursive(
					&gil.lightmapOctree,
					0,
					0, 0, 0,
					1 << 10);
			asm volatile("" : : : "memory");
			gil.gpuPush.lightmapState = LIGHTMAP_STATE_WAITING_ON_JS;
			//printf("%d\n", gil.gpuPush.lightmapListCount);
		}
		emscripten_thread_sleep(1);
	}
}
*/

__attribute__((used)) struct UpdateStructure *jobs_setup()
{
	pthread_t thread;
	pthread_create(&thread, NULL, (void*)job_thread, NULL);
	pthread_detach(thread);
	
	/*
	pthread_t gilThread;
	pthread_create(&gilThread, NULL, (void*)gil_thread, NULL);
	pthread_detach(gilThread);
	*/
	
	return &updateStructure;
}

__attribute__((used)) uint32_t set_load_distance(int distance)
{
	gameSettings.loadDistance = distance;
	return 0;
}

/*
__attribute__((used)) uint32_t set_gil_budget(int percentage)
{
	gil.budget = (float)percentage / 100.0f;
	return 0;
}
*/

__attribute__((used)) uint32_t get_spawn_y()
{
	return heightMap[(1024*heightMapDimension)|1024];
}

void pause_thread()
{
	threadHasStopped = false;
	threadShouldStop = true;

	while (threadHasStopped == false) {
		emscripten_thread_sleep(1);
	}
}

__attribute__((used)) uint32_t unpause_thread()
{
	threadShouldStop = false;
	return 0;
}

__attribute__((used)) uint32_t get_seed()
{
	return seed;
}

__attribute__((used)) struct ChunkCache *get_chunkcache()
{
	pause_thread();
	octree_update_cache_recursive(
			&generationOctree, 0, 0, 0, 0, 1 << 10);
	asm volatile("" : : : "memory");
	return &chunkCache;
}

__attribute__((used)) struct ChunkCache *set_world_before(
		uint32_t currentPrime, uint32_t stackCount,
		uint32_t stackAllocationCount, uint32_t newSeed)
{
	pause_thread();

	// everything has to be completely remade
	generationOctree.count = 0;
	uint8_t *progress2DGeneration = generationOctree.data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[generationOctree.allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[generationOctree.allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[generationOctree.allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[generationOctree.allocationCount]);
	*nodeStackCount = 0;
	nodestack_allocate(&generationOctree);
	
	for (uint32_t i = 0; i < accelerationBufferSize/4; i++) {
		accelerationBufferData[i] = 3u << 30;
	}
	
	free(chunkStack.voxels);
	free(chunkStack.indices);
	chunkstack_create(&chunkStack, maxChunkCount, false, false);

	free(chunkCache.bitfields);
	free(chunkCache.PSL);

	free(chunkCache.stack.voxels);
	free(chunkCache.stack.indices);

	chunkCache.currentPrime = currentPrime;
	uint32_t allocatedCount = primes[chunkCache.currentPrime]+16;
	chunkCache.bitfields = malloc(sizeof(uint64_t)*allocatedCount);
	chunkCache.PSL = malloc(sizeof(uint16_t)*allocatedCount);
	for (uint32_t i = 0; i < allocatedCount; i++) {
		chunkCache.bitfields[i] = 0;
	}

	chunkstack_create(&chunkCache.stack, stackAllocationCount, true, true);
	chunkCache.stack.count = stackCount;

	free_heightmap();
	seed = newSeed;
	generate_heightmap(2048);
	
	octreeGenerateChunksCurrentDistance = 32;
	octree_generate_chunks();

	return &chunkCache;
}

__attribute__((used)) uint32_t set_world_after()
{
	chunkcache_resize(true);
	unpause_thread();
	return 0;
}

__attribute__((used)) uint32_t get_click_request(
		uint32_t type, uint32_t selectedVoxel,
		float originX, float originY, float originZ,
		float dirX, float dirY, float dirZ)
{

	if (clickRequestCount < 64) {
		clickRequests[clickRequestCount] = (struct ClickRequest) {
			.type = type,
			.selectedVoxel = selectedVoxel+1,
			.originX = originX,
			.originY = originY,
			.originZ = originZ,
			.dirX = dirX,
			.dirY = dirY,
			.dirZ = dirZ,
		};
		asm volatile("" : : : "memory");
		clickRequestCount++;
	}
	return 0;
}

__attribute__((used)) struct SharedWithJS *init()
{
	xWorld = 65536;
	yWorld = 65536;
	zWorld = 65536;
	seed = (uint32_t)(emscripten_random()*1000000.0f);
	
	generate_heightmap(2048);

	update_structure_initialize();

	gameSettings.loadDistance = 256;

	sharedWithJS.lightsToShareLocation = (void*)lightsToShare;
	sharedWithJS.lightsToShareCountLocation = (void*)&lightsToShareCount;
	
	cachedSAT = malloc(40*40*40*sizeof(uint16_t));
	posix_memalign((void**)&cachedSIMDSAT, 16, 40*40*40*sizeof(v128_t));

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

// creates a set of 4x4x4 chunks
void chunk_area_create(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	for (uint8_t z = 0; z < 4; z++) {
	for (uint8_t y = 0; y < 4; y++) {
	for (uint8_t x = 0; x < 4; x++) {
		uint8_t chunkX = x+inax;
		uint8_t chunkY = y+inay;
		uint8_t chunkZ = z+inaz;

		if (chunkX == 0 || chunkY == 0 || chunkZ == 0
				|| chunkX == 255 || chunkY == 255 || chunkZ == 255) {
			continue;
		}
		
		chunk_create_from_2dnoise(chunkX, chunkY, chunkZ);
	}}}
	// TODO: do this elsewhere somehow
	chunk_area_calculate_df(inax, inay, inaz);
	for (uint8_t z = 0; z < 4; z++) {
	for (uint8_t y = 0; y < 4; y++) {
	for (uint8_t x = 0; x < 4; x++) {
		uint8_t chunkX = x+inax;
		uint8_t chunkY = y+inay;
		uint8_t chunkZ = z+inaz;
		
		if (chunkX == 0 || chunkY == 0 || chunkZ == 0
				|| chunkX == 255 || chunkY == 255 || chunkZ == 255) {
			continue;
		}
		
		uint32_t accelerationIndex = ((chunkZ) << 16)
			| ((chunkY) << 8) | (chunkX);

		octreeGenerateAccelerationDf[chunkZ/32][chunkY/32][chunkX/32] = 1;

		// not yet!
		//ua_push_index(accelerationIndex);
		if ((accelerationBufferData[accelerationIndex] & (3u << 30)) == 0u) {
			uc_push_index(accelerationIndex);
		}
	}}}
}

void chunk_area_optimize_and_push(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	shouldLockChunks = true;
	chunk_area_calculate_df(inax, inay, inaz);
	shouldLockChunks = false;
	for (uint8_t z = 0; z < 4; z++) {
	for (uint8_t y = 0; y < 4; y++) {
	for (uint8_t x = 0; x < 4; x++) {
		uint8_t chunkX = x+inax;
		uint8_t chunkY = y+inay;
		uint8_t chunkZ = z+inaz;
		
		uint32_t accelerationIndex = ((chunkZ) << 16)
			| ((chunkY) << 8) | (chunkX);

		if ((accelerationBufferData[accelerationIndex] & (3u << 30)) == 0u) {
			uc_push_index(accelerationIndex);

			/*
			if (quickHack) {
				gil_push(accelerationIndex | GIL_OPERATION_CHUNK_MODIFIED);
			}
			*/

			//chunk_unlock(accelerationIndex);
		}
	}}}
}

void chunk_area_destroy(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	for (uint8_t z = 0; z < 4; z++) {
	for (uint8_t y = 0; y < 4; y++) {
	for (uint8_t x = 0; x < 4; x++) {
		uint8_t chunkX = x+inax;
		uint8_t chunkY = y+inay;
		uint8_t chunkZ = z+inaz;
		
		uint32_t accelerationIndex = ((chunkZ) << 16)
			| ((chunkY) << 8) | (chunkX);
		uint32_t oldVal = accelerationBufferData[accelerationIndex];

		if ((accelerationBufferData[accelerationIndex] & (3 << 30)) == 0u) {
			// make sure to copy to cache before deleting
			uint32_t chunkCacheIndex = chunkcache_get(chunkX, chunkY, chunkZ);
			if (chunkCacheIndex != 0xFFFFFFFF) {
				uint32_t stackIndex;
				if (chunkCache.bitfields[chunkCacheIndex]
						& CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT) {
					stackIndex = chunkCache.bitfields[chunkCacheIndex] >> 32;
				} else {
					stackIndex = chunkstack_allocate(&chunkCache.stack);
					chunkCache.bitfields[chunkCacheIndex] |=
						CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT
						| (((uint64_t)stackIndex) << 32);
				}
				memcpy(
						&chunkCache.stack.voxels[stackIndex<<9],
						(void*)&chunkBufferData[accelerationBufferData[accelerationIndex]],
						8*8*8*sizeof(uint32_t));
			}
			chunkstack_free(&chunkStack, accelerationBufferData[accelerationIndex]>>9);
		}
		accelerationBufferData[accelerationIndex] = 3u << 30;

		if (oldVal != accelerationBufferData[accelerationIndex]) {
			ua_push_index(accelerationIndex);
		}
	}}}
}

void chunk_area_update_cache(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	for (uint8_t z = 0; z < 4; z++) {
	for (uint8_t y = 0; y < 4; y++) {
	for (uint8_t x = 0; x < 4; x++) {
		uint8_t chunkX = x+inax;
		uint8_t chunkY = y+inay;
		uint8_t chunkZ = z+inaz;
		
		uint32_t accelerationIndex = ((chunkZ) << 16)
			| ((chunkY) << 8) | (chunkX);

		if ((accelerationBufferData[accelerationIndex] & (3 << 30)) == 0u) {
			uint32_t chunkCacheIndex = chunkcache_get(chunkX, chunkY, chunkZ);
			if (chunkCacheIndex != 0xFFFFFFFF) {
				uint32_t stackIndex;
				if (chunkCache.bitfields[chunkCacheIndex]
						& CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT) {
					stackIndex = chunkCache.bitfields[chunkCacheIndex] >> 32;
				} else {
					stackIndex = chunkstack_allocate(&chunkCache.stack);
					chunkCache.bitfields[chunkCacheIndex] |=
						CHUNKCACHE_FLAG_STORED_IN_CACHE_BIT
						| (((uint64_t)stackIndex) << 32);
				}
				memcpy(
						&chunkCache.stack.voxels[stackIndex<<9],
						(void*)&chunkBufferData[accelerationBufferData[accelerationIndex]],
						8*8*8*sizeof(uint32_t));
			}
		}
	}}}
}

void chunk_area_push_indices(uint8_t inax, uint8_t inay, uint8_t inaz)
{
	for (uint32_t z = 0; z < 4; z++) {
	for (uint32_t y = 0; y < 4; y++) {
	for (uint32_t x = 0; x < 4; x++) {
		uint32_t caAX = inax+x;
		uint32_t caAY = inay+y;
		uint32_t caAZ = inaz+z;
		uint32_t caAIndex = (caAZ<<16)|(caAY<<8)|caAX;
		if ((accelerationBufferData[caAIndex] & (3u << 30)) == 0u) {
			/*
			if (shouldLockChunks) {
				chunk_unlock(caAIndex);
			}
			*/
			uc_push_index(caAIndex);
		}
	}}}
}

// optimizes a set of 4x4x4 chunks (32*32*32) voxels
void chunk_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz)
{
	uint8_t count = 0;
	for (uint16_t z = 0; z < 4; z++) {
	for (uint16_t y = 0; y < 4; y++) {
	for (uint16_t x = 0; x < 4; x++) {
		uint16_t ax = inax+x;
		uint16_t ay = inay+y;
		uint16_t az = inaz+z;
		uint32_t accelerationIndex =
			(az << 16) | (ay << 8) | ax;
		if (shouldLockChunks &&
				(accelerationBufferData[accelerationIndex] & (3<<30)) == 0u) {
			//chunk_lock(accelerationIndex);
		}
		count += (accelerationBufferData[accelerationIndex] & (3<<30)) == 0u;
	}}}
	if (count == 0) {
		return;
	}
	uint16_t *sat = cachedSAT;
	uint16_t maxDFIterationCount = 16;
	
	v128_t *simdSAT = cachedSIMDSAT;

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
		if (accelerationX <= 0
				|| accelerationY <= 0
				|| accelerationZ <= 0
				|| accelerationX >= 255
				|| accelerationY >= 255
				|| accelerationZ >= 255) {
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
	
	for (uint16_t z = 0, index = 0; z < 40; z++) {
	for (uint16_t y = 0; y < 40; y++) {
	for (uint16_t x = 0; x < 40; x++,index++) {
		uint32_t sum = sat[index];
		uint16_t negX = x == 0 ? 0 : sat_get_element(sat, 40, x-1, y, z);
		uint16_t negY = y == 0 ? 0 : sat_get_element(sat, 40, x, y-1, z);
		uint16_t negZ = z == 0 ? 0 : sat_get_element(sat, 40, x, y, z-1);
		uint16_t posX = x == 40-1 ? sum : sat_get_element(sat, 40, x+1, y, z);
		uint16_t posY = y == 40-1 ? sum : sat_get_element(sat, 40, x, y+1, z);
		uint16_t posZ = z == 40-1 ? sum : sat_get_element(sat, 40, x, y, z+1);

		simdSAT[index] = wasm_u16x8_make(sum, negX, negY, negZ, posX, posY, posZ, 0);
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
			
			uint8_t stopBitfield = 0;
			for (uint16_t i = 0; i < maxDFIterationCount; i++) {
				if (x0 < 1) xNegExpand = 0, stopBitfield |= 1;
				if (y0 < 1) yNegExpand = 0, stopBitfield |= 2;
				if (z0 < 1) zNegExpand = 0, stopBitfield |= 4;
				if (x1 > 38) xPosExpand = 0, stopBitfield |= 8;
				if (y1 > 38) yPosExpand = 0, stopBitfield |= 16;
				if (z1 > 38) zPosExpand = 0, stopBitfield |= 32;

				uint16_t targetX0 = x0-(xNegExpand == 2);
				uint16_t targetY0 = y0-(yNegExpand == 2);
				uint16_t targetZ0 = z0-(zNegExpand == 2);
				uint16_t targetX1 = x1+(xPosExpand == 2);
				uint16_t targetY1 = y1+(yPosExpand == 2);
				uint16_t targetZ1 = z1+(zPosExpand == 2);

				union SumUnion {
					v128_t vector;
					uint16_t uints[8];
				} sums;
				v128_t position = wasm_u16x8_make(0, x0, y0, z0, x1, y1, z1, 0);
				sums.vector = sat_simd_get_sum(simdSAT, 40, position);
				if (sums.uints[1]) {
					xNegExpand = 0, stopBitfield |= 1;
				}
				if (sums.uints[2]) {
					yNegExpand = 0, stopBitfield |= 2;
				}
				if (sums.uints[3]) {
					zNegExpand = 0, stopBitfield |= 4;
				}
				if (sums.uints[4]) {
					xPosExpand = 0, stopBitfield |= 8;
				}
				if (sums.uints[5]) {
					yPosExpand = 0, stopBitfield |= 16;
				}
				if (sums.uints[6]) {
					zPosExpand = 0, stopBitfield |= 32;
				}
				if (stopBitfield == (1+2+4+8+16+32)) {
					break;
				}

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

		/* done outside this function
		uc_push_index(accelerationIndex);
		ua_push_index(accelerationIndex);
		*/
	}}}
}

void acceleration_area_calculate_df(uint16_t inax, uint16_t inay, uint16_t inaz)
{
	// 40x40x40 summed area table (expanded by 4 voxels in each direction)
	uint16_t *sat = cachedSAT;
	uint16_t maxDFIterationCount = 31;

	v128_t *simdSAT = cachedSIMDSAT;

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
		if (accelerationX <= 0
				|| accelerationY <= 0
				|| accelerationZ <= 0
				|| accelerationX >= 255
				|| accelerationY >= 255
				|| accelerationZ >= 255) {
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
	for (uint16_t z = 0, index = 0; z < 40; z++) {
	for (uint16_t y = 0; y < 40; y++) {
	for (uint16_t x = 0; x < 40; x++,index++) {
		uint32_t sum = sat[index];
		uint16_t negX = x == 0 ? 0 : sat_get_element(sat, 40, x-1, y, z);
		uint16_t negY = y == 0 ? 0 : sat_get_element(sat, 40, x, y-1, z);
		uint16_t negZ = z == 0 ? 0 : sat_get_element(sat, 40, x, y, z-1);
		uint16_t posX = x == 40-1 ? sum : sat_get_element(sat, 40, x+1, y, z);
		uint16_t posY = y == 40-1 ? sum : sat_get_element(sat, 40, x, y+1, z);
		uint16_t posZ = z == 40-1 ? sum : sat_get_element(sat, 40, x, y, z+1);

		simdSAT[index] = wasm_u16x8_make(sum, negX, negY, negZ, posX, posY, posZ, 0);
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
			// done by javascript instead:
			//ua_push_index(accelerationIndex);
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
		
		uint8_t stopBitfield = 0;
		// TODO: an optimization would be to compare
		for (uint16_t i = 0; i < maxDFIterationCount; i++) {
			if (x0 < 1) xNegExpand = 0, stopBitfield |= 1;
			if (y0 < 1) yNegExpand = 0, stopBitfield |= 2;
			if (z0 < 1) zNegExpand = 0, stopBitfield |= 4;
			if (x1 > 38) xPosExpand = 0, stopBitfield |= 8;
			if (y1 > 38) yPosExpand = 0, stopBitfield |= 16;
			if (z1 > 38) zPosExpand = 0, stopBitfield |= 32;

			uint16_t targetX0 = x0-(xNegExpand == 2);
			uint16_t targetY0 = y0-(yNegExpand == 2);
			uint16_t targetZ0 = z0-(zNegExpand == 2);
			uint16_t targetX1 = x1+(xPosExpand == 2);
			uint16_t targetY1 = y1+(yPosExpand == 2);
			uint16_t targetZ1 = z1+(zPosExpand == 2);

			union SumUnion {
				v128_t vector;
				uint16_t uints[8];
			} sums;
			v128_t position = wasm_u16x8_make(0, x0, y0, z0, x1, y1, z1, 0);
			sums.vector = sat_simd_get_sum(simdSAT, 40, position);
			if (sums.uints[1]) {
				xNegExpand = 0, stopBitfield |= 1;
			}
			if (sums.uints[2]) {
				yNegExpand = 0, stopBitfield |= 2;
			}
			if (sums.uints[3]) {
				zNegExpand = 0, stopBitfield |= 4;
			}
			if (sums.uints[4]) {
				xPosExpand = 0, stopBitfield |= 8;
			}
			if (sums.uints[5]) {
				yPosExpand = 0, stopBitfield |= 16;
			}
			if (sums.uints[6]) {
				zPosExpand = 0, stopBitfield |= 32;
			}
			if (stopBitfield == (1+2+4+8+16+32)) {
				break;
			}

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

		ua_push_index(accelerationIndex);
	}}}
}

void chunk_create_empty(uint32_t inax, uint32_t inay, uint32_t inaz)
{
	uint32_t accelerationX = inax;
	uint32_t accelerationY = inay;
	uint32_t accelerationZ = inaz;

	uint32_t chunkIndex = chunkstack_allocate(&chunkStack) << 9;
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
}

__attribute__((used)) void *create_chunks(
		uint32_t inAccelerationBufferSize, uint32_t inChunkBufferSize)
{
	accelerationBufferSize = inAccelerationBufferSize;
	chunkBufferSize = inChunkBufferSize;
	maxChunkCount = chunkBufferSize / (512*sizeof(uint32_t));

	posix_memalign((void**)&accelerationBufferData, 16, accelerationBufferSize);
	posix_memalign((void**)&chunkBufferData, 16, chunkBufferSize);

	//chunkLocks = malloc(sizeof(pthread_mutex_t)*maxChunkCount);

	//gil_setup();
	
	// this is an absurd amount of memory allocated
	// TODO: lesser amount and use realloc!
	uint32_t octreeAllocationCount = 256*256*256*2;
	octree_create(
			&generationOctree,
			octreeAllocationCount,
			octreeAllocationCount*(
				sizeof(uint8_t)+ // progress2DGeneration
				sizeof(uint32_t)+ // nodeGeneratedCount
				sizeof(uint32_t)+ // nodeStack
				sizeof(uint8_t)+ // shouldUpdateCount
				sizeof(uint32_t) // floodFillEntryCount
			)+sizeof(uint32_t), // nodeStackCount
			OCTREE_TYPE_DEFAULT
	);
	nodestack_create(&generationOctree);
	nodestack_allocate(&generationOctree); // preallocates and resets top 8 nodes

	uint32_t *returnedData = malloc(64);
	returnedData[0] = (uint32_t)accelerationBufferData;
	returnedData[1] = (uint32_t)chunkBufferData;
	
	for (uint32_t i = 0; i < accelerationBufferSize/4; i++) {
		accelerationBufferData[i] = 3u << 30;
	}
	
	chunkstack_create(&chunkStack, maxChunkCount, false, false);

	chunkcache_create();

	octreeGenerateChunksCurrentDistance = 32;
	octree_generate_chunks();

	// returned accelerationBufferData and chunkBufferData fills instead
	uc_reset();
	ua_reset();
	return returnedData;
}

void reset_generation_octree(
		struct Octree *octree)
{
	uint8_t *progress2DGeneration = generationOctree.data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[generationOctree.allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[generationOctree.allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[generationOctree.allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[generationOctree.allocationCount]);
	
	*nodeStackCount = 0;
	for (uint32_t i = 0; i < octree->allocationCount; i++) {
		nodeStack[i] = i;
	}

	nodestack_allocate(octree); // preallocate and reset top level nodes
}

// you have to set first nodes yourself!
void octree_create(
		struct Octree *octree,
		uint32_t allocationCount,
		uint32_t dataAllocationCount,
		enum OctreeType type)
{
	// TODO: use malloc instead
	octree->allocationCount = allocationCount;
	octree->indices = calloc(allocationCount, sizeof(uint32_t));
	if (dataAllocationCount != 0) {
		octree->data = calloc(dataAllocationCount, 1);
	}
	octree->type = type;
}

struct ReturnedNode octree_get_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize)
{
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint32_t currentSet = 0;
	uint16_t voxelSize = 1 << 10;
	uint32_t currentNode = 0;
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

struct ReturnedNode octree_force_node(
		struct Octree *octree,
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize)
{
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint32_t currentSet = 0;
	uint16_t voxelSize = 1 << 10;
	uint32_t currentNode = 0;
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
			currentSet = nodestack_allocate(octree);
			octree->indices[currentNode] = currentSet;
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

bool generationoctree_set_node_as_generated(
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize)
{
	struct Octree *octree = &generationOctree;
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	IGNORE_UNUSED_WARNING(nodeStackCount);

	uint32_t stack[16];
	uint32_t stackCount = 0;

	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint32_t currentSet = 0;
	uint16_t voxelSize = 1 << 10;
	uint32_t currentNode = 0;
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

		stack[stackCount++] = currentNode;
		if (voxelSize == targetVoxelSize) {
			break;
		}
		
		currentSet = octree->indices[currentNode];
		if (currentSet == 0) {
			return false;
		}

		voxelSize >>= 1;
	}
	progress2DGeneration[currentNode] = 0;

	uint32_t originalStackCount = stackCount;

	--stackCount;
	uint8_t carry = 8;
	for (;;) {
		progress2DGeneration[stack[stackCount]] += carry;
		if (stackCount == 0)  {
			break;
		}
		if (progress2DGeneration[stack[stackCount]] == 8) {
			carry = 1;
		} else {
			break;
		}
		stackCount--;
	}
	for (uint32_t i = 0; i < originalStackCount; i++) {
		nodeGeneratedCount[stack[i]]++;
		shouldUpdateCount[stack[i]]++;
	}

	return true;
}

bool generationoctree_add_to_floodfillentrycount(
		uint16_t targetX, uint16_t targetY, uint16_t targetZ,
		uint16_t targetVoxelSize, uint32_t numberToAdd,
		bool onlyAddIfZero, bool force)
{
	struct Octree *octree = &generationOctree;
	uint8_t *progress2DGeneration = octree->data;
	uint32_t *nodeGeneratedCount = (uint32_t*)(&progress2DGeneration[octree->allocationCount]);
	uint32_t *nodeStack = (uint32_t*)(&nodeGeneratedCount[octree->allocationCount]);
	uint8_t *shouldUpdateCount = (uint8_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *nodeStackCount = (uint32_t*)(&shouldUpdateCount[octree->allocationCount]);
	uint32_t *floodFillEntryCount = (uint32_t*)(&nodeStackCount[1]);

	uint32_t stack[16];
	uint32_t stackCount = 0;

	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint32_t currentSet = 0;
	uint16_t voxelSize = 1 << 10;
	uint32_t currentNode = 0;
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

		stack[stackCount++] = currentNode;
		if (voxelSize == targetVoxelSize) {
			break;
		}
		
		currentSet = octree->indices[currentNode];
		if (currentSet == 0) {
			if (force) {
				currentSet = nodestack_allocate(octree);
				octree->indices[currentNode] = currentSet;
			} else return false;
		}

		voxelSize >>= 1;
	}
	if (onlyAddIfZero && floodFillEntryCount[currentNode] != 0) {
		return false;
	}
	for (uint32_t i = 0; i < stackCount; i++) {
		floodFillEntryCount[stack[i]] += numberToAdd;
	}
	return true;
}

/*
void chunk_lock(uint32_t accelerationIndex)
{
	// confirmed to be chunk here
	uint32_t index = accelerationBufferData[accelerationIndex] >> 9;
	pthread_mutex_lock(&chunkLocks[index]);
}
void chunk_unlock(uint32_t accelerationIndex)
{
	uint32_t index = accelerationBufferData[accelerationIndex] >> 9;
	pthread_mutex_unlock(&chunkLocks[index]);
}

void gil_push(uint32_t accelerationIndex)
{
	if ((gil.writeIndex + 1) % gil.maxOperationCount == gil.readIndex) {
		printf("oh noo.\n");
	}
	gil.operations[gil.writeIndex] = accelerationIndex;
	gil.writeIndex = (gil.writeIndex+1) % gil.maxOperationCount;
}

// first make sure that readIndex != doneWriteIndex
uint32_t gil_pop()
{
	uint32_t returnVal = gil.operations[gil.readIndex];
	gil.readIndex = (gil.readIndex+1) % gil.maxOperationCount;
	return returnVal;
}

void gil_setup()
{
	gil.maxOperationCount = 8000000;
	gil.operations = malloc(gil.maxOperationCount*sizeof(uint32_t));
	gil.writeIndex = 0;
	gil.readIndex = 0;
	gil.doneWriteIndex = 0;

	gil.maxCount[0] = 4096;
	gil.maxCount[1] = 16384;
	gil.maxCount[2] = 65536;
	gil.maxCount[3] = 131072;
	gil.maxCount[4] = 524288;
	for (uint32_t i = 0; i < 4; i++) { // skip 1x1
		gil.count[i] = (float)gil.maxCount[i] * gil.budget;
	}
	gil.count[4] = 2048*2048
			- gil.count[0]*16*16
			- gil.count[1]*8*8
			- gil.count[2]*4*4
			- gil.count[3]*2*2;
	for (uint32_t i = 0; i < 5; i++) {
		gil.freeIndices[i] = malloc(sizeof(uint32_t)*gil.count[i]);
		gil.freeCount[i] = 0;
		for (uint32_t j = 0; j < gil.count[i]; j++) {
			gil.freeIndices[i][j] = j;
		}
	}

	gil.gilInfoBufferMaxCount = 1000000;
	gil.gilInfoBuffer = malloc(gil.gilInfoBufferMaxCount*8*sizeof(uint32_t));
	gil.gilInfoBufferFreeIndices = malloc(gil.gilInfoBufferMaxCount*sizeof(uint32_t));
	gil.gilInfoBufferFreeCount = 0;
	for (uint32_t i = 0; i < gil.gilInfoBufferMaxCount; i++) {
		gil.gilInfoBufferFreeIndices[i] = i;
	}

	// allocate index 0 since the rest of the code thinks 0 = no address
	gil_info_buffer_allocate();
	
	gil.gpuPush.maxCount = 2097152;
	gil.gpuPush.indices = malloc(gil.gpuPush.maxCount*sizeof(uint32_t));
	gil.gpuPush.gilInfoBuffer = gil.gilInfoBuffer;
	gil.gpuPush.writeIndex = 0;
	gil.gpuPush.readIndex = 0;

	uint32_t octreeAllocationCount = 256*256*256*2*2;
	octree_create(
			&gil.lightmapOctree,
			octreeAllocationCount,
			octreeAllocationCount*(
				sizeof(uint32_t) + // nodeStack
				sizeof(uint32_t) // lightmapCount
			)+sizeof(uint8_t),
			OCTREE_TYPE_LIGHTMAP);
	nodestack_create(&gil.lightmapOctree);
	// allocate parent nodes and reset them
	nodestack_allocate(&gil.lightmapOctree);

	gil.gpuPush.lightmapListMaxCount = 2048*2048;
	gil.gpuPush.lightmapList = malloc(gil.gpuPush.lightmapListMaxCount*2*sizeof(uint32_t));
	gil.gpuPush.lightmapState = LIGHTMAP_STATE_WAITING_ON_C;
}

uint32_t gil_info_buffer_allocate()
{
	if (gil.gilInfoBufferFreeCount >= gil.gilInfoBufferMaxCount) {
		printf("out of lightmap memory\n");
		return 0xFFFFFFFF;
	}
	uint32_t index = gil.gilInfoBufferFreeIndices[gil.gilInfoBufferFreeCount];
	gil.gilInfoBufferFreeCount += 1;
	return index;
}

// DOES NOT FREE LIGHTMAPS!
void gil_info_buffer_free(uint32_t index)
{
	gil.gilInfoBufferFreeCount -= 1;
	gil.gilInfoBufferFreeIndices[gil.gilInfoBufferFreeCount] = index;
}

uint32_t get_voxel(uint32_t x, uint32_t y, uint32_t z)
{
	uint32_t accelerationIndex = ((z>>3)<<16) | ((y>>3) << 8) | (x>>3);
	uint32_t chunkIndex = accelerationBufferData[accelerationIndex];
	if ((chunkIndex & (3u << 30)) != 0u) {
		return chunkIndex & (3u << 30);
	} else {
		uint32_t cx = x & 7;
		uint32_t cy = y & 7;
		uint32_t cz = z & 7;
		uint32_t voxelIndex = chunkIndex | (cz<<6)|(cy<<3)|cx;
		return chunkBufferData[voxelIndex];
	}
}

uint32_t gil_allocate_lightmap(uint32_t target )
{
	uint32_t returnVal = LM_NONE;
	if (target == LM_NONE) {
		return returnVal;
	}
	uint32_t *freeIndices = gil.freeIndices[target-1];
	uint32_t index = freeIndices[gil.freeCount[target-1]];
	gil.freeCount[target-1] += 1;
	return (index << 3) | target;
}

void gil_free_lightmap(uint32_t lightmap)
{
	uint32_t target = lightmap & 7;
	uint32_t *freeIndices = gil.freeIndices[target-1];
	gil.freeCount[target-1] -= 1;
	freeIndices[gil.freeCount[target-1]] = lightmap >> 3;
}

// TODO: make circular buffer a struct so I dont have to write the same shit 50 times
void gil_push_to_gpu(uint32_t index) {
	if ((gil.gpuPush.writeIndex + 1) % gil.gpuPush.maxCount
			== gil.gpuPush.readIndex) {
		printf("oh noo.\n");
	}
	emscripten_atomic_store_u32(
			(void*)&gil.gpuPush.indices[gil.gpuPush.writeIndex],
			index);
	asm volatile("" : : : "memory");
	emscripten_atomic_store_u32(
			(void*)&gil.gpuPush.writeIndex,
			(gil.gpuPush.writeIndex+1) % gil.gpuPush.maxCount);
	printf("c: %d = %d\n", index, gil.gilInfoBuffer[index]);
}

__attribute__((used)) struct GILAndJSPushData *get_gil_gpupush_structure()
{
	return &gil.gpuPush;
}

void lightmapoctree_add_to_count(
		struct Octree *octree,
		uint16_t targetX,
		uint16_t targetY,
		uint16_t targetZ,
		uint32_t change)
{
	uint32_t *nodeStack = octree->data;
	uint32_t *nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *lightmapCount = (uint32_t*)(&nodeStackCount[1]);
	
	uint32_t stack[16];
	uint32_t stackCount = 0;

	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	uint32_t currentSet = 0;
	uint16_t voxelSize = 1 << 10;
	uint32_t currentNode = 0;
	uint16_t targetVoxelSize = 1;
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

		stack[stackCount++] = currentNode;
		if (voxelSize == targetVoxelSize) {
			break;
		}
		
		currentSet = octree->indices[currentNode];
		if (currentSet == 0) {
			currentSet = nodestack_allocate(octree);
			octree->indices[currentNode] = currentSet;
		}

		voxelSize >>= 1;
	}
	for (uint32_t i = 0; i < stackCount; i++) {
		lightmapCount[stack[i]] += change;
	}
}

// starts from the middle and gives the highest quality light maps to the
// closest faces
// TODO
uint32_t octree_allocate_lightmaps_flood_fill(
		struct Octree *octree,
		uint8_t initialX, uint8_t initialY, uint8_t initialZ)
{
	uint32_t *nodeStack = octree->data;
	uint32_t *nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *lightmapCount = (uint32_t*)(&nodeStackCount[1]);

	floodFillStack.count = 0;
	floodFillStack.maxCount = 2024000;
	floodFillStack.array = malloc(floodFillStack.maxCount*sizeof(uint8_t)*3);
}

// TODO: free lightmaps in the end (convert to LM_NONE)
void octree_create_lightmap_list_recursive(
		struct Octree *octree,
		uint32_t currentSet,
		uint16_t x, uint16_t y, uint16_t z,
		uint16_t voxelSize)
{
	uint32_t *nodeStack = octree->data;
	uint32_t *nodeStackCount = (uint32_t*)(&nodeStack[octree->allocationCount]);
	uint32_t *lightmapCount = (uint32_t*)(&nodeStackCount[1]);
	
	uint32_t reorder[] = {0, 1, 2, 5, 3, 6, 4, 7};
	
	for (uint8_t cube = 0; cube < 8; cube++) {
		uint32_t currentNode = currentSet+cube;
		uint16_t newX = x+(-(cube&1) & voxelSize);
		uint16_t newY = y+((-(cube&2)>>1) & voxelSize);
		uint16_t newZ = z+((-(cube&4)>>2) & voxelSize);

		if (lightmapCount[currentNode] == 0) {
			continue;
		}
		if (voxelSize == 1) {
			if (gil.gpuPush.lightmapListCount > gil.gpuPush.lightmapListMaxCount) {
				printf("HOW\n"); // this should not be possible (i think)
			}
			uint32_t voxel = get_voxel(newX, newY, newZ);
			if ((voxel & (3u << 30)) != 0u || !(voxel & GILINFO_ADDRESS_MASK)) {
				// WAT
				continue;
			}
			uint32_t address = (voxel & GILINFO_ADDRESS_MASK) >> 5;
			for (uint32_t i = 2; i < 8; i++) {
				uint32_t face = i;
				if (gil.gpuPush.gilInfoBuffer[address*8 + i] != LM_NONE) {
					uint32_t type = gil.gpuPush.gilInfoBuffer[address*8 + i] & 7;
					if (type == LM_1x1) {
						// push only 1 index
						uint32_t index = gil.gpuPush.lightmapListCount++;
						gil.gpuPush.lightmapList[index*2] = newX | (newY << 16);
						gil.gpuPush.lightmapList[index*2+1] = newZ | (0 << 16) | (face << 24);

						if (index == 0) {
						}
					}
				}
			}
		} else {
			uint32_t newSet = octree->indices[currentNode];
			if (newSet == 0) {
				continue;
			}
			octree_create_lightmap_list_recursive(
				octree, newSet,
				newX, newY, newZ,
				voxelSize>>1
			);
		}
	}
}
*/
