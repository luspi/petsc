/*$Id: sp1wd.c,v 1.34 2000/04/12 04:24:20 bsmith Exp balay $*/

#include "petscmat.h"
#include "src/mat/order/order.h"

EXTERN_C_BEGIN
/*
    MatOrdering_1WD - Find the 1-way dissection ordering of a given matrix.
*/    
#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"MatOrdering_1WD"
int MatOrdering_1WD(Mat mat,MatOrderingType type,IS *row,IS *col)
{
  int        i,*mask,*xls,nblks,*xblk,*ls,nrow,*perm,ierr,*ia,*ja;
  PetscTruth done;

  PetscFunctionBegin;
  ierr = MatGetRowIJ(mat,1,PETSC_TRUE,&nrow,&ia,&ja,&done);CHKERRQ(ierr);
  if (!done) SETERRQ(PETSC_ERR_SUP,0,"Cannot get rows for matrix");

  mask = (int *)PetscMalloc((5*nrow+1) * sizeof(int));CHKPTRQ(mask);
  xls  = mask + nrow;
  ls   = xls + nrow + 1;
  xblk = ls + nrow;
  perm = xblk + nrow;
  SPARSEPACKgen1wd(&nrow,ia,ja,mask,&nblks,xblk,perm,xls,ls);
  ierr = MatRestoreRowIJ(mat,1,PETSC_TRUE,&nrow,&ia,&ja,&done);CHKERRQ(ierr);

  for (i=0; i<nrow; i++) perm[i]--;

  ierr = ISCreateGeneral(PETSC_COMM_SELF,nrow,perm,row);CHKERRQ(ierr);
  ierr = ISCreateGeneral(PETSC_COMM_SELF,nrow,perm,col);CHKERRQ(ierr);
  ierr = PetscFree(mask);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}
EXTERN_C_END

