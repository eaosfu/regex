#define EMPTY_COLLATION_CLASS_ERROR \
"Malformed bracket expression. Correct syntax for a collation expression is: " \
"'[[.<matching list>.]]'.\nOr rewrite bracket expression as '[.[]]' to match" \
" any of the characters: '.[]'\n"

#define INVALID_RANGE_EXPRESSION_ERROR \
"Invalid range expression '[low_bound, high_bound]'" \
" with low_bound > high_bound\n"

#define INVALID_MULTI_MULTI_ERROR \
"Invalid use of consecutive 'multi' operators\n"

#define INVALID_INTERVAL_EXPRESSION_ERROR \
"Invalid interval, min > max\n"

#define MALFORMED_BRACKET_EXPRESSION_ERROR \
"Malformed bracket expression; missing '.]' or ':]' or '=]'\n"

#define EMPTY_BRACKET_EXPRESSION_ERROR \
"Empty bracket expression\n"

#define UNKNOWN_CHARCLASS_ERROR \
"Unrecognized character-class\n"