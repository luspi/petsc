/*
    Provides an implementation of the Unevenly Sampled FFT algorithm as a Mat.
    Testing examples can be found in ~/src/mat/tests FIX: should these be moved to dm/da/tests?
*/

#include <petsc/private/matimpl.h>            /*I "petscmat.h" I*/
#include <petscdmda.h> /*I "petscdmda.h"  I*/ /* Unlike equispaced FFT, USFFT requires geometric information encoded by a DMDA */
#include <fftw3.h>

typedef struct {
  PetscInt  dim;
  Vec       sampleCoords;
  PetscInt  dof;
  DM        freqDA;     /* frequency DMDA */
  PetscInt *freqSizes;  /* sizes of the frequency DMDA, one per each dim */
  DM        resampleDa; /* the Battle-Lemarie interpolant DMDA */
  Vec       resample;   /* Vec of samples, one per dof per sample point */
  fftw_plan p_forward, p_backward;
  unsigned  p_flag; /* planner flags, FFTW_ESTIMATE, FFTW_MEASURE, FFTW_PATIENT, FFTW_EXHAUSTIVE */
} Mat_USFFT;

#if 0
static PetscErrorCode MatApply_USFFT_Private(Mat A, fftw_plan *plan, int direction, Vec x, Vec y)
{
  #if 0
  PetscScalar    *r_array, *y_array;
  Mat_USFFT* = (Mat_USFFT*)(A->data);
  #endif

  PetscFunctionBegin;
  #if 0
  /* resample x to usfft->resample */
  PetscCall(MatResample_USFFT_Private(A, x));

  /* NB: for now we use outdim for both x and y; this will change once a full USFFT is implemented */
  PetscCall(VecGetArray(usfft->resample,&r_array));
  PetscCall(VecGetArray(y,&y_array));
  if (!*plan) { /* create a plan then execute it*/
    if (usfft->dof == 1) {
    #if defined(PETSC_DEBUG_USFFT)
      PetscCall(PetscPrintf(PetscObjectComm((PetscObject)A), "direction = %d, usfft->ndim = %d\n", direction, usfft->ndim));
      for (int ii = 0; ii < usfft->ndim; ++ii) {
        PetscCall(PetscPrintf(PetscObjectComm((PetscObject)A), "usfft->outdim[%d] = %d\n", ii, usfft->outdim[ii]));
      }
    #endif

      switch (usfft->dim) {
      case 1:
        *plan = fftw_plan_dft_1d(usfft->outdim[0],(fftw_complex*)x_array,(fftw_complex*)y_array,direction,usfft->p_flag);
        break;
      case 2:
        *plan = fftw_plan_dft_2d(usfft->outdim[0],usfft->outdim[1],(fftw_complex*)x_array,(fftw_complex*)y_array,direction,usfft->p_flag);
        break;
      case 3:
        *plan = fftw_plan_dft_3d(usfft->outdim[0],usfft->outdim[1],usfft->outdim[2],(fftw_complex*)x_array,(fftw_complex*)y_array,direction,usfft->p_flag);
        break;
      default:
        *plan = fftw_plan_dft(usfft->ndim,usfft->outdim,(fftw_complex*)x_array,(fftw_complex*)y_array,direction,usfft->p_flag);
        break;
      }
      fftw_execute(*plan);
    } /* if (dof == 1) */
    else { /* if (dof > 1) */
      *plan = fftw_plan_many_dft(/*rank*/usfft->ndim, /*n*/usfft->outdim, /*howmany*/usfft->dof,
                                 (fftw_complex*)x_array, /*nembed*/usfft->outdim, /*stride*/usfft->dof, /*dist*/1,
                                 (fftw_complex*)y_array, /*nembed*/usfft->outdim, /*stride*/usfft->dof, /*dist*/1,
                                 /*sign*/direction, /*flags*/usfft->p_flag);
      fftw_execute(*plan);
    } /* if (dof > 1) */
  } /* if (!*plan) */
  else {  /* if (*plan) */
    /* use existing plan */
    fftw_execute_dft(*plan,(fftw_complex*)x_array,(fftw_complex*)y_array);
  }
  PetscCall(VecRestoreArray(y,&y_array));
  PetscCall(VecRestoreArray(x,&x_array));
  #endif
  PetscFunctionReturn(PETSC_SUCCESS);
} /* MatApply_USFFT_Private() */

