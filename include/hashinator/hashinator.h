/* File:    hashinator.h
 * Authors: Kostis Papadakis, Urs Ganse and Markus Battarbee (2023)
 * Description: A hybrid hashmap that can operate on both
 *              CPUs and GPUs using CUDA unified memory.
 *
 * This file defines the following classes:
 *    --Hashinator::Hashmap;
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * */
#pragma once
#include <cstddef>
#ifdef HASHINATOR_CPU_ONLY_MODE
#define SPLIT_CPU_ONLY_MODE
#endif
#include "../common.h"
#include "../splitvector/gpu_wrappers.h"
#include "../splitvector/split_allocators.h"
#include "../splitvector/splitvec.h"
#include "defaults.h"
#include "hash_pair.h"
#include "hashfunctions.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#ifndef HASHINATOR_CPU_ONLY_MODE
#include "../splitvector/split_tools.h"
#include "hashers.h"
#endif

namespace Hashinator {

#ifndef HASHINATOR_CPU_ONLY_MODE
template <typename T>
using DefaultAllocator = split::split_unified_allocator<T>;
#define DefaultHasher                                                                                                  \
   Hashers::Hasher<KEY_TYPE, VAL_TYPE, HashFunction, EMPTYBUCKET, TOMBSTONE, defaults::WARPSIZE,                       \
                   defaults::elementsPerWarp>
#else
template <typename T>
using DefaultAllocator = std::allocator<T>;
#define DefaultHasher void
#endif

using MapInfo = Hashinator::Info;
template <typename KEY_TYPE, typename VAL_TYPE, class Allocator = DefaultAllocator<hash_pair<KEY_TYPE,VAL_TYPE>>,
          KEY_TYPE EMPTYBUCKET = std::numeric_limits<KEY_TYPE>::max(),KEY_TYPE TOMBSTONE = EMPTYBUCKET - 1, 
          class HashFunction = HashFunctions::Fibonacci<KEY_TYPE>,class DeviceHasher = DefaultHasher>
class Hashmap {

private:
   
   constexpr size_t get_number_of_Ts_for_Map_Info()const noexcept{
      constexpr size_t size_of_T=sizeof(hash_pair<KEY_TYPE,VAL_TYPE>);
      constexpr size_t size_of_info=sizeof(MapInfo);
      if constexpr (size_of_T>size_of_info){
         return 1;
      }
      return std::ceil(size_of_info/size_of_T);
   }
   
   // CUDA device handle
   Hashmap* device_map;
   //~CUDA device handle

   // Host members
   split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator> buckets;
   Allocator _allocator; // Allocator used to allocate and deallocate memory for metadata
   MapInfo* _mapInfo;
   //~Host members

   // Wrapper over available hash functions
public:
   HASHINATOR_HOSTDEVICE
   uint32_t hash(KEY_TYPE in) const {
      static_assert(std::is_arithmetic<KEY_TYPE>::value);
      return HashFunction::_hash(in, _mapInfo->sizePower);
   }
private:

   // Used by the constructors. Preallocates the device pointer and bookeepping info for later use on device.
   // This helps in reducing the number of calls to split_gpuMalloc
   void preallocate_device_handles() {
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMalloc((void**)&device_map, sizeof(Hashmap)));
#endif
   }

   // Deallocates the bookeepping info and the device pointer
   void deallocate_device_handles() {
      if (device_map == nullptr) {
         return;
      }
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuFree(device_map));
      device_map = nullptr;
#endif
   }

   HASHINATOR_HOSTDEVICE
   inline void set_status(status code) noexcept { _mapInfo->err = code; }

public:
   //By default we allocate enough space for 1<<5 (32) elements
   Hashmap():_allocator(Allocator{}) {
      preallocate_device_handles();
      _mapInfo = reinterpret_cast<MapInfo*>(_allocator.allocate(get_number_of_Ts_for_Map_Info()));
      *_mapInfo = MapInfo(5);
      buckets = split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>(
          1 << _mapInfo->sizePower, hash_pair<KEY_TYPE, VAL_TYPE>(EMPTYBUCKET, VAL_TYPE()),_allocator);
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
   };
   
   Hashmap(const Allocator& allocator):buckets(split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>(
          1 << 5, hash_pair<KEY_TYPE, VAL_TYPE>(EMPTYBUCKET, VAL_TYPE()),allocator)),_allocator(allocator){
      preallocate_device_handles();
      _mapInfo = reinterpret_cast<MapInfo*>(_allocator.allocate(get_number_of_Ts_for_Map_Info()));
      *_mapInfo = MapInfo(5);
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
   };

   Hashmap(int sizepower):_allocator(Allocator{})  {
      preallocate_device_handles();
      _mapInfo = reinterpret_cast<MapInfo*>(_allocator.allocate(get_number_of_Ts_for_Map_Info()));
      *_mapInfo = MapInfo(sizepower);
      buckets = split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>(
          1 << _mapInfo->sizePower, hash_pair<KEY_TYPE, VAL_TYPE>(EMPTYBUCKET, VAL_TYPE()),_allocator);
      #ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
   };

   Hashmap(int sizepower,const Allocator& allocator):buckets(split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>(
          1 << sizepower, hash_pair<KEY_TYPE, VAL_TYPE>(EMPTYBUCKET, VAL_TYPE()),allocator)),_allocator(allocator){
      preallocate_device_handles();
      _mapInfo = reinterpret_cast<MapInfo*>(_allocator.allocate(get_number_of_Ts_for_Map_Info()));
      *_mapInfo = MapInfo(sizepower);
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
   };

   Hashmap(const Hashmap<KEY_TYPE, VAL_TYPE,Allocator>& other) {
      _allocator=other._allocator;
      preallocate_device_handles();
      _mapInfo = reinterpret_cast<MapInfo*>(_allocator.allocate(get_number_of_Ts_for_Map_Info()));
      *_mapInfo = *(other._mapInfo);
      buckets = other.buckets;
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
   };
   
   Hashmap(Hashmap<KEY_TYPE, VAL_TYPE,Allocator>&& other) {
      _allocator=other._allocator;
      preallocate_device_handles();
      _mapInfo = other._mapInfo;
      other._mapInfo = nullptr;
      buckets = std::move(other.buckets);
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
   };

   //Allocator is not replaces with assignment operation
   Hashmap& operator=(const Hashmap<KEY_TYPE, VAL_TYPE,Allocator>& other) {
      if (this == &other) {
         return *this;
      }
      *_mapInfo = *(other._mapInfo);
      buckets = other.buckets;
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
      return *this;
   }

#ifndef HASHINATOR_CPU_ONLY_MODE
   /** Copy assign but using a provided stream */
   void overwrite(const Hashmap<KEY_TYPE, VAL_TYPE,Allocator>& other, split_gpuStream_t stream = 0) {
      if (this == &other) {
         return;
      }
      SPLIT_CHECK_ERR(
          split_gpuMemcpyAsync(_mapInfo, other._mapInfo, sizeof(MapInfo), split_gpuMemcpyDeviceToDevice, stream));
      buckets.overwrite(other.buckets, stream);
      SPLIT_CHECK_ERR(split_gpuMemcpyAsync(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice, stream));
      return;
   }
#endif

   Hashmap& operator=(Hashmap<KEY_TYPE, VAL_TYPE,Allocator>&& other) {
      if (this == &other) {
         return *this;
      }
      _allocator.deallocate(reinterpret_cast<hash_pair<KEY_TYPE,VAL_TYPE>*>(_mapInfo), get_number_of_Ts_for_Map_Info());
      _mapInfo = other._mapInfo;
      other._mapInfo = nullptr;
      buckets = std::move(other.buckets);
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
      return *this;
   }

   ~Hashmap() {
      deallocate_device_handles();
      _allocator.deallocate(reinterpret_cast<hash_pair<KEY_TYPE,VAL_TYPE>*>(_mapInfo), get_number_of_Ts_for_Map_Info());
   };

#ifdef HASHINATOR_CPU_ONLY_MODE
   void* operator new(size_t len) {
      void* ptr = (void*)malloc(len);
      return ptr;
   }

   void operator delete(void* ptr) { free(ptr); }

   void* operator new[](size_t len) {
      void* ptr = (void*)malloc(len);
      return ptr;
   }

   void operator delete[](void* ptr) { free(ptr); }

#else
   void* operator new(size_t len) {
      void* ptr;
      SPLIT_CHECK_ERR(split_gpuMallocManaged(&ptr, len));
      return ptr;
   }

   void operator delete(void* ptr) { SPLIT_CHECK_ERR(split_gpuFree(ptr)); }

   void* operator new[](size_t len) {
      void* ptr;
      SPLIT_CHECK_ERR(split_gpuMallocManaged(&ptr, len));
      return ptr;
   }

   void operator delete[](void* ptr) { split_gpuFree(ptr); }

   void copyMetadata(MapInfo* dst, split_gpuStream_t s = 0) {
      SPLIT_CHECK_ERR(split_gpuMemcpyAsync(dst, _mapInfo, sizeof(MapInfo), split_gpuMemcpyDeviceToHost, s));
   }

