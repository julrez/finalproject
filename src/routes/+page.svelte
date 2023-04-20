<canvas id="webglCanvas"></canvas>
<p id="fps" style="white-space: pre-line">{debugString}</p>

<style>
	#fps {
		color: white;
		position: fixed;
		top: 0;
		left: 0;
	}
	canvas {
		position: fixed;
		top: 0;
		left: 0;
	}
</style>

<script>
	/*
	 * TODO: consistent function naming theme
	 *       e.g. create_voxelrenderer() -> voxelrenderer_create()
	 */
	import {onMount} from 'svelte';
	import {createNoise2D} from 'simplex-noise';
	import alea from 'alea';

	export let data;

	let debugString;

	let canvas, gl;
	
	/*
	 *	voxelTexture:
	 *		- width: 8192
	 *		- height: 8192
	 *
	 *	accelerationTexture:
	 *		- width: 256
	 *		- height: 256
	 *		- depth: 256
	 * 
	 *	voxelRenderer.voxelData
	 *		- new Uint32Array(8192*8192)
	 *		- list of voxels
	 *		- loaded into voxelTexture
	 *
	 *	voxelRenderer.accelerationData
	 *		- new Uint32Array(256*256*256)
	 *		- list of indices (or air or stop) in voxelData that specify a chunk
	 *		- loaded into accelerationTexture
	 * 
	 *	voxelRenderer.freeVoxelData
	 *		- used to find allocation
	 *		- one stack for every chunk dimension combination
	 *		- stack grows exponentially
	 *		- if no spot found -> look higher up
	 *		- [x][1] = list of indices in [x][0] list/stack
	 *		- [0][0] = new Uint32Array(), indices of aligned 16x16x16 spots
	 *		- [1][0] = new Uint32Array(), indices of aligned 15x16x16 spots
	 *		- ...
	 *		- [255][0] = new Uint32Array(), indices of aligned 1x1x1 spots
	 *		- top bits to signal chunk used for low priority cache?
	 */

	let voxelRenderer = {
		"voxelData": undefined,
		// instead of voxel type, it's a summed area table value (relative to chunk)
		// keeps same offsets as voxelData
		"voxelSumData": undefined,
		"accelerationData": undefined,
		"acceleration": {
			"pos": undefined, // position of [0, 0, 0] in world space
			// position of border
			"x1": undefined,
			"y1": undefined,
			"z1": undefined,
			"x2": undefined,
			"y2": undefined,
			"z2": undefined,
		},
		"freeChunks": {
			"count": undefined,
			"start": undefined,
			"end": undefined,
			"data": undefined,
		},
	};

	let octree = {
		"allocationCount": undefined,
		"count": undefined,
		"indices": undefined, // index to the next node or 0
		/*
		 * indices also point to this list
		 * from LSB -> MSB
		 * bit count    description
		 * 4:           number of nodes that have been generated (and lightly optimized)
		 * 4:           number of nodes that have chunk optimization level 0
		 * 4:           number of nodes that have chunk optimization level 1
		 * 4:           number of nodes that have chunk optimization level 2
		 * 4:           number of nodes that have chunk optimization level 3
		 * 4:           number of nodes that have acceleration optimization level 0
		 * 4:           number of nodes that have acceleration optimization level 1
		 * 4:           number of nodes that have acceleration optimization level 2
		 */
		"progress": undefined,
		// for accelerationData
		"requiresUpdateCounts": undefined,
		// TODO: optimization
	};
	// tree with 8192 final leaves
	// represents the voxelData texture.
	// a node has 8 children (except final leaves)
	// (cursed octree)
	// 8192x16 (16=8192/(8*8*8))
	let voxelTextureUpdateTree = {
		// list = new Uint16Array
		// 0->8191 = final leaves
		//		element = bit field
		//		lowest bit = lowest chunk needs update
		//		highest bit = chunk at coords (15, x) needs update
		// 8192->(8192+1023) = level 1
		//		element = count of chunks needing an update
		// 9216->(9216+127) = level 2
		//		same thing
		// total memory: 8192+1024+128 = 9344 * 2 bytes
		"list": undefined,
	};

	// represents changes to be made in the accelerationData texture
	// quadtree over the 4096x4096 texture
	let accelerationTextureUpdateTree = {
		// list = new Uint32Array
		// 0->4095 = final leaves
		"list": undefined,
	};
	
	let world = {
		"seed": undefined,
		"noise_function": undefined,
	};
	
	// temporary:
	let rawVoxelData;
	let rawVoxelDataDimension;

	let mousePitch = 0, mouseYaw = 270;

	// relative to acceleration.pos
	//let cameraPos = [1023, 60, 1023];
	let cameraPos = [1023, 60, 1023];
	let cameraAxis = [0, 0, -1];

	let keysPressed = {};

	onMount(() => {
		addEventListener("keydown", (event) => {
			keysPressed[event.code] = true;
		});
		addEventListener("keyup", (event) => {
			keysPressed[event.code] = false;
		});
		addEventListener("click", (event) => {
			lock_cursor(canvas);
		});
		addEventListener("mousemove", (event) => {
			if (!(canvas === document.pointerLockElement)) {
				return;
			}
			let xOffset = event.movementX;
			let yOffset = -event.movementY;

			let m_yaw = 0.022; // to get same sensitivity as the Source engine

			let sensitivity = 3.0;

			let incrementX = xOffset * sensitivity * m_yaw;
			let incrementY = yOffset * sensitivity * m_yaw;

			mouseYaw += incrementX;
			mousePitch += incrementY;
			
			if (mousePitch > 89) {
				mousePitch = 89;
			} else if (mousePitch < -89) {
				mousePitch = -89;
			}

			cameraAxis[0] = Math.cos(mouseYaw*Math.PI/180)
				* Math.cos(mousePitch*Math.PI/180);
			cameraAxis[1] = Math.sin(mousePitch*Math.PI/180);
			cameraAxis[2] = Math.sin(mouseYaw*Math.PI/180)
				* Math.cos(mousePitch*Math.PI/180);
		});

		canvas = document.getElementById("webglCanvas");

		gl = canvas.getContext("webgl2");
		if (!gl) {
			console.log("error: webgl2 not available");
		}

		create_world();

		create_voxelrenderer();
		console.log("start: " + voxelRenderer.freeChunks.start);
	});

	function distance2_point_to_cube(
			cubeX, cubeY, cubeZ, voxelSize,
			pointX, pointY, pointZ)
	{
		let centerX = cubeX+(voxelSize>>1);
		let centerY = cubeY+(voxelSize>>1);
		let centerZ = cubeZ+(voxelSize>>1);

		let halfVoxelSize = voxelSize>>1;

		let dx = Math.max(Math.abs(centerX-pointX) - halfVoxelSize, 0);
		let dy = Math.max(Math.abs(centerY-pointY) - halfVoxelSize, 0);
		let dz = Math.max(Math.abs(centerZ-pointZ) - halfVoxelSize, 0);

		return dx*dx + dy*dy + dz*dz;
	}

	let generateChunksFunctionTargetDistance2;
	// NOTE: does not take borders into account!!!
	function generate_chunks(currentSet, x, y, z, voxelSize)
	{
		const largestDistanceMultiplier = Math.sqrt(3);

		let progressChange = 0;
		for (let cube = 0; cube < 8; cube++) {
			let currentNode = currentSet+cube;
			let newX = x+(-(cube&1) & voxelSize);
			let newY = y+((-(cube&2)>>1) & voxelSize);
			let newZ = z+((-(cube&4)>>2) & voxelSize);

			let distance2 = distance2_point_to_cube(
				newX, newY, newZ, voxelSize,
				cameraPos[0], cameraPos[1], cameraPos[2]
			);
			if (distance2 > generateChunksFunctionTargetDistance2) {
				continue;
			}
			// stop if already completely filled
			if (octree.progress[currentNode] == 8) {
				continue;
			}

			if (voxelSize == 8) {
				// TODO: propagate
				octree.progress[currentNode] = 8;
				progressChange += 1;
				// generate actual chunk
				let chunkX = newX>>3;
				let chunkY = newY>>3;
				let chunkZ = newZ>>3;
				create_chunk(chunkX, chunkY, chunkZ);
				continue;
			}
			// IF max distance < targetDistance: generate chunks
			/*
			if ((distance2+largestDistanceMultiplier*voxelSize) < ) {
				// generate all chunks
				continue;
			}
			*/

			let newSet = octree.indices[currentNode];
			if (newSet == 0) { // or voxelSize == 8 (implicit)
				newSet = octree.count;
				octree.indices[currentNode] = newSet;
				// NOTE: octree.indices[octree.count] -> octree.count+7
				//       MUST be zero!
				octree.count += 8;
			}

			let newProgressChange = generate_chunks(
				newSet,
				newX, newY, newZ,
				voxelSize>>1
			);
			octree.progress[currentNode] += newProgressChange;
			if (newProgressChange == 8) {
				progressChange += 1;
			}
		}
		return progressChange;
	}

	function optimize_acceleration_data()
	{
	}

	function optimize_chunk()
	{
	}

	function optimize_chunks()
	{
	}

	let disableVsync = false;
	let firstFrame = true;
	let oldTime;

	let lastUpdate = performance.now();
	function draw()
	{
		const timestamp = performance.now();
		if (firstFrame) {
			oldTime = timestamp;
			firstFrame = false;
		}
		let deltaTime = (timestamp-oldTime)/1000; // in seconds
		oldTime = timestamp;
		let maxDeltaTime = 0.3;
		if (deltaTime > maxDeltaTime) {
			deltaTime = maxDeltaTime;
		}
		debugString = "frame time (ms): " + (deltaTime*1000).toFixed(1)
			+ "\nPOS: "
			+ Math.floor(cameraPos[0]) + ", "
			+ Math.floor(cameraPos[1]) + ", "
			+ Math.floor(cameraPos[2])
			+ "\nchunkCount: " + voxelRenderer.freeChunks.start
			+ "\nnodeCount: " + octree.count
		;
		
		let distance = 256;
		generateChunksFunctionTargetDistance2 = distance*distance;
		generate_chunks(0, 0, 0, 0, 1 << 10);
		//if ((performance.now()-lastUpdate) > 1000) {
		if (true) {
			lastUpdate = performance.now();

			gl.bindTexture(gl.TEXTURE_2D, voxelRenderer.voxelTexture);
			for (let l2 = 0; l2 < 128; l2++) {
				if (voxelTextureUpdateTree.list[8192+1024+l2] > 0) {
					voxelTextureUpdateTree.list[8192+1024+l2] = 0;
					console.log("joe biden israel version 2");
					const height = 8192/128;
					let xOffset = 0;
					let yOffset = l2*height;
					gl.texSubImage2D(
						gl.TEXTURE_2D,
						0,
						0,
						yOffset,
						8192,
						height,
						gl.RED_INTEGER,
						gl.UNSIGNED_INT,
						voxelRenderer.voxelData,
						8192*(yOffset)
					);
				}
			}

			gl.bindTexture(gl.TEXTURE_2D, voxelRenderer.accelerationTexture);
			for (let i = 0; i < 4096; i++) {
				if (accelerationTextureUpdateTree[i] != 0) {
					accelerationTextureUpdateTree[i] = 0;
					gl.texSubImage2D(
						gl.TEXTURE_2D,
						0,
						0,
						i,
						4096,
						1,
						gl.RED_INTEGER,
						gl.UNSIGNED_INT,
						voxelRenderer.accelerationData,
						4096*i
					);
				}
			}
		}

		calculate_movement(deltaTime);

		canvas.width = document.documentElement.clientWidth;
		canvas.height = document.documentElement.clientHeight;
		gl.viewport(0, 0, gl.canvas.width, gl.canvas.height);
		gl.clearColor(0, 0, 0, 1);
		gl.clear(gl.COLOR_BUFFER_BIT);

		gl.useProgram(voxelRenderer.program);
		
		gl.uniform2f(voxelRenderer.invResolutionLocation, 1.0/canvas.width, 1.0/canvas.height);
		gl.uniform3f(voxelRenderer.positionLocation, cameraPos[0], cameraPos[1], cameraPos[2]);
		set_raydir_helpers();

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, voxelRenderer.voxelTexture);
		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, voxelRenderer.accelerationTexture);
		gl.drawArrays(gl.TRIANGLES, 0, 6);
		
		window.requestAnimationFrame(draw);
	}

	function calculate_movement(deltaTime)
	{
		let speed = 32.0 * deltaTime;

		let up = [0.0, 1.0, 0.0];

		let newAxis = normalize_vec3([cameraAxis[0], 0.0, cameraAxis[2]]);

		let verticalVector = scale_vec3_by_constant(newAxis, speed);
		let horizontalVector = scale_vec3_by_constant(
			normalize_vec3(cross_vec3(newAxis, up)),
			speed
		);
		let scaledUpVector = [0.0, speed, 0.0];

		if (keysPressed.Space) {
			cameraPos = add_vec3(cameraPos, scaledUpVector);
		}
		if (keysPressed.ShiftLeft) {
			cameraPos = sub_vec3(cameraPos, scaledUpVector);
		}
		if (keysPressed.KeyW) {
			cameraPos = add_vec3(cameraPos, verticalVector);
		}
		if (keysPressed.KeyS) {
			cameraPos = sub_vec3(cameraPos, verticalVector);
		}
		if (keysPressed.KeyA) {
			cameraPos = sub_vec3(cameraPos, horizontalVector);
		}
		if (keysPressed.KeyD) {
			cameraPos = add_vec3(cameraPos, horizontalVector);
		}
	}

	function set_raydir_helpers()
	{
		let up = [0.0, 1.0, 0.0];

		let fov = (Math.PI/180.0)*90.0;
		let invAspectRatio = canvas.height / canvas.width;
		let focalLength = 1.0;
		let width = focalLength * Math.tan(fov*0.5) * 2.0;
		let height = width * invAspectRatio;

		// needs to be normalized! (which it hopefully is)
		let axis = [...cameraAxis];

		let toHorizontalSide = scale_vec3_by_constant(
			normalize_vec3(cross_vec3(axis, up)),
			width*0.5
		);
		let toVerticalSide = scale_vec3_by_constant(
			normalize_vec3(cross_vec3(axis, toHorizontalSide)),
			height*0.5
		);
		let scaledAxis = scale_vec3_by_constant(axis, focalLength);

		let cameraPosition = [0, 0, 0]
		let faceCenter = add_vec3(scaledAxis, cameraPosition);
		
		let vert0 = [0, 0, 0];
		let vert1 = [0, 0, 0];
		let vert2 = [0, 0, 0];
		let vert3 = [0, 0, 0];

		vert0 = add_vec3(
			sub_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		vert1 = add_vec3(
			add_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		vert2 = sub_vec3(
			sub_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		vert3 = sub_vec3(
			add_vec3(faceCenter, toHorizontalSide),
			toVerticalSide
		);
		
		gl.uniform3f(voxelRenderer.vert0Location, vert0[0], vert0[1], vert0[2]);
		gl.uniform3f(voxelRenderer.vert1Location, vert1[0], vert1[1], vert1[2]);
		gl.uniform3f(voxelRenderer.vert2Location, vert2[0], vert2[1], vert2[2]);
		gl.uniform3f(voxelRenderer.vert3Location, vert3[0], vert3[1], vert3[2]);
	}

	function get_empty_chunk()
	{
		// TODO: check for out of memory
		let index = voxelRenderer.freeChunks.start;
		voxelRenderer.freeChunks.start++;

		return index;
	}
	
	function create_world()
	{
		world.seed = 20;
		world.noise_function = createNoise2D(alea(world.seed));
		
		voxelRenderer.acceleration.pos = [0, 0, 0];
	}

	// NOTE: does not take borders into account!
	function create_chunk(chunkX, chunkY, chunkZ)
	{
		const dimension = 8;
		let empty = true;

		let wx = chunkX*8+voxelRenderer.acceleration.pos[0];
		let wy = chunkY*8+voxelRenderer.acceleration.pos[1];
		let wz = chunkZ*8+voxelRenderer.acceleration.pos[2];

		let chunkIndex = get_empty_chunk();
		for (let z = 0, i = 0; z < dimension; z++) {
		for (let y = 0; y < dimension; y++) {
		for (let x = 0; x < dimension; x++,i++) {
			const realX = x+wx;
			const realY = y+wy;
			const realZ = z+wz;
			let height = 20+(world.noise_function(realX/100, realZ/100)+1)/2 * 20;
			if (realY < height) {
				voxelRenderer.voxelData[(chunkIndex<<9)+i] = Math.floor(Math.random() * 120) + 120;
				empty = false;
			} else {
				voxelRenderer.voxelData[(chunkIndex<<9)+i] = 1 << 31;
			}
		}}}

		let accelerationIndex = (chunkZ<<16) | (chunkY<<8) | chunkX;

		if (empty) {
			// undo allocation
			voxelRenderer.freeChunks.start--;

			voxelRenderer.accelerationData[accelerationIndex] = 1 << 31;
		} else {
			voxelRenderer.accelerationData[accelerationIndex] = chunkIndex;
			
			let yIndex = chunkIndex>>4;
			let xIndex = 1 << (chunkIndex & 15);
			voxelTextureUpdateTree.list[yIndex] |= xIndex;
			voxelTextureUpdateTree.list[8192+(yIndex>>3)] += 1;
			voxelTextureUpdateTree.list[8192+1024+(yIndex>>6)] += 1;

			yIndex = (accelerationIndex>>12);
			accelerationTextureUpdateTree[yIndex] += 1;
		}
	}

	// returns the index to the closest node that could be found
	function get_octree_node(targetX, targetY, targetZ, targetLevel)
	{
		let x = 0, y = 0, z = 0;
		let currentSet = 0;
		let voxelSize = 1 << 10;
		let currentNode = 0;
		let targetVoxelSize = 1 << targetLevel;
		for (;;) {
			let midX = x + voxelSize;
			let midY = y + voxelSize;
			let midZ = z + voxelSize;

			let cube = (targetX >= midX)
				+ ((targetY >= midY) << 1)
				+ ((targetZ >= midZ) << 2);
			currentNode = currentSet + cube;
			
			x += voxelSize & -(targetX >= midX);
			y += voxelSize & -(targetY >= midY);
			z += voxelSize & -(targetZ >= midZ);
			
			if (voxelSize == targetVoxelSize) {
				break;
			}
			
			currentSet = octree.indices[currentNode];
			if (currentSet == 0) {
				break;
			}

			voxelSize >>= 1;
		}
		return {"index": currentNode, "voxelSize": voxelSize, "x": x, "y": y, "z": z};
	}
	// forces node at that position to be made and returns it
	function force_octree_node(targetX, targetY, targetZ, targetLevel)
	{
		let x = 0, y = 0, z = 0;
		let currentSet = 0;
		let voxelSize = 1 << 10;
		let currentNode = 0;
		let targetVoxelSize = 1 << targetLevel;
		for (;;) {
			let midX = x + voxelSize;
			let midY = y + voxelSize;
			let midZ = z + voxelSize;

			let cube = (targetX >= midX)
				+ ((targetY >= midY) << 1)
				+ ((targetZ >= midZ) << 2);
			currentNode = currentSet + cube;
			
			x += voxelSize & -(targetX >= midX);
			y += voxelSize & -(targetY >= midY);
			z += voxelSize & -(targetZ >= midZ);
			
			if (voxelSize == targetVoxelSize) {
				break;
			}
			
			currentSet = octree.indices[currentNode];
			if (currentSet == 0) {
				// allocates new nodes
				currentSet = octree.count;
				octree.indices[currentNode] = currentSet;
				// NOTE: octree.indices[octree.count] -> octree.count+7
				//       MUST be zero!
				octree.count += 8;
			}

			voxelSize >>= 1;
		}
		return {"index": currentNode, "voxelSize": voxelSize, "x": x, "y": y, "z": z};
	}

	function create_voxelrenderer()
	{
		voxelRenderer.voxelData = new Uint32Array(8192*8192);
		voxelRenderer.accelerationData = new Uint32Array(256*256*256);

		voxelRenderer.freeChunks.count = (8192*8192)/(8*8*8);
		voxelRenderer.freeChunks.start = 0; // where the first free chunk is
		voxelRenderer.freeChunks.end = voxelRenderer.freeChunks.count-1;
		voxelRenderer.freeChunks.data =
			new Uint32Array(voxelRenderer.freeChunksAllocationCount);
		
		octree.allocationCount = 4194304; // this is way too much
		octree.indices = new Uint32Array(octree.allocationCount);
		octree.progress = new Uint32Array(octree.allocationCount);
		octree.count = 8;

		// 9344 = 8192+1024+128
		voxelTextureUpdateTree.list = new Uint16Array(9344);
		accelerationTextureUpdateTree.list = new Uint16Array(1118208);

		let chunkCount = (8192*8192)/(8*8*8);
		for (let i = 0; i < chunkCount; i++) {
			voxelRenderer.freeChunks.data[i] = i;
		}

		let dimension = 256;
		
		// set everything to miss
		for (let cz = 0; cz < dimension; cz++) {
		for (let cy = 0; cy < dimension; cy++) {
		for (let cx = 0; cx < dimension; cx++) {
			let index = (cz<<16)|(cy<<8)|cx;
			voxelRenderer.accelerationData[index] = 3221225472;
		}}}
		
		//let rawVoxelData = new Uint32Array(dimension*dimension*dimension);
		//let rawHeightMap = new Uint32Array(dimension*dimension);
		//for (let z = 0, i = 0; z < dimension; z++) {
		//for (let y = 0; y < dimension; y++) {
		//for (let x = 0; x < dimension; x++,i++) {
			//let height = 20+(world.noise_function(x/100, z/100)+1)/2 * 20;
			//if (y < height) {
				//rawVoxelData[i] = Math.floor(Math.random() * 120) + 120;
			//} else {
				//rawVoxelData[i] = 0;
			//}
		//}}}
		///*
		//rawVoxelData[99*dimension*dimension+100*dimension+100] = 150;
		//rawVoxelData[40*dimension*dimension+120*dimension+105] = 150;
		//rawVoxelData[9*dimension*dimension+80*dimension+30] = 150;
		//*/

		/*
		let summedAreaTable = new Uint32Array(dimension*dimension*dimension);
		function access_sumtable_element(xI, yI, zI) {
			return summedAreaTable[(zI<<16)|(yI<<8)|xI];
		}
		function get_sumtable_sum(x1, y1, z1, x2, y2, z2)
		{
			let sum = access_sumtable_element(x2, y2, z2);
			if (z1 > 0) {
				sum -= access_sumtable_element(x2, y2, z1-1);
			}

			if (y1 > 0) {
				sum -= access_sumtable_element(x2, y1-1, z2);
			}
			if (x1 > 0) {
				sum -= access_sumtable_element(x1-1, y2, z2);
			}

			if (x1 > 0 && y1 > 0) {
				sum += access_sumtable_element(x1-1, y1-1, z2);
			}
			if (x1 > 0 && z1 > 0) {
				sum += access_sumtable_element(x1-1, y2, z1-1);
			}
			if (y1 > 0 && z1 > 0) {
				sum += access_sumtable_element(x2, y1-1, z1-1);
			}
			
			if (x1 > 0 && y1 > 0 && z1 > 0) {
				sum -= access_sumtable_element(x1-1, y1-1, z1-1);
			}
			return sum;
		}
		let actualSum = 0;
		for (let z = 0, i = 0; z < dimension; z++) {
		for (let y = 0; y < dimension; y++) {
		for (let x = 0; x < dimension; x++,i++) {
			let sum = rawVoxelData[i] > 0;
			actualSum += sum;
			if (x > 0 && y > 0 && z > 0) {
				sum += access_sumtable_element(x-1, y-1, z-1);
			}
			if (z > 0) {
				sum += access_sumtable_element(x, y, z-1);
			}
			if (y > 0) {
				sum += access_sumtable_element(x, y-1, z);
			}
			if (x > 0) {
				sum += access_sumtable_element(x-1, y, z);
			}
			if (x > 0 && y > 0) {
				sum -= access_sumtable_element(x-1, y-1, z);
			}
			if (y > 0 && z > 0) {
				sum -= access_sumtable_element(x, y-1, z-1);
			}
			if (x > 0 && z > 0) {
				sum -= access_sumtable_element(x-1, y, z-1);
			}

			summedAreaTable[(z<<16)|(y<<8)|x] = sum;
		}}}
		*/

		/*
		for (let z = 0, i = 0; z < dimension; z++) {
		for (let y = 0; y < dimension; y++) {
		for (let x = 0; x < dimension; x++,i++) {
			let voxel = rawVoxelData[i];
			if (voxel == 0) {
				voxel = 1 << 31;
			}
			rawVoxelData[i] = voxel;
		}}}
		*/

		voxelRenderer.acceleration.x1 = 0;
		voxelRenderer.acceleration.y1 = 0;
		voxelRenderer.acceleration.z1 = 0;
		voxelRenderer.acceleration.x2 = 32;
		voxelRenderer.acceleration.y2 = 32;
		voxelRenderer.acceleration.z2 = 32;

		let midX = Math.floor(cameraPos[0]/8);
		let midY = Math.floor(cameraPos[1]/8);
		let midZ = Math.floor(cameraPos[2]/8);

		let startX = (midX-8) < 1 ? 1 : midX-8;
		let startY = (midY-8) < 1 ? 1 : midY-8;
		let startZ = (midZ-8) < 1 ? 1 : midZ-8;

		let endX = (midX+7) > 254 ? 254 : midX+7;
		let endY = (midY+7) > 254 ? 254 : midY+7;
		let endZ = (midZ+7) > 254 ? 254 : midZ+7;

		generateChunksFunctionTargetDistance2 = 128*128;
		generate_chunks(0, 0, 0, 0, 1 << 10);

		voxelRenderer.voxelTexture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, voxelRenderer.voxelTexture);
		gl.texImage2D(
			gl.TEXTURE_2D,
			0,
			gl.R32UI,
			8192,
			8192,
			0, // border
			gl.RED_INTEGER,
			gl.UNSIGNED_INT,
			voxelRenderer.voxelData
		);
		
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

		voxelRenderer.accelerationTexture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, voxelRenderer.accelerationTexture);
		gl.texImage2D(
			gl.TEXTURE_2D,
			0,
			gl.R32UI,
			4096,
			4096,
			0, // border
			gl.RED_INTEGER,
			gl.UNSIGNED_INT,
			voxelRenderer.accelerationData
		);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		
		let vertexShaderSource = data.vertexShaderSource;
		let fragmentShaderSource = data.fragmentShaderSource;
		
		let vertexShader = create_shader(gl.VERTEX_SHADER, vertexShaderSource);
		let fragmentShader = create_shader(gl.FRAGMENT_SHADER, fragmentShaderSource);
		voxelRenderer.program = create_program(vertexShader, fragmentShader);

		voxelRenderer.voxelTextureLocation = gl.getUniformLocation(voxelRenderer.program, "voxelTexture");
		voxelRenderer.accelerationTextureLocation = gl.getUniformLocation(
			voxelRenderer.program,
			"accelerationTexture"
		);
		voxelRenderer.invResolutionLocation = gl.getUniformLocation(voxelRenderer.program, "invResolution");
		voxelRenderer.positionLocation = gl.getUniformLocation(voxelRenderer.program, "position");
		voxelRenderer.vert0Location = gl.getUniformLocation(voxelRenderer.program, "vert0");
		voxelRenderer.vert1Location = gl.getUniformLocation(voxelRenderer.program, "vert1");
		voxelRenderer.vert2Location = gl.getUniformLocation(voxelRenderer.program, "vert2");
		voxelRenderer.vert3Location = gl.getUniformLocation(voxelRenderer.program, "vert3");

		gl.useProgram(voxelRenderer.program);
		gl.uniform1i(voxelRenderer.voxelTextureLocation, 0);
		gl.uniform1i(voxelRenderer.accelerationTextureLocation, 1);

		voxelRenderer.setupComplete = true;
	

		gl.disable(gl.CULL_FACE);

		window.requestAnimationFrame(draw, true);
	}

	function create_shader(type, source)
	{
		let shader = gl.createShader(type);
		gl.shaderSource(shader, source);
		gl.compileShader(shader);
		if (gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
			return shader;
		} else {
			console.log("error compiling shader: " + gl.getShaderInfoLog(shader));
		}
	}

	function create_program(vertexShader, fragmentShader)
	{
		let program = gl.createProgram();
		gl.attachShader(program, vertexShader);
		gl.attachShader(program, fragmentShader);
		gl.linkProgram(program);
		if (gl.getProgramParameter(program, gl.LINK_STATUS)) {
			return program;
		} else {
			console.log("error making shader program: " + gl.getProgramInfoLog(program));
		}
	}

	function lock_cursor(element)
	{
		if (!(element === document.pointerLockElement)) {
			element.requestPointerLock();
		}
	}

	function normalize_vec3(input)
	{
		let lengthINV = 1/Math.sqrt(input[0]*input[0]+input[1]*input[1]+input[2]*input[2]);
		return [input[0]*lengthINV, input[1]*lengthINV, input[2]*lengthINV];
	}

	function cross_vec3(a, b)
	{
		return [
			a[1] * b[2] - a[2] * b[1],
			a[2] * b[0] - a[0] * b[2],
			a[0] * b[1] - a[1] * b[0]
		];
	}

	function scale_vec3_by_constant(a, c)
	{
		return [
			a[0] * c,
			a[1] * c,
			a[2] * c,
		];
	}

	function add_vec3(a, b)
	{
		return [
			a[0]+b[0],
			a[1]+b[1],
			a[2]+b[2],
		];
	}

	function sub_vec3(a, b)
	{
		return [
			a[0]-b[0],
			a[1]-b[1],
			a[2]-b[2],
		];
	}
</script>
