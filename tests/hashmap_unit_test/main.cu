#include <iostream>
#include <stdlib.h>
#include <chrono>
#include <random>
#include "../../include/hashinator/hashinator.h"
#include <gtest/gtest.h>
#include <cuda/std/utility>

#define BLOCKSIZE 32
#define expect_true EXPECT_TRUE
#define expect_false EXPECT_FALSE
#define expect_eq EXPECT_EQ

using namespace std::chrono;
using namespace Hashinator;
typedef uint32_t val_type;
typedef uint32_t key_type;
typedef split::SplitVector<cuda::std::pair<key_type,val_type>,split::split_unified_allocator<cuda::std::pair<val_type,val_type>>,split::split_unified_allocator<size_t>> vector ;
typedef Hashmap<key_type,val_type> hashmap;


template <class Fn, class ... Args>
auto execute_and_time(const char* name,Fn fn, Args && ... args) ->bool{
   std::chrono::time_point<std::chrono::_V2::system_clock, std::chrono::_V2::system_clock::duration> start,stop;
   double total_time=0;
   start = std::chrono::high_resolution_clock::now();
   bool retval=fn(args...);
   stop = std::chrono::high_resolution_clock::now();
   auto duration = duration_cast<microseconds>(stop- start).count();
   total_time+=duration;
   std::cout<<name<<" took "<<total_time<<" us"<<std::endl;
   return retval;
}


void create_input(vector& src, uint32_t bias=0){
   for (size_t i=0; i<src.size(); ++i){
      cuda::std::pair<key_type,val_type>& kval=src.at(i);
      kval.first=i + bias;
      kval.second=rand()%1000000;
   }
}


void cpu_write(hashmap& hmap, vector& src){
   for (size_t i=0; i<src.size(); ++i){
      const cuda::std::pair<key_type,val_type>& kval=src.at(i);
      hmap.at(kval.first)=kval.second;
   }
}

__global__ 
void gpu_write(hashmap* hmap, cuda::std::pair<key_type,val_type>*src, size_t N){
   size_t index = blockIdx.x * blockDim.x + threadIdx.x;
   if (index < N ){
      //hmap->set_element(src[index].first, src[index].second);
      hmap->device_insert(cuda::std::make_pair(src[index].first, src[index].second));
   }
}

__global__
void gpu_delete_even(hashmap* hmap, cuda::std::pair<key_type,val_type>*src,size_t N){
   size_t index = blockIdx.x * blockDim.x + threadIdx.x;
   if (index<N ){
      auto kpos=hmap->device_find(src[index].first);
      if (kpos==hmap->device_end()){assert(0 && "Catastrophic crash in deletion");}
      if (kpos->second %2==0 ){
         hmap->device_erase(kpos);
      }
   }
   return;
}

__global__
void gpu_recover_all_elements(hashmap* hmap,cuda::std::pair<key_type,val_type>* src,size_t N  ){
   size_t index = blockIdx.x * blockDim.x + threadIdx.x;
   if (index < N ){
      key_type key= src[index].first;
      val_type val= src[index].second;
      auto it=hmap->device_find(key);
      if (it==hmap->device_end()){
         printf("END FOUND DEVICE\n");
         assert( 0 && "Failed in GPU RECOVER ALL ");
      }
      if (it->first!=key || it->second!=val){
         assert( 0 && "Failed in GPU RECOVER ALL ");
      }
   }
   return;
}


__global__
void gpu_recover_odd_elements(hashmap* hmap,cuda::std::pair<key_type,val_type>* src,size_t N ){
   size_t index = blockIdx.x * blockDim.x + threadIdx.x;
   if (index < N ){
      key_type key= src[index].first;
      val_type val= src[index].second;
      if (val%2!=0){
         auto it=hmap->device_find(key);
         if (it==hmap->device_end()){
            assert( 0 && "Failed in GPU RECOVER ALL ");
         }
         if (it->first!=key || it->second!=val){
            assert( 0 && "Failed in GPU RECOVER ALL ");
         }
      }
   }
   return;
}

bool recover_odd_elements(const hashmap& hmap, vector& src){
   for (size_t i=0; i<src.size(); ++i){
      const cuda::std::pair<key_type,val_type>& kval=src.at(i);
      if (kval.second%2!=0){
         auto retval=hmap.find(kval.first);
         if (retval==hmap.end()){return false;}
         bool sane=retval->first==kval.first  &&  retval->second== kval.second ;
         if (!sane){ 
            return false; 
         }
      }
   }
   return true;
}

bool recover_all_elements(const hashmap& hmap, vector& src){
   for (size_t i=0; i<src.size(); ++i){
      const cuda::std::pair<key_type,val_type>& kval=src.at(i);
      auto retval=hmap.find(kval.first);
      if (retval==hmap.end()){return false;}
      bool sane=retval->first==kval.first  &&  retval->second== kval.second ;
      if (!sane){ 
         return false; 
      }
   }
   return true;
}

