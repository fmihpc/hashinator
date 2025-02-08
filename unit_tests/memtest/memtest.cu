#include "memtest.h"

/* Memory reporting function
 */
void gpu_reportMemory(const std::vector<testStructure> &objs) {
   size_t o_size=0;
   size_t o_cap=0;
   const size_t nob=objs.size();
   std::cerr<<" nob "<<nob<<std::endl;
   for (uint i=0; i<nob; ++i) {
      o_size += (objs.at(i)).size();
      o_cap += (objs.at(i)).capacity();
   }
   
   size_t free_byte ;
   size_t total_byte ;
   SPLIT_CHECK_ERR( cudaMemGetInfo( &free_byte, &total_byte) );
   size_t used_mb = (total_byte-free_byte)/(1024*1024);
   std::cerr<<" =================================="<<std::endl;
   std::cerr<<" GPU Memory report"<<std::endl;
   std::cerr<<"   objects size:            "<<o_size/(1024*1024)<<" Mbytes"<<std::endl;
   std::cerr<<"   objects capacity:        "<<o_cap/(1024*1024)<<" Mbytes"<<std::endl;
   std::cerr<<"   Reported Hardware use:   "<<used_mb<<" Mbytes"<<std::endl;
   std::cerr<<" =================================="<<std::endl;
   return;
}

TEST(Test_GPU,Memory) {
   //int myDevice;
   const int n_objs = 20;
   const int n_loops = 27; 

   std::vector<testStructure> storage;

   std::cerr<<"pre-init"<<std::endl;
   gpu_reportMemory(storage);
   
   //SPLIT_CHECK_ERR( gpuGetDevice(&myDevice) );
   for (uint i=0; i<n_objs; ++i) {
      std::cerr<<"init "<<i<<std::endl;
      storage.push_back(testStructure());
   }

   std::cerr<<"initial"<<std::endl;
   const int initial_size = storage[0].size();
   gpu_reportMemory(storage);

   //final_size = std::pow(2,35) // 34 gigs
   for (uint j=0; j<n_loops; ++j) {
      size_t newSize = initial_size * std::pow(2,j);

      std::cerr<<" recapacitate "<<j<<std::endl;
      for (uint i=0; i<n_objs; ++i) {
         storage[i].recapacitate(newSize);
      }
      gpu_reportMemory(storage);
      
      std::cerr<<" resize "<<j<<std::endl;
      for (uint i=0; i<n_objs; ++i) {
         storage[i].resize(newSize-1);
      }
      gpu_reportMemory(storage);
   }

   SPLIT_CHECK_ERR( split_gpuDeviceSynchronize() );
   EXPECT_TRUE(true);
}
__host__ int main(int argc, char* argv[]) {
   ::testing::InitGoogleTest(&argc, argv);
   return RUN_ALL_TESTS();
}

