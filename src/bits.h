#ifndef BITS_H_
#define BITS_H_
#include <stdint.h>
#include <limits.h>


#if ( __x86_64__ || __amd64__ || __ia_64__ )
  #define BITS_PER_BLOCK 64
  #define BIT_MAP_TYPE uint64_t
#else
  #define BITS_PER_BLOCK 32
  #define BIT_MAP_TYPE uint32_t
#endif


// get_bit_array_idx
//
// 'v' bit-vector
// 'w' bit-vector width
// Compute what bit-vector-array index value 'v' should go given each
// element in the bit-vector-array has a width 'w'
// 
#define get_bit_array_idx(v, w) \
  (((v) - 1)/(w))


// get_bit_array_bucket
//
// - 'a' array of bit-vector
// - 'w' width of each bit-vector
// - 'v' 1-based integer
// Return bucket containig the bit for value 'v'
//
#define get_bit_array_bucket(a, w, v) \
  ((a)[get_bit_array_idx((v),(w))])
  

#define set_high_bit(t, w) ((t)0x01 << ((w) - 1))

// set_bit
//
// 'v' bit-vector
// 'w' bit-vector width
// 't' storage type of each array 'bucket'
// Turn on bit at offset corresponding to value 'v'
//
#define set_bit(t, w, i) \
  (set_high_bit(t, (w)) >> ((w) - ((i) % (w))))


// set_bit_array
//
//  'a' is an array of bit-vectors
//  'w' is the width of each bit-vector in array 'a'
//  'v' must be a 1-based integer
// Treating array 'a' as a long bit-vector, set the bit at position
// corresponding to value 'v'.
//
// E.x.: v = 1, set a[0] |=  1b
//       v = 2, set a[0] |= 10b
//       ...
//       v = 33, set a[1] |= 1b
//       ...
//
#define set_bit_array(t, a, w, v) \
  ((a)[get_bit_array_idx((v),(w))] |= (set_bit(t, (w), (v))))


#define check_bit_array(t, a, w, v) \
 ((a)[get_bit_array_idx((v), (w))] & set_bit(t, (w), (v)))

#define set_all_bits(t, w) (((set_high_bit(t, w) - 1) << 1) | (t)(0x1b))

// clear_bit_array
//
// - 'a' array of bit-vector
// - 'w' width of each bit-vector
// - 'v' 1-based integer
// Treating array 'a' as a long bit-vector, find the index into array 'a' and
// bit-offset from that index that corresponds to value 'v' and turn off the
// bit. 
//
#define clear_bit_array(t, a, w, v) \
  ((a)[get_bit_array_idx((v),(w))] &= (set_all_bits(t, w) ^ (set_bit(t, (w), (v)))))


#endif