bool recover_odd_elements(hashmap* hmap, vector& src){
   for (size_t i=0; i<src.size(); ++i){
      const cuda::std::pair<key_type,val_type>& kval=src.at(i);
      if (kval.second%2!=0){
         auto retval=hmap->find(kval.first);
         if (retval==hmap->end()){return false;}
         bool sane=retval->first==kval.first  &&  retval->second== kval.second ;
         if (!sane){ 
            return false; 
         }
      }
   }
   return true;
}

bool recover_all_elements(hashmap* hmap, vector& src){
   for (size_t i=0; i<src.size(); ++i){
      const cuda::std::pair<key_type,val_type>& kval=src.at(i);
      auto retval=hmap->find(kval.first);
      if (retval==hmap->end()){return false;}
      bool sane=retval->first==kval.first  &&  retval->second== kval.second ;
      if (!sane){ 
         return false; 
      }
   }
   return true;
}

bool test_hashmap_1(int power){
   size_t N = 1<<power;
   size_t blocksize=BLOCKSIZE;
   size_t blocks=2*N/blocksize;

   bool cpuOK=true;

   //Create some input data
   vector src(N);
   create_input(src);
   hashmap hmap;
   hashmap* d_hmap;
   hmap.resize(power+1);

   //Upload to device and insert input
   d_hmap=hmap.upload();
   gpu_write<<<blocks,blocksize>>>(d_hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   hmap.download();

   //Verify all elements
   cpuOK=recover_all_elements(hmap,src);
   gpu_recover_all_elements<<<blocks,blocksize>>>(d_hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   return true;
   if (!cpuOK){
      return false;
   }

   //Delete some selection of the source data
   d_hmap=hmap.upload();
   gpu_delete_even<<<blocks,blocksize>>>(d_hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   hmap.download();

   //Quick check to verify there are no even elements
   for (const auto& kval : hmap){
      if (kval.second%2==0){
         std::cerr<<kval.first<<" "<<kval.second<<std::endl;
         return false;
      }
   }
   
   //Verify odd elements;
   cpuOK=recover_odd_elements(hmap,src);
   gpu_recover_odd_elements<<<blocks,blocksize>>>(d_hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   if (!cpuOK){
      return false;
   }

   //Reinsert so that we can also test duplicate insertion
   d_hmap=hmap.upload();
   gpu_write<<<blocks,blocksize>>>(d_hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   //Download
   hmap.download();


   //Verify all elements
   cpuOK=recover_all_elements(hmap,src);
   gpu_recover_all_elements<<<blocks,blocksize>>>(d_hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   if (!cpuOK ){
      return false;
   }

   //If we made it to here we should be ok 
   return true;
}


bool test_hashmap_2(int power){
   size_t N = 1<<power;
   size_t blocksize=BLOCKSIZE;
   size_t blocks=2*N/blocksize;
   bool cpuOK=true;

   //Create some input data
   vector src(N);
   create_input(src);


   hashmap* hmap = new hashmap();
   hmap->resize(power++);

   //Upload to device and insert input
   gpu_write<<<blocks,blocksize>>>(hmap,src.data(),src.size());
   cudaDeviceSynchronize();

   //Verify all elements
   cpuOK=recover_all_elements(hmap,src);
   gpu_recover_all_elements<<<blocks,blocksize>>>(hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   if (!cpuOK ){
      return false;
   }

   //Delete some selection of the source data
   gpu_delete_even<<<blocks,blocksize>>>(hmap,src.data(),src.size());
   cudaDeviceSynchronize();

   //Quick check to verify there are no even elements
   for (const auto& kval : *hmap){
      if (kval.second%2==0){
         std::cerr<<kval.first<<" "<<kval.second<<std::endl;
         return false;
      }
   }
   
   //Verify odd elements;
   cpuOK=recover_odd_elements(hmap,src);
   gpu_recover_odd_elements<<<blocks,blocksize>>>(hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   if (!cpuOK){
      return false;
   }

   //Reinsert so that we can also test duplicate insertion
   gpu_write<<<blocks,blocksize>>>(hmap,src.data(),src.size());
   cudaDeviceSynchronize();

   //Verify all elements
   cpuOK=recover_all_elements(hmap,src);
   gpu_recover_all_elements<<<blocks,blocksize>>>(hmap,src.data(),src.size());
   cudaDeviceSynchronize();
   if (!cpuOK ){
      return false;
   }


   delete hmap;
   hmap=nullptr;
   return true;

}





TEST(HashmapUnitTets , Host_Device_Insert_Delete_Global_Tets){
   for (int power=5; power<6; ++power){
      std::string name= "Power= "+std::to_string(power);
      bool retval = execute_and_time(name.c_str(),test_hashmap_2 ,power);
      expect_true(retval);
   }
}

int main(int argc, char* argv[]){
   srand(time(NULL));
   ::testing::InitGoogleTest(&argc, argv);
   return RUN_ALL_TESTS();
}
