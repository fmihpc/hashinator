#include <iostream>
#include <stdlib.h>
#include <chrono>
#include <vector>
#include <gtest/gtest.h>
#ifndef SPLIT_CPU_ONLY_MODE
#define  SPLIT_CPU_ONLY_MODE
#endif
#include "../../include/splitvector/splitvec.h"
#include "umpire/Allocator.hpp"
#include "umpire/ResourceManager.hpp"
#include "umpire/TypedAllocator.hpp"


#define expect_true EXPECT_TRUE
#define expect_false EXPECT_FALSE
#define expect_eq EXPECT_EQ
#define N 1<<12



static umpire::TypedAllocator<int>* vector_alloc;
typedef split::SplitVector<int,umpire::TypedAllocator<int>> vec ;
typedef std::vector<int,umpire::TypedAllocator<int>> stdvec ;
typedef split::SplitVector<int,umpire::TypedAllocator<int>>::iterator   split_iterator;



template<typename VECTOR>
void print_vec_elements(VECTOR& v){
   std::cout<<"****Vector Contents********"<<std::endl;
   std::cout<<"Size= "<<v.size()<<std::endl;
   std::cout<<"Capacity= "<<v.capacity()<<std::endl;
   for (auto i:v){
      std::cout<<i<<",";
   }
   std::cout<<std::endl;
   std::cout<<"****~Vector Contents********"<<std::endl;
}

TEST(Constructors,Move){
   vec b(*vector_alloc);
   for (size_t i=0; i<N;i++){
      b.push_back(2);
   }
   for (size_t i=0 ; i<N; ++i){
      expect_true(b[i]=2);
   }
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


TEST(Vector_Functionality , Reserve){
   vec a(*vector_alloc);
   size_t cap =1000000;
   a.reserve(cap);
   expect_true(a.size()==0);
   expect_true(a.capacity()==cap);
}

TEST(Vector_Functionality , Reserve2){

   for (int i =1; i<100; i++){
      vec a(N,i,*vector_alloc);
      vec b(a);

      size_t cap =32*N;
      a.reserve(cap);
      expect_true(a==b);
   }
}

TEST(Vector_Functionality , Resize){
   vec a(*vector_alloc);
   size_t size =1<<20;
   a.resize(size);
   expect_true(a.size()==size);
   expect_true(a.capacity()==a.size());
}

TEST(Vector_Functionality , Swap){
   vec a(10,2,*vector_alloc),b(10,2,*vector_alloc);
   a.swap(b);
   expect_true(a==b);
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

int main(int argc, char* argv[]){
   auto& rm = umpire::ResourceManager::getInstance();
   umpire::Allocator alloc = rm.getAllocator("HOST");
   auto va=umpire::TypedAllocator<int>(alloc);   
   vector_alloc=&va;
   ::testing::InitGoogleTest(&argc, argv);
   return RUN_ALL_TESTS();
}

