/*$Id: ex44.c,v 1.9 1999/10/24 14:02:39 bsmith Exp balay $*/

static char help[] = 
"Loads matrix dumped by ex43.\n\n";

#include "petscmat.h"

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **args)
{
  Mat     C;
  Viewer  viewer;
  int     ierr;

  PetscInitialize(&argc,&args,0,help);
  ierr = ViewerBinaryOpen(PETSC_COMM_WORLD,"matrix.dat",BINARY_RDONLY,&viewer); 
        CHKERRA(ierr);
  MatLoad(viewer,MATMPIDENSE,&C);CHKERRA(ierr);
  ierr = ViewerDestroy(viewer);CHKERRA(ierr);
  ierr = MatView(C,VIEWER_STDOUT_WORLD);CHKERRA(ierr);
  ierr = MatDestroy(C);CHKERRA(ierr);
  PetscFinalize();
  return 0;
}


