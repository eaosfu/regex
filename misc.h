#ifndef MISC_H_
#define MISC_H_

#ifndef SUCCESS
#define SUCCESS 1
#endif

#ifndef FAILURE
#define FAILURE 0
#endif

void fatal(const char *);
void warn(const char *);
void * xmalloc(unsigned int);
#endif
