#ifndef BITS_H_
#define BITS_H_
#include <stdint.h>
#include <limits.h>


#if ( __x86_64__ || __amd64__ || __ia_64__ )
  #define BITS_PER_BLOCK 64
  #define BIT_MAP_TYPE uint64_t
#else // assuming a 32 bit arch
  #define BITS_PER_BLOCK 32
  #define BIT_MAP_TYPE uint32_t
#endif

// currently limited to ASCII.. technically 127 but 128 shouldn't hurt
#define SIZE_OF_LOCALE 128

// turn the most-significant-bit on
#define set_high_bit(t, w) ((t)0x01 << ((w) - 1))


// turn on all the bits
#define set_all_bits(t) (~(t)0x0)


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


// check if bit is set in bucket
#define check_bit_array(t, a, w, v) \
 ((a)[get_bit_array_idx((v), (w))] & set_bit(t, (w), (v)))


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
  ((a)[get_bit_array_idx((v),(w))] &= (set_all_bits(t) ^ (set_bit(t, (w), (v)))))


// This perform the same functions as the ones listed above but take a 0 indexed
// array whereas the ones above take a 1 indexed array
#define z_get_bit_array_idx(v, w) \
  (((v))/(w))

#define z_set_bit(t, w, v)\
  ((t)0x1 << (v - (z_get_bit_array_idx(v, w) * w)))

#define z_clear_bit_array(t, a, w, v)\
  ((a)[z_get_bit_array_idx((v),(w))] &= (set_all_bits(t) ^ (z_set_bit(t, (w), (v)))))

#define z_set_bit_array(t, a, w, v)\
  ((a)[z_get_bit_array_idx((v),(w))] |= (z_set_bit(t, (w), (v))))

#endif
