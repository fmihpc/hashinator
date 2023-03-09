#pragma once
#ifdef HASHINATOR_HOST_ONLY

#define HASHINATOR_HOSTONLY
#define HASHINATOR_DEVICEONLY
#define HASHINATOR_HOSTDEVICE


#else


#define HASHINATOR_HOSTONLY __host__
#define HASHINATOR_DEVICEONLY __device__
#define HASHINATOR_HOSTDEVICE __host__ __device__

#endif

//Modified from (http://www-graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2) to support 64-bit uints
HASHINATOR_HOSTDEVICE 
inline size_t nextPow2(size_t v){
   v--;
   v |= v >> 1;
   v |= v >> 2;
   v |= v >> 4;
   v |= v >> 8;
   v |= v >> 16;
   v |= v >> 32;
   v++;
   return v;
}

