/*$Id: ex15.c,v 1.9 2000/01/11 21:00:17 bsmith Exp balay $*/

static char help[] = "Tests VecSetValuesBlocked() on Seq vectors\n\n";

#include "petscvec.h"
#include "petscsys.h"

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **argv)
{
  int          n = 9,ierr,size,bs = 3,indices[2],i;
  Scalar       values[6];
  Vec          x;

  PetscInitialize(&argc,&argv,(char*)0,help);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRA(ierr);

  if (size != 1) SETERRA(1,0,"Must be run with one processor");

  /* create vector */
  ierr = VecCreateSeq(PETSC_COMM_SELF,n,&x);CHKERRA(ierr);
  ierr = VecSetBlockSize(x,bs);CHKERRA(ierr);

  for (i=0; i<6; i++) values[i] = 4.0*i;
  indices[0] = 0; indices[1] = 2;
  ierr = VecSetValuesBlocked(x,2,indices,values,INSERT_VALUES);CHKERRA(ierr);

  ierr = VecAssemblyBegin(x);CHKERRA(ierr);
  ierr = VecAssemblyEnd(x);CHKERRA(ierr);

  /* 
      Resulting vector should be 0 4 8  0 0 0 12 16 20
  */
  ierr = VecView(x,VIEWER_STDOUT_WORLD);CHKERRA(ierr);

  ierr = VecDestroy(x);CHKERRA(ierr);

  PetscFinalize();
  return 0;
}
 
