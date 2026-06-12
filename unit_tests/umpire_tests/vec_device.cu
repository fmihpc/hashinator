#include <iostream>
#include <stdlib.h>
#include <chrono>
#include <gtest/gtest.h>
#include "../../include/splitvector/splitvec.h"
#include "../../include/splitvector/split_tools.h"
#include "umpire/Allocator.hpp"
#include "umpire/ResourceManager.hpp"
#include "umpire/TypedAllocator.hpp"


#define expect_true EXPECT_TRUE
#define expect_false EXPECT_FALSE
#define expect_eq EXPECT_EQ
#define N 1<<12

static umpire::TypedAllocator<int>* vector_alloc;
typedef split::SplitVector<int,umpire::TypedAllocator<int>> vec ;
// typedef split::SplitVector<int,split::split_unified_allocator<int>> vec ;


__global__
void add_vectors(vec* a , vec* b,vec* c){

   int index = blockIdx.x * blockDim.x + threadIdx.x;
   if (index< a->size()){
      c->at(index)=a->at(index)+b->at(index);
   }

}


__global__
void resize_vector(vec* a , int size){
   a->device_resize(size);
}


__global__
void push_back_kernel(vec* a){

   int index = blockIdx.x * blockDim.x + threadIdx.x;
   a->device_push_back(index);
}

__global__
void merge_kernel(vec* a,vec *b ){

   int index = blockIdx.x * blockDim.x + threadIdx.x;
   if (index==0){
      a->device_insert(a->end(),b->begin(),b->end());
   }
}

__global__
void merge_kernel_2(vec* a){

   int index = blockIdx.x * blockDim.x + threadIdx.x;
   if (index==0){
      a->device_insert(a->begin()++,3,42);
   }
}

__global__
void erase_kernel(vec* a){
   auto it=a->begin();
   a->erase(it);
   
}



void print_vec_elements(const vec& v){
   std::cout<<"****Vector Contents********"<<std::endl;
   std::cout<<"Size= "<<v.size()<<std::endl;
   std::cout<<"Capacity= "<<v.capacity()<<std::endl;
   for (const auto i:v){
      std::cout<<i<<" ";
   }

   std::cout<<"\n****~Vector Contents********"<<std::endl;
}

TEST(Test_GPU,VectorAddition){
   vec a(N,1,*vector_alloc);
   vec b(N,2,*vector_alloc);
   vec c(N,0,*vector_alloc);
   
   vec* d_a=a.upload();
   vec* d_b=b.upload();
   vec* d_c=c.upload();

   add_vectors<<<N,32>>>(d_a,d_b,d_c);
   SPLIT_CHECK_ERR( split_gpuDeviceSynchronize() );

   for (const auto& e:c){
      expect_true(e==3);
   }


}

TEST(Constructors,Default){
   vec a(*vector_alloc);
   expect_true(a.size()==0 && a.capacity()==0);
   expect_true(a.data()==nullptr);
}

TEST(Constructors,Size_based){
   vec a(N,*vector_alloc);
   expect_true(a.size()==N && a.capacity()==N);
   expect_true(a.data()!=nullptr);
}


TEST(Constructors,Specific_Value){
   vec a(N,5,*vector_alloc);
   expect_true(a.size()==N && a.capacity()==N);
   for (size_t i=0; i<N;i++){
      expect_true(a[i]==5);
      expect_true(a.at(i)==5);
   }
}

TEST(Constructors,Copy){
   vec a(N,5,*vector_alloc);
   vec b(a);
   for (size_t i=0; i<N;i++){
      expect_true(a[i]==b[i]);
      expect_true(a.at(i)==b.at(i));
   }
}

TEST(Vector_Functionality , Reserve){
   vec a(*vector_alloc);
   size_t cap =1000000;
   a.reserve(cap);
   expect_true(a.size()==0);
   expect_true(a.capacity()==cap);
}

TEST(Vector_Functionality , Resize){
   vec a(*vector_alloc);
   size_t size =1<<20;
   a.resize(size);
   expect_true(a.size()==size);
   expect_true(a.capacity()==a.size());
}