#endif

   // Resize the table to fit more things. This is automatically invoked once
   // maxBucketOverflow has triggered. This can only be done on host (so far)
   void rehash(int newSizePower) {
      if (newSizePower > 32) {
         throw std::out_of_range("Hashmap ran into rehashing catastrophe and exceeded 32bit buckets.");
      }
      split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator> newBuckets(
          1 << newSizePower, hash_pair<KEY_TYPE, VAL_TYPE>(EMPTYBUCKET, VAL_TYPE()),_allocator);
      _mapInfo->sizePower = newSizePower;
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size

      // Iterate through all old elements and rehash them into the new array.
      for (auto& e : buckets) {
         // Skip empty buckets ; We also check for TOMBSTONE elements
         // as we might be coming off a kernel that overflew the hashmap
         if (e.first == EMPTYBUCKET || e.first == TOMBSTONE) {
            continue;
         }

         uint32_t newHash = hash(e.first);
         bool found = false;
         for (int i = 0; i < Hashinator::defaults::BUCKET_OVERFLOW; i++) {
            hash_pair<KEY_TYPE, VAL_TYPE>& candidate = newBuckets[(newHash + i) & bitMask];
            if (candidate.first == EMPTYBUCKET) {
               // Found an empty bucket, assign that one.
               candidate = e;
               found = true;
               break;
            }
         }

         if (!found) {
            // Having arrived here means that we unsuccessfully rehashed and
            // are *still* overflowing our buckets. So we need to try again with a bigger one.
            return rehash(newSizePower + 1);
         }
      }

      // Replace our buckets with the new ones
      buckets = newBuckets;
      _mapInfo->currentMaxBucketOverflow = Hashinator::defaults::BUCKET_OVERFLOW;
      _mapInfo->tombstoneCounter = 0;
#ifndef HASHINATOR_CPU_ONLY_MODE
      SPLIT_CHECK_ERR(split_gpuMemcpy(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice));
#endif
   }

#ifndef HASHINATOR_CPU_ONLY_MODE
   // Resize the table to fit more things. This is automatically invoked once
   // maxBucketOverflow has triggered. This can only be done on host (so far)
   template <bool prefetches = true>
   void device_rehash(int newSizePower, split_gpuStream_t s = 0) {
      if (newSizePower > 32) {
         throw std::out_of_range("Hashmap ran into rehashing catastrophe and exceeded 32bit buckets.");
      }

      size_t priorFill = _mapInfo->fill;
      // Extract all valid elements
      hash_pair<KEY_TYPE, VAL_TYPE>* validElements=nullptr;
      const std::size_t alloc_size=_mapInfo->fill+1;
      validElements=_allocator.allocate(alloc_size);
      if (!validElements){
         throw std::runtime_error("ERORR: Failed to allocate temporary buffer in device rehash");
      }
      if constexpr (prefetches) {
         optimizeGPU(s);
         SPLIT_CHECK_ERR(split_gpuStreamSynchronize(s));
      }

      auto isValidKey = [] __host__ __device__(hash_pair<KEY_TYPE, VAL_TYPE> & element) {
         if (element.first != TOMBSTONE && element.first != EMPTYBUCKET) {
            return true;
         }
         return false;
      };
      uint32_t nValidElements = extractPattern(validElements, isValidKey, s);

      SPLIT_CHECK_ERR(split_gpuStreamSynchronize(s));
      assert(nValidElements == _mapInfo->fill && "Something really bad happened during rehashing! Ask Kostis!");
      // We can now clear our buckets
      // Easy optimization: If our bucket had no valid elements and the same size was requested
      // we can just clear it
      if (newSizePower == _mapInfo->sizePower && nValidElements == 0) {
         clear<prefetches>(targets::device, s, 1 << newSizePower);
         set_status((priorFill == _mapInfo->fill) ? status::success : status::fail);
         _allocator.deallocate(validElements,alloc_size); 
         return;
      }
      if (newSizePower == _mapInfo->sizePower) {
         // Just clear the current contents
         clear<prefetches>(targets::device, s, 1 << newSizePower);
         // DeviceHasher::reset_all(buckets.data(),_mapInfo, buckets.size(), s);
      } else {
         // Need new buckets
         buckets = std::move(split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>(
             1 << newSizePower, hash_pair<KEY_TYPE, VAL_TYPE>(EMPTYBUCKET, VAL_TYPE()),_allocator));
         SPLIT_CHECK_ERR(split_gpuMemcpyAsync(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice, s));
         optimizeGPU(s);
      }
      *_mapInfo = Info(newSizePower);
      // Insert valid elements to now larger buckets
      insert(validElements, nValidElements, 1, s);
      set_status((priorFill == _mapInfo->fill) ? status::success : status::fail);
      _allocator.deallocate(validElements,alloc_size); 
      return;
   }
#endif

   // Element access (by reference). Nonexistent elements get created.
   VAL_TYPE& _at(const KEY_TYPE& key) {
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);

      // Try to find the matching bucket.
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {

         hash_pair<KEY_TYPE, VAL_TYPE>& candidate = buckets[(hashIndex + i) & bitMask];

         if (candidate.first == key) {
            // Found a match, return that
            return candidate.second;
         }

         if (candidate.first == EMPTYBUCKET) {
            // Found an empty bucket, assign and return that.
            candidate.first = key;
            _mapInfo->fill++;
            return candidate.second;
         }

         if (candidate.first == TOMBSTONE) {
            bool alreadyExists = false;

            // We remove this Tombstone
            candidate.first = key;
            _mapInfo->tombstoneCounter--;

            // We look ahead in case candidate was already in the hashmap
            // If we find it then we swap the duplicate with empty and do not increment fill
            // but we only reduce the tombstone count
            const size_t bsize = buckets.size();
            for (size_t j = i + 1; j < bsize; ++j) {
               hash_pair<KEY_TYPE, VAL_TYPE>& duplicate = buckets[(hashIndex + j) & bitMask];
               if (duplicate.first == candidate.first) {
                  alreadyExists = true;
                  candidate.second = duplicate.second;
                  if (buckets[(hashIndex + j + 1) & bitMask].first == EMPTYBUCKET ||
                      j + 1 >= _mapInfo->currentMaxBucketOverflow) {
                     duplicate.first = EMPTYBUCKET;
                  } else {
                     duplicate.first = TOMBSTONE;
                     _mapInfo->tombstoneCounter++;
                  }
                  break;
               }
            }
            if (!alreadyExists) {
               _mapInfo->fill++;
            }
            return candidate.second;
         }
      }

      // Not found, and we have no free slots to create a new one. So we need to rehash to a larger size.
#ifdef HASHINATOR_CPU_ONLY_MODE
      rehash(_mapInfo->sizePower + 1);
#else
      device_rehash(_mapInfo->sizePower + 1);
      assert(peek_status() == status::success);
#endif
      return at(key); // Recursive tail call to try again with larger table.
   }

   const VAL_TYPE& _at(const KEY_TYPE& key) const {
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);

      // Try to find the matching bucket.
      for (size_t i = 0; i < _mapInfo->currentMaxBucketOverflow; i++) {
         const hash_pair<KEY_TYPE, VAL_TYPE>& candidate = buckets[(hashIndex + i) & bitMask];

         if (candidate.first == TOMBSTONE) {
            continue;
         }

         if (candidate.first == key) {
            // Found a match, return that
            return candidate.second;
         }
         if (candidate.first == EMPTYBUCKET) {
            // Found an empty bucket, so error.
            throw std::out_of_range("Element not found in Hashmap.at");
         }
      }

      // Not found, so error.
      throw std::out_of_range("Element not found in Hashmap.at");
   }

   //---------------------------------------

   HASHINATOR_HOSTDEVICE
   inline status peek_status(void) noexcept {
      status retval = _mapInfo->err;
      _mapInfo->err = status::invalid;
      return retval;
   }

   HASHINATOR_HOSTDEVICE
   inline int getSizePower(void) const noexcept { return _mapInfo->sizePower; }

   // For STL compatibility: size(), bucket_count(), count(KEY_TYPE), clear()
   HASHINATOR_HOSTDEVICE
   size_t size() const { return _mapInfo->fill; }

   HASHINATOR_HOSTDEVICE
   size_t bucket_count() const { return buckets.size(); }

   HASHINATOR_HOSTDEVICE
   constexpr KEY_TYPE get_emptybucket() const { return EMPTYBUCKET; }

   HASHINATOR_HOSTDEVICE
   constexpr KEY_TYPE get_tombstone() const { return TOMBSTONE; }

   HASHINATOR_HOSTDEVICE
   float load_factor() const { return (float)size() / bucket_count(); }

   HASHINATOR_HOSTDEVICE
   size_t count(const KEY_TYPE& key) const {
      if (find(key) != end()) {
         return 1;
      } else {
         return 0;
      }
   }

   // Dangerous methods for exposing internals
   template <bool warn = true>
   HASHINATOR_HOSTDEVICE MapInfo* expose_mapinfo() noexcept {
      if constexpr(warn) {
         printf("Warning, exposing Hashmap internal info struct!\n");
      }
      return _mapInfo;
   }
   template <bool warn = true>
   HASHINATOR_HOSTDEVICE hash_pair<KEY_TYPE, VAL_TYPE>* expose_bucketdata() noexcept {
      if constexpr(warn) {
         printf("Warning, exposing Hashmap internal bucket data!\n");
      }
      return buckets.data();
   }
   template <bool warn = true>
   HASHINATOR_HOSTDEVICE const hash_pair<KEY_TYPE, VAL_TYPE>* expose_bucketdata() const noexcept {
      if constexpr(warn) {
         printf("Warning, exposing Hashmap internal bucket data!\n");
      }
      return buckets.data();
   }

