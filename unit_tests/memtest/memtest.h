#include "submodules/hashinator/include/splitvector/splitvec.h"
#include "submodules/hashinator/include/hashinator/hashinator.h"

#define CHK_ERR(err) (cuda_error(err, __FILE__, __LINE__))

struct testStructure {
   split::SplitVector<uint> splitVec;

   testStructure(size_t initSize) {
      splitVec.resize(initSize);
   }
   size_t capacity() {
      return splitVec.capacity();
   }
   size_t size() {
      return splitVec.size();
   }
   void shrink_to_fit() {
      splitVec.shrink_to_fit();
   }
   void shrink_to_fit_2() {
      split::SplitVector<uint> splitVec_new(size());
      splitVec_new.overwrite(splitVec);
      splitVec.swap(splitVec_new);
   }
   void recapacitate(size_t newCapacity) {
      splitVec.reserve(newCapacity,true);
   }  
   void resize(size_t newSize) {
      splitVec.resize(newSize);
   }  
   int capacityInBytes() {
      return splitVEc.capacity() * sizeof(uint);
   }
};

