/*$Id: ex3.c,v 1.45 2000/01/11 21:00:17 bsmith Exp balay $*/

static char help[] = "Tests parallel vector assembly.  Input arguments are\n\
  -n <length> : local vector length\n\n";

#include "petscvec.h"
#include "petscsys.h"

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **argv)
{
  int          n = 5,ierr,size,rank;
  Scalar       one = 1.0,two = 2.0,three = 3.0;
  Vec          x,y;
  int          idx;

  PetscInitialize(&argc,&argv,(char*)0,help);
  ierr = OptionsGetInt(PETSC_NULL,"-n",&n,PETSC_NULL);CHKERRA(ierr);
  if (n < 5) n = 5;
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRA(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRA(ierr);

  if (size < 2) SETERRA(1,0,"Must be run with at least two processors");

  /* create two vector */
  ierr = VecCreateSeq(PETSC_COMM_SELF,n,&x);CHKERRA(ierr);
  ierr = VecCreateMPI(PETSC_COMM_WORLD,n,PETSC_DECIDE,&y);CHKERRA(ierr);
  ierr = VecSet(&one,x);CHKERRA(ierr);
  ierr = VecSet(&two,y);CHKERRA(ierr);

  if (rank == 1) {
    idx = 2; ierr = VecSetValues(y,1,&idx,&three,INSERT_VALUES);CHKERRA(ierr);
    idx = 0; ierr = VecSetValues(y,1,&idx,&two,INSERT_VALUES);CHKERRA(ierr); 
    idx = 0; ierr = VecSetValues(y,1,&idx,&one,INSERT_VALUES);CHKERRA(ierr); 
  }
  else {
    idx = 7; ierr = VecSetValues(y,1,&idx,&three,INSERT_VALUES);CHKERRA(ierr); 
  } 
  ierr = VecAssemblyBegin(y);CHKERRA(ierr);
  ierr = VecAssemblyEnd(y);CHKERRA(ierr);

  ierr = VecView(y,VIEWER_STDOUT_WORLD);CHKERRA(ierr);

  ierr = VecDestroy(x);CHKERRA(ierr);
  ierr = VecDestroy(y);CHKERRA(ierr);

  PetscFinalize();
  return 0;
}
 