#ifdef HASHINATOR_CPU_ONLY_MODE
   void clear() {
      buckets = split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>(1 << _mapInfo->sizePower, {EMPTYBUCKET, VAL_TYPE()},_allocator); 
      *_mapInfo = MapInfo(_mapInfo->sizePower);
      return;
   }
#else
   template <bool prefetches = true>
   void clear(targets t = targets::host, split_gpuStream_t s = 0, size_t len = 0) {
      switch (t) {
      case targets::host:
         buckets =
             split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>(1 << _mapInfo->sizePower, {EMPTYBUCKET, VAL_TYPE()},_allocator);
         *_mapInfo = MapInfo(_mapInfo->sizePower);
         break;

      case targets::device:
         if constexpr (prefetches) {
            optimizeGPU(s);
         }
         if (len == 0) { // If size is provided, no need to page fault size information.
            len = buckets.size();
         }
         DeviceHasher::reset_all(buckets.data(), _mapInfo, len, s);
#ifdef HASHINATOR_DEBUG
         set_status((_mapInfo->fill == 0) ? success : fail);
#endif
         break;

      default:
         clear(targets::host);
         break;
      }
      return;
   }
#endif

#ifdef HASHINATOR_CPU_ONLY_MODE
   // Try to grow our buckets until we achieve a targetLF load factor
   void resize_to_lf(float targetLF = 0.5) {
      while (load_factor() > targetLF) {
         rehash(_mapInfo->sizePower + 1);
      }
   }
#else
   // Try to grow our buckets until we achieve a targetLF load factor
   void resize_to_lf(float targetLF = 0.5, targets t = targets::host, split_gpuStream_t s = 0) {
      while (load_factor() > targetLF) {
         switch (t) {
         case targets::host:
            rehash(_mapInfo->sizePower + 1);
            break;
         case targets::device:
            device_rehash(_mapInfo->sizePower + 1, s);
            break;
         default:
            std::cerr << "Defaulting to host rehashing" << std::endl;
            resize(_mapInfo->sizePower + 1, targets::host);
            break;
         }
      }
      return;
   }
#endif

#ifdef HASHINATOR_CPU_ONLY_MODE
   void resize(int newSizePower) { rehash(newSizePower); }
#else
   void resize(int newSizePower, targets t = targets::host, split_gpuStream_t s = 0) {
      switch (t) {
      case targets::host:
         rehash(newSizePower);
         break;
      case targets::device:
         device_rehash(newSizePower, s);
         break;
      default:
         std::cerr << "Defaulting to host rehashing" << std::endl;
         resize(newSizePower, targets::host);
         break;
      }
      return;
   }
#endif

   HASHINATOR_HOSTDEVICE
   void print_pair(const hash_pair<KEY_TYPE, VAL_TYPE>& i) const {
      size_t currentSizePower = _mapInfo->sizePower;
      const size_t hashIndex = HashFunction::_hash(i.first, currentSizePower);
      const int bitMask = (1 << (currentSizePower)) - 1;
      size_t optimalIndex = hashIndex & bitMask;
      const_iterator it = find(i.first);
      int64_t overflow = llabs(it.getIndex() - optimalIndex);
      if (i.first == TOMBSTONE) {
         printf("[╀] ");
      } else if (i.first == EMPTYBUCKET) {
         printf("[▢] ");
      } else {
         if (overflow > 0) {
            printf("[%d,%d,\033[1;31m%li\033[0m] ", i.first, i.second, overflow);
         } else {
            printf("[%d,%d,%zu] ", i.first, i.second, overflow);
         }
      }
   }

   HASHINATOR_HOSTDEVICE
   void dump_buckets() const {
      printf("Hashinator Stats \n");
      printf("Fill= %zu, LoadFactor=%f \n", _mapInfo->fill, load_factor());
      printf("Tombstones= %zu\n", _mapInfo->tombstoneCounter);
      for (int i = 0; i < buckets.size(); ++i) {
         print_pair(buckets[i]);
      }
      printf("\n");
   }

   HASHINATOR_HOSTDEVICE
   void stats() const {
      printf("Hashinator Stats \n");
      printf("Bucket size= %lu\n", buckets.size());
      printf("Fill= %lu, LoadFactor=%f \n", _mapInfo->fill, load_factor());
      printf("Tombstones= %lu\n", _mapInfo->tombstoneCounter);
      printf("Overflow= %lu\n", _mapInfo->currentMaxBucketOverflow);
   }

   HASHINATOR_HOSTDEVICE
   size_t tombstone_count() const { return _mapInfo->tombstoneCounter; }

   HASHINATOR_HOSTDEVICE
   float tombstone_ratio() const {
      if (tombstone_count() == 0) {
         return 0.0;
      }

      return (float)_mapInfo->tombstoneCounter / (float)buckets.size();
   }

   void swap(Hashmap<KEY_TYPE, VAL_TYPE>& other) noexcept {
      buckets.swap(other.buckets);
      std::swap(_mapInfo, other._mapInfo);
      std::swap(device_map, other.device_map);
   }

#ifdef HASHINATOR_CPU_ONLY_MODE
   // Try to get the overflow back to the original one
   void performCleanupTasks() {
      while (_mapInfo->currentMaxBucketOverflow > Hashinator::defaults::BUCKET_OVERFLOW) {
         rehash(_mapInfo->sizePower + 1);
      }
      // When operating in CPU only mode we rehash to get rid of tombstones
      if (tombstone_ratio() > 0.25) {
         rehash(_mapInfo->sizePower);
      }
   }
#else
   // Try to get the overflow back to the original one
   template <bool prefetches = true>
   void performCleanupTasks(split_gpuStream_t s = 0) {
      if (tombstone_ratio() > 0.025) {
         clean_tombstones<prefetches>(s);
      }
      while (_mapInfo->currentMaxBucketOverflow > Hashinator::defaults::BUCKET_OVERFLOW) {
         device_rehash<prefetches>(_mapInfo->sizePower + 1, s);
      }
   }