TEST(Vector_Functionality , Swap){
   vec a(10,2,*vector_alloc);
   vec b(10,2,*vector_alloc);
   a.swap(b);
   vec c(100,1,*vector_alloc);
   vec d (200,3,*vector_alloc);
   c.swap(d);
   expect_true(c.size()==200);
   expect_true(d.size()==100);
   expect_true(c.front()==3);
   expect_true(d.front()==1);

}

TEST(Vector_Functionality , Resize2){
   vec a(*vector_alloc);
   size_t size =1<<20;
   a.resize(size);
   expect_true(a.size()==size);
   expect_true(a.capacity()==a.size());
}

TEST(Vector_Functionality , Clear){
   vec a(10,*vector_alloc);
   size_t size =1<<20;
   a.resize(size);
   expect_true(a.size()==size);
   auto cap=a.capacity();
   a.clear();
   expect_true(a.size()==0);
   expect_true(a.capacity()==cap);
}


TEST(Vector_Functionality , Push_Back){
   vec a(*vector_alloc);
   for (auto i=a.begin(); i!=a.end();i++){
      expect_true(false);
   }

   size_t initial_size=a.size();
   size_t initial_cap=a.capacity();

   a.push_back(11);
   expect_true(11==a[a.size()-1]);
   a.push_back(12);
   expect_true(12==a[a.size()-1]);

}


TEST(Vector_Functionality , Shrink_to_Fit){
   vec a(*vector_alloc);
   for (auto i=a.begin(); i!=a.end();i++){
      expect_true(false);
   }

   size_t initial_size=a.size();
   size_t initial_cap=a.capacity();

   for (int i =0 ; i< 1024; i++){
      a.push_back(i);
   }

   expect_true(a.size()<a.capacity());
   a.shrink_to_fit();
   expect_true(a.size()==a.capacity());

}

TEST(Vector_Functionality , PushBack_And_Erase_Device){
      vec a(*vector_alloc);
      a.reserve(100);
      vec* d_a=a.upload();
      push_back_kernel<<<4,8>>>(d_a);
      SPLIT_CHECK_ERR( split_gpuDeviceSynchronize() );
      vec* d_b=a.upload();
      erase_kernel<<<1,1>>>(d_b);
      SPLIT_CHECK_ERR( split_gpuDeviceSynchronize() );
}



TEST(Vector_Functionality , Resizing_Device){

   {
      vec a(32,42,*vector_alloc);
      expect_true(a.size()==a.capacity());
      a.resize(16);
      expect_true(a.size()==16);
      expect_true(a.capacity()==32);
   }

   {
      vec a(32,42,*vector_alloc);
      expect_true(a.size()==a.capacity());
      vec* d_a=a.upload();
      resize_vector<<<1,1>>>(d_a,16);
      SPLIT_CHECK_ERR( split_gpuDeviceSynchronize() );
      expect_true(a.size()==16);
      expect_true(a.capacity()==32);
   }


   {
      vec a(32,42,*vector_alloc);
      expect_true(a.size()==a.capacity());
      a.reserve(100);
      expect_true(a.capacity()>100);
      vec* d_a=a.upload();
      resize_vector<<<1,1>>>(d_a,64);
      SPLIT_CHECK_ERR( split_gpuDeviceSynchronize() );
      expect_true(a.size()==64);
      expect_true(a.capacity()>100);
      for (size_t i = 0 ; i< a.size(); ++i){
         a.at(i)=3;
         expect_true(a.at(i)=3);
      }
   }
}

TEST(Vector_Functionality , Test_CopyMetaData){

   vec a(32,42,*vector_alloc);
   expect_true(a.size()==a.capacity());
   a.resize(16);
   expect_true(a.size()==16);
   expect_true(a.capacity()==32);
   split::SplitInfo* info;
   SPLIT_CHECK_ERR( split_gpuMallocHost((void **) &info, sizeof(split::SplitInfo)) );
   a.copyMetadata(info);
   SPLIT_CHECK_ERR( split_gpuDeviceSynchronize() );
   expect_true(a.capacity()==info->capacity);
   expect_true(a.size()==info->size);
}


int main(int argc, char* argv[]){
   auto& rm = umpire::ResourceManager::getInstance();
   umpire::Allocator alloc = rm.getAllocator("UM");
   auto va=umpire::TypedAllocator<int>(alloc);   
   vector_alloc=&va;
   ::testing::InitGoogleTest(&argc, argv);
   return RUN_ALL_TESTS();
}
