// g++ -g -Wall -Wextra -Werror -Wno-subobject-linkage -march=native -O3 src/tools/tree_r.cpp -Isrc/poseidon -Ideps/sppark -Ideps/blst/src -L deps/blst -lblst

// Only supports constant arity 8 throughout the tree (2KB, 32G, etc);
//
// arguments
//  last_layer_filename
// optional arguments
//  data_filename - This indicates whether or not we have a CC sector

#include <cstdint>             // uint*
#include <sys/mman.h>          // mapping
#include <sys/stat.h>          // file stats
#include <cassert>             // assertions
#include <cmath>               // log2
#include <fcntl.h>             // file open
#include <unistd.h>            // file close
#include <iostream>            // printing
#include <iomanip>             // printing
#include <chrono>              // time

#ifdef __NVCC__
// Enable GPU tree-r building
#include "../pc2/cuda/pc2.cu"
#else
// CPU only
#include <ff/bls12-381.hpp>
#endif
#ifndef __CUDA_ARCH__
#include "../pc1/tree_r.hpp"
#include "../util/debug_helpers.cpp"
#include "../sealing/sector_parameters.cpp"

void usage(char* argv[]) {
  std::cout << "If no staged data file, CC is assumed" << std::endl;
  std::cout << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
  std::cout << "-h        Print help message" << std::endl;
  std::cout << "-c <int>  Parallel number of cores" << std::endl;
  std::cout << "-l <path> Last layer file" << std::endl;
  std::cout << "-d <path> Staged data file" << std::endl;
  std::cout << "-o <path> Output directory" << std::endl;
  exit(0);
}

#ifdef __NVCC__
void gpu_tree_r(std::string config_filename,
                std::string last_layer_filename,
                std::string data_filename,
                std::string output_dir) {
  topology_t topology(config_filename.c_str());
  set_core_affinity(topology.pc2_hasher);

  // Get the sector size
  size_t sector_size = SECTOR_SIZE;
  SectorParameters params(sector_size);
        
  // Total number of streams across all GPUs
  size_t stream_count = 64;

  // Batch size in nodes. Each node includes all parallel sectors
  // TODO: optimize batch size
  size_t batch_size = 64;

  // Nodes to read per partition
  size_t nodes_to_read = params.GetNumNodes() / params.GetNumTreeRCFiles();

  std::vector<std::string> layer_filenames;
  layer_filenames.push_back(last_layer_filename);
  streaming_node_reader_t<sealing_config1_t> node_reader
    (params, layer_filenames, sector_size);
  
  // Allocate storage for 2x the streams to support tree-c and tree-r
  node_reader.alloc_slots(stream_count * 2, params.GetNumLayers() * batch_size, true);

  bool tree_r_only = true;
  const char* data_filenames[1];
  if (!data_filename.empty()) {
    data_filenames[0] = data_filename.c_str();
  } else {
    data_filenames[0] = nullptr;
  }
  pc2_hash<sealing_config1_t>(params, topology, tree_r_only, node_reader,
                              nodes_to_read, batch_size, stream_count,
                              data_filenames, output_dir.c_str());
}
#endif

int main(int argc, char* argv[]) {
  int  opt   = 0;
  std::string last_layer_filename = "";
  std::string data_filename       = "";
  std::string out_dir             = "";
  int cores                       = 0;
  std::string config_filename     = "demos/rust/supra_seal.cfg";
  
  while ((opt = getopt(argc, argv, "l:d:o:c:h")) != -1) {
    switch(opt) {
      case 'c':
       std::cout << "number of cores input " << optarg << std::endl;
       cores = atoi(optarg);
       break;
      case 'l':
        std::cout << "last_layer_filename input " << optarg << std::endl;
        last_layer_filename = optarg;
        break;
      case 'd':
        std::cout << "data_filename input       " << optarg << std::endl;
        data_filename = optarg;
        break;
      case 'o':
        std::cout << "out_dir                   " << optarg << std::endl;
        out_dir = optarg;
        break;
      case 'h':
      case ':':
      case '?':
        usage(argv);
        break;
    }
  }

  if (last_layer_filename.empty()) {
    printf("-l <last_layer_file> must be specified\n");
    usage(argv);
  }
  
#ifdef __NVCC__
  if (ngpus()) {
    gpu_tree_r(config_filename, last_layer_filename, data_filename, out_dir);
    return 0;
  }
#endif
  TreeR tree_r;
  tree_r.BuildTreeR(last_layer_filename, data_filename, out_dir, cores);
  return 0;
}
#endif