#endif

   // Read only  access to reference.
   const VAL_TYPE& at(const KEY_TYPE& key) const {
      performCleanupTasks();
      return _at(key);
   }

   // See _at(key)
   VAL_TYPE& at(const KEY_TYPE& key) {
      performCleanupTasks();
      return _at(key);
   }

   // Typical array-like access with [] operator
   VAL_TYPE& operator[](const KEY_TYPE& key) {
      performCleanupTasks();
      return at(key);
   }

   // Iterator type. Iterates through all non-empty buckets.
   class iterator {
      Hashmap<KEY_TYPE, VAL_TYPE,Allocator>* hashtable;
      size_t index;

   public:
      iterator(Hashmap<KEY_TYPE, VAL_TYPE,Allocator>& hashtable, size_t index) : hashtable(&hashtable), index(index) {}

      iterator& operator++() {
         index++;
         while (index < hashtable->buckets.size()) {
            if (hashtable->buckets[index].first != EMPTYBUCKET && hashtable->buckets[index].first != TOMBSTONE) {
               break;
            }
            index++;
         }
         return *this;
      }

      iterator operator++(int) { // Postfix version
         iterator temp = *this;
         ++(*this);
         return temp;
      }
      bool operator==(iterator other) const {
         return &hashtable->buckets[index] == &other.hashtable->buckets[other.index];
      }
      bool operator!=(iterator other) const {
         return &hashtable->buckets[index] != &other.hashtable->buckets[other.index];
      }
      hash_pair<KEY_TYPE, VAL_TYPE>& operator*() const { return hashtable->buckets[index]; }
      hash_pair<KEY_TYPE, VAL_TYPE>* operator->() const { return &hashtable->buckets[index]; }
      size_t getIndex() { return index; }
   };

   // Const iterator.
   class const_iterator {
      const Hashmap<KEY_TYPE, VAL_TYPE,Allocator>* hashtable;
      size_t index;

   public:
      explicit const_iterator(const Hashmap<KEY_TYPE, VAL_TYPE,Allocator>& hashtable, size_t index)
          : hashtable(&hashtable), index(index) {}
      const_iterator& operator++() {
         index++;
         while (index < hashtable->buckets.size()) {
            if (hashtable->buckets[index].first != EMPTYBUCKET && hashtable->buckets[index].first != TOMBSTONE) {
               break;
            }
            index++;
         }
         return *this;
      }
      const_iterator operator++(int) { // Postfix version
         const_iterator temp = *this;
         ++(*this);
         return temp;
      }
      bool operator==(const_iterator other) const {
         return &hashtable->buckets[index] == &other.hashtable->buckets[other.index];
      }
      bool operator!=(const_iterator other) const {
         return &hashtable->buckets[index] != &other.hashtable->buckets[other.index];
      }
      const hash_pair<KEY_TYPE, VAL_TYPE>& operator*() const { return hashtable->buckets[index]; }
      const hash_pair<KEY_TYPE, VAL_TYPE>* operator->() const { return &hashtable->buckets[index]; }
      size_t getIndex() { return index; }
   };

   // Element access by iterator
   const const_iterator find(KEY_TYPE key) const {
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);

      // Try to find the matching bucket.
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {
         const hash_pair<KEY_TYPE, VAL_TYPE>& candidate = buckets[(hashIndex + i) & bitMask];

         if (candidate.first == TOMBSTONE) {
            continue;
         }

         if (candidate.first == key) {
            // Found a match, return that
            return const_iterator(*this, (hashIndex + i) & bitMask);
         }

         if (candidate.first == EMPTYBUCKET) {
            // Found an empty bucket. Return empty.
            return end();
         }
      }

      // Not found
      return end();
   }

   iterator find(KEY_TYPE key) {
      performCleanupTasks();
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);

      // Try to find the matching bucket.
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {
         const hash_pair<KEY_TYPE, VAL_TYPE>& candidate = buckets[(hashIndex + i) & bitMask];

         if (candidate.first == TOMBSTONE) {
            continue;
         }

         if (candidate.first == key) {
            // Found a match, return that
            return iterator(*this, (hashIndex + i) & bitMask);
         }

         if (candidate.first == EMPTYBUCKET) {
            // Found an empty bucket. Return empty.
            return end();
         }
      }

      // Not found
      return end();
   }

   iterator begin() {
      for (size_t i = 0; i < buckets.size(); i++) {
         if (buckets[i].first != EMPTYBUCKET && buckets[i].first != TOMBSTONE) {
            return iterator(*this, i);
         }
      }
      return end();
   }

   const_iterator begin() const {
      for (size_t i = 0; i < buckets.size(); i++) {
         if (buckets[i].first != EMPTYBUCKET && buckets[i].first != TOMBSTONE) {
            return const_iterator(*this, i);
         }
      }
      return end();
   }

   iterator end() { return iterator(*this, buckets.size()); }

   const_iterator end() const { return const_iterator(*this, buckets.size()); }

   // Remove one element from the hash table.
   iterator erase(iterator keyPos) {
      size_t index = keyPos.getIndex();
      if (buckets[index].first != EMPTYBUCKET && buckets[index].first != TOMBSTONE) {
         buckets[index].first = TOMBSTONE;
         _mapInfo->fill--;
         _mapInfo->tombstoneCounter++;
      }
      // return the next valid bucket member
      ++keyPos;
      return keyPos;
   }

   hash_pair<iterator, bool> insert(hash_pair<KEY_TYPE, VAL_TYPE> newEntry) {
      bool found = find(newEntry.first) != end();
      if (!found) {
         at(newEntry.first) = newEntry.second;
      }
      return hash_pair<iterator, bool>(find(newEntry.first), !found);
   }

   size_t erase(const KEY_TYPE& key) {
      iterator element = find(key);
      if (element == end()) {
         return 0;
      } else {
         erase(element);
         return 1;
      }
   }

