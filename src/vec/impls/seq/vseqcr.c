/*$Id: vseqcr.c,v 1.12 2000/04/12 04:22:20 bsmith Exp balay $*/
/*
   Implements the sequential vectors.
*/

#include "src/vec/vecimpl.h"          /*I  "petscvec.h"   I*/
#include "src/vec/impls/dvecimpl.h" 

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"VecCreateSeq"
/*@C
   VecCreateSeq - Creates a standard, sequential array-style vector.

   Collective on MPI_Comm

   Input Parameter:
+  comm - the communicator, should be PETSC_COMM_SELF
-  n - the vector length 

   Output Parameter:
.  V - the vector

   Notes:
   Use VecDuplicate() or VecDuplicateVecs() to form additional vectors of the
   same type as an existing vector.

   Level: intermediate

.keywords: vector, sequential, create, BLAS

.seealso: VecCreateMPI(), VecCreate(), VecDuplicate(), VecDuplicateVecs(), VecCreateGhost()
@*/
int VecCreateSeq(MPI_Comm comm,int n,Vec *v)
{
  int ierr,size;

  PetscFunctionBegin;
  ierr = MPI_Comm_size(comm,&size);CHKERRQ(ierr);
  if (size > 1) SETERRQ(1,1,"Cannot only create sequential vectors on 1 processor");
  ierr = VecCreate(comm,n,n,v);CHKERRQ(ierr);
  ierr = VecSetType(*v,VEC_SEQ);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
