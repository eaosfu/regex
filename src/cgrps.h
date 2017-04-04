#ifndef CGRPS_H_
#define CGRPS_H_
#include "bits.h"

#define CGRP_MAX 9

#define CGRP_MAP_SIZE                                            \
  ((CGRP_MAX/BITS_PER_BLOCK) == 0) ? 1 : (CGRP_MAX/BITS_PER_BLOCK)

#define mark_closure_map_backref(cgrp, i)                       \
  ({do {                                                        \
    if(((i) > 0) && ((i) <= CGRP_MAX)) {                        \
      set_bit_array(BIT_MAP_TYPE, (cgrp), BITS_PER_BLOCK, (i)); \
    }                                                           \
  } while(0);})

#define cgrp_has_backref(cgrp, i)                                \
  ((((i) > 0) && ((i) <= CGRP_MAX))                              \
  ? (check_bit_array(BIT_MAP_TYPE, (cgrp), BITS_PER_BLOCK, (i))) \
  : 0)

#endif