#ifndef HASHINATOR_CPU_ONLY_MODE
   template <bool skipOverWrites = false>
   HASHINATOR_DEVICEONLY void warpInsert(const KEY_TYPE& candidateKey, const VAL_TYPE& candidateVal,
                                         const size_t w_tid) noexcept {

      const int sizePower = _mapInfo->sizePower;
      const int bitMask = (1 << (sizePower)) - 1;
      const auto hashIndex = HashFunction::_hash(candidateKey, sizePower);
      const size_t optimalindex = (hashIndex)&bitMask;
      const auto submask = SPLIT_VOTING_MASK;
      bool warpDone = false;
      uint64_t threadOverflow = 1;

#ifdef HASHINATOR_DEBUG
// Safety check: make sure everyone has the same key/val and all threads are here.
#ifdef __CUDACC__
      assert(__activemask() == SPLIT_VOTING_MASK && "Tried to warpInsert with part of warp predicated off");
#endif
      KEY_TYPE storeKey = split::s_shuffle(candidateKey, 0, SPLIT_VOTING_MASK);
      KEY_TYPE storeVal = split::s_shuffle(candidateVal, 0, SPLIT_VOTING_MASK);
      bool isSafe = (split::s_warpVote(candidateKey == storeKey, SPLIT_VOTING_MASK) &
                     split::s_warpVote(candidateVal == storeVal, SPLIT_VOTING_MASK)) == SPLIT_VOTING_MASK;
      assert(isSafe && "Tried to warpInsert with different keys/vals in the same warp");
#endif

      for (size_t i = 0; i < (1 << sizePower); i += defaults::WARPSIZE) {
         // Check if this virtual warp is done.
         if (warpDone) {
            break;
         }

         // Get the position we should be looking into
         size_t probingindex = ((hashIndex + i + w_tid) & bitMask);
         auto target = buckets[probingindex];

         // vote for available emptybuckets in warp region
         // Note that this has to be done before voting for already existing elements (below)
         auto mask = split::s_warpVote(target.first == EMPTYBUCKET, submask);

         // Check if this elements already exists
         auto already_exists = split::s_warpVote(target.first == candidateKey, submask);
         if (already_exists) {
            int winner = split::s_findFirstSig(already_exists);
            if (w_tid == winner-1) {
               if constexpr (!skipOverWrites) {
                  split::s_atomicExch(&(buckets[probingindex].second), candidateVal);
               }
               // This virtual warp is now done.
               warpDone = 1;
            }
         }

         // If any duplicate was there now is the time for the whole Virtual warp to find out!
         warpDone = split::s_warpVote(warpDone > 0, submask) & submask;

         while (mask && !warpDone) {
            int winner = split::s_findFirstSig(mask) - 1;
            if (w_tid == winner) {
               KEY_TYPE old = split::s_atomicCAS(&(buckets[probingindex].first), EMPTYBUCKET, candidateKey);
               if (old == EMPTYBUCKET) {
                  threadOverflow = (probingindex < optimalindex) ? (1 << sizePower) : (probingindex - optimalindex + 1);
                  split::s_atomicExch(&(buckets[probingindex].second), candidateVal);
                  warpDone = 1;
                  split::s_atomicAdd(&(_mapInfo->fill), 1);
                  // Minor optimization to get rid of some unnecessary atomic calls
                  if (threadOverflow > _mapInfo->currentMaxBucketOverflow) {
                     split::s_atomicMax(&(_mapInfo->currentMaxBucketOverflow),
                                        nextOverflow(threadOverflow, defaults::WARPSIZE));
                  }

               } else if (old == candidateKey) {
                  // Parallel insertion already added this key.
                  if constexpr (!skipOverWrites) {
                     split::s_atomicExch(&(buckets[probingindex].second), candidateVal);
                  }
                  warpDone = 1;
               } // else some other key+value was written here.
            }
            // If any of the virtual warp threads are done the the whole
            // Virtual warp is done
            warpDone = split::s_warpVote(warpDone > 0, submask);
            mask ^= (1UL << winner);
         }
      }
   }

   template <bool skipOverWrites = false>
   HASHINATOR_DEVICEONLY bool warpInsert_V(const KEY_TYPE& candidateKey, const VAL_TYPE& candidateVal,
                                           const size_t w_tid) noexcept {

      const int sizePower = _mapInfo->sizePower;
      const int bitMask = (1 << (sizePower)) - 1;
      const auto hashIndex = HashFunction::_hash(candidateKey, sizePower);
      const size_t optimalindex = (hashIndex)&bitMask;
      const auto submask = SPLIT_VOTING_MASK;
      bool warpDone = false;
      uint64_t threadOverflow = 1;
      int localCount = 0;

#ifdef HASHINATOR_DEBUG
// Safety check: make sure everyone has the same key/val and all threads are here.
#ifdef __CUDACC__
      assert(__activemask() == SPLIT_VOTING_MASK && "Tried to warpInsert_V with part of warp predicated off");
#endif
      KEY_TYPE storeKey = split::s_shuffle(candidateKey, 0, SPLIT_VOTING_MASK);
      KEY_TYPE storeVal = split::s_shuffle(candidateVal, 0, SPLIT_VOTING_MASK);
      bool isSafe = (split::s_warpVote(candidateKey == storeKey, SPLIT_VOTING_MASK) &
                     split::s_warpVote(candidateVal == storeVal, SPLIT_VOTING_MASK)) == SPLIT_VOTING_MASK;
      assert(isSafe && "Tried to warpInsert_V with different keys/vals in the same warp");
#endif

      for (size_t i = 0; i < (1 << sizePower); i += defaults::WARPSIZE) {
         // Check if this virtual warp is done.
         if (warpDone) {
            break;
         }

         // Get the position we should be looking into
         size_t probingindex = ((hashIndex + i + w_tid) & bitMask);
         auto target = buckets[probingindex];

         // vote for available emptybuckets in warp region
         // Note that this has to be done before voting for already existing elements (below)
         auto mask = split::s_warpVote(target.first == EMPTYBUCKET, submask);

         // Check if this elements already exists
         auto already_exists = split::s_warpVote(target.first == candidateKey, submask);
         if (already_exists) {
            int winner = split::s_findFirstSig(already_exists);
            if (w_tid == winner-1) {
               if constexpr (!skipOverWrites) {
                     split::s_atomicExch(&(buckets[probingindex].second), candidateVal);
               }
               // This virtual warp is now done.
               warpDone = 1;
            }
         }

         // If any duplicate was there now is the time for the whole Virtual warp to find out!
         warpDone = split::s_warpVote(warpDone > 0, submask) & submask;

         while (mask && !warpDone) {
            int winner = split::s_findFirstSig(mask) - 1;
            if (w_tid == winner) {
               KEY_TYPE old = split::s_atomicCAS(&(buckets[probingindex].first), EMPTYBUCKET, candidateKey);
               if (old == EMPTYBUCKET) {
                  threadOverflow = (probingindex < optimalindex) ? (1 << sizePower) : (probingindex - optimalindex + 1);
                  split::s_atomicExch(&(buckets[probingindex].second), candidateVal);
                  warpDone = 1;
                  localCount = 1;
                  split::s_atomicAdd(&(_mapInfo->fill), 1);
                  // Minor optimization to get rid of some unnecessary atomic calls
                  if (threadOverflow > _mapInfo->currentMaxBucketOverflow) {
                     split::s_atomicMax(&(_mapInfo->currentMaxBucketOverflow),
                                        nextOverflow(threadOverflow, defaults::WARPSIZE));
                  }
               } else if (old == candidateKey) {
                  // Parallel stuff are fun. Major edge case!
                  if constexpr (!skipOverWrites) {
                     split::s_atomicExch(&(buckets[probingindex].second), candidateVal);
                  }
                  warpDone = 1;
               }
            }
            // If any of the virtual warp threads are done the the whole
            // Virtual warp is done
            warpDone = split::s_warpVote(warpDone > 0, submask);
            mask ^= (1UL << winner);
         }
      }

      auto res = split::s_warpVote(localCount > 0, submask);
      return (res != 0);
   }

   HASHINATOR_DEVICEONLY
   void warpFind(const KEY_TYPE& candidateKey, VAL_TYPE& candidateVal, const size_t w_tid) const noexcept {

      const int sizePower = _mapInfo->sizePower;
      //const size_t maxoverflow = _mapInfo->currentMaxBucketOverflow;
      const int bitMask = (1 << (sizePower)) - 1;
      const auto hashIndex = HashFunction::_hash(candidateKey, sizePower);
      const auto submask = SPLIT_VOTING_MASK;
      bool warpDone = false;
      int winner = 0;

#ifdef HASHINATOR_DEBUG
// Safety check: make sure everyone has the same key/val and all threads are here.
#ifdef __CUDACC__
      assert(__activemask() == SPLIT_VOTING_MASK && "Tried to warpFind with part of warp predicated off");
#endif
      KEY_TYPE storeKey = split::s_shuffle(candidateKey, 0, SPLIT_VOTING_MASK);
      bool isSafe = split::s_warpVote(candidateKey == storeKey, SPLIT_VOTING_MASK) == SPLIT_VOTING_MASK;
      assert(isSafe && "Tried to warpFind with different keys/vals in the same warp");
#endif

      for (size_t i = 0; i < (1 << sizePower); i += defaults::WARPSIZE) {
         if (warpDone) {
            break;
         }

         // Get the position we should be looking into
         size_t probingindex = ((hashIndex + i + w_tid) & bitMask);
         const auto maskExists =
             split::s_warpVote(buckets[probingindex].first == candidateKey, SPLIT_VOTING_MASK) & submask;
         const auto emptyFound =
             split::s_warpVote(buckets[probingindex].first == EMPTYBUCKET, SPLIT_VOTING_MASK) & submask;
         // If we encountered empty and the key is not in the range of this warp that means the key is not in hashmap.
         if (!maskExists && emptyFound) {
            warpDone = true;
         }
         if (maskExists) {
            winner = split::s_findFirstSig(maskExists) ;
            if (w_tid == winner-1) {
               candidateVal = buckets[probingindex].second;
            }
            warpDone = true;
         }
      }
      if (winner!=0) {
         candidateVal = split::s_shuffle(candidateVal, winner-1, SPLIT_VOTING_MASK);
      }
      return;
   }

   HASHINATOR_DEVICEONLY
   void warpErase(const KEY_TYPE& candidateKey, const size_t w_tid) noexcept {

      const int sizePower = _mapInfo->sizePower;
      //const size_t maxoverflow = _mapInfo->currentMaxBucketOverflow;
      const int bitMask = (1 << (sizePower)) - 1;
      const auto hashIndex = HashFunction::_hash(candidateKey, sizePower);
      const auto submask = SPLIT_VOTING_MASK;
      bool warpDone = false;
      int winner = 0;

#ifdef HASHINATOR_DEBUG
// Safety check: make sure everyone has the same key/val and all threads are here.
#ifdef __CUDACC__
      assert(__activemask() == SPLIT_VOTING_MASK && "Tried to warpFind with part of warp predicated off");
#endif
      KEY_TYPE storeKey = split::s_shuffle(candidateKey, 0, SPLIT_VOTING_MASK);
      bool isSafe = split::s_warpVote(candidateKey == storeKey, SPLIT_VOTING_MASK) == SPLIT_VOTING_MASK;
      assert(isSafe && "Tried to warpFind with different keys/vals in the same warp");
#endif

      for (size_t i = 0; i < (1 << sizePower); i += defaults::WARPSIZE) {
         if (warpDone) {
            break;
         }

         // Get the position we should be looking into
         size_t probingindex = ((hashIndex + i + w_tid) & bitMask);
         const auto maskExists =
             split::s_warpVote(buckets[probingindex].first == candidateKey, SPLIT_VOTING_MASK) & submask;
         const auto emptyFound =
             split::s_warpVote(buckets[probingindex].first == EMPTYBUCKET, SPLIT_VOTING_MASK) & submask;
         // If we encountered empty and the key is not in the range of this warp that means the key is not in hashmap.
         if (!maskExists && emptyFound) {
            warpDone = true;
         }
         if (maskExists) {
            winner = split::s_findFirstSig(maskExists);
            if (w_tid == winner-1) {
               KEY_TYPE old = split::s_atomicCAS(&(buckets[probingindex].first), candidateKey, TOMBSTONE);
               if (old==candidateKey) {
                  split::s_atomicSub(&(_mapInfo->fill), 1);
                  split::s_atomicAdd(&(_mapInfo->tombstoneCounter), 1);
               }
            }
            warpDone = true;
         }
      }
      return;
   }

   // Pass memAdvice to hashinator and the underlying splitvector
   HOSTONLY void memAdvise(split_gpuMemoryAdvise advice, int device, split_gpuStream_t stream = 0) {
      buckets.memAdvise(advice, device, stream);
      split_gpuMemAdvise(_mapInfo, sizeof(MapInfo), advice, device);
   }

   /*
    * Fills the splitvector "elements" with **copies** of the keys that match the pattern
    * dictated by Rule.
    * Example Usage:
    *
    * Define this somewhere:
    *
    *  template <typename T, typename U>
    *  struct Rule{
    *  Rule(){}
    *     __host__ __device__
    *     inline bool operator()( hash_pair<T,U>& element)const{
    *        if (element.first<100 ){return true;}
    *        return false;
    *     }
    *  };
    *
    * Then call this:
    *   hmap.extractPattern(elements,Rule<uint32_t,uint32_t>());
    * */
   template <bool prefetches = true, typename Rule>
   size_t extractPattern(split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>& elements, Rule rule,
                         split_gpuStream_t s = 0) {
      elements.resize(_mapInfo->fill + 1, true);
      if constexpr (prefetches) {
         elements.optimizeGPU(s);
      }
      // Extract elements matching the Pattern Rule(element)==true;
      split::tools::copy_if<hash_pair<KEY_TYPE, VAL_TYPE>, Rule, defaults::MAX_BLOCKSIZE, defaults::WARPSIZE>(
          buckets, elements, rule, s);
      return elements.size();
   }

   template <typename Rule, int BLOCKSIZE = 1024>
   size_t extractPattern(hash_pair<KEY_TYPE, VAL_TYPE>* elements, Rule rule, split_gpuStream_t s = 0) {
      // Extract elements matching the Pattern Rule(element)==true;

      // Figure out Blocks to use
      size_t _s = std::ceil((float(buckets.size())) / (float)BLOCKSIZE);
      size_t nBlocks = nextPow2(_s);
      if (nBlocks == 0) {
         nBlocks += 1;
      }

      // Allocate with Mempool
      const size_t memory_for_pool = 8 * nBlocks * sizeof(uint32_t);
      split::tools::splitStackArena mPool(memory_for_pool, s);
      size_t retval =
          split::tools::copy_if_raw<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator, Rule, defaults::MAX_BLOCKSIZE, defaults::WARPSIZE>(
              buckets, elements, rule, nBlocks, mPool, s);
      return retval;
   }
   template <typename Rule>
   void extractPatternLoop(split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>& elements, Rule rule,
                           split_gpuStream_t s = 0) {
      // Extract elements matching the Pattern Rule(element)==true;
      split::tools::copy_if_loop<hash_pair<KEY_TYPE, VAL_TYPE>, Rule, defaults::MAX_BLOCKSIZE, defaults::WARPSIZE>(
          device_map->buckets, elements, rule, s);
   }
   void extractLoop(split::SplitVector<hash_pair<KEY_TYPE, VAL_TYPE>,Allocator>& elements, split_gpuStream_t s = 0) {
      // Extract all valid elements
      auto rule = [] __host__ __device__(const hash_pair<KEY_TYPE, VAL_TYPE>& kval) -> bool {
         return kval.first != EMPTYBUCKET && kval.first != TOMBSTONE;
      };
      extractPatternLoop(elements, rule, s);
   }

   template <bool prefetches = true, typename Rule>
   size_t extractKeysByPattern(split::SplitVector<KEY_TYPE>& elements, Rule rule, split_gpuStream_t s = 0) {
      elements.resize(_mapInfo->fill + 1, true);
      if constexpr (prefetches) {
         elements.optimizeGPU(s);
      }
      // Extract element **keys** matching the Pattern Rule(element)==true;
      split::tools::copy_keys_if<hash_pair<KEY_TYPE, VAL_TYPE>, KEY_TYPE, Rule, defaults::MAX_BLOCKSIZE,
                                 defaults::WARPSIZE>(buckets, elements, rule, s);
      // FIXME: there is an issue where paging to host occurs and following calls to hashmap operations take a hit.
      // temp fix: call optimizeGPU() here
      if constexpr (prefetches) {
         optimizeGPU(s);
      }
      return elements.size();
   }
   
   template <typename KeyAlloc,bool prefetches = true, typename Rule>
   size_t extractKeysByPattern(split::SplitVector<KEY_TYPE,KeyAlloc>& elements, Rule rule, split_gpuStream_t s = 0) {
      elements.resize(_mapInfo->fill + 1, true);
      if constexpr (prefetches) {
         elements.optimizeGPU(s);
      }
      // Extract element **keys** matching the Pattern Rule(element)==true;
      split::tools::copy_keys_if<hash_pair<KEY_TYPE, VAL_TYPE>, KEY_TYPE,Allocator,KeyAlloc, Rule,defaults::MAX_BLOCKSIZE,
                                 defaults::WARPSIZE>(buckets, elements, rule, s);
      // FIXME: there is an issue where paging to host occurs and following calls to hashmap operations take a hit.
      // temp fix: call optimizeGPU() here
      if constexpr (prefetches) {
         optimizeGPU(s);
      }
      return elements.size();
   }
   
   template <bool prefetches = true, typename Rule>
   size_t extractKeysByPattern(split::SplitVector<KEY_TYPE>& elements, Rule rule, void* stack, size_t max_size,
                               split_gpuStream_t s = 0) {
      elements.resize(_mapInfo->fill + 1, true);
      if constexpr (prefetches) {
         elements.optimizeGPU(s);
      }
      // Extract element **keys** matching the Pattern Rule(element)==true;
      split::tools::copy_keys_if<hash_pair<KEY_TYPE, VAL_TYPE>, KEY_TYPE, Rule, defaults::MAX_BLOCKSIZE,
                                 defaults::WARPSIZE>(buckets, elements, rule, stack, max_size, s);
      return elements.size();
   }
   
   template <typename KeyAlloc, bool prefetches = true, typename Rule>
   size_t extractKeysByPattern(split::SplitVector<KEY_TYPE,KeyAlloc>& elements, Rule rule, void* stack, size_t max_size,
                               split_gpuStream_t s = 0) {
      elements.resize(_mapInfo->fill + 1, true);
      if constexpr (prefetches) {
         elements.optimizeGPU(s);
      }
      // Extract element **keys** matching the Pattern Rule(element)==true;
      split::tools::copy_keys_if<hash_pair<KEY_TYPE, VAL_TYPE>, KEY_TYPE,Allocator,KeyAlloc, Rule, defaults::MAX_BLOCKSIZE,
                                 defaults::WARPSIZE>(buckets, elements, rule, stack, max_size, s);
      return elements.size();
   }
   
   template <typename Rule>
   void extractKeysByPatternLoop(split::SplitVector<KEY_TYPE>& elements, Rule rule, split_gpuStream_t s = 0) {
      // Extract element **keys** matching the Pattern Rule(element)==true;
      split::tools::copy_if_keys_loop<hash_pair<KEY_TYPE, VAL_TYPE>, KEY_TYPE, Rule, defaults::MAX_BLOCKSIZE,
                                      defaults::WARPSIZE>(device_map->buckets, elements, rule, s);
   }

   template <bool prefetches = true>
   size_t extractAllKeys(split::SplitVector<KEY_TYPE>& elements, split_gpuStream_t s = 0) {
      // Extract all keys
      auto rule = [] __host__ __device__(const hash_pair<KEY_TYPE, VAL_TYPE>& kval) -> bool {
         return kval.first != EMPTYBUCKET && kval.first != TOMBSTONE;
      };
      return extractKeysByPattern<prefetches>(elements, rule, s);
   }
   template <bool prefetches = true>
   size_t extractAllKeys(split::SplitVector<KEY_TYPE>& elements, void* stack, size_t max_size,
                         split_gpuStream_t s = 0) {
      // Extract all keys
      auto rule = [] __host__ __device__(const hash_pair<KEY_TYPE, VAL_TYPE>& kval) -> bool {
         return kval.first != EMPTYBUCKET && kval.first != TOMBSTONE;
      };
      return extractKeysByPattern<prefetches>(elements, rule, stack, max_size, s);
   }
   void extractAllKeysLoop(split::SplitVector<KEY_TYPE>& elements, split_gpuStream_t s = 0) {
      // Extract all keys
      auto rule = [] __host__ __device__(const hash_pair<KEY_TYPE, VAL_TYPE>& kval) -> bool {
         return kval.first != EMPTYBUCKET && kval.first != TOMBSTONE;
      };
      extractKeysByPatternLoop(elements, rule, s);
   }

   template <bool prefetches = true>
   void clean_tombstones(split_gpuStream_t s = 0) {

      if (_mapInfo->tombstoneCounter == 0) {
         return;
      }

      // Reset the tomstone counter
      _mapInfo->tombstoneCounter = 0;
      // Allocate memory for overflown elements. So far this is the same size as our buckets but we can be better than
      // this

      hash_pair<KEY_TYPE, VAL_TYPE>* overflownElements=nullptr;
      const std::size_t alloc_size=_mapInfo->fill+1;
      overflownElements=_allocator.allocate(alloc_size);
      if (!overflownElements){
         throw std::runtime_error("ERORR: Failed to allocate temporary buffer in tombstone cleaning");
      }
      if constexpr (prefetches) {
         optimizeGPU(s);
      }
      SPLIT_CHECK_ERR(split_gpuStreamSynchronize(s));

      int currentSizePower = _mapInfo->sizePower;
      hash_pair<KEY_TYPE, VAL_TYPE>* bck_ptr = buckets.data();

      auto isOverflown = [bck_ptr, currentSizePower] __host__ __device__(hash_pair<KEY_TYPE, VAL_TYPE> & element) {
         if (element.first == TOMBSTONE) {
            element.first = EMPTYBUCKET;
            return false;
         }
         if (element.first == EMPTYBUCKET) {
            return false;
         }
         const size_t hashIndex = HashFunction::_hash(element.first, currentSizePower);
         const int bitMask = (1 << (currentSizePower)) - 1;
         bool isOverflown = (bck_ptr[hashIndex & bitMask].first != element.first);
         return isOverflown;
      };

      // Extract overflown elements and reset overflow
      uint32_t nOverflownElements = extractPattern(overflownElements, isOverflown, s);
      _mapInfo->currentMaxBucketOverflow = defaults::BUCKET_OVERFLOW;

      if (nOverflownElements == 0) {
         _allocator.deallocate(overflownElements,alloc_size);
         return;
      }
      // If we do have overflown elements we put them back in the buckets
      SPLIT_CHECK_ERR(split_gpuStreamSynchronize(s));
      DeviceHasher::reset(overflownElements, buckets.data(), _mapInfo, nOverflownElements, s);

      DeviceHasher::insert(overflownElements, buckets.data(), _mapInfo, nOverflownElements, s);

      _allocator.deallocate(overflownElements,alloc_size);
      return;
   }

   // Uses Hasher's insert_kernel to insert all elements
   template <bool prefetches = true>
   void insert(KEY_TYPE* keys, VAL_TYPE* vals, size_t len, float targetLF = 0.5, split_gpuStream_t s = 0) {
      // Here we do some calculations to estimate how much if any we need to grow our buckets
      // TODO fix these if paths or at least annotate them .
      if (len == 0) {
         set_status(status::success);
         return;
      }
      if constexpr (prefetches) {
         buckets.optimizeGPU(s);
      }
      int64_t neededPowerSize = std::ceil(std::log2((_mapInfo->fill + len) * (1.0 / targetLF)));
      if (neededPowerSize > _mapInfo->sizePower) {
         resize(neededPowerSize, targets::device, s);
      }
      DeviceHasher::insert(keys, vals, buckets.data(), _mapInfo, len, s);
      return;
   }

   // Uses Hasher's insert_index_kernel to insert all elements, with the index as the value
   template <bool prefetches = true>
   void insertIndex(KEY_TYPE* keys, size_t len, float targetLF = 0.5, split_gpuStream_t s = 0) {
      // Here we do some calculations to estimate how much if any we need to grow our buckets
      // TODO fix these if paths or at least annotate them .
      if (len == 0) {
         set_status(status::success);
         return;
      }
      if constexpr (prefetches) {
         buckets.optimizeGPU(s);
      }
      int64_t neededPowerSize = std::ceil(std::log2((_mapInfo->fill + len) * (1.0 / targetLF)));
      if (neededPowerSize > _mapInfo->sizePower) {
         resize(neededPowerSize, targets::device, s);
      }
      DeviceHasher::insertIndex(keys, buckets.data(), _mapInfo, len, s);
      return;
   }

   // Uses Hasher's insert_kernel to insert all elements
   template <bool prefetches = true>
   void insert(hash_pair<KEY_TYPE, VAL_TYPE>* src, size_t len, float targetLF = 0.5, split_gpuStream_t s = 0) {
      if (len == 0) {
         set_status(status::success);
         return;
      }
      if constexpr (prefetches) {
         buckets.optimizeGPU(s);
      }
      // Here we do some calculations to estimate how much if any we need to grow our buckets
      int64_t neededPowerSize = std::ceil(std::log2(((_mapInfo->fill) + len) * (1.0 / targetLF)));
      if (neededPowerSize > _mapInfo->sizePower) {
         resize(neededPowerSize, targets::device, s);
      }
      DeviceHasher::insert(src, buckets.data(), _mapInfo, len, s);
      return;
   }

   // Uses Hasher's retrieve_kernel to read all elements
   template <bool prefetches = true>
   void retrieve(KEY_TYPE* keys, VAL_TYPE* vals, size_t len, split_gpuStream_t s = 0) {
      if constexpr (prefetches) {
         buckets.optimizeGPU(s);
      }
      DeviceHasher::retrieve(keys, vals, buckets.data(), _mapInfo, len, s);
      return;
   }

   // Uses Hasher's retrieve_kernel to read all elements
   template <bool prefetches = true>
   void retrieve(hash_pair<KEY_TYPE, VAL_TYPE>* src, size_t len, split_gpuStream_t s = 0) {
      if constexpr (prefetches) {
         buckets.optimizeGPU(s);
      }
      DeviceHasher::retrieve(src, buckets.data(), _mapInfo, len, s);
      return;
   }

   // Uses Hasher's erase_kernel to delete all elements
   template <bool prefetches = true>
   void erase(KEY_TYPE* keys, size_t len, split_gpuStream_t s = 0) {
      if constexpr (prefetches) {
         buckets.optimizeGPU(s);
      }
      // Remember the last numeber of tombstones
      size_t tbStore = tombstone_count();
      DeviceHasher::erase(keys, buckets.data(), _mapInfo, len, s);
      size_t tombstonesAdded = tombstone_count() - tbStore;
      // Fill should be decremented by the number of tombstones added;
      _mapInfo->fill -= tombstonesAdded;
      return;
   }

   /**
    * Host function  that returns a device pointer that can be passed to CUDA kernels
    * The pointer is internally cleaned up by the destructors, however the user **must**
    * call download() after usage on device.
    */
   template <bool prefetches = true>
   Hashmap* upload(split_gpuStream_t stream = 0) {
      if constexpr (prefetches) {
         optimizeGPU(stream);
      }
      SPLIT_CHECK_ERR(split_gpuMemcpyAsync(device_map, this, sizeof(Hashmap), split_gpuMemcpyHostToDevice, stream));
      return device_map;
   }

   void optimizeGPU(split_gpuStream_t stream = 0) noexcept {
      int device;
      SPLIT_CHECK_ERR(split_gpuGetDevice(&device));
      SPLIT_CHECK_ERR(split_gpuMemPrefetchAsync(_mapInfo, sizeof(MapInfo), device, stream));
      buckets.optimizeGPU(stream);
   }

   /*Manually prefetch data on Host*/
   void optimizeCPU(split_gpuStream_t stream = 0) noexcept {
      SPLIT_CHECK_ERR(split_gpuMemPrefetchAsync(_mapInfo, sizeof(MapInfo), split_gpuCpuDeviceId, stream));
      buckets.optimizeCPU(stream);
   }

   void streamAttach(split_gpuStream_t s, uint32_t flags = split_gpuMemAttachSingle) {
      buckets.streamAttach(s, flags);
      SPLIT_CHECK_ERR(split_gpuStreamAttachMemAsync(s, (void*)_mapInfo, sizeof(MapInfo), flags));
      return;
   }

   // Just return the device pointer. Upload should be called fist
   // othewise map bookeepping info will not be updated on device.
   Hashmap* get_device_pointer() { return device_map; }

   /**
    * This must be called after exiting a CUDA kernel. These functions
    * will do the following :
    *  • handle communicating bookeepping info back to host.
    *  • If the hashmap has overflown on device it will try
    *  • If there are Tombstones then those are removed
    * */
   void download(split_gpuStream_t stream = 0) {
      // Copy over fill as it might have changed
      optimizeCPU(stream);
      if (_mapInfo->currentMaxBucketOverflow > Hashinator::defaults::BUCKET_OVERFLOW) {
         rehash(_mapInfo->sizePower + 1);
      } else {
         if (tombstone_count() > 0) {
            clean_tombstones(stream);
         }
      }
   }

   // Device Iterator type. Iterates through all non-empty buckets.
   class device_iterator {
   private:
      size_t index;
      Hashmap<KEY_TYPE, VAL_TYPE,Allocator>* hashtable;

   public:
      HASHINATOR_DEVICEONLY
      device_iterator(Hashmap<KEY_TYPE, VAL_TYPE,Allocator>& hashtable, size_t index) : index(index), hashtable(&hashtable) {}

      HASHINATOR_DEVICEONLY
      size_t getIndex() { return index; }

      HASHINATOR_DEVICEONLY
      device_iterator& operator++() {
         index++;
         while (index < hashtable->buckets.size()) {
            if (hashtable->buckets[index].first != EMPTYBUCKET && hashtable->buckets[index].first != TOMBSTONE) {
               break;
            }
            index++;
         }
         return *this;
      }

      HASHINATOR_DEVICEONLY
      device_iterator operator++(int) {
         device_iterator temp = *this;
         ++(*this);
         return temp;
      }

      HASHINATOR_DEVICEONLY
      bool operator==(device_iterator other) const {
         return &hashtable->buckets[index] == &other.hashtable->buckets[other.index];
      }
      HASHINATOR_DEVICEONLY
      bool operator!=(device_iterator other) const {
         return &hashtable->buckets[index] != &other.hashtable->buckets[other.index];
      }

      HASHINATOR_DEVICEONLY
      hash_pair<KEY_TYPE, VAL_TYPE>& operator*() const { return hashtable->buckets[index]; }
      HASHINATOR_DEVICEONLY
      hash_pair<KEY_TYPE, VAL_TYPE>* operator->() const { return &hashtable->buckets[index]; }
   };

   class const_device_iterator {
   private:
      size_t index;
      const Hashmap<KEY_TYPE, VAL_TYPE>* hashtable;

   public:
      HASHINATOR_DEVICEONLY
      explicit const_device_iterator(const Hashmap<KEY_TYPE, VAL_TYPE>& hashtable, size_t index)
          : index(index), hashtable(&hashtable) {}

      HASHINATOR_DEVICEONLY
      size_t getIndex() { return index; }

      HASHINATOR_DEVICEONLY
      const_device_iterator& operator++() {
         index++;
         while (index < hashtable->buckets.size()) {
            if (hashtable->buckets[index].first != EMPTYBUCKET && hashtable->buckets[index].first != TOMBSTONE) {
               break;
            }
            index++;
         }
         return *this;
      }

      HASHINATOR_DEVICEONLY
      const_device_iterator operator++(int) {
         const_device_iterator temp = *this;
         ++(*this);
         return temp;
      }

      HASHINATOR_DEVICEONLY
      bool operator==(const_device_iterator other) const {
         return &hashtable->buckets[index] == &other.hashtable->buckets[other.index];
      }
      HASHINATOR_DEVICEONLY
      bool operator!=(const_device_iterator other) const {
         return &hashtable->buckets[index] != &other.hashtable->buckets[other.index];
      }

      HASHINATOR_DEVICEONLY
      const hash_pair<KEY_TYPE, VAL_TYPE>& operator*() const { return hashtable->buckets[index]; }
      HASHINATOR_DEVICEONLY
      const hash_pair<KEY_TYPE, VAL_TYPE>* operator->() const { return &hashtable->buckets[index]; }
   };

   // Element access by iterator
   HASHINATOR_DEVICEONLY
   device_iterator device_find(KEY_TYPE key) {
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);

      // Try to find the matching bucket.
      //for (size_t i = 0; i < _mapInfo->currentMaxBucketOverflow; i++) {
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {
         const hash_pair<KEY_TYPE, VAL_TYPE>& candidate = buckets[(hashIndex + i) & bitMask];

         if (candidate.first == TOMBSTONE) {
            continue;
         }

         if (candidate.first == key) {
            // Found a match, return that
            return device_iterator(*this, (hashIndex + i) & bitMask);
         }

         if (candidate.first == EMPTYBUCKET) {
            // Found an empty bucket. Return empty.
            return device_end();
         }
      }

      // Not found
      return device_end();
   }

   HASHINATOR_DEVICEONLY
   const const_device_iterator device_find(KEY_TYPE key) const {
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);

      // Try to find the matching bucket.
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {
         const hash_pair<KEY_TYPE, VAL_TYPE>& candidate = buckets[(hashIndex + i) & bitMask];

         if (candidate.first == TOMBSTONE) {
            continue;
         }

         if (candidate.first == key) {
            // Found a match, return that
            return const_device_iterator(*this, (hashIndex + i) & bitMask);
         }

         if (candidate.first == EMPTYBUCKET) {
            // Found an empty bucket. Return empty.
            return device_end();
         }
      }

      // Not found
      return device_end();
   }

   HASHINATOR_DEVICEONLY
   device_iterator device_end() { return device_iterator(*this, buckets.size()); }

   HASHINATOR_DEVICEONLY
   const_device_iterator device_end() const { return const_device_iterator(*this, buckets.size()); }

   HASHINATOR_DEVICEONLY
   device_iterator device_begin() {
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {
         if (buckets[i].first != EMPTYBUCKET && buckets[i].first != TOMBSTONE) {
            return device_iterator(*this, i);
         }
      }
      return device_end();
   }

   HASHINATOR_DEVICEONLY
   const_device_iterator device_begin() const {
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {
         if (buckets[i].first != EMPTYBUCKET && buckets[i].first != TOMBSTONE) {
            return const_device_iterator(*this, i);
         }
      }
      return device_end();
   }

   HASHINATOR_DEVICEONLY
   size_t device_erase(const KEY_TYPE& key) {
      device_iterator element = device_find(key);
      if (element == device_end()) {
         return 0;
      } else {
         device_erase(element);
         return 1;
      }
   }

   HASHINATOR_DEVICEONLY
   size_t device_count(const KEY_TYPE& key) const {
      if (device_find(key) != device_end()) {
         return 1;
      } else {
         return 0;
      }
   }

   // Remove with tombstones on device
   HASHINATOR_DEVICEONLY
   device_iterator device_erase(device_iterator keyPos) {
      // Get the index of this entry
      const size_t index = keyPos.getIndex();

      // If this is an empty bucket or a tombstone we can return already
      // NOTE: threadsafety requires a read of value, not reference
      const KEY_TYPE item = buckets[index].first;
      if (item == EMPTYBUCKET || item == TOMBSTONE) {
         return ++keyPos;
      }

      // Let's simply add a tombstone here
      KEY_TYPE old = split::s_atomicCAS(&(buckets[index].first), item, TOMBSTONE);
      if (old==item) {
         split::s_atomicSub(&(_mapInfo->fill), 1);
         split::s_atomicAdd(&(_mapInfo->tombstoneCounter), 1);
      }
      return ++keyPos;
   }

