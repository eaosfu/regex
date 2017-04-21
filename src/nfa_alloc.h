#ifndef NFA_ALLOC_H_
#define NFA_ALLOC_H_

void nfa_alloc_init(NFACtrl *, int);
void nfa_dealloc(NFAlloc *, NFA *);
void nfa_free_alloc(NFAlloc **);
NFA * nfa_alloc(NFAlloc **);

#endif
