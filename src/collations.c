#include "collations.h"

int ncoll = 12;
named_collations collations[] = {
 {"alnum", 5, 3, {{65,90}, {97,122}, {48,57}}},
 {"alpha", 5, 2, {{65,90}, {97,122}}},
 {"blank", 5, 2, {{32,32}, {11,11}}},
 {"cntrl", 5, 2, {{0,31}, {127,127}}},
 {"digit", 5, 1, {{48,57}}},
 {"graph", 5, 1, {{33,126}}},
 {"lower", 5, 1, {{97,122}}},
 {"print", 5, 1, {{32,126}}},
 {"punct", 5, 4, {{33,47}, {58,64}, {91,96}, {123,126}}},
 {"space", 5, 2, {{9,13}, {2,32}}},
 {"upper", 5, 1, {{65,90}}},
 {"xdigit",6, 2, {{65,78}, {97,102}}}
};