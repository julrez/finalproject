const http = require('http');
const fs = require('fs');
const {exec} = require('child_process');

let files = {};

let debugFlags = " -O0 -s ASSERTIONS -sSAFE_HEAP=1 -sSAFE_HEAP_LOG=1";
let releaseFlags = " -O3";

//let memoryFlags = " -s ALLOW_MEMORY_GROWTH=0 -s INITIAL_MEMORY=4294901760";
let memoryFlags = " -s ALLOW_MEMORY_GROWTH=1";
let emccFlags = "-Wall -Wextra -Wdouble-promotion -msimd128 -pthread -s WASM=1 -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency -s EXPORTED_RUNTIME_METHODS=[\"cwrap\",\"ccall\"] -s EXPORTED_FUNCTIONS=[\"_malloc\"]" + memoryFlags + releaseFlags;

function get_filenames(callback)
{
}

// compiles wasm
function compile_files(callback)
{
	let filenames;
	fs.readdir("src/", (dirError, dirData) => {
		filenames = dirData;
		let finishCount = 0;
		for (let i = 0; i < filenames.length; i++) {
			let type = filenames[i].split('.').pop();
			if (type == 'c') {
				let command = "emcc " + emccFlags + " src/" + filenames[i] + " -o out/" + filenames[i] + ".js";
				console.log("compiling: " + filenames[i]);
				console.log(command);
				exec(command, (error, stdout, stderr) => {
					console.log(stderr);
					console.log(stdout);
					finishCount++;
					check_finish_count();
				});
			} else {
				finishCount++;
			}
			check_finish_count();
		}
		function check_finish_count()
		{
			if (finishCount == filenames.length) {
				callback();
			}
		}
	});
}

function read_files(callback)
{
	let filenames = [];
	fs.readdir("src/", (dirError1, srcFiles) => {
		for (let i = 0; i < srcFiles.length; i++) {
			filenames.push("src/"+srcFiles[i]);
		}
		fs.readdir("out/", (dirError2, outFiles) => {
			for (let i = 0; i < outFiles.length; i++) {
				filenames.push("out/"+outFiles[i]);
			}
			fs.readdir("static/", (dirError2, staticFiles) => {
				for (let i = 0; i < staticFiles.length; i++) {
					filenames.push("static/"+staticFiles[i]);
				}
				after_dirs_read();
			});
		});
	});
	function after_dirs_read()
	{
		let fileLoadGoal = filenames.length;
		let fileLoadCurr = 0;
		for (let i = 0; i < filenames.length; i++) {
			let path = filenames[i];

			fs.readFile(path, (fileError, fileData) => {
				if (fileError) {
					console.log("ERROR: " + path + " not found");
				}
				files[path] = fileData;
				fileLoadCurr++;

				if (fileLoadGoal == fileLoadCurr) {
					callback();
				}
			});
		}
	}
}

function create_server() {
	console.log("address: \x1b[32mlocalhost:8080\x1b[0m");
	http.createServer(function (request, response) {
		if (request.url == '/') {
			response.writeHead(200, {
				'Content-Type': 'text/html',
				'Cross-Origin-Embedder-Policy': 'require-corp',
				'Cross-Origin-Opener-Policy': 'same-origin',
			});
			response.write(files['src/index.html']);
			return response.end();
		} else {
			let type = request.url.split('.').pop();
			let headers = {}
			switch (type) {
				case 'js':
					headers = {
						'Content-Type': 'text/javascript',
						'Cross-Origin-Embedder-Policy': 'require-corp'
					};
					break;
				case 'css':
					headers = {'Content-Type': 'text/css'};
					break;
				case 'wasm':
					headers = {'Content-Type': 'application/wasm'};
					break;
				case 'png':
					headers = {'Content-Type': 'image/png'};
					break;
				default:
					headers = {'Content-Type': 'text/plain'};
					break;
			}
			let returnedData = files[request.url.slice(1)];
			if (returnedData == undefined) {
				response.writeHead(404, { });
				return response.end();
			} else {
				response.writeHead(200, headers);
				response.write(returnedData);
				return response.end();
			}
		}
	}).listen(8080);
}

function option_dev()
{
	stage_compile_files();
	function stage_compile_files()
	{
		console.log("\x1b[1mStarting compilation stage\x1b[0m");
		compile_files(stage_load_files);
	}
	function stage_load_files()
	{
		console.log("\x1b[1mLoading files\x1b[0m");
		read_files(stage_create_server);
	}
	function stage_create_server()
	{
		console.log("\x1b[1mStarting server\x1b[0m");
		create_server();
	}
}

console.log(process.argv);
option_dev();
