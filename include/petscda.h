
/*
      An index set is essentially a subset of the integers
*/
#if !defined(__IS_PACKAGE)
#define __IS_PACKAGE
#include "petsc.h"

#define IS_COOKIE PETSC_COOKIE+2

typedef struct _IS* IS;

extern int   ISCreateSequential(MPI_Comm,int,int *,IS *);
extern int   ISCreateMPI(int,int,int *,MPI_Comm,IS *);
extern int   ISCreateStrideSequential(MPI_Comm,int,int,int,IS *);
extern int   ISStrideGetInfo(IS,int *,int*);

extern int   ISSetPermutation(IS);
extern int   ISIsPermutation(IS); 
extern int   ISGetIndices(IS,int **);
extern int   ISRestoreIndices(IS,int **);
extern int   ISGetSize(IS,int *);
extern int   ISGetLocalSize(IS,int *);
extern int   ISDestroy(IS);
extern int   ISInvertPermutation(IS,IS*);
extern int   ISView(IS,Viewer);

#endif
