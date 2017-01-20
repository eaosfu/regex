#ifndef BITS_H_
#define BITS_H_

#include <limits.h>

#ifdef __linux__
  #ifdef __x86_64__
    #define REGULAR_BITVEC_WIDTH (4 * CHAR_BIT)
  #endif
#endif


// get_bit_array_bucket
//
// - 'a' array of bit-vector
// - 'w' width of each bit-vector
// - 'v' 1-based integer
// Return bucket containig the bit for value 'v'
//
#define get_bit_array_bucket(a, w, v) \
  ((a)[(((v) - 1)/(w))])
  

// get_bit_array_idx
//
// 'v' bit-vector
// 'w' bit-vector width
// Compute what bit-vector-array index value 'v' should go given each
// element in the bit-vector-array has a width 'w'
// 
#define get_bit_array_idx(v, w) \
  (((v) - 1)/(w))


// set_bit
//
// 'v' bit-vector
// 'w' bit-vector width
// Turn on bit at offset corresponding to value 'v'
//
#define set_bit(w, i) \
  ((unsigned int)(UINT_MAX & (1 << ((w) - 1))) >> ((w) - ((i) % (w))))


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
#define set_bit_array(a, w, v) \
  ((a)[get_bit_array_idx((v),(w))] |= set_bit((w), (v)))


// clear_bit_array
//
// - 'a' array of bit-vector
// - 'w' width of each bit-vector
// - 'v' 1-based integer
// Treating array 'a' as a long bit-vector, find the index into array 'a' and
// bit-offset from that index that corresponds to value 'v' and turn off the
// bit. 
//
#define clear_bit_array(a, w, v) \
  ((a)[get_bit_array_idx((v),(w))] &= (UINT_MAX ^ set_bit((w), (v))))


#endif
