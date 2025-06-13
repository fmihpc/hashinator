#include <iostream>
#include <stdlib.h>
//#include <chrono>
#include "../../include/splitvector/splitvec.h"
#include "../../include/splitvector/split_tools.h"
#include <gtest/gtest.h>

#define CHK_ERR(err) (cuda_error(err, __FILE__, __LINE__))

typedef split::SplitVector<int,split::split_unified_allocator<int>> vec ;


struct testStructure {
   testStructure(const size_t initSize=100) {
      std::cerr<<"a"<<std::endl;
      testContent = vec(initSize);
      std::cerr<<"b"<<std::endl;
   };
   size_t capacity() const {
      return testContent.capacity();
   };
   size_t size() const {
      return testContent.size();
   };
   void shrink_to_fit() {
      testContent.shrink_to_fit();
   };
   void shrink_to_fit_2() {
      vec testContent_new(size());
      testContent_new.overwrite(testContent);
      testContent.swap(testContent_new);
   };
   void recapacitate(size_t newCapacity) {
      testContent.reserve(newCapacity,true);
   }; 
   void resize(size_t newSize) {
      testContent.resize(newSize);
   }; 
   size_t capacityInBytes() const {
      return testContent.capacity() * sizeof(uint);
   };

   vec testContent;
};