private:
   /**Device code for inserting elements. Nonexistent elements get created.
      Tombstones are accounted for.
    */
   template <bool skipOverWrites = false>
   HASHINATOR_DEVICEONLY
   bool insert_element(const KEY_TYPE& key, VAL_TYPE value, size_t& thread_overflowLookup) {
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);
      size_t i = 0;
      const size_t bsize = buckets.size();
      while (i < bsize) {
         uint32_t vecindex = (hashIndex + i) & bitMask;
         KEY_TYPE old = split::s_atomicCAS(&(buckets[vecindex].first), EMPTYBUCKET, key);
         // Key does not exist so we create it and incerement fill
         if (old == EMPTYBUCKET) {
            split::s_atomicExch(&(buckets[vecindex].second), value);
            split::s_atomicAdd(&(_mapInfo->fill), 1);
            thread_overflowLookup = i + 1;
            return true;
         }

         // Key exists so we overwrite it. Fill stays the same
         if (old == key) {
            if constexpr (!skipOverWrites) {
               split::s_atomicExch(&(buckets[vecindex].second), value);
            }
            thread_overflowLookup = i + 1;
            return false;
         }

         i++;
      }
      assert(false && "Hashmap completely overflown");
      return false;
   }

public:
   template <bool skipOverWrites = false>
   HASHINATOR_DEVICEONLY
   hash_pair<device_iterator, bool> device_insert(hash_pair<KEY_TYPE, VAL_TYPE> newEntry) {
      bool newentry = set_element<skipOverWrites>(newEntry.first, newEntry.second);
      return hash_pair<device_iterator, bool>(device_find(newEntry.first), newentry);
   }

   template <bool skipOverWrites = false>
   HASHINATOR_DEVICEONLY
   bool set_element(const KEY_TYPE& key, VAL_TYPE val) {
      bool newentry = false;
      size_t thread_overflowLookup = 0;
      newentry = insert_element<skipOverWrites>(key, val, thread_overflowLookup);
      split::s_atomicMax(&(_mapInfo->currentMaxBucketOverflow),
                         nextOverflow(thread_overflowLookup, defaults::WARPSIZE / defaults::elementsPerWarp));
      return newentry;
   }

   HASHINATOR_DEVICEONLY
   const VAL_TYPE& read_element(const KEY_TYPE& key) const {
      int bitMask = (1 << _mapInfo->sizePower) - 1; // For efficient modulo of the array size
      auto hashIndex = hash(key);

      // Try to find the matching bucket.
      const size_t bsize = buckets.size();
      for (size_t i = 0; i < bsize; i++) {
         uint32_t vecindex = (hashIndex + i) & bitMask;
         const hash_pair<KEY_TYPE, VAL_TYPE>& candidate = buckets[vecindex];
         if (candidate.first == key) {
            // Found a match, return that
            return candidate.second;
         }
         if (candidate.first == EMPTYBUCKET) {
            // Found an empty bucket, so error.
            assert(false && "Key does not exist");
         }
      }
      assert(false && "Key does not exist");
   }

#else

   // Uses Hasher's insert_kernel to insert all elements
   void insert(KEY_TYPE* keys, VAL_TYPE* vals, size_t len, float targetLF = 0.5) {
      for (size_t i = 0; i < len; ++i) {
         _at(keys[i]) = vals[i];
      }
   }

   // Uses Hasher's insert_kernel to insert all elements
   void insert(hash_pair<KEY_TYPE, VAL_TYPE>* src, size_t len, float targetLF = 0.5) {
      for (size_t i = 0; i < len; ++i) {
         _at(src[i].first) = src[i].second;
      }
   }

   // Uses Hasher's retrieve_kernel to read all elements
   void retrieve(KEY_TYPE* keys, VAL_TYPE* vals, size_t len) {
      for (size_t i = 0; i < len; ++i) {
         vals[i] = at(keys[i]);
      }
   }

   // Uses Hasher's erase_kernel to delete all elements
   void erase(KEY_TYPE* keys, VAL_TYPE* vals, size_t len) {
      for (size_t i = 0; i < len; ++i) {
         erase(keys[i].first);
      }
   }

#endif
};
} // namespace Hashinator