PetscErrorCode MatUSFFT_ProjectOnBattleLemarie_Private(Vec x,double *r)
/* Project onto the Battle-Lemarie function centered around r */
{
  PetscScalar    *x_array, *y_array;

  PetscFunctionBegin;
  PetscFunctionReturn(PETSC_SUCCESS);
} /* MatUSFFT_ProjectOnBattleLemarie_Private() */

PetscErrorCode MatInterpolate_USFFT_Private(Vec x,Vec y)
{
  PetscScalar    *x_array, *y_array;

  PetscFunctionBegin;
  PetscFunctionReturn(PETSC_SUCCESS);
} /* MatInterpolate_USFFT_Private() */

PetscErrorCode MatMult_SeqUSFFT(Mat A,Vec x,Vec y)
{
  Mat_USFFT      *usfft = (Mat_USFFT*)A->data;

  PetscFunctionBegin;
  /* NB: for now we use outdim for both x and y; this will change once a full USFFT is implemented */
  PetscCall(MatApply_USFFT_Private(A, &usfft->p_forward, FFTW_FORWARD, x,y));
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode MatMultTranspose_SeqUSFFT(Mat A,Vec x,Vec y)
{
  Mat_USFFT      *usfft = (Mat_USFFT*)A->data;

  PetscFunctionBegin;
  /* NB: for now we use outdim for both x and y; this will change once a full USFFT is implemented */
  PetscCall(MatApply_USFFT_Private(usfft, &usfft->p_backward, FFTW_BACKWARD, x,y));
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode MatDestroy_SeqUSFFT(Mat A)
{
  Mat_USFFT      *usfft = (Mat_USFFT*)A->data;

  PetscFunctionBegin;
  fftw_destroy_plan(usfft->p_forward);
  fftw_destroy_plan(usfft->p_backward);
  PetscCall(PetscFree(usfft->indim));
  PetscCall(PetscFree(usfft->outdim));
  PetscCall(PetscFree(usfft));
  PetscCall(PetscObjectChangeTypeName((PetscObject)A,0));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/*@C
      MatCreateSeqUSFFT - Creates a matrix object that provides sequential USFFT
  via the external package FFTW

   Collective

   Input Parameter:
.  da - geometry of the domain encoded by a `DMDA`

   Output Parameter:
.  A  - the matrix

  Options Database Key:
. -mat_usfft_plannerflags - set the FFTW planner flags

   Level: intermediate

.seealso: `Mat`, `Vec`, `DMDA`, `DM`
@*/
PetscErrorCode  MatCreateSeqUSFFT(Vec sampleCoords, DMDA freqDA, Mat *A)
{
  Mat_USFFT      *usfft;
  PetscInt       m,n,M,N,i;
  const char     *p_flags[]={"FFTW_ESTIMATE","FFTW_MEASURE","FFTW_PATIENT","FFTW_EXHAUSTIVE"};
  PetscBool      flg;
  PetscInt       p_flag;
  PetscInt       dof, dim, freqSizes[3];
  MPI_Comm       comm;
  PetscInt       size;

  PetscFunctionBegin;
  PetscCall(PetscObjectGetComm((PetscObject)inda, &comm));
  PetscCallMPI(MPI_Comm_size(comm, &size));
  PetscCheck(size <= 1,comm,PETSC_ERR_USER, "Parallel DMDA (in) not yet supported by USFFT");
  PetscCall(PetscObjectGetComm((PetscObject)outda, &comm));
  PetscCallMPI(MPI_Comm_size(comm, &size));
  PetscCheck(size <= 1,comm,PETSC_ERR_USER, "Parallel DMDA (out) not yet supported by USFFT");
  PetscCall(MatCreate(comm,A));
  PetscCall(PetscNew(&usfft));
  (*A)->data   = (void*)usfft;
  usfft->inda  = inda;
  usfft->outda = outda;
  /* inda */
  PetscCall(DMDAGetInfo(usfft->inda, &ndim, dim+0, dim+1, dim+2, NULL, NULL, NULL, &dof, NULL, NULL, NULL));
  PetscCheck(ndim > 0,PETSC_COMM_SELF,PETSC_ERR_USER,"ndim %d must be > 0",ndim);
  PetscCheck(dof > 0,PETSC_COMM_SELF,PETSC_ERR_USER,"dof %d must be > 0",dof);
  usfft->ndim   = ndim;
  usfft->dof    = dof;
  usfft->freqDA = freqDA;
  /* NB: we reverse the freq and resample DMDA sizes, since the DMDA ordering (natural on x-y-z, with x varying the fastest)
     is the order opposite of that assumed by FFTW: z varying the fastest */
  PetscCall(PetscMalloc1(usfft->ndim+1,&usfft->indim));
  for (i = usfft->ndim; i > 0; --i) usfft->indim[usfft->ndim-i] = dim[i-1];

  /* outda */
  PetscCall(DMDAGetInfo(usfft->outda, &ndim, dim+0, dim+1, dim+2, NULL, NULL, NULL, &dof, NULL, NULL, NULL));
  PetscCheck(ndim == usfft->ndim,PETSC_COMM_SELF,PETSC_ERR_USER,"in and out DMDA dimensions must match: %d != %d",usfft->ndim, ndim);
  PetscCheck(dof == usfft->dof,PETSC_COMM_SELF,PETSC_ERR_USER,"in and out DMDA dof must match: %d != %d",usfft->dof, dof);
  /* Store output dimensions */
  /* NB: we reverse the DMDA dimensions, since the DMDA ordering (natural on x-y-z, with x varying the fastest)
     is the order opposite of that assumed by FFTW: z varying the fastest */
  PetscCall(PetscMalloc1(usfft->ndim+1,&usfft->outdim));
  for (i = usfft->ndim; i > 0; --i) usfft->outdim[usfft->ndim-i] = dim[i-1];

  /* TODO: Use the new form of DMDACreate() */
  #if 0
  PetscCall(DMDACreate(comm,usfft->dim, DMDA_NONPERIODIC, DMDA_STENCIL_STAR, usfft->freqSizes[0], usfft->freqSizes[1], usfft->freqSizes[2],
                       PETSC_DECIDE, PETSC_DECIDE, PETSC_DECIDE, dof, 0, NULL, NULL, NULL,  0, &(usfft->resampleDA)));
  #endif
  PetscCall(DMDAGetVec(usfft->resampleDA, usfft->resample));

  /* CONTINUE: Need to build the connectivity "Sieve" attaching sample points to the resample points they are close to */

  /* CONTINUE: recalculate matrix sizes based on the connectivity "Sieve" */
  /* mat sizes */
  m = 1; n = 1;
  for (i=0; i<usfft->ndim; i++) {
    PetscCheck(usfft->indim[i] > 0,PETSC_COMM_SELF,PETSC_ERR_USER,"indim[%d]=%d must be > 0",i,usfft->indim[i]);
    PetscCheck(usfft->outdim[i] > 0,PETSC_COMM_SELF,PETSC_ERR_USER,"outdim[%d]=%d must be > 0",i,usfft->outdim[i]);
    n *= usfft->indim[i];
    m *= usfft->outdim[i];
  }
  N        = n*usfft->dof;
  M        = m*usfft->dof;
  PetscCall(MatSetSizes(*A,M,N,M,N)); /* "in size" is the number of columns, "out size" is the number of rows" */
  PetscCall(PetscObjectChangeTypeName((PetscObject)*A,MATSEQUSFFT));
  usfft->m = m; usfft->n = n; usfft->M = M; usfft->N = N;
  /* FFTW */
  usfft->p_forward  = 0;
  usfft->p_backward = 0;
  usfft->p_flag     = FFTW_ESTIMATE;
  /* set Mat ops */
  (*A)->ops->mult          = MatMult_SeqUSFFT;
  (*A)->ops->multtranspose = MatMultTranspose_SeqUSFFT;
  (*A)->assembled          = PETSC_TRUE;
  (*A)->ops->destroy       = MatDestroy_SeqUSFFT;
  /* get runtime options */
  PetscOptionsBegin(((PetscObject)(*A))->comm,((PetscObject)(*A))->prefix,"USFFT Options","Mat");
  PetscCall(PetscOptionsEList("-mat_usfft_fftw_plannerflags","Planner Flags","None",p_flags,4,p_flags[0],&p_flag,&flg));
  if (flg) usfft->p_flag = (unsigned)p_flag;
  PetscOptionsEnd();
  PetscFunctionReturn(PETSC_SUCCESS);
} /* MatCreateSeqUSFFT() */

#endif
