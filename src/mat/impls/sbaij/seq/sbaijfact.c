/* Using Modified Sparse Row (MSR) storage.
See page 85, "Iterative Methods ..." by Saad. */

/*$Id: sbaijfact.c,v 1.41 2000/11/02 19:35:20 hzhang Exp hzhang $*/
/*
    Symbolic U^T*D*U factorization for SBAIJ format. Modified from SSF of YSMP.
*/
#include "sbaij.h"
#include "src/mat/impls/baij/seq/baij.h" 
#include "src/vec/vecimpl.h"
#include "src/inline/ilu.h"
#include "include/petscis.h"

#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorSymbolic_SeqSBAIJ"
int MatCholeskyFactorSymbolic_SeqSBAIJ(Mat A,IS perm,PetscReal f,Mat *B)
{
  Mat_SeqSBAIJ *a = (Mat_SeqSBAIJ*)A->data,*b;
  int         *rip,ierr,i,mbs = a->mbs,*ai,*aj;
  int         *jutmp,bs = a->bs,bs2=a->bs2;
  int         m,nzi,realloc = 0;
  int         *jl,*q,jumin,jmin,jmax,juptr,nzk,qm,*iu,*ju,k,j,vj,umax,maxadd;
  /* PetscTruth  ident; */

  PetscFunctionBegin;
  PetscValidHeaderSpecific(perm,IS_COOKIE);
  if (A->M != A->N) SETERRQ(PETSC_ERR_ARG_WRONG,"matrix must be square");

  /* check whether perm is the identity mapping */
  /*
  ierr = ISView(perm, VIEWER_STDOUT_SELF);CHKERRA(ierr);
  ierr = ISIdentity(perm,&ident);CHKERRQ(ierr);
  printf("ident = %d\n", ident);
  */   
  ierr = ISGetIndices(perm,&rip);CHKERRQ(ierr);   
  for (i=0; i<mbs; i++){  
    if (rip[i] != i){
      a->permute = PETSC_TRUE;
      /* printf("non-trivial perm\n"); */
      break;
    }
  }

  if (!a->permute){ /* without permutation */
    ai = a->i; aj = a->j;
  } else {       /* non-trivial permutation */    
    ierr = MatReorderingSeqSBAIJ(A, perm);CHKERRA(ierr);   
    ai = a->inew; aj = a->jnew;
  }
  
  /* initialization */
  /* Don't know how many column pointers are needed so estimate. 
     Use Modified Sparse Row storage for u and ju, see Sasd pp.85 */
  iu   = (int*)PetscMalloc((mbs+1)*sizeof(int));CHKPTRQ(iu);
  umax = (int)(f*ai[mbs] + 1); umax += mbs + 1; 
  ju   = (int*)PetscMalloc(umax*sizeof(int));CHKPTRQ(ju);
  iu[0] = mbs+1; 
  juptr = mbs;
  jl =  (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  q  =  (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(q);
  for (i=0; i<mbs; i++){
    jl[i] = mbs; q[i] = 0;
  }

  /* for each row k */
  for (k=0; k<mbs; k++){   
    nzk = 0; /* num. of nz blocks in k-th block row with diagonal block excluded */
    q[k] = mbs;
    /* initialize nonzero structure of k-th row to row rip[k] of A */
    jmin = ai[rip[k]];
    jmax = ai[rip[k]+1];
    for (j=jmin; j<jmax; j++){
      vj = rip[aj[j]]; /* col. value */
      if(vj > k){
        qm = k; 
        do {
          m  = qm; qm = q[m];
        } while(qm < vj);
        if (qm == vj) {
          printf(" error: duplicate entry in A\n"); break;
        }     
        nzk++;
        q[m] = vj;
        q[vj] = qm;  
      } /* if(vj > k) */
    } /* for (j=jmin; j<jmax; j++) */

    /* modify nonzero structure of k-th row by computing fill-in
       for each row i to be merged in */
    i = k; 
    i = jl[i]; /* next pivot row (== mbs for symbolic factorization) */
    /* printf(" next pivot row i=%d\n",i); */
    while (i < mbs){
      /* merge row i into k-th row */
      nzi = iu[i+1] - (iu[i]+1);
      jmin = iu[i] + 1; jmax = iu[i] + nzi;
      qm = k;
      for (j=jmin; j<jmax+1; j++){
        vj = ju[j];
        do {
          m = qm; qm = q[m];
        } while (qm < vj);
        if (qm != vj){
         nzk++; q[m] = vj; q[vj] = qm; qm = vj;
        }
      } 
      i = jl[i]; /* next pivot row */     
    }  
   
    /* add k to row list for first nonzero element in k-th row */
    if (nzk > 0){
      i = q[k]; /* col value of first nonzero element in U(k, k+1:mbs-1) */    
      jl[k] = jl[i]; jl[i] = k;
    } 
    iu[k+1] = iu[k] + nzk;   /* printf(" iu[%d]=%d, umax=%d\n", k+1, iu[k+1],umax);*/

    /* allocate more space to ju if needed */
    if (iu[k+1] > umax) { printf("allocate more space, iu[%d]=%d > umax=%d\n",k+1, iu[k+1],umax);
      /* estimate how much additional space we will need */
      /* use the strategy suggested by David Hysom <hysom@perch-t.icase.edu> */
      /* just double the memory each time */
      maxadd = umax;      
      if (maxadd < nzk) maxadd = (mbs-k)*(nzk+1)/2;
      umax += maxadd;

      /* allocate a longer ju */
      jutmp = (int*)PetscMalloc(umax*sizeof(int));CHKPTRQ(jutmp);
      ierr  = PetscMemcpy(jutmp,ju,iu[k]*sizeof(int));CHKERRQ(ierr);
      ierr  = PetscFree(ju);CHKERRQ(ierr);       
      ju    = jutmp; 
      realloc++; /* count how many times we realloc */
    }

    /* save nonzero structure of k-th row in ju */
    i=k;
    jumin = juptr + 1; juptr += nzk; 
    for (j=jumin; j<juptr+1; j++){
      i=q[i];
      ju[j]=i;
    }     
  } 

  if (ai[mbs] != 0) {
    PetscReal af = ((PetscReal)iu[mbs])/((PetscReal)ai[mbs]);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:Reallocs %d Fill ratio:given %g needed %g\n",realloc,f,af);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:Run with -pc_lu_fill %g or use \n",af);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:PCLUSetFill(pc,%g);\n",af);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:for best performance.\n");
  } else {
     PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:Empty matrix.\n");
  }

  ierr = ISRestoreIndices(perm,&rip);CHKERRQ(ierr);
  ierr = PetscFree(q);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr);

  /* put together the new matrix */
  ierr = MatCreateSeqSBAIJ(A->comm,bs,bs*mbs,bs*mbs,0,PETSC_NULL,B);CHKERRQ(ierr);
  /* PLogObjectParent(*B,iperm); */
  b = (Mat_SeqSBAIJ*)(*B)->data;
  ierr = PetscFree(b->imax);CHKERRQ(ierr);
  b->singlemalloc = PETSC_FALSE;
  /* the next line frees the default space generated by the Create() */
  ierr = PetscFree(b->a);CHKERRQ(ierr);
  ierr = PetscFree(b->ilen);CHKERRQ(ierr);
  b->a          = (MatScalar*)PetscMalloc((iu[mbs]+1)*sizeof(MatScalar)*bs2);CHKPTRQ(b->a);
  b->j          = ju;
  b->i          = iu;
  b->diag       = 0;
  b->ilen       = 0;
  b->imax       = 0;
  b->row        = perm;
  ierr          = PetscObjectReference((PetscObject)perm);CHKERRQ(ierr); 
  b->icol       = perm;
  ierr          = PetscObjectReference((PetscObject)perm);CHKERRQ(ierr);
  b->solve_work = (Scalar*)PetscMalloc((bs*mbs+bs)*sizeof(Scalar));CHKPTRQ(b->solve_work);
  /* In b structure:  Free imax, ilen, old a, old j.  
     Allocate idnew, solve_work, new a, new j */
  PLogObjectMemory(*B,(iu[mbs]-mbs)*(sizeof(int)+sizeof(MatScalar)));
  b->s_maxnz = b->s_nz = iu[mbs];
  
  (*B)->factor                 = FACTOR_CHOLESKY;
  (*B)->info.factor_mallocs    = realloc;
  (*B)->info.fill_ratio_given  = f;
  if (ai[mbs] != 0) {
    (*B)->info.fill_ratio_needed = ((PetscReal)iu[mbs])/((PetscReal)ai[mbs]);
  } else {
    (*B)->info.fill_ratio_needed = 0.0;
  }

  PetscFunctionReturn(0); 
}

#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_N"
int MatCholeskyFactorNumeric_SeqSBAIJ_N(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 perm = b->row;
  int                *perm_ptr,ierr,i,j,mbs=a->mbs,*bi=b->i,*bj=b->j;
  int                *ai,*aj,*a2anew,k,k1,jmin,jmax,*jl,*il,vj,nexti,ili;
  int                bs=a->bs,bs2 = a->bs2;
  MatScalar          *ba = b->a,*aa,*ap,*dk,*uik;
  MatScalar          *u,*diag,*rtmp,*rtmp_ptr;
  MatScalar          *W,*work;
  int                *pivots;

  PetscFunctionBegin;
  /* initialization */
  printf("called MatCholeskyFactorNumeric_SeqSBAIJ_N \n");
  rtmp  = (MatScalar*)PetscMalloc(bs2*mbs*sizeof(MatScalar));CHKPTRQ(rtmp);
  ierr  = PetscMemzero(rtmp,bs2*mbs*sizeof(MatScalar));CHKERRQ(ierr); 
  il    = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl    = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    jl[i] = mbs; il[0] = 0;
  }
  dk    = (MatScalar*)PetscMalloc(bs2*sizeof(MatScalar));CHKPTRQ(dk);
  uik   = (MatScalar*)PetscMalloc(bs2*sizeof(MatScalar));CHKPTRQ(uik);
  W     = (MatScalar*)PetscMalloc(bs2*sizeof(MatScalar));CHKPTRQ(W);
  work  = (MatScalar*)PetscMalloc(bs*sizeof(MatScalar));CHKPTRQ(work);
  pivots= (int*)PetscMalloc(bs*sizeof(int));CHKPTRQ(pivots);
 
  ierr  = ISGetIndices(perm,&perm_ptr);CHKERRQ(ierr);
  
  /* check permutation */
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(bs2*ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,bs2*ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    a2anew  = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(a2anew); 
    ierr= PetscMemcpy(a2anew,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    for (i=0; i<mbs; i++){
      jmin = ai[i]; jmax = ai[i+1];
      for (j=jmin; j<jmax; j++){
        while (a2anew[j] != j){  
          k = a2anew[j]; a2anew[j] = a2anew[k]; a2anew[k] = k;  
          for (k1=0; k1<bs2; k1++){
            dk[k1]       = aa[k*bs2+k1]; 
            aa[k*bs2+k1] = aa[j*bs2+k1]; 
            aa[j*bs2+k1] = dk[k1];   
          }
        }
        /* transform columnoriented blocks that lie in the lower triangle to roworiented blocks */
        if (i > aj[j]){ 
          /* printf("change orientation, row: %d, col: %d\n",i,aj[j]); */
          ap = aa + j*bs2;                     /* ptr to the beginning of j-th block of aa */
          for (k=0; k<bs2; k++) dk[k] = ap[k]; /* dk <- j-th block of aa */
          for (k=0; k<bs; k++){               /* j-th block of aa <- dk^T */
            for (k1=0; k1<bs; k1++) *ap++ = dk[k + bs*k1];         
          }
        }
      }
    }
    ierr = PetscFree(a2anew);CHKERRA(ierr); 
  }
  
  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[perm_ptr[k]]; jmax = ai[perm_ptr[k]+1];
    if (jmin < jmax) {
      ap = aa + jmin*bs2;
      for (j = jmin; j < jmax; j++){
        vj = perm_ptr[aj[j]];         /* block col. index */  
        rtmp_ptr = rtmp + vj*bs2;
        for (i=0; i<bs2; i++) *rtmp_ptr++ = *ap++;        
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    ierr = PetscMemcpy(dk,rtmp+k*bs2,bs2*sizeof(MatScalar));CHKERRQ(ierr); 
    i = jl[k]; /* first row to be added to k_th row  */  

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */

      /* uik = -inv(Di)*U_bar(i,k) */
      diag = ba + i*bs2;
      u    = ba + ili*bs2;
      ierr = PetscMemzero(uik,bs2*sizeof(MatScalar));CHKERRQ(ierr);
      Kernel_A_gets_A_minus_B_times_C(bs,uik,diag,u);
      
      /* update D(k) += -U(i,k)^T * U_bar(i,k) */
      Kernel_A_gets_A_plus_Btranspose_times_C(bs,dk,uik,u);
 
      /* update -U(i,k) */
      ierr = PetscMemcpy(ba+ili*bs2,uik,bs2*sizeof(MatScalar));CHKERRQ(ierr); 

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) {
          /* rtmp += -U(i,k)^T * U_bar(i,j) */
          rtmp_ptr = rtmp + bj[j]*bs2;
          u = ba + j*bs2;
          Kernel_A_gets_A_plus_Btranspose_times_C(bs,rtmp_ptr,uik,u);
        }
      
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;      
    }

    /* save nonzero entries in k-th row of U ... */

    /* invert diagonal block */
    diag = ba+k*bs2;
    ierr = PetscMemcpy(diag,dk,bs2*sizeof(MatScalar));CHKERRQ(ierr);   
    Kernel_A_gets_inverse_A(bs,diag,pivots,work);
    
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j];           /* block col. index of U */
         u   = ba + j*bs2;
         rtmp_ptr = rtmp + vj*bs2;        
         for (k1=0; k1<bs2; k1++){
           *u++        = *rtmp_ptr; 
           *rtmp_ptr++ = 0.0;
         }
      } 
      
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }    
  } 

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  ierr = PetscFree(dk);CHKERRQ(ierr);
  ierr = PetscFree(uik);CHKERRQ(ierr);
  ierr = PetscFree(W);CHKERRQ(ierr);
  ierr = PetscFree(work);CHKERRQ(ierr);
  ierr = PetscFree(pivots);CHKERRQ(ierr);
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(perm,&perm_ptr);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*bs*bs2*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/* Version for when blocks are 7 by 7 */
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_7"
int MatCholeskyFactorNumeric_SeqSBAIJ_7(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 perm = b->row;
  int                *perm_ptr,ierr,i,j,mbs=a->mbs,*bi=b->i,*bj=b->j;
  int                *ai,*aj,*a2anew,k,k1,jmin,jmax,*jl,*il,vj,nexti,ili;
  MatScalar          *ba = b->a,*aa,*ap,*dk,*uik;
  MatScalar          *u,*d,*w,*wp;

  PetscFunctionBegin;
  /* initialization */
  printf("called MatCholeskyFactorNumeric_SeqSBAIJ_7 \n");
  w  = (MatScalar*)PetscMalloc(49*mbs*sizeof(MatScalar));CHKPTRQ(w);
  ierr = PetscMemzero(w,49*mbs*sizeof(MatScalar));CHKERRQ(ierr); 
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    jl[i] = mbs; il[0] = 0;
  }
  dk    = (MatScalar*)PetscMalloc(49*sizeof(MatScalar));CHKPTRQ(dk);
  uik   = (MatScalar*)PetscMalloc(49*sizeof(MatScalar));CHKPTRQ(uik);     
  ierr  = ISGetIndices(perm,&perm_ptr);CHKERRQ(ierr);

  /* check permutation */
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(49*ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,49*ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    a2anew  = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(a2anew); 
    ierr= PetscMemcpy(a2anew,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    for (i=0; i<mbs; i++){
      jmin = ai[i]; jmax = ai[i+1];
      for (j=jmin; j<jmax; j++){
        while (a2anew[j] != j){  
          k = a2anew[j]; a2anew[j] = a2anew[k]; a2anew[k] = k;  
          for (k1=0; k1<49; k1++){
            dk[k1]       = aa[k*49+k1]; 
            aa[k*49+k1] = aa[j*49+k1]; 
            aa[j*49+k1] = dk[k1];   
          }
        }
        /* transform columnoriented blocks that lie in the lower triangle to roworiented blocks */
        if (i > aj[j]){ 
          /* printf("change orientation, row: %d, col: %d\n",i,aj[j]); */
          ap = aa + j*49;                     /* ptr to the beginning of j-th block of aa */
          for (k=0; k<49; k++) dk[k] = ap[k]; /* dk <- j-th block of aa */
          for (k=0; k<7; k++){               /* j-th block of aa <- dk^T */
            for (k1=0; k1<7; k1++) *ap++ = dk[k + 7*k1];         
          }
        }
      }
    }
    ierr = PetscFree(a2anew);CHKERRA(ierr); 
  }
  
  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[perm_ptr[k]]; jmax = ai[perm_ptr[k]+1];
    if (jmin < jmax) {
      ap = aa + jmin*49;
      for (j = jmin; j < jmax; j++){
        vj = perm_ptr[aj[j]];         /* block col. index */  
        wp = w + vj*49;
        for (i=0; i<49; i++) *wp++ = *ap++;        
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    ierr = PetscMemcpy(dk,w+k*49,49*sizeof(MatScalar));CHKERRQ(ierr); 
    i = jl[k]; /* first row to be added to k_th row  */  

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */

      /* uik = -inv(Di)*U_bar(i,k) */
      d = ba + i*49;
      u    = ba + ili*49;

      uik[0] = -(d[0]*u[0] + d[7]*u[1]+ d[14]*u[2]+ d[21]*u[3]+ d[28]*u[4]+ d[35]*u[5]+ d[42]*u[6]);
      uik[1] = -(d[1]*u[0] + d[8]*u[1]+ d[15]*u[2]+ d[22]*u[3]+ d[29]*u[4]+ d[36]*u[5]+ d[43]*u[6]);
      uik[2] = -(d[2]*u[0] + d[9]*u[1]+ d[16]*u[2]+ d[23]*u[3]+ d[30]*u[4]+ d[37]*u[5]+ d[44]*u[6]);
      uik[3] = -(d[3]*u[0]+ d[10]*u[1]+ d[17]*u[2]+ d[24]*u[3]+ d[31]*u[4]+ d[38]*u[5]+ d[45]*u[6]);
      uik[4] = -(d[4]*u[0]+ d[11]*u[1]+ d[18]*u[2]+ d[25]*u[3]+ d[32]*u[4]+ d[39]*u[5]+ d[46]*u[6]);
      uik[5] = -(d[5]*u[0]+ d[12]*u[1]+ d[19]*u[2]+ d[26]*u[3]+ d[33]*u[4]+ d[40]*u[5]+ d[47]*u[6]);
      uik[6] = -(d[6]*u[0]+ d[13]*u[1]+ d[20]*u[2]+ d[27]*u[3]+ d[34]*u[4]+ d[41]*u[5]+ d[48]*u[6]);

      uik[7] = -(d[0]*u[7] + d[7]*u[8]+ d[14]*u[9]+ d[21]*u[10]+ d[28]*u[11]+ d[35]*u[12]+ d[42]*u[13]);
      uik[8] = -(d[1]*u[7] + d[8]*u[8]+ d[15]*u[9]+ d[22]*u[10]+ d[29]*u[11]+ d[36]*u[12]+ d[43]*u[13]);
      uik[9] = -(d[2]*u[7] + d[9]*u[8]+ d[16]*u[9]+ d[23]*u[10]+ d[30]*u[11]+ d[37]*u[12]+ d[44]*u[13]);
      uik[10]= -(d[3]*u[7]+ d[10]*u[8]+ d[17]*u[9]+ d[24]*u[10]+ d[31]*u[11]+ d[38]*u[12]+ d[45]*u[13]);
      uik[11]= -(d[4]*u[7]+ d[11]*u[8]+ d[18]*u[9]+ d[25]*u[10]+ d[32]*u[11]+ d[39]*u[12]+ d[46]*u[13]);
      uik[12]= -(d[5]*u[7]+ d[12]*u[8]+ d[19]*u[9]+ d[26]*u[10]+ d[33]*u[11]+ d[40]*u[12]+ d[47]*u[13]);
      uik[13]= -(d[6]*u[7]+ d[13]*u[8]+ d[20]*u[9]+ d[27]*u[10]+ d[34]*u[11]+ d[41]*u[12]+ d[48]*u[13]);

      uik[14]= -(d[0]*u[14] + d[7]*u[15]+ d[14]*u[16]+ d[21]*u[17]+ d[28]*u[18]+ d[35]*u[19]+ d[42]*u[20]);
      uik[15]= -(d[1]*u[14] + d[8]*u[15]+ d[15]*u[16]+ d[22]*u[17]+ d[29]*u[18]+ d[36]*u[19]+ d[43]*u[20]);
      uik[16]= -(d[2]*u[14] + d[9]*u[15]+ d[16]*u[16]+ d[23]*u[17]+ d[30]*u[18]+ d[37]*u[19]+ d[44]*u[20]);
      uik[17]= -(d[3]*u[14]+ d[10]*u[15]+ d[17]*u[16]+ d[24]*u[17]+ d[31]*u[18]+ d[38]*u[19]+ d[45]*u[20]);
      uik[18]= -(d[4]*u[14]+ d[11]*u[15]+ d[18]*u[16]+ d[25]*u[17]+ d[32]*u[18]+ d[39]*u[19]+ d[46]*u[20]);
      uik[19]= -(d[5]*u[14]+ d[12]*u[15]+ d[19]*u[16]+ d[26]*u[17]+ d[33]*u[18]+ d[40]*u[19]+ d[47]*u[20]);
      uik[20]= -(d[6]*u[14]+ d[13]*u[15]+ d[20]*u[16]+ d[27]*u[17]+ d[34]*u[18]+ d[41]*u[19]+ d[48]*u[20]);

      uik[21]= -(d[0]*u[21] + d[7]*u[22]+ d[14]*u[23]+ d[21]*u[24]+ d[28]*u[25]+ d[35]*u[26]+ d[42]*u[27]);
      uik[22]= -(d[1]*u[21] + d[8]*u[22]+ d[15]*u[23]+ d[22]*u[24]+ d[29]*u[25]+ d[36]*u[26]+ d[43]*u[27]);
      uik[23]= -(d[2]*u[21] + d[9]*u[22]+ d[16]*u[23]+ d[23]*u[24]+ d[30]*u[25]+ d[37]*u[26]+ d[44]*u[27]);
      uik[24]= -(d[3]*u[21]+ d[10]*u[22]+ d[17]*u[23]+ d[24]*u[24]+ d[31]*u[25]+ d[38]*u[26]+ d[45]*u[27]);
      uik[25]= -(d[4]*u[21]+ d[11]*u[22]+ d[18]*u[23]+ d[25]*u[24]+ d[32]*u[25]+ d[39]*u[26]+ d[46]*u[27]);
      uik[26]= -(d[5]*u[21]+ d[12]*u[22]+ d[19]*u[23]+ d[26]*u[24]+ d[33]*u[25]+ d[40]*u[26]+ d[47]*u[27]);
      uik[27]= -(d[6]*u[21]+ d[13]*u[22]+ d[20]*u[23]+ d[27]*u[24]+ d[34]*u[25]+ d[41]*u[26]+ d[48]*u[27]);

      uik[28]= -(d[0]*u[28] + d[7]*u[29]+ d[14]*u[30]+ d[21]*u[31]+ d[28]*u[32]+ d[35]*u[33]+ d[42]*u[34]);
      uik[29]= -(d[1]*u[28] + d[8]*u[29]+ d[15]*u[30]+ d[22]*u[31]+ d[29]*u[32]+ d[36]*u[33]+ d[43]*u[34]);
      uik[30]= -(d[2]*u[28] + d[9]*u[29]+ d[16]*u[30]+ d[23]*u[31]+ d[30]*u[32]+ d[37]*u[33]+ d[44]*u[34]);
      uik[31]= -(d[3]*u[28]+ d[10]*u[29]+ d[17]*u[30]+ d[24]*u[31]+ d[31]*u[32]+ d[38]*u[33]+ d[45]*u[34]);
      uik[32]= -(d[4]*u[28]+ d[11]*u[29]+ d[18]*u[30]+ d[25]*u[31]+ d[32]*u[32]+ d[39]*u[33]+ d[46]*u[34]);
      uik[33]= -(d[5]*u[28]+ d[12]*u[29]+ d[19]*u[30]+ d[26]*u[31]+ d[33]*u[32]+ d[40]*u[33]+ d[47]*u[34]);
      uik[34]= -(d[6]*u[28]+ d[13]*u[29]+ d[20]*u[30]+ d[27]*u[31]+ d[34]*u[32]+ d[41]*u[33]+ d[48]*u[34]);

      uik[35]= -(d[0]*u[35] + d[7]*u[36]+ d[14]*u[37]+ d[21]*u[38]+ d[28]*u[39]+ d[35]*u[40]+ d[42]*u[41]);
      uik[36]= -(d[1]*u[35] + d[8]*u[36]+ d[15]*u[37]+ d[22]*u[38]+ d[29]*u[39]+ d[36]*u[40]+ d[43]*u[41]);
      uik[37]= -(d[2]*u[35] + d[9]*u[36]+ d[16]*u[37]+ d[23]*u[38]+ d[30]*u[39]+ d[37]*u[40]+ d[44]*u[41]);
      uik[38]= -(d[3]*u[35]+ d[10]*u[36]+ d[17]*u[37]+ d[24]*u[38]+ d[31]*u[39]+ d[38]*u[40]+ d[45]*u[41]);
      uik[39]= -(d[4]*u[35]+ d[11]*u[36]+ d[18]*u[37]+ d[25]*u[38]+ d[32]*u[39]+ d[39]*u[40]+ d[46]*u[41]);
      uik[40]= -(d[5]*u[35]+ d[12]*u[36]+ d[19]*u[37]+ d[26]*u[38]+ d[33]*u[39]+ d[40]*u[40]+ d[47]*u[41]);
      uik[41]= -(d[6]*u[35]+ d[13]*u[36]+ d[20]*u[37]+ d[27]*u[38]+ d[34]*u[39]+ d[41]*u[40]+ d[48]*u[41]);

      uik[42]= -(d[0]*u[42] + d[7]*u[43]+ d[14]*u[44]+ d[21]*u[45]+ d[28]*u[46]+ d[35]*u[47]+ d[42]*u[48]);
      uik[43]= -(d[1]*u[42] + d[8]*u[43]+ d[15]*u[44]+ d[22]*u[45]+ d[29]*u[46]+ d[36]*u[47]+ d[43]*u[48]);
      uik[44]= -(d[2]*u[42] + d[9]*u[43]+ d[16]*u[44]+ d[23]*u[45]+ d[30]*u[46]+ d[37]*u[47]+ d[44]*u[48]);
      uik[45]= -(d[3]*u[42]+ d[10]*u[43]+ d[17]*u[44]+ d[24]*u[45]+ d[31]*u[46]+ d[38]*u[47]+ d[45]*u[48]);
      uik[46]= -(d[4]*u[42]+ d[11]*u[43]+ d[18]*u[44]+ d[25]*u[45]+ d[32]*u[46]+ d[39]*u[47]+ d[46]*u[48]);
      uik[47]= -(d[5]*u[42]+ d[12]*u[43]+ d[19]*u[44]+ d[26]*u[45]+ d[33]*u[46]+ d[40]*u[47]+ d[47]*u[48]);
      uik[48]= -(d[6]*u[42]+ d[13]*u[43]+ d[20]*u[44]+ d[27]*u[45]+ d[34]*u[46]+ d[41]*u[47]+ d[48]*u[48]);

      /* update D(k) += -U(i,k)^T * U_bar(i,k) */  
      dk[0]+=  uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3] + uik[4]*u[4] + uik[5]*u[5] + uik[6]*u[6];
      dk[1]+=  uik[7]*u[0] + uik[8]*u[1] + uik[9]*u[2]+ uik[10]*u[3]+ uik[11]*u[4]+ uik[12]*u[5]+ uik[13]*u[6];
      dk[2]+= uik[14]*u[0]+ uik[15]*u[1]+ uik[16]*u[2]+ uik[17]*u[3]+ uik[18]*u[4]+ uik[19]*u[5]+ uik[20]*u[6];
      dk[3]+= uik[21]*u[0]+ uik[22]*u[1]+ uik[23]*u[2]+ uik[24]*u[3]+ uik[25]*u[4]+ uik[26]*u[5]+ uik[27]*u[6];
      dk[4]+= uik[28]*u[0]+ uik[29]*u[1]+ uik[30]*u[2]+ uik[31]*u[3]+ uik[32]*u[4]+ uik[33]*u[5]+ uik[34]*u[6];
      dk[5]+= uik[35]*u[0]+ uik[36]*u[1]+ uik[37]*u[2]+ uik[38]*u[3]+ uik[39]*u[4]+ uik[40]*u[5]+ uik[41]*u[6];
      dk[6]+= uik[42]*u[0]+ uik[43]*u[1]+ uik[44]*u[2]+ uik[45]*u[3]+ uik[46]*u[4]+ uik[47]*u[5]+ uik[48]*u[6];

      dk[7]+=  uik[0]*u[7] + uik[1]*u[8] + uik[2]*u[9] + uik[3]*u[10] + uik[4]*u[11] + uik[5]*u[12] + uik[6]*u[13];
      dk[8]+=  uik[7]*u[7] + uik[8]*u[8] + uik[9]*u[9]+ uik[10]*u[10]+ uik[11]*u[11]+ uik[12]*u[12]+ uik[13]*u[13];
      dk[9]+= uik[14]*u[7]+ uik[15]*u[8]+ uik[16]*u[9]+ uik[17]*u[10]+ uik[18]*u[11]+ uik[19]*u[12]+ uik[20]*u[13];
      dk[10]+=uik[21]*u[7]+ uik[22]*u[8]+ uik[23]*u[9]+ uik[24]*u[10]+ uik[25]*u[11]+ uik[26]*u[12]+ uik[27]*u[13];
      dk[11]+=uik[28]*u[7]+ uik[29]*u[8]+ uik[30]*u[9]+ uik[31]*u[10]+ uik[32]*u[11]+ uik[33]*u[12]+ uik[34]*u[13];
      dk[12]+=uik[35]*u[7]+ uik[36]*u[8]+ uik[37]*u[9]+ uik[38]*u[10]+ uik[39]*u[11]+ uik[40]*u[12]+ uik[41]*u[13];
      dk[13]+=uik[42]*u[7]+ uik[43]*u[8]+ uik[44]*u[9]+ uik[45]*u[10]+ uik[46]*u[11]+ uik[47]*u[12]+ uik[48]*u[13];

      dk[14]+=  uik[0]*u[14] + uik[1]*u[15] + uik[2]*u[16] + uik[3]*u[17] + uik[4]*u[18] + uik[5]*u[19] + uik[6]*u[20];
      dk[15]+=  uik[7]*u[14] + uik[8]*u[15] + uik[9]*u[16]+ uik[10]*u[17]+ uik[11]*u[18]+ uik[12]*u[19]+ uik[13]*u[20];
      dk[16]+= uik[14]*u[14]+ uik[15]*u[15]+ uik[16]*u[16]+ uik[17]*u[17]+ uik[18]*u[18]+ uik[19]*u[19]+ uik[20]*u[20];
      dk[17]+= uik[21]*u[14]+ uik[22]*u[15]+ uik[23]*u[16]+ uik[24]*u[17]+ uik[25]*u[18]+ uik[26]*u[19]+ uik[27]*u[20];
      dk[18]+= uik[28]*u[14]+ uik[29]*u[15]+ uik[30]*u[16]+ uik[31]*u[17]+ uik[32]*u[18]+ uik[33]*u[19]+ uik[34]*u[20];
      dk[19]+= uik[35]*u[14]+ uik[36]*u[15]+ uik[37]*u[16]+ uik[38]*u[17]+ uik[39]*u[18]+ uik[40]*u[19]+ uik[41]*u[20];
      dk[20]+= uik[42]*u[14]+ uik[43]*u[15]+ uik[44]*u[16]+ uik[45]*u[17]+ uik[46]*u[18]+ uik[47]*u[19]+ uik[48]*u[20];

      dk[21]+=  uik[0]*u[21] + uik[1]*u[22] + uik[2]*u[23] + uik[3]*u[24] + uik[4]*u[25] + uik[5]*u[26] + uik[6]*u[27];
      dk[22]+=  uik[7]*u[21] + uik[8]*u[22] + uik[9]*u[23]+ uik[10]*u[24]+ uik[11]*u[25]+ uik[12]*u[26]+ uik[13]*u[27];
      dk[23]+= uik[14]*u[21]+ uik[15]*u[22]+ uik[16]*u[23]+ uik[17]*u[24]+ uik[18]*u[25]+ uik[19]*u[26]+ uik[20]*u[27];
      dk[24]+= uik[21]*u[21]+ uik[22]*u[22]+ uik[23]*u[23]+ uik[24]*u[24]+ uik[25]*u[25]+ uik[26]*u[26]+ uik[27]*u[27];
      dk[25]+= uik[28]*u[21]+ uik[29]*u[22]+ uik[30]*u[23]+ uik[31]*u[24]+ uik[32]*u[25]+ uik[33]*u[26]+ uik[34]*u[27];
      dk[26]+= uik[35]*u[21]+ uik[36]*u[22]+ uik[37]*u[23]+ uik[38]*u[24]+ uik[39]*u[25]+ uik[40]*u[26]+ uik[41]*u[27];
      dk[27]+= uik[42]*u[21]+ uik[43]*u[22]+ uik[44]*u[23]+ uik[45]*u[24]+ uik[46]*u[25]+ uik[47]*u[26]+ uik[48]*u[27];

      dk[28]+=  uik[0]*u[28] + uik[1]*u[29] + uik[2]*u[30] + uik[3]*u[31] + uik[4]*u[32] + uik[5]*u[33] + uik[6]*u[34];
      dk[29]+=  uik[7]*u[28] + uik[8]*u[29] + uik[9]*u[30]+ uik[10]*u[31]+ uik[11]*u[32]+ uik[12]*u[33]+ uik[13]*u[34];
      dk[30]+= uik[14]*u[28]+ uik[15]*u[29]+ uik[16]*u[30]+ uik[17]*u[31]+ uik[18]*u[32]+ uik[19]*u[33]+ uik[20]*u[34];
      dk[31]+= uik[21]*u[28]+ uik[22]*u[29]+ uik[23]*u[30]+ uik[24]*u[31]+ uik[25]*u[32]+ uik[26]*u[33]+ uik[27]*u[34];
      dk[32]+= uik[28]*u[28]+ uik[29]*u[29]+ uik[30]*u[30]+ uik[31]*u[31]+ uik[32]*u[32]+ uik[33]*u[33]+ uik[34]*u[34];
      dk[33]+= uik[35]*u[28]+ uik[36]*u[29]+ uik[37]*u[30]+ uik[38]*u[31]+ uik[39]*u[32]+ uik[40]*u[33]+ uik[41]*u[34];
      dk[34]+= uik[42]*u[28]+ uik[43]*u[29]+ uik[44]*u[30]+ uik[45]*u[31]+ uik[46]*u[32]+ uik[47]*u[33]+ uik[48]*u[34];

      dk[35]+=  uik[0]*u[35] + uik[1]*u[36] + uik[2]*u[37] + uik[3]*u[38] + uik[4]*u[39] + uik[5]*u[40] + uik[6]*u[41];
      dk[36]+=  uik[7]*u[35] + uik[8]*u[36] + uik[9]*u[37]+ uik[10]*u[38]+ uik[11]*u[39]+ uik[12]*u[40]+ uik[13]*u[41];
      dk[37]+= uik[14]*u[35]+ uik[15]*u[36]+ uik[16]*u[37]+ uik[17]*u[38]+ uik[18]*u[39]+ uik[19]*u[40]+ uik[20]*u[41];
      dk[38]+= uik[21]*u[35]+ uik[22]*u[36]+ uik[23]*u[37]+ uik[24]*u[38]+ uik[25]*u[39]+ uik[26]*u[40]+ uik[27]*u[41];
      dk[39]+= uik[28]*u[35]+ uik[29]*u[36]+ uik[30]*u[37]+ uik[31]*u[38]+ uik[32]*u[39]+ uik[33]*u[40]+ uik[34]*u[41];
      dk[40]+= uik[35]*u[35]+ uik[36]*u[36]+ uik[37]*u[37]+ uik[38]*u[38]+ uik[39]*u[39]+ uik[40]*u[40]+ uik[41]*u[41];
      dk[41]+= uik[42]*u[35]+ uik[43]*u[36]+ uik[44]*u[37]+ uik[45]*u[38]+ uik[46]*u[39]+ uik[47]*u[40]+ uik[48]*u[41];

      dk[42]+=  uik[0]*u[42] + uik[1]*u[43] + uik[2]*u[44] + uik[3]*u[45] + uik[4]*u[46] + uik[5]*u[47] + uik[6]*u[48];
      dk[43]+=  uik[7]*u[42] + uik[8]*u[43] + uik[9]*u[44]+ uik[10]*u[45]+ uik[11]*u[46]+ uik[12]*u[47]+ uik[13]*u[48];
      dk[44]+= uik[14]*u[42]+ uik[15]*u[43]+ uik[16]*u[44]+ uik[17]*u[45]+ uik[18]*u[46]+ uik[19]*u[47]+ uik[20]*u[48];
      dk[45]+= uik[21]*u[42]+ uik[22]*u[43]+ uik[23]*u[44]+ uik[24]*u[45]+ uik[25]*u[46]+ uik[26]*u[47]+ uik[27]*u[48];
      dk[46]+= uik[28]*u[42]+ uik[29]*u[43]+ uik[30]*u[44]+ uik[31]*u[45]+ uik[32]*u[46]+ uik[33]*u[47]+ uik[34]*u[48];
      dk[47]+= uik[35]*u[42]+ uik[36]*u[43]+ uik[37]*u[44]+ uik[38]*u[45]+ uik[39]*u[46]+ uik[40]*u[47]+ uik[41]*u[48];
      dk[48]+= uik[42]*u[42]+ uik[43]*u[43]+ uik[44]*u[44]+ uik[45]*u[45]+ uik[46]*u[46]+ uik[47]*u[47]+ uik[48]*u[48];

      /* update -U(i,k) */
      ierr = PetscMemcpy(ba+ili*49,uik,49*sizeof(MatScalar));CHKERRQ(ierr); 

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) {
          /* w += -U(i,k)^T * U_bar(i,j) */
          wp = w + bj[j]*49;
          u = ba + j*49;

          wp[0]+=  uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3] + uik[4]*u[4] + uik[5]*u[5] + uik[6]*u[6];
          wp[1]+=  uik[7]*u[0] + uik[8]*u[1] + uik[9]*u[2]+ uik[10]*u[3]+ uik[11]*u[4]+ uik[12]*u[5]+ uik[13]*u[6];
          wp[2]+= uik[14]*u[0]+ uik[15]*u[1]+ uik[16]*u[2]+ uik[17]*u[3]+ uik[18]*u[4]+ uik[19]*u[5]+ uik[20]*u[6];
          wp[3]+= uik[21]*u[0]+ uik[22]*u[1]+ uik[23]*u[2]+ uik[24]*u[3]+ uik[25]*u[4]+ uik[26]*u[5]+ uik[27]*u[6];
          wp[4]+= uik[28]*u[0]+ uik[29]*u[1]+ uik[30]*u[2]+ uik[31]*u[3]+ uik[32]*u[4]+ uik[33]*u[5]+ uik[34]*u[6];
          wp[5]+= uik[35]*u[0]+ uik[36]*u[1]+ uik[37]*u[2]+ uik[38]*u[3]+ uik[39]*u[4]+ uik[40]*u[5]+ uik[41]*u[6];
          wp[6]+= uik[42]*u[0]+ uik[43]*u[1]+ uik[44]*u[2]+ uik[45]*u[3]+ uik[46]*u[4]+ uik[47]*u[5]+ uik[48]*u[6];

          wp[7]+=  uik[0]*u[7] + uik[1]*u[8] + uik[2]*u[9] + uik[3]*u[10] + uik[4]*u[11] + uik[5]*u[12] + uik[6]*u[13];
          wp[8]+=  uik[7]*u[7] + uik[8]*u[8] + uik[9]*u[9]+ uik[10]*u[10]+ uik[11]*u[11]+ uik[12]*u[12]+ uik[13]*u[13];
          wp[9]+= uik[14]*u[7]+ uik[15]*u[8]+ uik[16]*u[9]+ uik[17]*u[10]+ uik[18]*u[11]+ uik[19]*u[12]+ uik[20]*u[13];
          wp[10]+=uik[21]*u[7]+ uik[22]*u[8]+ uik[23]*u[9]+ uik[24]*u[10]+ uik[25]*u[11]+ uik[26]*u[12]+ uik[27]*u[13];
          wp[11]+=uik[28]*u[7]+ uik[29]*u[8]+ uik[30]*u[9]+ uik[31]*u[10]+ uik[32]*u[11]+ uik[33]*u[12]+ uik[34]*u[13];
          wp[12]+=uik[35]*u[7]+ uik[36]*u[8]+ uik[37]*u[9]+ uik[38]*u[10]+ uik[39]*u[11]+ uik[40]*u[12]+ uik[41]*u[13];
          wp[13]+=uik[42]*u[7]+ uik[43]*u[8]+ uik[44]*u[9]+ uik[45]*u[10]+ uik[46]*u[11]+ uik[47]*u[12]+ uik[48]*u[13];

          wp[14]+=  uik[0]*u[14] + uik[1]*u[15] + uik[2]*u[16] + uik[3]*u[17] + uik[4]*u[18] + uik[5]*u[19] + uik[6]*u[20];
          wp[15]+=  uik[7]*u[14] + uik[8]*u[15] + uik[9]*u[16]+ uik[10]*u[17]+ uik[11]*u[18]+ uik[12]*u[19]+ uik[13]*u[20];
          wp[16]+= uik[14]*u[14]+ uik[15]*u[15]+ uik[16]*u[16]+ uik[17]*u[17]+ uik[18]*u[18]+ uik[19]*u[19]+ uik[20]*u[20];
          wp[17]+= uik[21]*u[14]+ uik[22]*u[15]+ uik[23]*u[16]+ uik[24]*u[17]+ uik[25]*u[18]+ uik[26]*u[19]+ uik[27]*u[20];
          wp[18]+= uik[28]*u[14]+ uik[29]*u[15]+ uik[30]*u[16]+ uik[31]*u[17]+ uik[32]*u[18]+ uik[33]*u[19]+ uik[34]*u[20];
          wp[19]+= uik[35]*u[14]+ uik[36]*u[15]+ uik[37]*u[16]+ uik[38]*u[17]+ uik[39]*u[18]+ uik[40]*u[19]+ uik[41]*u[20];
          wp[20]+= uik[42]*u[14]+ uik[43]*u[15]+ uik[44]*u[16]+ uik[45]*u[17]+ uik[46]*u[18]+ uik[47]*u[19]+ uik[48]*u[20];

          wp[21]+=  uik[0]*u[21] + uik[1]*u[22] + uik[2]*u[23] + uik[3]*u[24] + uik[4]*u[25] + uik[5]*u[26] + uik[6]*u[27];
          wp[22]+=  uik[7]*u[21] + uik[8]*u[22] + uik[9]*u[23]+ uik[10]*u[24]+ uik[11]*u[25]+ uik[12]*u[26]+ uik[13]*u[27];
          wp[23]+= uik[14]*u[21]+ uik[15]*u[22]+ uik[16]*u[23]+ uik[17]*u[24]+ uik[18]*u[25]+ uik[19]*u[26]+ uik[20]*u[27];
          wp[24]+= uik[21]*u[21]+ uik[22]*u[22]+ uik[23]*u[23]+ uik[24]*u[24]+ uik[25]*u[25]+ uik[26]*u[26]+ uik[27]*u[27];
          wp[25]+= uik[28]*u[21]+ uik[29]*u[22]+ uik[30]*u[23]+ uik[31]*u[24]+ uik[32]*u[25]+ uik[33]*u[26]+ uik[34]*u[27];
          wp[26]+= uik[35]*u[21]+ uik[36]*u[22]+ uik[37]*u[23]+ uik[38]*u[24]+ uik[39]*u[25]+ uik[40]*u[26]+ uik[41]*u[27];
          wp[27]+= uik[42]*u[21]+ uik[43]*u[22]+ uik[44]*u[23]+ uik[45]*u[24]+ uik[46]*u[25]+ uik[47]*u[26]+ uik[48]*u[27];

          wp[28]+=  uik[0]*u[28] + uik[1]*u[29] + uik[2]*u[30] + uik[3]*u[31] + uik[4]*u[32] + uik[5]*u[33] + uik[6]*u[34];
          wp[29]+=  uik[7]*u[28] + uik[8]*u[29] + uik[9]*u[30]+ uik[10]*u[31]+ uik[11]*u[32]+ uik[12]*u[33]+ uik[13]*u[34];
          wp[30]+= uik[14]*u[28]+ uik[15]*u[29]+ uik[16]*u[30]+ uik[17]*u[31]+ uik[18]*u[32]+ uik[19]*u[33]+ uik[20]*u[34];
          wp[31]+= uik[21]*u[28]+ uik[22]*u[29]+ uik[23]*u[30]+ uik[24]*u[31]+ uik[25]*u[32]+ uik[26]*u[33]+ uik[27]*u[34];
          wp[32]+= uik[28]*u[28]+ uik[29]*u[29]+ uik[30]*u[30]+ uik[31]*u[31]+ uik[32]*u[32]+ uik[33]*u[33]+ uik[34]*u[34];
          wp[33]+= uik[35]*u[28]+ uik[36]*u[29]+ uik[37]*u[30]+ uik[38]*u[31]+ uik[39]*u[32]+ uik[40]*u[33]+ uik[41]*u[34];
          wp[34]+= uik[42]*u[28]+ uik[43]*u[29]+ uik[44]*u[30]+ uik[45]*u[31]+ uik[46]*u[32]+ uik[47]*u[33]+ uik[48]*u[34];

          wp[35]+=  uik[0]*u[35] + uik[1]*u[36] + uik[2]*u[37] + uik[3]*u[38] + uik[4]*u[39] + uik[5]*u[40] + uik[6]*u[41];
          wp[36]+=  uik[7]*u[35] + uik[8]*u[36] + uik[9]*u[37]+ uik[10]*u[38]+ uik[11]*u[39]+ uik[12]*u[40]+ uik[13]*u[41];
          wp[37]+= uik[14]*u[35]+ uik[15]*u[36]+ uik[16]*u[37]+ uik[17]*u[38]+ uik[18]*u[39]+ uik[19]*u[40]+ uik[20]*u[41];
          wp[38]+= uik[21]*u[35]+ uik[22]*u[36]+ uik[23]*u[37]+ uik[24]*u[38]+ uik[25]*u[39]+ uik[26]*u[40]+ uik[27]*u[41];
          wp[39]+= uik[28]*u[35]+ uik[29]*u[36]+ uik[30]*u[37]+ uik[31]*u[38]+ uik[32]*u[39]+ uik[33]*u[40]+ uik[34]*u[41];
          wp[40]+= uik[35]*u[35]+ uik[36]*u[36]+ uik[37]*u[37]+ uik[38]*u[38]+ uik[39]*u[39]+ uik[40]*u[40]+ uik[41]*u[41];
          wp[41]+= uik[42]*u[35]+ uik[43]*u[36]+ uik[44]*u[37]+ uik[45]*u[38]+ uik[46]*u[39]+ uik[47]*u[40]+ uik[48]*u[41];

          wp[42]+=  uik[0]*u[42] + uik[1]*u[43] + uik[2]*u[44] + uik[3]*u[45] + uik[4]*u[46] + uik[5]*u[47] + uik[6]*u[48];
          wp[43]+=  uik[7]*u[42] + uik[8]*u[43] + uik[9]*u[44]+ uik[10]*u[45]+ uik[11]*u[46]+ uik[12]*u[47]+ uik[13]*u[48];
          wp[44]+= uik[14]*u[42]+ uik[15]*u[43]+ uik[16]*u[44]+ uik[17]*u[45]+ uik[18]*u[46]+ uik[19]*u[47]+ uik[20]*u[48];
          wp[45]+= uik[21]*u[42]+ uik[22]*u[43]+ uik[23]*u[44]+ uik[24]*u[45]+ uik[25]*u[46]+ uik[26]*u[47]+ uik[27]*u[48];
          wp[46]+= uik[28]*u[42]+ uik[29]*u[43]+ uik[30]*u[44]+ uik[31]*u[45]+ uik[32]*u[46]+ uik[33]*u[47]+ uik[34]*u[48];
          wp[47]+= uik[35]*u[42]+ uik[36]*u[43]+ uik[37]*u[44]+ uik[38]*u[45]+ uik[39]*u[46]+ uik[40]*u[47]+ uik[41]*u[48];
          wp[48]+= uik[42]*u[42]+ uik[43]*u[43]+ uik[44]*u[44]+ uik[45]*u[45]+ uik[46]*u[46]+ uik[47]*u[47]+ uik[48]*u[48];
        }
      
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;      
    }

    /* save nonzero entries in k-th row of U ... */

    /* invert diagonal block */
    d = ba+k*49;
    ierr = PetscMemcpy(d,dk,49*sizeof(MatScalar));CHKERRQ(ierr);
    ierr = Kernel_A_gets_inverse_A_7(d);CHKERRQ(ierr);
    
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j];           /* block col. index of U */
         u   = ba + j*49;
         wp = w + vj*49;        
         for (k1=0; k1<49; k1++){
           *u++        = *wp; 
           *wp++ = 0.0;
         }
      } 
      
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }    
  } 

  ierr = PetscFree(w);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  ierr = PetscFree(dk);CHKERRQ(ierr);
  ierr = PetscFree(uik);CHKERRQ(ierr);
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(perm,&perm_ptr);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;  
  PLogFlops(1.3333*343*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 7 by 7 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_7_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_7_NaturalOrdering(Mat A,Mat *B)
{
  Mat          C = *B;
  Mat_SeqBAIJ  *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int          ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int          *ajtmpold,*ajtmp,nz,row;
  int          *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar    *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar    x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15;
  MatScalar    x16,x17,x18,x19,x20,x21,x22,x23,x24,x25;
  MatScalar    p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15;
  MatScalar    p16,p17,p18,p19,p20,p21,p22,p23,p24,p25;
  MatScalar    m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15;
  MatScalar    m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar    p26,p27,p28,p29,p30,p31,p32,p33,p34,p35,p36;
  MatScalar    p37,p38,p39,p40,p41,p42,p43,p44,p45,p46,p47,p48,p49;
  MatScalar    x26,x27,x28,x29,x30,x31,x32,x33,x34,x35,x36;
  MatScalar    x37,x38,x39,x40,x41,x42,x43,x44,x45,x46,x47,x48,x49;
  MatScalar    m26,m27,m28,m29,m30,m31,m32,m33,m34,m35,m36;
  MatScalar    m37,m38,m39,m40,m41,m42,m43,m44,m45,m46,m47,m48,m49;
  MatScalar    *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(49*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);
  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+49*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = x[16] = x[17] = 0.0;
      x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = x[25] = 0.0 ;
      x[26] = x[27] = x[28] = x[29] = x[30] = x[31] = x[32] = x[33] = 0.0 ;
      x[34] = x[35] = x[36] = x[37] = x[38] = x[39] = x[40] = x[41] = 0.0 ;
      x[42] = x[43] = x[44] = x[45] = x[46] = x[47] = x[48] = 0.0 ;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 49*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+49*ajtmpold[j];
      x[0] =  v[0];  x[1] =  v[1];  x[2] =  v[2];  x[3] =  v[3];
      x[4] =  v[4];  x[5] =  v[5];  x[6] =  v[6];  x[7] =  v[7]; 
      x[8] =  v[8];  x[9] =  v[9];  x[10] = v[10]; x[11] = v[11]; 
      x[12] = v[12]; x[13] = v[13]; x[14] = v[14]; x[15] = v[15]; 
      x[16] = v[16]; x[17] = v[17]; x[18] = v[18]; x[19] = v[19]; 
      x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23]; 
      x[24] = v[24]; x[25] = v[25]; x[26] = v[26]; x[27] = v[27]; 
      x[28] = v[28]; x[29] = v[29]; x[30] = v[30]; x[31] = v[31]; 
      x[32] = v[32]; x[33] = v[33]; x[34] = v[34]; x[35] = v[35]; 
      x[36] = v[36]; x[37] = v[37]; x[38] = v[38]; x[39] = v[39]; 
      x[40] = v[40]; x[41] = v[41]; x[42] = v[42]; x[43] = v[43]; 
      x[44] = v[44]; x[45] = v[45]; x[46] = v[46]; x[47] = v[47]; 
      x[48] = v[48];
      v    += 49;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 49*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];
      p9  = pc[8];  p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; 
      p13 = pc[12]; p14 = pc[13]; p15 = pc[14]; p16 = pc[15]; 
      p17 = pc[16]; p18 = pc[17]; p19 = pc[18]; p20 = pc[19];
      p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; p24 = pc[23];
      p25 = pc[24]; p26 = pc[25]; p27 = pc[26]; p28 = pc[27];
      p29 = pc[28]; p30 = pc[29]; p31 = pc[30]; p32 = pc[31];
      p33 = pc[32]; p34 = pc[33]; p35 = pc[34]; p36 = pc[35];
      p37 = pc[36]; p38 = pc[37]; p39 = pc[38]; p40 = pc[39];
      p41 = pc[40]; p42 = pc[41]; p43 = pc[42]; p44 = pc[43];
      p45 = pc[44]; p46 = pc[45]; p47 = pc[46]; p48 = pc[47];
      p49 = pc[48];
      if (p1  != 0.0 || p2  != 0.0 || p3  != 0.0 || p4  != 0.0 || 
          p5  != 0.0 || p6  != 0.0 || p7  != 0.0 || p8  != 0.0 ||
          p9  != 0.0 || p10 != 0.0 || p11 != 0.0 || p12 != 0.0 || 
          p13 != 0.0 || p14 != 0.0 || p15 != 0.0 || p16 != 0.0 ||
          p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0 ||
          p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 ||
          p25 != 0.0 || p26 != 0.0 || p27 != 0.0 || p28 != 0.0 ||
          p29 != 0.0 || p30 != 0.0 || p31 != 0.0 || p32 != 0.0 ||
          p33 != 0.0 || p34 != 0.0 || p35 != 0.0 || p36 != 0.0 ||
          p37 != 0.0 || p38 != 0.0 || p39 != 0.0 || p40 != 0.0 ||
          p41 != 0.0 || p42 != 0.0 || p43 != 0.0 || p44 != 0.0 ||
          p45 != 0.0 || p46 != 0.0 || p47 != 0.0 || p48 != 0.0 ||
          p49 != 0.0) { 
        pv = ba + 49*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
	x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	x37 = pv[36]; x38 = pv[37]; x39 = pv[38]; x40 = pv[39];
	x41 = pv[40]; x42 = pv[41]; x43 = pv[42]; x44 = pv[43];
	x45 = pv[44]; x46 = pv[45]; x47 = pv[46]; x48 = pv[47];
        x49 = pv[48];
        pc[0]  = m1  = p1*x1  + p8*x2   + p15*x3  + p22*x4  + p29*x5  + p36*x6 + p43*x7;
        pc[1]  = m2  = p2*x1  + p9*x2   + p16*x3  + p23*x4  + p30*x5  + p37*x6 + p44*x7;
        pc[2]  = m3  = p3*x1  + p10*x2  + p17*x3  + p24*x4  + p31*x5  + p38*x6 + p45*x7;
        pc[3]  = m4  = p4*x1  + p11*x2  + p18*x3  + p25*x4  + p32*x5  + p39*x6 + p46*x7;
        pc[4]  = m5  = p5*x1  + p12*x2  + p19*x3  + p26*x4  + p33*x5  + p40*x6 + p47*x7;
        pc[5]  = m6  = p6*x1  + p13*x2  + p20*x3  + p27*x4  + p34*x5  + p41*x6 + p48*x7;
        pc[6]  = m7  = p7*x1  + p14*x2  + p21*x3  + p28*x4  + p35*x5  + p42*x6 + p49*x7;

        pc[7]  = m8  = p1*x8  + p8*x9   + p15*x10 + p22*x11 + p29*x12 + p36*x13 + p43*x14;
        pc[8]  = m9  = p2*x8  + p9*x9   + p16*x10 + p23*x11 + p30*x12 + p37*x13 + p44*x14;
        pc[9]  = m10 = p3*x8  + p10*x9  + p17*x10 + p24*x11 + p31*x12 + p38*x13 + p45*x14;
        pc[10] = m11 = p4*x8  + p11*x9  + p18*x10 + p25*x11 + p32*x12 + p39*x13 + p46*x14;
        pc[11] = m12 = p5*x8  + p12*x9  + p19*x10 + p26*x11 + p33*x12 + p40*x13 + p47*x14;
        pc[12] = m13 = p6*x8  + p13*x9  + p20*x10 + p27*x11 + p34*x12 + p41*x13 + p48*x14;
        pc[13] = m14 = p7*x8  + p14*x9  + p21*x10 + p28*x11 + p35*x12 + p42*x13 + p49*x14;

        pc[14] = m15 = p1*x15 + p8*x16  + p15*x17 + p22*x18 + p29*x19 + p36*x20 + p43*x21;
        pc[15] = m16 = p2*x15 + p9*x16  + p16*x17 + p23*x18 + p30*x19 + p37*x20 + p44*x21;
        pc[16] = m17 = p3*x15 + p10*x16 + p17*x17 + p24*x18 + p31*x19 + p38*x20 + p45*x21;
        pc[17] = m18 = p4*x15 + p11*x16 + p18*x17 + p25*x18 + p32*x19 + p39*x20 + p46*x21;
        pc[18] = m19 = p5*x15 + p12*x16 + p19*x17 + p26*x18 + p33*x19 + p40*x20 + p47*x21;
        pc[19] = m20 = p6*x15 + p13*x16 + p20*x17 + p27*x18 + p34*x19 + p41*x20 + p48*x21;
        pc[20] = m21 = p7*x15 + p14*x16 + p21*x17 + p28*x18 + p35*x19 + p42*x20 + p49*x21;

        pc[21] = m22 = p1*x22 + p8*x23  + p15*x24 + p22*x25 + p29*x26 + p36*x27 + p43*x28;
        pc[22] = m23 = p2*x22 + p9*x23  + p16*x24 + p23*x25 + p30*x26 + p37*x27 + p44*x28;
        pc[23] = m24 = p3*x22 + p10*x23 + p17*x24 + p24*x25 + p31*x26 + p38*x27 + p45*x28;
        pc[24] = m25 = p4*x22 + p11*x23 + p18*x24 + p25*x25 + p32*x26 + p39*x27 + p46*x28;
        pc[25] = m26 = p5*x22 + p12*x23 + p19*x24 + p26*x25 + p33*x26 + p40*x27 + p47*x28;
        pc[26] = m27 = p6*x22 + p13*x23 + p20*x24 + p27*x25 + p34*x26 + p41*x27 + p48*x28;
        pc[27] = m28 = p7*x22 + p14*x23 + p21*x24 + p28*x25 + p35*x26 + p42*x27 + p49*x28;

        pc[28] = m29 = p1*x29 + p8*x30  + p15*x31 + p22*x32 + p29*x33 + p36*x34 + p43*x35;
        pc[29] = m30 = p2*x29 + p9*x30  + p16*x31 + p23*x32 + p30*x33 + p37*x34 + p44*x35;
        pc[30] = m31 = p3*x29 + p10*x30 + p17*x31 + p24*x32 + p31*x33 + p38*x34 + p45*x35;
        pc[31] = m32 = p4*x29 + p11*x30 + p18*x31 + p25*x32 + p32*x33 + p39*x34 + p46*x35;
        pc[32] = m33 = p5*x29 + p12*x30 + p19*x31 + p26*x32 + p33*x33 + p40*x34 + p47*x35;
        pc[33] = m34 = p6*x29 + p13*x30 + p20*x31 + p27*x32 + p34*x33 + p41*x34 + p48*x35;
        pc[34] = m35 = p7*x29 + p14*x30 + p21*x31 + p28*x32 + p35*x33 + p42*x34 + p49*x35;

        pc[35] = m36 = p1*x36 + p8*x37  + p15*x38 + p22*x39 + p29*x40 + p36*x41 + p43*x42;
        pc[36] = m37 = p2*x36 + p9*x37  + p16*x38 + p23*x39 + p30*x40 + p37*x41 + p44*x42;
        pc[37] = m38 = p3*x36 + p10*x37 + p17*x38 + p24*x39 + p31*x40 + p38*x41 + p45*x42;
        pc[38] = m39 = p4*x36 + p11*x37 + p18*x38 + p25*x39 + p32*x40 + p39*x41 + p46*x42;
        pc[39] = m40 = p5*x36 + p12*x37 + p19*x38 + p26*x39 + p33*x40 + p40*x41 + p47*x42;
        pc[40] = m41 = p6*x36 + p13*x37 + p20*x38 + p27*x39 + p34*x40 + p41*x41 + p48*x42;
        pc[41] = m42 = p7*x36 + p14*x37 + p21*x38 + p28*x39 + p35*x40 + p42*x41 + p49*x42;

        pc[42] = m43 = p1*x43 + p8*x44  + p15*x45 + p22*x46 + p29*x47 + p36*x48 + p43*x49;
        pc[43] = m44 = p2*x43 + p9*x44  + p16*x45 + p23*x46 + p30*x47 + p37*x48 + p44*x49;
        pc[44] = m45 = p3*x43 + p10*x44 + p17*x45 + p24*x46 + p31*x47 + p38*x48 + p45*x49;
        pc[45] = m46 = p4*x43 + p11*x44 + p18*x45 + p25*x46 + p32*x47 + p39*x48 + p46*x49;
        pc[46] = m47 = p5*x43 + p12*x44 + p19*x45 + p26*x46 + p33*x47 + p40*x48 + p47*x49;
        pc[47] = m48 = p6*x43 + p13*x44 + p20*x45 + p27*x46 + p34*x47 + p41*x48 + p48*x49;
        pc[48] = m49 = p7*x43 + p14*x44 + p21*x45 + p28*x46 + p35*x47 + p42*x48 + p49*x49;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 49;
        for (j=0; j<nz; j++) {
	  x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	  x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	  x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	  x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	  x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	  x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	  x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	  x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	  x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	  x37 = pv[36]; x38 = pv[37]; x39 = pv[38]; x40 = pv[39];
	  x41 = pv[40]; x42 = pv[41]; x43 = pv[42]; x44 = pv[43];
	  x45 = pv[44]; x46 = pv[45]; x47 = pv[46]; x48 = pv[47];
	  x49 = pv[48];
	  x    = rtmp + 49*pj[j];
	  x[0]  -= m1*x1  + m8*x2   + m15*x3  + m22*x4  + m29*x5  + m36*x6 + m43*x7;
	  x[1]  -= m2*x1  + m9*x2   + m16*x3  + m23*x4  + m30*x5  + m37*x6 + m44*x7;
	  x[2]  -= m3*x1  + m10*x2  + m17*x3  + m24*x4  + m31*x5  + m38*x6 + m45*x7;
	  x[3]  -= m4*x1  + m11*x2  + m18*x3  + m25*x4  + m32*x5  + m39*x6 + m46*x7;
	  x[4]  -= m5*x1  + m12*x2  + m19*x3  + m26*x4  + m33*x5  + m40*x6 + m47*x7;
	  x[5]  -= m6*x1  + m13*x2  + m20*x3  + m27*x4  + m34*x5  + m41*x6 + m48*x7;
	  x[6]  -= m7*x1  + m14*x2  + m21*x3  + m28*x4  + m35*x5  + m42*x6 + m49*x7;

	  x[7]  -= m1*x8  + m8*x9   + m15*x10 + m22*x11 + m29*x12 + m36*x13 + m43*x14;
	  x[8]  -= m2*x8  + m9*x9   + m16*x10 + m23*x11 + m30*x12 + m37*x13 + m44*x14;
	  x[9]  -= m3*x8  + m10*x9  + m17*x10 + m24*x11 + m31*x12 + m38*x13 + m45*x14;
	  x[10] -= m4*x8  + m11*x9  + m18*x10 + m25*x11 + m32*x12 + m39*x13 + m46*x14;
	  x[11] -= m5*x8  + m12*x9  + m19*x10 + m26*x11 + m33*x12 + m40*x13 + m47*x14;
	  x[12] -= m6*x8  + m13*x9  + m20*x10 + m27*x11 + m34*x12 + m41*x13 + m48*x14;
	  x[13] -= m7*x8  + m14*x9  + m21*x10 + m28*x11 + m35*x12 + m42*x13 + m49*x14;

	  x[14] -= m1*x15 + m8*x16  + m15*x17 + m22*x18 + m29*x19 + m36*x20 + m43*x21;
	  x[15] -= m2*x15 + m9*x16  + m16*x17 + m23*x18 + m30*x19 + m37*x20 + m44*x21;
	  x[16] -= m3*x15 + m10*x16 + m17*x17 + m24*x18 + m31*x19 + m38*x20 + m45*x21;
	  x[17] -= m4*x15 + m11*x16 + m18*x17 + m25*x18 + m32*x19 + m39*x20 + m46*x21;
	  x[18] -= m5*x15 + m12*x16 + m19*x17 + m26*x18 + m33*x19 + m40*x20 + m47*x21;
	  x[19] -= m6*x15 + m13*x16 + m20*x17 + m27*x18 + m34*x19 + m41*x20 + m48*x21;
	  x[20] -= m7*x15 + m14*x16 + m21*x17 + m28*x18 + m35*x19 + m42*x20 + m49*x21;

	  x[21] -= m1*x22 + m8*x23  + m15*x24 + m22*x25 + m29*x26 + m36*x27 + m43*x28;
	  x[22] -= m2*x22 + m9*x23  + m16*x24 + m23*x25 + m30*x26 + m37*x27 + m44*x28;
	  x[23] -= m3*x22 + m10*x23 + m17*x24 + m24*x25 + m31*x26 + m38*x27 + m45*x28;
	  x[24] -= m4*x22 + m11*x23 + m18*x24 + m25*x25 + m32*x26 + m39*x27 + m46*x28;
	  x[25] -= m5*x22 + m12*x23 + m19*x24 + m26*x25 + m33*x26 + m40*x27 + m47*x28;
	  x[26] -= m6*x22 + m13*x23 + m20*x24 + m27*x25 + m34*x26 + m41*x27 + m48*x28;
	  x[27] -= m7*x22 + m14*x23 + m21*x24 + m28*x25 + m35*x26 + m42*x27 + m49*x28;

	  x[28] -= m1*x29 + m8*x30  + m15*x31 + m22*x32 + m29*x33 + m36*x34 + m43*x35;
	  x[29] -= m2*x29 + m9*x30  + m16*x31 + m23*x32 + m30*x33 + m37*x34 + m44*x35;
	  x[30] -= m3*x29 + m10*x30 + m17*x31 + m24*x32 + m31*x33 + m38*x34 + m45*x35;
	  x[31] -= m4*x29 + m11*x30 + m18*x31 + m25*x32 + m32*x33 + m39*x34 + m46*x35;
	  x[32] -= m5*x29 + m12*x30 + m19*x31 + m26*x32 + m33*x33 + m40*x34 + m47*x35;
	  x[33] -= m6*x29 + m13*x30 + m20*x31 + m27*x32 + m34*x33 + m41*x34 + m48*x35;
	  x[34] -= m7*x29 + m14*x30 + m21*x31 + m28*x32 + m35*x33 + m42*x34 + m49*x35;

	  x[35] -= m1*x36 + m8*x37  + m15*x38 + m22*x39 + m29*x40 + m36*x41 + m43*x42;
	  x[36] -= m2*x36 + m9*x37  + m16*x38 + m23*x39 + m30*x40 + m37*x41 + m44*x42;
	  x[37] -= m3*x36 + m10*x37 + m17*x38 + m24*x39 + m31*x40 + m38*x41 + m45*x42;
	  x[38] -= m4*x36 + m11*x37 + m18*x38 + m25*x39 + m32*x40 + m39*x41 + m46*x42;
	  x[39] -= m5*x36 + m12*x37 + m19*x38 + m26*x39 + m33*x40 + m40*x41 + m47*x42;
	  x[40] -= m6*x36 + m13*x37 + m20*x38 + m27*x39 + m34*x40 + m41*x41 + m48*x42;
	  x[41] -= m7*x36 + m14*x37 + m21*x38 + m28*x39 + m35*x40 + m42*x41 + m49*x42;

	  x[42] -= m1*x43 + m8*x44  + m15*x45 + m22*x46 + m29*x47 + m36*x48 + m43*x49;
	  x[43] -= m2*x43 + m9*x44  + m16*x45 + m23*x46 + m30*x47 + m37*x48 + m44*x49;
	  x[44] -= m3*x43 + m10*x44 + m17*x45 + m24*x46 + m31*x47 + m38*x48 + m45*x49;
	  x[45] -= m4*x43 + m11*x44 + m18*x45 + m25*x46 + m32*x47 + m39*x48 + m46*x49;
	  x[46] -= m5*x43 + m12*x44 + m19*x45 + m26*x46 + m33*x47 + m40*x48 + m47*x49;
	  x[47] -= m6*x43 + m13*x44 + m20*x45 + m27*x46 + m34*x47 + m41*x48 + m48*x49;
	  x[48] -= m7*x43 + m14*x44 + m21*x45 + m28*x46 + m35*x47 + m42*x48 + m49*x49;
          pv   += 49;
        }
        PLogFlops(686*nz+637);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 49*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+49*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; 
      pv[8]  = x[8];  pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; 
      pv[12] = x[12]; pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; 
      pv[16] = x[16]; pv[17] = x[17]; pv[18] = x[18]; pv[19] = x[19]; 
      pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22]; pv[23] = x[23]; 
      pv[24] = x[24]; pv[25] = x[25]; pv[26] = x[26]; pv[27] = x[27]; 
      pv[28] = x[28]; pv[29] = x[29]; pv[30] = x[30]; pv[31] = x[31]; 
      pv[32] = x[32]; pv[33] = x[33]; pv[34] = x[34]; pv[35] = x[35]; 
      pv[36] = x[36]; pv[37] = x[37]; pv[38] = x[38]; pv[39] = x[39]; 
      pv[40] = x[40]; pv[41] = x[41]; pv[42] = x[42]; pv[43] = x[43]; 
      pv[44] = x[44]; pv[45] = x[45]; pv[46] = x[46]; pv[47] = x[47]; 
      pv[48] = x[48];  
      pv   += 49;
    }
    /* invert diagonal block */
    w = ba + 49*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_7(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*343*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/* Version for when blocks are 6 by 6 */
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_6"
int MatCholeskyFactorNumeric_SeqSBAIJ_6(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 perm = b->row;
  int                *perm_ptr,ierr,i,j,mbs=a->mbs,*bi=b->i,*bj=b->j;
  int                *ai,*aj,*a2anew,k,k1,jmin,jmax,*jl,*il,vj,nexti,ili;
  MatScalar          *ba = b->a,*aa,*ap,*dk,*uik;
  MatScalar          *u,*d,*w,*wp;

  PetscFunctionBegin;
  /* initialization */
  w  = (MatScalar*)PetscMalloc(36*mbs*sizeof(MatScalar));CHKPTRQ(w);
  ierr = PetscMemzero(w,36*mbs*sizeof(MatScalar));CHKERRQ(ierr); 
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    jl[i] = mbs; il[0] = 0;
  }
  dk    = (MatScalar*)PetscMalloc(36*sizeof(MatScalar));CHKPTRQ(dk);
  uik   = (MatScalar*)PetscMalloc(36*sizeof(MatScalar));CHKPTRQ(uik);     
  ierr  = ISGetIndices(perm,&perm_ptr);CHKERRQ(ierr);

  /* check permutation */
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(36*ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,36*ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    a2anew  = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(a2anew); 
    ierr= PetscMemcpy(a2anew,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    for (i=0; i<mbs; i++){
      jmin = ai[i]; jmax = ai[i+1];
      for (j=jmin; j<jmax; j++){
        while (a2anew[j] != j){  
          k = a2anew[j]; a2anew[j] = a2anew[k]; a2anew[k] = k;  
          for (k1=0; k1<36; k1++){
            dk[k1]       = aa[k*36+k1]; 
            aa[k*36+k1] = aa[j*36+k1]; 
            aa[j*36+k1] = dk[k1];   
          }
        }
        /* transform columnoriented blocks that lie in the lower triangle to roworiented blocks */
        if (i > aj[j]){ 
          /* printf("change orientation, row: %d, col: %d\n",i,aj[j]); */
          ap = aa + j*36;                     /* ptr to the beginning of j-th block of aa */
          for (k=0; k<36; k++) dk[k] = ap[k]; /* dk <- j-th block of aa */
          for (k=0; k<6; k++){               /* j-th block of aa <- dk^T */
            for (k1=0; k1<6; k1++) *ap++ = dk[k + 6*k1];         
          }
        }
      }
    }
    ierr = PetscFree(a2anew);CHKERRA(ierr); 
  }
  
  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[perm_ptr[k]]; jmax = ai[perm_ptr[k]+1];
    if (jmin < jmax) {
      ap = aa + jmin*36;
      for (j = jmin; j < jmax; j++){
        vj = perm_ptr[aj[j]];         /* block col. index */  
        wp = w + vj*36;
        for (i=0; i<36; i++) *wp++ = *ap++;        
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    ierr = PetscMemcpy(dk,w+k*36,36*sizeof(MatScalar));CHKERRQ(ierr); 
    i = jl[k]; /* first row to be added to k_th row  */  

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */

      /* uik = -inv(Di)*U_bar(i,k) */
      d = ba + i*36;
      u    = ba + ili*36;

      uik[0] = -(d[0]*u[0] + d[6]*u[1] + d[12]*u[2] + d[18]*u[3] + d[24]*u[4] + d[30]*u[5]);
      uik[1] = -(d[1]*u[0] + d[7]*u[1] + d[13]*u[2] + d[19]*u[3] + d[25]*u[4] + d[31]*u[5]);
      uik[2] = -(d[2]*u[0] + d[8]*u[1] + d[14]*u[2] + d[20]*u[3] + d[26]*u[4] + d[32]*u[5]);
      uik[3] = -(d[3]*u[0] + d[9]*u[1] + d[15]*u[2] + d[21]*u[3] + d[27]*u[4] + d[33]*u[5]);
      uik[4] = -(d[4]*u[0]+ d[10]*u[1] + d[16]*u[2] + d[22]*u[3] + d[28]*u[4] + d[34]*u[5]);
      uik[5] = -(d[5]*u[0]+ d[11]*u[1] + d[17]*u[2] + d[23]*u[3] + d[29]*u[4] + d[35]*u[5]);

      uik[6] = -(d[0]*u[6] + d[6]*u[7] + d[12]*u[8] + d[18]*u[9] + d[24]*u[10] + d[30]*u[11]);
      uik[7] = -(d[1]*u[6] + d[7]*u[7] + d[13]*u[8] + d[19]*u[9] + d[25]*u[10] + d[31]*u[11]);
      uik[8] = -(d[2]*u[6] + d[8]*u[7] + d[14]*u[8] + d[20]*u[9] + d[26]*u[10] + d[32]*u[11]);
      uik[9] = -(d[3]*u[6] + d[9]*u[7] + d[15]*u[8] + d[21]*u[9] + d[27]*u[10] + d[33]*u[11]);
      uik[10]= -(d[4]*u[6]+ d[10]*u[7] + d[16]*u[8] + d[22]*u[9] + d[28]*u[10] + d[34]*u[11]);
      uik[11]= -(d[5]*u[6]+ d[11]*u[7] + d[17]*u[8] + d[23]*u[9] + d[29]*u[10] + d[35]*u[11]);

      uik[12] = -(d[0]*u[12] + d[6]*u[13] + d[12]*u[14] + d[18]*u[15] + d[24]*u[16] + d[30]*u[17]);
      uik[13] = -(d[1]*u[12] + d[7]*u[13] + d[13]*u[14] + d[19]*u[15] + d[25]*u[16] + d[31]*u[17]);
      uik[14] = -(d[2]*u[12] + d[8]*u[13] + d[14]*u[14] + d[20]*u[15] + d[26]*u[16] + d[32]*u[17]);
      uik[15] = -(d[3]*u[12] + d[9]*u[13] + d[15]*u[14] + d[21]*u[15] + d[27]*u[16] + d[33]*u[17]);
      uik[16] = -(d[4]*u[12]+ d[10]*u[13] + d[16]*u[14] + d[22]*u[15] + d[28]*u[16] + d[34]*u[17]);
      uik[17] = -(d[5]*u[12]+ d[11]*u[13] + d[17]*u[14] + d[23]*u[15] + d[29]*u[16] + d[35]*u[17]);

      uik[18] = -(d[0]*u[18] + d[6]*u[19] + d[12]*u[20] + d[18]*u[21] + d[24]*u[22] + d[30]*u[23]);
      uik[19] = -(d[1]*u[18] + d[7]*u[19] + d[13]*u[20] + d[19]*u[21] + d[25]*u[22] + d[31]*u[23]);
      uik[20] = -(d[2]*u[18] + d[8]*u[19] + d[14]*u[20] + d[20]*u[21] + d[26]*u[22] + d[32]*u[23]);
      uik[21] = -(d[3]*u[18] + d[9]*u[19] + d[15]*u[20] + d[21]*u[21] + d[27]*u[22] + d[33]*u[23]);
      uik[22] = -(d[4]*u[18]+ d[10]*u[19] + d[16]*u[20] + d[22]*u[21] + d[28]*u[22] + d[34]*u[23]);
      uik[23] = -(d[5]*u[18]+ d[11]*u[19] + d[17]*u[20] + d[23]*u[21] + d[29]*u[22] + d[35]*u[23]);

      uik[24] = -(d[0]*u[24] + d[6]*u[25] + d[12]*u[26] + d[18]*u[27] + d[24]*u[28] + d[30]*u[29]);
      uik[25] = -(d[1]*u[24] + d[7]*u[25] + d[13]*u[26] + d[19]*u[27] + d[25]*u[28] + d[31]*u[29]);
      uik[26] = -(d[2]*u[24] + d[8]*u[25] + d[14]*u[26] + d[20]*u[27] + d[26]*u[28] + d[32]*u[29]);
      uik[27] = -(d[3]*u[24] + d[9]*u[25] + d[15]*u[26] + d[21]*u[27] + d[27]*u[28] + d[33]*u[29]);
      uik[28] = -(d[4]*u[24]+ d[10]*u[25] + d[16]*u[26] + d[22]*u[27] + d[28]*u[28] + d[34]*u[29]);
      uik[29] = -(d[5]*u[24]+ d[11]*u[25] + d[17]*u[26] + d[23]*u[27] + d[29]*u[28] + d[35]*u[29]);

      uik[30] = -(d[0]*u[30] + d[6]*u[31] + d[12]*u[32] + d[18]*u[33] + d[24]*u[34] + d[30]*u[35]);
      uik[31] = -(d[1]*u[30] + d[7]*u[31] + d[13]*u[32] + d[19]*u[33] + d[25]*u[34] + d[31]*u[35]);
      uik[32] = -(d[2]*u[30] + d[8]*u[31] + d[14]*u[32] + d[20]*u[33] + d[26]*u[34] + d[32]*u[35]);
      uik[33] = -(d[3]*u[30] + d[9]*u[31] + d[15]*u[32] + d[21]*u[33] + d[27]*u[34] + d[33]*u[35]);
      uik[34] = -(d[4]*u[30]+ d[10]*u[31] + d[16]*u[32] + d[22]*u[33] + d[28]*u[34] + d[34]*u[35]);
      uik[35] = -(d[5]*u[30]+ d[11]*u[31] + d[17]*u[32] + d[23]*u[33] + d[29]*u[34] + d[35]*u[35]);

      /* update D(k) += -U(i,k)^T * U_bar(i,k) */  
      dk[0] +=  uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3] + uik[4]*u[4] + uik[5]*u[5];
      dk[1] +=  uik[6]*u[0] + uik[7]*u[1] + uik[8]*u[2] + uik[9]*u[3]+ uik[10]*u[4]+ uik[11]*u[5];
      dk[2] += uik[12]*u[0]+ uik[13]*u[1]+ uik[14]*u[2]+ uik[15]*u[3]+ uik[16]*u[4]+ uik[17]*u[5];
      dk[3] += uik[18]*u[0]+ uik[19]*u[1]+ uik[20]*u[2]+ uik[21]*u[3]+ uik[22]*u[4]+ uik[23]*u[5];
      dk[4] += uik[24]*u[0]+ uik[25]*u[1]+ uik[26]*u[2]+ uik[27]*u[3]+ uik[28]*u[4]+ uik[29]*u[5];
      dk[5] += uik[30]*u[0]+ uik[31]*u[1]+ uik[32]*u[2]+ uik[33]*u[3]+ uik[34]*u[4]+ uik[35]*u[5];

      dk[6] +=  uik[0]*u[6] + uik[1]*u[7] + uik[2]*u[8] + uik[3]*u[9] + uik[4]*u[10] + uik[5]*u[11];
      dk[7] +=  uik[6]*u[6] + uik[7]*u[7] + uik[8]*u[8] + uik[9]*u[9]+ uik[10]*u[10]+ uik[11]*u[11];
      dk[8] += uik[12]*u[6]+ uik[13]*u[7]+ uik[14]*u[8]+ uik[15]*u[9]+ uik[16]*u[10]+ uik[17]*u[11];
      dk[9] += uik[18]*u[6]+ uik[19]*u[7]+ uik[20]*u[8]+ uik[21]*u[9]+ uik[22]*u[10]+ uik[23]*u[11];
      dk[10]+= uik[24]*u[6]+ uik[25]*u[7]+ uik[26]*u[8]+ uik[27]*u[9]+ uik[28]*u[10]+ uik[29]*u[11];
      dk[11]+= uik[30]*u[6]+ uik[31]*u[7]+ uik[32]*u[8]+ uik[33]*u[9]+ uik[34]*u[10]+ uik[35]*u[11];

      dk[12]+=  uik[0]*u[12] + uik[1]*u[13] + uik[2]*u[14] + uik[3]*u[15] + uik[4]*u[16] + uik[5]*u[17];
      dk[13]+=  uik[6]*u[12] + uik[7]*u[13] + uik[8]*u[14] + uik[9]*u[15]+ uik[10]*u[16]+ uik[11]*u[17];
      dk[14]+= uik[12]*u[12]+ uik[13]*u[13]+ uik[14]*u[14]+ uik[15]*u[15]+ uik[16]*u[16]+ uik[17]*u[17];
      dk[15]+= uik[18]*u[12]+ uik[19]*u[13]+ uik[20]*u[14]+ uik[21]*u[15]+ uik[22]*u[16]+ uik[23]*u[17];
      dk[16]+= uik[24]*u[12]+ uik[25]*u[13]+ uik[26]*u[14]+ uik[27]*u[15]+ uik[28]*u[16]+ uik[29]*u[17];
      dk[17]+= uik[30]*u[12]+ uik[31]*u[13]+ uik[32]*u[14]+ uik[33]*u[15]+ uik[34]*u[16]+ uik[35]*u[17];

      dk[18]+=  uik[0]*u[18] + uik[1]*u[19] + uik[2]*u[20] + uik[3]*u[21] + uik[4]*u[22] + uik[5]*u[23];
      dk[19]+=  uik[6]*u[18] + uik[7]*u[19] + uik[8]*u[20] + uik[9]*u[21]+ uik[10]*u[22]+ uik[11]*u[23];
      dk[20]+= uik[12]*u[18]+ uik[13]*u[19]+ uik[14]*u[20]+ uik[15]*u[21]+ uik[16]*u[22]+ uik[17]*u[23];
      dk[21]+= uik[18]*u[18]+ uik[19]*u[19]+ uik[20]*u[20]+ uik[21]*u[21]+ uik[22]*u[22]+ uik[23]*u[23];
      dk[22]+= uik[24]*u[18]+ uik[25]*u[19]+ uik[26]*u[20]+ uik[27]*u[21]+ uik[28]*u[22]+ uik[29]*u[23];
      dk[23]+= uik[30]*u[18]+ uik[31]*u[19]+ uik[32]*u[20]+ uik[33]*u[21]+ uik[34]*u[22]+ uik[35]*u[23];

      dk[24]+=  uik[0]*u[24] + uik[1]*u[25] + uik[2]*u[26] + uik[3]*u[27] + uik[4]*u[28] + uik[5]*u[29];
      dk[25]+=  uik[6]*u[24] + uik[7]*u[25] + uik[8]*u[26] + uik[9]*u[27]+ uik[10]*u[28]+ uik[11]*u[29];
      dk[26]+= uik[12]*u[24]+ uik[13]*u[25]+ uik[14]*u[26]+ uik[15]*u[27]+ uik[16]*u[28]+ uik[17]*u[29];
      dk[27]+= uik[18]*u[24]+ uik[19]*u[25]+ uik[20]*u[26]+ uik[21]*u[27]+ uik[22]*u[28]+ uik[23]*u[29];
      dk[28]+= uik[24]*u[24]+ uik[25]*u[25]+ uik[26]*u[26]+ uik[27]*u[27]+ uik[28]*u[28]+ uik[29]*u[29];
      dk[29]+= uik[30]*u[24]+ uik[31]*u[25]+ uik[32]*u[26]+ uik[33]*u[27]+ uik[34]*u[28]+ uik[35]*u[29];

      dk[30]+=  uik[0]*u[30] + uik[1]*u[31] + uik[2]*u[32] + uik[3]*u[33] + uik[4]*u[34] + uik[5]*u[35];
      dk[31]+=  uik[6]*u[30] + uik[7]*u[31] + uik[8]*u[32] + uik[9]*u[33]+ uik[10]*u[34]+ uik[11]*u[35];
      dk[32]+= uik[12]*u[30]+ uik[13]*u[31]+ uik[14]*u[32]+ uik[15]*u[33]+ uik[16]*u[34]+ uik[17]*u[35];
      dk[33]+= uik[18]*u[30]+ uik[19]*u[31]+ uik[20]*u[32]+ uik[21]*u[33]+ uik[22]*u[34]+ uik[23]*u[35];
      dk[34]+= uik[24]*u[30]+ uik[25]*u[31]+ uik[26]*u[32]+ uik[27]*u[33]+ uik[28]*u[34]+ uik[29]*u[35];
      dk[35]+= uik[30]*u[30]+ uik[31]*u[31]+ uik[32]*u[32]+ uik[33]*u[33]+ uik[34]*u[34]+ uik[35]*u[35];
 
      /* update -U(i,k) */
      ierr = PetscMemcpy(ba+ili*36,uik,36*sizeof(MatScalar));CHKERRQ(ierr); 

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) {
          /* w += -U(i,k)^T * U_bar(i,j) */
          wp = w + bj[j]*36;
          u = ba + j*36;
          wp[0] +=  uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3] + uik[4]*u[4] + uik[5]*u[5];
          wp[1] +=  uik[6]*u[0] + uik[7]*u[1] + uik[8]*u[2] + uik[9]*u[3]+ uik[10]*u[4]+ uik[11]*u[5];
          wp[2] += uik[12]*u[0]+ uik[13]*u[1]+ uik[14]*u[2]+ uik[15]*u[3]+ uik[16]*u[4]+ uik[17]*u[5];
          wp[3] += uik[18]*u[0]+ uik[19]*u[1]+ uik[20]*u[2]+ uik[21]*u[3]+ uik[22]*u[4]+ uik[23]*u[5];
          wp[4] += uik[24]*u[0]+ uik[25]*u[1]+ uik[26]*u[2]+ uik[27]*u[3]+ uik[28]*u[4]+ uik[29]*u[5];
          wp[5] += uik[30]*u[0]+ uik[31]*u[1]+ uik[32]*u[2]+ uik[33]*u[3]+ uik[34]*u[4]+ uik[35]*u[5];

          wp[6] +=  uik[0]*u[6] + uik[1]*u[7] + uik[2]*u[8] + uik[3]*u[9] + uik[4]*u[10] + uik[5]*u[11];
          wp[7] +=  uik[6]*u[6] + uik[7]*u[7] + uik[8]*u[8] + uik[9]*u[9]+ uik[10]*u[10]+ uik[11]*u[11];
          wp[8] += uik[12]*u[6]+ uik[13]*u[7]+ uik[14]*u[8]+ uik[15]*u[9]+ uik[16]*u[10]+ uik[17]*u[11];
          wp[9] += uik[18]*u[6]+ uik[19]*u[7]+ uik[20]*u[8]+ uik[21]*u[9]+ uik[22]*u[10]+ uik[23]*u[11];
          wp[10]+= uik[24]*u[6]+ uik[25]*u[7]+ uik[26]*u[8]+ uik[27]*u[9]+ uik[28]*u[10]+ uik[29]*u[11];
          wp[11]+= uik[30]*u[6]+ uik[31]*u[7]+ uik[32]*u[8]+ uik[33]*u[9]+ uik[34]*u[10]+ uik[35]*u[11];

          wp[12]+=  uik[0]*u[12] + uik[1]*u[13] + uik[2]*u[14] + uik[3]*u[15] + uik[4]*u[16] + uik[5]*u[17];
          wp[13]+=  uik[6]*u[12] + uik[7]*u[13] + uik[8]*u[14] + uik[9]*u[15]+ uik[10]*u[16]+ uik[11]*u[17];
          wp[14]+= uik[12]*u[12]+ uik[13]*u[13]+ uik[14]*u[14]+ uik[15]*u[15]+ uik[16]*u[16]+ uik[17]*u[17];
          wp[15]+= uik[18]*u[12]+ uik[19]*u[13]+ uik[20]*u[14]+ uik[21]*u[15]+ uik[22]*u[16]+ uik[23]*u[17];
          wp[16]+= uik[24]*u[12]+ uik[25]*u[13]+ uik[26]*u[14]+ uik[27]*u[15]+ uik[28]*u[16]+ uik[29]*u[17];
          wp[17]+= uik[30]*u[12]+ uik[31]*u[13]+ uik[32]*u[14]+ uik[33]*u[15]+ uik[34]*u[16]+ uik[35]*u[17];

          wp[18]+=  uik[0]*u[18] + uik[1]*u[19] + uik[2]*u[20] + uik[3]*u[21] + uik[4]*u[22] + uik[5]*u[23];
          wp[19]+=  uik[6]*u[18] + uik[7]*u[19] + uik[8]*u[20] + uik[9]*u[21]+ uik[10]*u[22]+ uik[11]*u[23];
          wp[20]+= uik[12]*u[18]+ uik[13]*u[19]+ uik[14]*u[20]+ uik[15]*u[21]+ uik[16]*u[22]+ uik[17]*u[23];
          wp[21]+= uik[18]*u[18]+ uik[19]*u[19]+ uik[20]*u[20]+ uik[21]*u[21]+ uik[22]*u[22]+ uik[23]*u[23];
          wp[22]+= uik[24]*u[18]+ uik[25]*u[19]+ uik[26]*u[20]+ uik[27]*u[21]+ uik[28]*u[22]+ uik[29]*u[23];
          wp[23]+= uik[30]*u[18]+ uik[31]*u[19]+ uik[32]*u[20]+ uik[33]*u[21]+ uik[34]*u[22]+ uik[35]*u[23];

          wp[24]+=  uik[0]*u[24] + uik[1]*u[25] + uik[2]*u[26] + uik[3]*u[27] + uik[4]*u[28] + uik[5]*u[29];
          wp[25]+=  uik[6]*u[24] + uik[7]*u[25] + uik[8]*u[26] + uik[9]*u[27]+ uik[10]*u[28]+ uik[11]*u[29];
          wp[26]+= uik[12]*u[24]+ uik[13]*u[25]+ uik[14]*u[26]+ uik[15]*u[27]+ uik[16]*u[28]+ uik[17]*u[29];
          wp[27]+= uik[18]*u[24]+ uik[19]*u[25]+ uik[20]*u[26]+ uik[21]*u[27]+ uik[22]*u[28]+ uik[23]*u[29];
          wp[28]+= uik[24]*u[24]+ uik[25]*u[25]+ uik[26]*u[26]+ uik[27]*u[27]+ uik[28]*u[28]+ uik[29]*u[29];
          wp[29]+= uik[30]*u[24]+ uik[31]*u[25]+ uik[32]*u[26]+ uik[33]*u[27]+ uik[34]*u[28]+ uik[35]*u[29];

          wp[30]+=  uik[0]*u[30] + uik[1]*u[31] + uik[2]*u[32] + uik[3]*u[33] + uik[4]*u[34] + uik[5]*u[35];
          wp[31]+=  uik[6]*u[30] + uik[7]*u[31] + uik[8]*u[32] + uik[9]*u[33]+ uik[10]*u[34]+ uik[11]*u[35];
          wp[32]+= uik[12]*u[30]+ uik[13]*u[31]+ uik[14]*u[32]+ uik[15]*u[33]+ uik[16]*u[34]+ uik[17]*u[35];
          wp[33]+= uik[18]*u[30]+ uik[19]*u[31]+ uik[20]*u[32]+ uik[21]*u[33]+ uik[22]*u[34]+ uik[23]*u[35];
          wp[34]+= uik[24]*u[30]+ uik[25]*u[31]+ uik[26]*u[32]+ uik[27]*u[33]+ uik[28]*u[34]+ uik[29]*u[35];
          wp[35]+= uik[30]*u[30]+ uik[31]*u[31]+ uik[32]*u[32]+ uik[33]*u[33]+ uik[34]*u[34]+ uik[35]*u[35];
        }
      
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;      
    }

    /* save nonzero entries in k-th row of U ... */

    /* invert diagonal block */
    d = ba+k*36;
    ierr = PetscMemcpy(d,dk,36*sizeof(MatScalar));CHKERRQ(ierr);
    ierr = Kernel_A_gets_inverse_A_6(d);CHKERRQ(ierr);
    
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j];           /* block col. index of U */
         u   = ba + j*36;
         wp = w + vj*36;        
         for (k1=0; k1<36; k1++){
           *u++        = *wp; 
           *wp++ = 0.0;
         }
      } 
      
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }    
  } 

  ierr = PetscFree(w);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  ierr = PetscFree(dk);CHKERRQ(ierr);
  ierr = PetscFree(uik);CHKERRQ(ierr);
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(perm,&perm_ptr);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;  
  PLogFlops(1.3333*216*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 6 by 6 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_6_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_6_NaturalOrdering(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int         ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15;
  MatScalar   x16,x17,x18,x19,x20,x21,x22,x23,x24,x25;
  MatScalar   p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15;
  MatScalar   p16,p17,p18,p19,p20,p21,p22,p23,p24,p25;
  MatScalar   m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15;
  MatScalar   m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar   p26,p27,p28,p29,p30,p31,p32,p33,p34,p35,p36;
  MatScalar   x26,x27,x28,x29,x30,x31,x32,x33,x34,x35,x36;
  MatScalar   m26,m27,m28,m29,m30,m31,m32,m33,m34,m35,m36;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(36*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);
  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+36*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = x[16] = x[17] = 0.0;
      x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = x[25] = 0.0 ;
      x[26] = x[27] = x[28] = x[29] = x[30] = x[31] = x[32] = x[33] = 0.0 ;
      x[34] = x[35] = 0.0 ;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 36*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+36*ajtmpold[j];
      x[0] =  v[0];  x[1] =  v[1];  x[2] =  v[2];  x[3] =  v[3];
      x[4] =  v[4];  x[5] =  v[5];  x[6] =  v[6];  x[7] =  v[7]; 
      x[8] =  v[8];  x[9] =  v[9];  x[10] = v[10]; x[11] = v[11]; 
      x[12] = v[12]; x[13] = v[13]; x[14] = v[14]; x[15] = v[15]; 
      x[16] = v[16]; x[17] = v[17]; x[18] = v[18]; x[19] = v[19]; 
      x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23]; 
      x[24] = v[24]; x[25] = v[25]; x[26] = v[26]; x[27] = v[27]; 
      x[28] = v[28]; x[29] = v[29]; x[30] = v[30]; x[31] = v[31]; 
      x[32] = v[32]; x[33] = v[33]; x[34] = v[34]; x[35] = v[35]; 
      v    += 36;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 36*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];
      p9  = pc[8];  p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; 
      p13 = pc[12]; p14 = pc[13]; p15 = pc[14]; p16 = pc[15]; 
      p17 = pc[16]; p18 = pc[17]; p19 = pc[18]; p20 = pc[19];
      p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; p24 = pc[23];
      p25 = pc[24]; p26 = pc[25]; p27 = pc[26]; p28 = pc[27];
      p29 = pc[28]; p30 = pc[29]; p31 = pc[30]; p32 = pc[31];
      p33 = pc[32]; p34 = pc[33]; p35 = pc[34]; p36 = pc[35];
      if (p1  != 0.0 || p2  != 0.0 || p3  != 0.0 || p4  != 0.0 || 
          p5  != 0.0 || p6  != 0.0 || p7  != 0.0 || p8  != 0.0 ||
          p9  != 0.0 || p10 != 0.0 || p11 != 0.0 || p12 != 0.0 || 
          p13 != 0.0 || p14 != 0.0 || p15 != 0.0 || p16 != 0.0 ||
          p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0 ||
          p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 ||
          p25 != 0.0 || p26 != 0.0 || p27 != 0.0 || p28 != 0.0 ||
          p29 != 0.0 || p30 != 0.0 || p31 != 0.0 || p32 != 0.0 ||
          p33 != 0.0 || p34 != 0.0 || p35 != 0.0 || p36 != 0.0) { 
        pv = ba + 36*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
	x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
        pc[0]  = m1  = p1*x1  + p7*x2   + p13*x3  + p19*x4  + p25*x5  + p31*x6;
        pc[1]  = m2  = p2*x1  + p8*x2   + p14*x3  + p20*x4  + p26*x5  + p32*x6;
        pc[2]  = m3  = p3*x1  + p9*x2   + p15*x3  + p21*x4  + p27*x5  + p33*x6;
        pc[3]  = m4  = p4*x1  + p10*x2  + p16*x3  + p22*x4  + p28*x5  + p34*x6;
        pc[4]  = m5  = p5*x1  + p11*x2  + p17*x3  + p23*x4  + p29*x5  + p35*x6;
        pc[5]  = m6  = p6*x1  + p12*x2  + p18*x3  + p24*x4  + p30*x5  + p36*x6;

        pc[6]  = m7  = p1*x7  + p7*x8   + p13*x9  + p19*x10 + p25*x11 + p31*x12;
        pc[7]  = m8  = p2*x7  + p8*x8   + p14*x9  + p20*x10 + p26*x11 + p32*x12;
        pc[8]  = m9  = p3*x7  + p9*x8   + p15*x9  + p21*x10 + p27*x11 + p33*x12;
        pc[9]  = m10 = p4*x7  + p10*x8  + p16*x9  + p22*x10 + p28*x11 + p34*x12;
        pc[10] = m11 = p5*x7  + p11*x8  + p17*x9  + p23*x10 + p29*x11 + p35*x12;
        pc[11] = m12 = p6*x7  + p12*x8  + p18*x9  + p24*x10 + p30*x11 + p36*x12;

        pc[12] = m13 = p1*x13 + p7*x14  + p13*x15 + p19*x16 + p25*x17 + p31*x18;
        pc[13] = m14 = p2*x13 + p8*x14  + p14*x15 + p20*x16 + p26*x17 + p32*x18;
        pc[14] = m15 = p3*x13 + p9*x14  + p15*x15 + p21*x16 + p27*x17 + p33*x18;
        pc[15] = m16 = p4*x13 + p10*x14 + p16*x15 + p22*x16 + p28*x17 + p34*x18;
        pc[16] = m17 = p5*x13 + p11*x14 + p17*x15 + p23*x16 + p29*x17 + p35*x18;
        pc[17] = m18 = p6*x13 + p12*x14 + p18*x15 + p24*x16 + p30*x17 + p36*x18;

        pc[18] = m19 = p1*x19 + p7*x20  + p13*x21 + p19*x22 + p25*x23 + p31*x24;
        pc[19] = m20 = p2*x19 + p8*x20  + p14*x21 + p20*x22 + p26*x23 + p32*x24;
        pc[20] = m21 = p3*x19 + p9*x20  + p15*x21 + p21*x22 + p27*x23 + p33*x24;
        pc[21] = m22 = p4*x19 + p10*x20 + p16*x21 + p22*x22 + p28*x23 + p34*x24;
        pc[22] = m23 = p5*x19 + p11*x20 + p17*x21 + p23*x22 + p29*x23 + p35*x24;
        pc[23] = m24 = p6*x19 + p12*x20 + p18*x21 + p24*x22 + p30*x23 + p36*x24;

        pc[24] = m25 = p1*x25 + p7*x26  + p13*x27 + p19*x28 + p25*x29 + p31*x30;
        pc[25] = m26 = p2*x25 + p8*x26  + p14*x27 + p20*x28 + p26*x29 + p32*x30;
        pc[26] = m27 = p3*x25 + p9*x26  + p15*x27 + p21*x28 + p27*x29 + p33*x30;
        pc[27] = m28 = p4*x25 + p10*x26 + p16*x27 + p22*x28 + p28*x29 + p34*x30;
        pc[28] = m29 = p5*x25 + p11*x26 + p17*x27 + p23*x28 + p29*x29 + p35*x30;
        pc[29] = m30 = p6*x25 + p12*x26 + p18*x27 + p24*x28 + p30*x29 + p36*x30;

        pc[30] = m31 = p1*x31 + p7*x32  + p13*x33 + p19*x34 + p25*x35 + p31*x36;
        pc[31] = m32 = p2*x31 + p8*x32  + p14*x33 + p20*x34 + p26*x35 + p32*x36;
        pc[32] = m33 = p3*x31 + p9*x32  + p15*x33 + p21*x34 + p27*x35 + p33*x36;
        pc[33] = m34 = p4*x31 + p10*x32 + p16*x33 + p22*x34 + p28*x35 + p34*x36;
        pc[34] = m35 = p5*x31 + p11*x32 + p17*x33 + p23*x34 + p29*x35 + p35*x36;
        pc[35] = m36 = p6*x31 + p12*x32 + p18*x33 + p24*x34 + p30*x35 + p36*x36;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 36;
        for (j=0; j<nz; j++) {
	  x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	  x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	  x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	  x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	  x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	  x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	  x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	  x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	  x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	  x    = rtmp + 36*pj[j];
          x[0]  -= m1*x1  + m7*x2   + m13*x3  + m19*x4  + m25*x5  + m31*x6;
          x[1]  -= m2*x1  + m8*x2   + m14*x3  + m20*x4  + m26*x5  + m32*x6;
          x[2]  -= m3*x1  + m9*x2   + m15*x3  + m21*x4  + m27*x5  + m33*x6;
          x[3]  -= m4*x1  + m10*x2  + m16*x3  + m22*x4  + m28*x5  + m34*x6;
          x[4]  -= m5*x1  + m11*x2  + m17*x3  + m23*x4  + m29*x5  + m35*x6;
          x[5]  -= m6*x1  + m12*x2  + m18*x3  + m24*x4  + m30*x5  + m36*x6;

	  x[6]  -= m1*x7  + m7*x8   + m13*x9  + m19*x10 + m25*x11 + m31*x12;
	  x[7]  -= m2*x7  + m8*x8   + m14*x9  + m20*x10 + m26*x11 + m32*x12;
	  x[8]  -= m3*x7  + m9*x8   + m15*x9  + m21*x10 + m27*x11 + m33*x12;
	  x[9]  -= m4*x7  + m10*x8  + m16*x9  + m22*x10 + m28*x11 + m34*x12;
	  x[10] -= m5*x7  + m11*x8  + m17*x9  + m23*x10 + m29*x11 + m35*x12;
	  x[11] -= m6*x7  + m12*x8  + m18*x9  + m24*x10 + m30*x11 + m36*x12;

	  x[12] -= m1*x13 + m7*x14  + m13*x15 + m19*x16 + m25*x17 + m31*x18;
	  x[13] -= m2*x13 + m8*x14  + m14*x15 + m20*x16 + m26*x17 + m32*x18;
	  x[14] -= m3*x13 + m9*x14  + m15*x15 + m21*x16 + m27*x17 + m33*x18;
	  x[15] -= m4*x13 + m10*x14 + m16*x15 + m22*x16 + m28*x17 + m34*x18;
	  x[16] -= m5*x13 + m11*x14 + m17*x15 + m23*x16 + m29*x17 + m35*x18;
	  x[17] -= m6*x13 + m12*x14 + m18*x15 + m24*x16 + m30*x17 + m36*x18;

	  x[18] -= m1*x19 + m7*x20  + m13*x21 + m19*x22 + m25*x23 + m31*x24;
	  x[19] -= m2*x19 + m8*x20  + m14*x21 + m20*x22 + m26*x23 + m32*x24;
	  x[20] -= m3*x19 + m9*x20  + m15*x21 + m21*x22 + m27*x23 + m33*x24;
	  x[21] -= m4*x19 + m10*x20 + m16*x21 + m22*x22 + m28*x23 + m34*x24;
	  x[22] -= m5*x19 + m11*x20 + m17*x21 + m23*x22 + m29*x23 + m35*x24;
	  x[23] -= m6*x19 + m12*x20 + m18*x21 + m24*x22 + m30*x23 + m36*x24;

	  x[24] -= m1*x25 + m7*x26  + m13*x27 + m19*x28 + m25*x29 + m31*x30;
	  x[25] -= m2*x25 + m8*x26  + m14*x27 + m20*x28 + m26*x29 + m32*x30;
	  x[26] -= m3*x25 + m9*x26  + m15*x27 + m21*x28 + m27*x29 + m33*x30;
	  x[27] -= m4*x25 + m10*x26 + m16*x27 + m22*x28 + m28*x29 + m34*x30;
	  x[28] -= m5*x25 + m11*x26 + m17*x27 + m23*x28 + m29*x29 + m35*x30;
	  x[29] -= m6*x25 + m12*x26 + m18*x27 + m24*x28 + m30*x29 + m36*x30;

	  x[30] -= m1*x31 + m7*x32  + m13*x33 + m19*x34 + m25*x35 + m31*x36;
	  x[31] -= m2*x31 + m8*x32  + m14*x33 + m20*x34 + m26*x35 + m32*x36;
	  x[32] -= m3*x31 + m9*x32  + m15*x33 + m21*x34 + m27*x35 + m33*x36;
	  x[33] -= m4*x31 + m10*x32 + m16*x33 + m22*x34 + m28*x35 + m34*x36;
	  x[34] -= m5*x31 + m11*x32 + m17*x33 + m23*x34 + m29*x35 + m35*x36;
	  x[35] -= m6*x31 + m12*x32 + m18*x33 + m24*x34 + m30*x35 + m36*x36;

          pv   += 36;
        }
        PLogFlops(432*nz+396);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 36*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+36*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; 
      pv[8]  = x[8];  pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; 
      pv[12] = x[12]; pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; 
      pv[16] = x[16]; pv[17] = x[17]; pv[18] = x[18]; pv[19] = x[19]; 
      pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22]; pv[23] = x[23]; 
      pv[24] = x[24]; pv[25] = x[25]; pv[26] = x[26]; pv[27] = x[27]; 
      pv[28] = x[28]; pv[29] = x[29]; pv[30] = x[30]; pv[31] = x[31]; 
      pv[32] = x[32]; pv[33] = x[33]; pv[34] = x[34]; pv[35] = x[35]; 
      pv   += 36;
    }
    /* invert diagonal block */
    w = ba + 36*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_6(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*216*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/* Version for when blocks are 5 by 5  */
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_5"
int MatCholeskyFactorNumeric_SeqSBAIJ_5(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 perm = b->row;
  int                *perm_ptr,ierr,i,j,mbs=a->mbs,*bi=b->i,*bj=b->j;
  int                *ai,*aj,*a2anew,k,k1,jmin,jmax,*jl,*il,vj,nexti,ili;
  MatScalar          *ba = b->a,*aa,*ap,*dk,*uik;
  MatScalar          *u,*d,*rtmp,*rtmp_ptr;

  PetscFunctionBegin;
  /* initialization */
  rtmp  = (MatScalar*)PetscMalloc(25*mbs*sizeof(MatScalar));CHKPTRQ(rtmp);
  ierr = PetscMemzero(rtmp,25*mbs*sizeof(MatScalar));CHKERRQ(ierr); 
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    jl[i] = mbs; il[0] = 0;
  }
  dk    = (MatScalar*)PetscMalloc(25*sizeof(MatScalar));CHKPTRQ(dk);
  uik   = (MatScalar*)PetscMalloc(25*sizeof(MatScalar));CHKPTRQ(uik);     
  ierr  = ISGetIndices(perm,&perm_ptr);CHKERRQ(ierr);

  /* check permutation */
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(25*ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,25*ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    a2anew  = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(a2anew); 
    ierr= PetscMemcpy(a2anew,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    for (i=0; i<mbs; i++){
      jmin = ai[i]; jmax = ai[i+1];
      for (j=jmin; j<jmax; j++){
        while (a2anew[j] != j){  
          k = a2anew[j]; a2anew[j] = a2anew[k]; a2anew[k] = k;  
          for (k1=0; k1<25; k1++){
            dk[k1]       = aa[k*25+k1]; 
            aa[k*25+k1] = aa[j*25+k1]; 
            aa[j*25+k1] = dk[k1];   
          }
        }
        /* transform columnoriented blocks that lie in the lower triangle to roworiented blocks */
        if (i > aj[j]){ 
          /* printf("change orientation, row: %d, col: %d\n",i,aj[j]); */
          ap = aa + j*25;                     /* ptr to the beginning of j-th block of aa */
          for (k=0; k<25; k++) dk[k] = ap[k]; /* dk <- j-th block of aa */
          for (k=0; k<5; k++){               /* j-th block of aa <- dk^T */
            for (k1=0; k1<5; k1++) *ap++ = dk[k + 5*k1];         
          }
        }
      }
    }
    ierr = PetscFree(a2anew);CHKERRA(ierr); 
  }
  
  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[perm_ptr[k]]; jmax = ai[perm_ptr[k]+1];
    if (jmin < jmax) {
      ap = aa + jmin*25;
      for (j = jmin; j < jmax; j++){
        vj = perm_ptr[aj[j]];         /* block col. index */  
        rtmp_ptr = rtmp + vj*25;
        for (i=0; i<25; i++) *rtmp_ptr++ = *ap++;        
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    ierr = PetscMemcpy(dk,rtmp+k*25,25*sizeof(MatScalar));CHKERRQ(ierr); 
    i = jl[k]; /* first row to be added to k_th row  */  

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */

      /* uik = -inv(Di)*U_bar(i,k) */
      d = ba + i*25;
      u    = ba + ili*25;

      uik[0] = -(d[0]*u[0] + d[5]*u[1] + d[10]*u[2] + d[15]*u[3] + d[20]*u[4]);
      uik[1] = -(d[1]*u[0] + d[6]*u[1] + d[11]*u[2] + d[16]*u[3] + d[21]*u[4]);
      uik[2] = -(d[2]*u[0] + d[7]*u[1] + d[12]*u[2] + d[17]*u[3] + d[22]*u[4]);
      uik[3] = -(d[3]*u[0] + d[8]*u[1] + d[13]*u[2] + d[18]*u[3] + d[23]*u[4]);
      uik[4] = -(d[4]*u[0] + d[9]*u[1] + d[14]*u[2] + d[19]*u[3] + d[24]*u[4]);

      uik[5] = -(d[0]*u[5] + d[5]*u[6] + d[10]*u[7] + d[15]*u[8] + d[20]*u[9]);
      uik[6] = -(d[1]*u[5] + d[6]*u[6] + d[11]*u[7] + d[16]*u[8] + d[21]*u[9]);
      uik[7] = -(d[2]*u[5] + d[7]*u[6] + d[12]*u[7] + d[17]*u[8] + d[22]*u[9]);
      uik[8] = -(d[3]*u[5] + d[8]*u[6] + d[13]*u[7] + d[18]*u[8] + d[23]*u[9]);
      uik[9] = -(d[4]*u[5] + d[9]*u[6] + d[14]*u[7] + d[19]*u[8] + d[24]*u[9]);

      uik[10]= -(d[0]*u[10] + d[5]*u[11] + d[10]*u[12] + d[15]*u[13] + d[20]*u[14]);
      uik[11]= -(d[1]*u[10] + d[6]*u[11] + d[11]*u[12] + d[16]*u[13] + d[21]*u[14]);
      uik[12]= -(d[2]*u[10] + d[7]*u[11] + d[12]*u[12] + d[17]*u[13] + d[22]*u[14]);
      uik[13]= -(d[3]*u[10] + d[8]*u[11] + d[13]*u[12] + d[18]*u[13] + d[23]*u[14]);
      uik[14]= -(d[4]*u[10] + d[9]*u[11] + d[14]*u[12] + d[19]*u[13] + d[24]*u[14]);

      uik[15]= -(d[0]*u[15] + d[5]*u[16] + d[10]*u[17] + d[15]*u[18] + d[20]*u[19]);
      uik[16]= -(d[1]*u[15] + d[6]*u[16] + d[11]*u[17] + d[16]*u[18] + d[21]*u[19]);
      uik[17]= -(d[2]*u[15] + d[7]*u[16] + d[12]*u[17] + d[17]*u[18] + d[22]*u[19]);
      uik[18]= -(d[3]*u[15] + d[8]*u[16] + d[13]*u[17] + d[18]*u[18] + d[23]*u[19]);
      uik[19]= -(d[4]*u[15] + d[9]*u[16] + d[14]*u[17] + d[19]*u[18] + d[24]*u[19]);

      uik[20]= -(d[0]*u[20] + d[5]*u[21] + d[10]*u[22] + d[15]*u[23] + d[20]*u[24]);
      uik[21]= -(d[1]*u[20] + d[6]*u[21] + d[11]*u[22] + d[16]*u[23] + d[21]*u[24]);
      uik[22]= -(d[2]*u[20] + d[7]*u[21] + d[12]*u[22] + d[17]*u[23] + d[22]*u[24]);
      uik[23]= -(d[3]*u[20] + d[8]*u[21] + d[13]*u[22] + d[18]*u[23] + d[23]*u[24]);
      uik[24]= -(d[4]*u[20] + d[9]*u[21] + d[14]*u[22] + d[19]*u[23] + d[24]*u[24]);


      /* update D(k) += -U(i,k)^T * U_bar(i,k) */  
      dk[0] +=  uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3] + uik[4]*u[4];
      dk[1] +=  uik[5]*u[0] + uik[6]*u[1] + uik[7]*u[2] + uik[8]*u[3] + uik[9]*u[4];
      dk[2] += uik[10]*u[0]+ uik[11]*u[1]+ uik[12]*u[2]+ uik[13]*u[3]+ uik[14]*u[4];
      dk[3] += uik[15]*u[0]+ uik[16]*u[1]+ uik[17]*u[2]+ uik[18]*u[3]+ uik[19]*u[4];
      dk[4] += uik[20]*u[0]+ uik[21]*u[1]+ uik[22]*u[2]+ uik[23]*u[3]+ uik[24]*u[4];

      dk[5] +=  uik[0]*u[5] + uik[1]*u[6] + uik[2]*u[7] + uik[3]*u[8] + uik[4]*u[9];
      dk[6] +=  uik[5]*u[5] + uik[6]*u[6] + uik[7]*u[7] + uik[8]*u[8] + uik[9]*u[9];
      dk[7] += uik[10]*u[5]+ uik[11]*u[6]+ uik[12]*u[7]+ uik[13]*u[8]+ uik[14]*u[9];
      dk[8] += uik[15]*u[5]+ uik[16]*u[6]+ uik[17]*u[7]+ uik[18]*u[8]+ uik[19]*u[9];
      dk[9] += uik[20]*u[5]+ uik[21]*u[6]+ uik[22]*u[7]+ uik[23]*u[8]+ uik[24]*u[9];

      dk[10] +=  uik[0]*u[10] + uik[1]*u[11] + uik[2]*u[12] + uik[3]*u[13] + uik[4]*u[14];
      dk[11] +=  uik[5]*u[10] + uik[6]*u[11] + uik[7]*u[12] + uik[8]*u[13] + uik[9]*u[14];
      dk[12] += uik[10]*u[10]+ uik[11]*u[11]+ uik[12]*u[12]+ uik[13]*u[13]+ uik[14]*u[14];
      dk[13] += uik[15]*u[10]+ uik[16]*u[11]+ uik[17]*u[12]+ uik[18]*u[13]+ uik[19]*u[14];
      dk[14] += uik[20]*u[10]+ uik[21]*u[11]+ uik[22]*u[12]+ uik[23]*u[13]+ uik[24]*u[14];

      dk[15] +=  uik[0]*u[15] + uik[1]*u[16] + uik[2]*u[17] + uik[3]*u[18] + uik[4]*u[19];
      dk[16] +=  uik[5]*u[15] + uik[6]*u[16] + uik[7]*u[17] + uik[8]*u[18] + uik[9]*u[19];
      dk[17] += uik[10]*u[15]+ uik[11]*u[16]+ uik[12]*u[17]+ uik[13]*u[18]+ uik[14]*u[19];
      dk[18] += uik[15]*u[15]+ uik[16]*u[16]+ uik[17]*u[17]+ uik[18]*u[18]+ uik[19]*u[19];
      dk[19] += uik[20]*u[15]+ uik[21]*u[16]+ uik[22]*u[17]+ uik[23]*u[18]+ uik[24]*u[19];

      dk[20] +=  uik[0]*u[20] + uik[1]*u[21] + uik[2]*u[22] + uik[3]*u[23] + uik[4]*u[24];
      dk[21] +=  uik[5]*u[20] + uik[6]*u[21] + uik[7]*u[22] + uik[8]*u[23] + uik[9]*u[24];
      dk[22] += uik[10]*u[20]+ uik[11]*u[21]+ uik[12]*u[22]+ uik[13]*u[23]+ uik[14]*u[24];
      dk[23] += uik[15]*u[20]+ uik[16]*u[21]+ uik[17]*u[22]+ uik[18]*u[23]+ uik[19]*u[24];
      dk[24] += uik[20]*u[20]+ uik[21]*u[21]+ uik[22]*u[22]+ uik[23]*u[23]+ uik[24]*u[24];

      /* update -U(i,k) */
      ierr = PetscMemcpy(ba+ili*25,uik,25*sizeof(MatScalar));CHKERRQ(ierr); 

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) {
          /* rtmp += -U(i,k)^T * U_bar(i,j) */
          rtmp_ptr = rtmp + bj[j]*25;
          u = ba + j*25;
          rtmp_ptr[0] +=  uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3] + uik[4]*u[4];
          rtmp_ptr[1] +=  uik[5]*u[0] + uik[6]*u[1] + uik[7]*u[2] + uik[8]*u[3] + uik[9]*u[4];
          rtmp_ptr[2] += uik[10]*u[0]+ uik[11]*u[1]+ uik[12]*u[2]+ uik[13]*u[3]+ uik[14]*u[4];
          rtmp_ptr[3] += uik[15]*u[0]+ uik[16]*u[1]+ uik[17]*u[2]+ uik[18]*u[3]+ uik[19]*u[4];
          rtmp_ptr[4] += uik[20]*u[0]+ uik[21]*u[1]+ uik[22]*u[2]+ uik[23]*u[3]+ uik[24]*u[4];

          rtmp_ptr[5] +=  uik[0]*u[5] + uik[1]*u[6] + uik[2]*u[7] + uik[3]*u[8] + uik[4]*u[9];
          rtmp_ptr[6] +=  uik[5]*u[5] + uik[6]*u[6] + uik[7]*u[7] + uik[8]*u[8] + uik[9]*u[9];
          rtmp_ptr[7] += uik[10]*u[5]+ uik[11]*u[6]+ uik[12]*u[7]+ uik[13]*u[8]+ uik[14]*u[9];
          rtmp_ptr[8] += uik[15]*u[5]+ uik[16]*u[6]+ uik[17]*u[7]+ uik[18]*u[8]+ uik[19]*u[9];
          rtmp_ptr[9] += uik[20]*u[5]+ uik[21]*u[6]+ uik[22]*u[7]+ uik[23]*u[8]+ uik[24]*u[9];

          rtmp_ptr[10] +=  uik[0]*u[10] + uik[1]*u[11] + uik[2]*u[12] + uik[3]*u[13] + uik[4]*u[14];
          rtmp_ptr[11] +=  uik[5]*u[10] + uik[6]*u[11] + uik[7]*u[12] + uik[8]*u[13] + uik[9]*u[14];
          rtmp_ptr[12] += uik[10]*u[10]+ uik[11]*u[11]+ uik[12]*u[12]+ uik[13]*u[13]+ uik[14]*u[14];
          rtmp_ptr[13] += uik[15]*u[10]+ uik[16]*u[11]+ uik[17]*u[12]+ uik[18]*u[13]+ uik[19]*u[14];
          rtmp_ptr[14] += uik[20]*u[10]+ uik[21]*u[11]+ uik[22]*u[12]+ uik[23]*u[13]+ uik[24]*u[14];

          rtmp_ptr[15] +=  uik[0]*u[15] + uik[1]*u[16] + uik[2]*u[17] + uik[3]*u[18] + uik[4]*u[19];
          rtmp_ptr[16] +=  uik[5]*u[15] + uik[6]*u[16] + uik[7]*u[17] + uik[8]*u[18] + uik[9]*u[19];
          rtmp_ptr[17] += uik[10]*u[15]+ uik[11]*u[16]+ uik[12]*u[17]+ uik[13]*u[18]+ uik[14]*u[19];
          rtmp_ptr[18] += uik[15]*u[15]+ uik[16]*u[16]+ uik[17]*u[17]+ uik[18]*u[18]+ uik[19]*u[19];
          rtmp_ptr[19] += uik[20]*u[15]+ uik[21]*u[16]+ uik[22]*u[17]+ uik[23]*u[18]+ uik[24]*u[19];

          rtmp_ptr[20] +=  uik[0]*u[20] + uik[1]*u[21] + uik[2]*u[22] + uik[3]*u[23] + uik[4]*u[24];
          rtmp_ptr[21] +=  uik[5]*u[20] + uik[6]*u[21] + uik[7]*u[22] + uik[8]*u[23] + uik[9]*u[24];
          rtmp_ptr[22] += uik[10]*u[20]+ uik[11]*u[21]+ uik[12]*u[22]+ uik[13]*u[23]+ uik[14]*u[24];
          rtmp_ptr[23] += uik[15]*u[20]+ uik[16]*u[21]+ uik[17]*u[22]+ uik[18]*u[23]+ uik[19]*u[24];
          rtmp_ptr[24] += uik[20]*u[20]+ uik[21]*u[21]+ uik[22]*u[22]+ uik[23]*u[23]+ uik[24]*u[24];
        }
      
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;      
    }

    /* save nonzero entries in k-th row of U ... */

    /* invert diagonal block */
    d = ba+k*25;
    ierr = PetscMemcpy(d,dk,25*sizeof(MatScalar));CHKERRQ(ierr);
    ierr = Kernel_A_gets_inverse_A_5(d);CHKERRQ(ierr);
    
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j];           /* block col. index of U */
         u   = ba + j*25;
         rtmp_ptr = rtmp + vj*25;        
         for (k1=0; k1<25; k1++){
           *u++        = *rtmp_ptr; 
           *rtmp_ptr++ = 0.0;
         }
      } 
      
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }    
  } 

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  ierr = PetscFree(dk);CHKERRQ(ierr);
  ierr = PetscFree(uik);CHKERRQ(ierr);
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(perm,&perm_ptr);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*125*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 5 by 5 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_5_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_5_NaturalOrdering(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int         ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15;
  MatScalar   x16,x17,x18,x19,x20,x21,x22,x23,x24,x25;
  MatScalar   p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15;
  MatScalar   p16,p17,p18,p19,p20,p21,p22,p23,p24,p25;
  MatScalar   m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15;
  MatScalar   m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(25*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);
  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+25*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = x[4]  = x[5]  = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = 0.0;
      x[16] = x[17] = x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 25*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+25*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      x[4]  = v[4];  x[5]  = v[5];  x[6]  = v[6];  x[7]  = v[7];  x[8]  = v[8];
      x[9]  = v[9];  x[10] = v[10]; x[11] = v[11]; x[12] = v[12]; x[13] = v[13];
      x[14] = v[14]; x[15] = v[15]; x[16] = v[16]; x[17] = v[17]; x[18] = v[18];
      x[19] = v[19]; x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23];
      x[24] = v[24];
      v    += 25;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 25*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];  p9  = pc[8];
      p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; p13 = pc[12]; p14 = pc[13];
      p15 = pc[14]; p16 = pc[15]; p17 = pc[16]; p18 = pc[17]; 
      p19 = pc[18]; p20 = pc[19]; p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; 
      p24 = pc[23]; p25 = pc[24]; 
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0 || p10 != 0.0 ||
          p11 != 0.0 || p12 != 0.0 || p13 != 0.0 || p14 != 0.0 || p15 != 0.0
          || p16 != 0.0 || p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0
          || p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 || p25 != 0.0) {
        pv = ba + 25*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];  x9  = pv[8];
        x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12]; x14 = pv[13];
        x15 = pv[14]; x16 = pv[15]; x17 = pv[16]; x18 = pv[17]; x19 = pv[18];
        x20 = pv[19]; x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
        x25 = pv[24];
        pc[0] = m1 = p1*x1 + p6*x2  + p11*x3 + p16*x4 + p21*x5;
        pc[1] = m2 = p2*x1 + p7*x2  + p12*x3 + p17*x4 + p22*x5;
        pc[2] = m3 = p3*x1 + p8*x2  + p13*x3 + p18*x4 + p23*x5;
        pc[3] = m4 = p4*x1 + p9*x2  + p14*x3 + p19*x4 + p24*x5;
        pc[4] = m5 = p5*x1 + p10*x2 + p15*x3 + p20*x4 + p25*x5;

        pc[5]  = m6  = p1*x6 + p6*x7  + p11*x8 + p16*x9 + p21*x10;
        pc[6]  = m7  = p2*x6 + p7*x7  + p12*x8 + p17*x9 + p22*x10;
        pc[7]  = m8  = p3*x6 + p8*x7  + p13*x8 + p18*x9 + p23*x10;
        pc[8]  = m9  = p4*x6 + p9*x7  + p14*x8 + p19*x9 + p24*x10;
        pc[9]  = m10 = p5*x6 + p10*x7 + p15*x8 + p20*x9 + p25*x10;

        pc[10] = m11 = p1*x11 + p6*x12  + p11*x13 + p16*x14 + p21*x15;
        pc[11] = m12 = p2*x11 + p7*x12  + p12*x13 + p17*x14 + p22*x15;
        pc[12] = m13 = p3*x11 + p8*x12  + p13*x13 + p18*x14 + p23*x15;
        pc[13] = m14 = p4*x11 + p9*x12  + p14*x13 + p19*x14 + p24*x15;
        pc[14] = m15 = p5*x11 + p10*x12 + p15*x13 + p20*x14 + p25*x15;

        pc[15] = m16 = p1*x16 + p6*x17  + p11*x18 + p16*x19 + p21*x20;
        pc[16] = m17 = p2*x16 + p7*x17  + p12*x18 + p17*x19 + p22*x20;
        pc[17] = m18 = p3*x16 + p8*x17  + p13*x18 + p18*x19 + p23*x20;
        pc[18] = m19 = p4*x16 + p9*x17  + p14*x18 + p19*x19 + p24*x20;
        pc[19] = m20 = p5*x16 + p10*x17 + p15*x18 + p20*x19 + p25*x20;

        pc[20] = m21 = p1*x21 + p6*x22  + p11*x23 + p16*x24 + p21*x25;
        pc[21] = m22 = p2*x21 + p7*x22  + p12*x23 + p17*x24 + p22*x25;
        pc[22] = m23 = p3*x21 + p8*x22  + p13*x23 + p18*x24 + p23*x25;
        pc[23] = m24 = p4*x21 + p9*x22  + p14*x23 + p19*x24 + p24*x25;
        pc[24] = m25 = p5*x21 + p10*x22 + p15*x23 + p20*x24 + p25*x25;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 25;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6  = pv[5];   x7 = pv[6];  x8  = pv[7]; x9 = pv[8];
          x10  = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12];
          x14  = pv[13]; x15 = pv[14]; x16 = pv[15]; x17 = pv[16]; x18 = pv[17];
          x19 = pv[18];  x20 = pv[19]; x21 = pv[20]; x22 = pv[21]; x23 = pv[22];
          x24 = pv[23];  x25 = pv[24];
          x    = rtmp + 25*pj[j];
          x[0] -= m1*x1 + m6*x2   + m11*x3  + m16*x4 + m21*x5;
          x[1] -= m2*x1 + m7*x2   + m12*x3  + m17*x4 + m22*x5;
          x[2] -= m3*x1 + m8*x2   + m13*x3  + m18*x4 + m23*x5;
          x[3] -= m4*x1 + m9*x2   + m14*x3  + m19*x4 + m24*x5;
          x[4] -= m5*x1 + m10*x2  + m15*x3  + m20*x4 + m25*x5;

          x[5] -= m1*x6 + m6*x7   + m11*x8  + m16*x9 + m21*x10;
          x[6] -= m2*x6 + m7*x7   + m12*x8  + m17*x9 + m22*x10;
          x[7] -= m3*x6 + m8*x7   + m13*x8  + m18*x9 + m23*x10;
          x[8] -= m4*x6 + m9*x7   + m14*x8  + m19*x9 + m24*x10;
          x[9] -= m5*x6 + m10*x7  + m15*x8  + m20*x9 + m25*x10;

          x[10] -= m1*x11 + m6*x12  + m11*x13 + m16*x14 + m21*x15;
          x[11] -= m2*x11 + m7*x12  + m12*x13 + m17*x14 + m22*x15;
          x[12] -= m3*x11 + m8*x12  + m13*x13 + m18*x14 + m23*x15;
          x[13] -= m4*x11 + m9*x12  + m14*x13 + m19*x14 + m24*x15;
          x[14] -= m5*x11 + m10*x12 + m15*x13 + m20*x14 + m25*x15;

          x[15] -= m1*x16 + m6*x17  + m11*x18 + m16*x19 + m21*x20;
          x[16] -= m2*x16 + m7*x17  + m12*x18 + m17*x19 + m22*x20;
          x[17] -= m3*x16 + m8*x17  + m13*x18 + m18*x19 + m23*x20;
          x[18] -= m4*x16 + m9*x17  + m14*x18 + m19*x19 + m24*x20;
          x[19] -= m5*x16 + m10*x17 + m15*x18 + m20*x19 + m25*x20;

          x[20] -= m1*x21 + m6*x22  + m11*x23 + m16*x24 + m21*x25;
          x[21] -= m2*x21 + m7*x22  + m12*x23 + m17*x24 + m22*x25;
          x[22] -= m3*x21 + m8*x22  + m13*x23 + m18*x24 + m23*x25;
          x[23] -= m4*x21 + m9*x22  + m14*x23 + m19*x24 + m24*x25;
          x[24] -= m5*x21 + m10*x22 + m15*x23 + m20*x24 + m25*x25;
          pv   += 25;
        }
        PLogFlops(250*nz+225);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 25*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+25*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; pv[8] = x[8];
      pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; pv[12] = x[12];
      pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; pv[16] = x[16]; pv[17] = x[17];
      pv[18] = x[18]; pv[19] = x[19]; pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22];
      pv[23] = x[23]; pv[24] = x[24];
      pv   += 25;
    }
    /* invert diagonal block */
    w = ba + 25*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_5(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*125*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 4 by 4 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_4_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_4_NaturalOrdering(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int         ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar   p5,p6,p7,p8,p9,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16;
  MatScalar   p10,p11,p12,p13,p14,p15,p16,m10,m11,m12;
  MatScalar   m13,m14,m15,m16;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(16*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+16*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = x[4]  = x[5]  = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 16*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+16*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      x[4]  = v[4];  x[5]  = v[5];  x[6]  = v[6];  x[7]  = v[7];  x[8]  = v[8];
      x[9]  = v[9];  x[10] = v[10]; x[11] = v[11]; x[12] = v[12]; x[13] = v[13];
      x[14] = v[14]; x[15] = v[15]; 
      v    += 16;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 16*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];  p9  = pc[8];
      p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; p13 = pc[12]; p14 = pc[13];
      p15 = pc[14]; p16 = pc[15]; 
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0 || p10 != 0.0 ||
          p11 != 0.0 || p12 != 0.0 || p13 != 0.0 || p14 != 0.0 || p15 != 0.0
          || p16 != 0.0) {
        pv = ba + 16*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];  x9  = pv[8];
        x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12]; x14 = pv[13];
        x15 = pv[14]; x16 = pv[15]; 
        pc[0] = m1 = p1*x1 + p5*x2  + p9*x3  + p13*x4;
        pc[1] = m2 = p2*x1 + p6*x2  + p10*x3 + p14*x4;
        pc[2] = m3 = p3*x1 + p7*x2  + p11*x3 + p15*x4;
        pc[3] = m4 = p4*x1 + p8*x2  + p12*x3 + p16*x4;

        pc[4] = m5 = p1*x5 + p5*x6  + p9*x7  + p13*x8;
        pc[5] = m6 = p2*x5 + p6*x6  + p10*x7 + p14*x8;
        pc[6] = m7 = p3*x5 + p7*x6  + p11*x7 + p15*x8;
        pc[7] = m8 = p4*x5 + p8*x6  + p12*x7 + p16*x8;

        pc[8]  = m9  = p1*x9 + p5*x10  + p9*x11  + p13*x12;
        pc[9]  = m10 = p2*x9 + p6*x10  + p10*x11 + p14*x12;
        pc[10] = m11 = p3*x9 + p7*x10  + p11*x11 + p15*x12;
        pc[11] = m12 = p4*x9 + p8*x10  + p12*x11 + p16*x12;

        pc[12] = m13 = p1*x13 + p5*x14  + p9*x15  + p13*x16;
        pc[13] = m14 = p2*x13 + p6*x14  + p10*x15 + p14*x16;
        pc[14] = m15 = p3*x13 + p7*x14  + p11*x15 + p15*x16;
        pc[15] = m16 = p4*x13 + p8*x14  + p12*x15 + p16*x16;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 16;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6  = pv[5];   x7 = pv[6];  x8  = pv[7]; x9 = pv[8];
          x10  = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12];
          x14  = pv[13]; x15 = pv[14]; x16 = pv[15];
          x    = rtmp + 16*pj[j];
          x[0] -= m1*x1 + m5*x2  + m9*x3  + m13*x4;
          x[1] -= m2*x1 + m6*x2  + m10*x3 + m14*x4;
          x[2] -= m3*x1 + m7*x2  + m11*x3 + m15*x4;
          x[3] -= m4*x1 + m8*x2  + m12*x3 + m16*x4;

          x[4] -= m1*x5 + m5*x6  + m9*x7  + m13*x8;
          x[5] -= m2*x5 + m6*x6  + m10*x7 + m14*x8;
          x[6] -= m3*x5 + m7*x6  + m11*x7 + m15*x8;
          x[7] -= m4*x5 + m8*x6  + m12*x7 + m16*x8;

          x[8]  -= m1*x9 + m5*x10 + m9*x11  + m13*x12;
          x[9]  -= m2*x9 + m6*x10 + m10*x11 + m14*x12;
          x[10] -= m3*x9 + m7*x10 + m11*x11 + m15*x12;
          x[11] -= m4*x9 + m8*x10 + m12*x11 + m16*x12;

          x[12] -= m1*x13 + m5*x14  + m9*x15  + m13*x16;
          x[13] -= m2*x13 + m6*x14  + m10*x15 + m14*x16;
          x[14] -= m3*x13 + m7*x14  + m11*x15 + m15*x16;
          x[15] -= m4*x13 + m8*x14  + m12*x15 + m16*x16;

          pv   += 16;
        }
        PLogFlops(128*nz+112);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 16*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+16*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; pv[8] = x[8];
      pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; pv[12] = x[12];
      pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15];
      pv   += 16;
    }
    /* invert diagonal block */
    w = ba + 16*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_4(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*64*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/* Version for when blocks are 4 by 4  */
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_4"
int MatCholeskyFactorNumeric_SeqSBAIJ_4(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 perm = b->row;
  int                *perm_ptr,ierr,i,j,mbs=a->mbs,*bi=b->i,*bj=b->j;
  int                *ai,*aj,*a2anew,k,k1,jmin,jmax,*jl,*il,vj,nexti,ili;
  MatScalar          *ba = b->a,*aa,*ap,*dk,*uik;
  MatScalar          *u,*diag,*rtmp,*rtmp_ptr;

  PetscFunctionBegin;
  /* initialization */
  rtmp  = (MatScalar*)PetscMalloc(16*mbs*sizeof(MatScalar));CHKPTRQ(rtmp);
  ierr = PetscMemzero(rtmp,16*mbs*sizeof(MatScalar));CHKERRQ(ierr); 
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    jl[i] = mbs; il[0] = 0;
  }
  dk    = (MatScalar*)PetscMalloc(16*sizeof(MatScalar));CHKPTRQ(dk);
  uik   = (MatScalar*)PetscMalloc(16*sizeof(MatScalar));CHKPTRQ(uik);     
  ierr  = ISGetIndices(perm,&perm_ptr);CHKERRQ(ierr);

  /* check permutation */
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(16*ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,16*ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    a2anew  = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(a2anew); 
    ierr= PetscMemcpy(a2anew,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    for (i=0; i<mbs; i++){
      jmin = ai[i]; jmax = ai[i+1];
      for (j=jmin; j<jmax; j++){
        while (a2anew[j] != j){  
          k = a2anew[j]; a2anew[j] = a2anew[k]; a2anew[k] = k;  
          for (k1=0; k1<16; k1++){
            dk[k1]       = aa[k*16+k1]; 
            aa[k*16+k1] = aa[j*16+k1]; 
            aa[j*16+k1] = dk[k1];   
          }
        }
        /* transform columnoriented blocks that lie in the lower triangle to roworiented blocks */
        if (i > aj[j]){ 
          /* printf("change orientation, row: %d, col: %d\n",i,aj[j]); */
          ap = aa + j*16;                     /* ptr to the beginning of j-th block of aa */
          for (k=0; k<16; k++) dk[k] = ap[k]; /* dk <- j-th block of aa */
          for (k=0; k<4; k++){               /* j-th block of aa <- dk^T */
            for (k1=0; k1<4; k1++) *ap++ = dk[k + 4*k1];         
          }
        }
      }
    }
    ierr = PetscFree(a2anew);CHKERRA(ierr); 
  }
  
  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[perm_ptr[k]]; jmax = ai[perm_ptr[k]+1];
    if (jmin < jmax) {
      ap = aa + jmin*16;
      for (j = jmin; j < jmax; j++){
        vj = perm_ptr[aj[j]];         /* block col. index */  
        rtmp_ptr = rtmp + vj*16;
        for (i=0; i<16; i++) *rtmp_ptr++ = *ap++;        
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    ierr = PetscMemcpy(dk,rtmp+k*16,16*sizeof(MatScalar));CHKERRQ(ierr); 
    i = jl[k]; /* first row to be added to k_th row  */  

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */

      /* uik = -inv(Di)*U_bar(i,k) */
      diag = ba + i*16;
      u    = ba + ili*16;

      uik[0] = -(diag[0]*u[0] + diag[4]*u[1] + diag[8]*u[2] + diag[12]*u[3]);
      uik[1] = -(diag[1]*u[0] + diag[5]*u[1] + diag[9]*u[2] + diag[13]*u[3]);
      uik[2] = -(diag[2]*u[0] + diag[6]*u[1] + diag[10]*u[2]+ diag[14]*u[3]);
      uik[3] = -(diag[3]*u[0] + diag[7]*u[1] + diag[11]*u[2]+ diag[15]*u[3]);

      uik[4] = -(diag[0]*u[4] + diag[4]*u[5] + diag[8]*u[6] + diag[12]*u[7]);
      uik[5] = -(diag[1]*u[4] + diag[5]*u[5] + diag[9]*u[6] + diag[13]*u[7]);
      uik[6] = -(diag[2]*u[4] + diag[6]*u[5] + diag[10]*u[6]+ diag[14]*u[7]);
      uik[7] = -(diag[3]*u[4] + diag[7]*u[5] + diag[11]*u[6]+ diag[15]*u[7]);

      uik[8] = -(diag[0]*u[8] + diag[4]*u[9] + diag[8]*u[10] + diag[12]*u[11]);
      uik[9] = -(diag[1]*u[8] + diag[5]*u[9] + diag[9]*u[10] + diag[13]*u[11]);
      uik[10]= -(diag[2]*u[8] + diag[6]*u[9] + diag[10]*u[10]+ diag[14]*u[11]);
      uik[11]= -(diag[3]*u[8] + diag[7]*u[9] + diag[11]*u[10]+ diag[15]*u[11]);

      uik[12]= -(diag[0]*u[12] + diag[4]*u[13] + diag[8]*u[14] + diag[12]*u[15]);
      uik[13]= -(diag[1]*u[12] + diag[5]*u[13] + diag[9]*u[14] + diag[13]*u[15]);
      uik[14]= -(diag[2]*u[12] + diag[6]*u[13] + diag[10]*u[14]+ diag[14]*u[15]);
      uik[15]= -(diag[3]*u[12] + diag[7]*u[13] + diag[11]*u[14]+ diag[15]*u[15]);

      /* update D(k) += -U(i,k)^T * U_bar(i,k) */
      dk[0] += uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3];
      dk[1] += uik[4]*u[0] + uik[5]*u[1] + uik[6]*u[2] + uik[7]*u[3];
      dk[2] += uik[8]*u[0] + uik[9]*u[1] + uik[10]*u[2]+ uik[11]*u[3];
      dk[3] += uik[12]*u[0]+ uik[13]*u[1]+ uik[14]*u[2]+ uik[15]*u[3];

      dk[4] += uik[0]*u[4] + uik[1]*u[5] + uik[2]*u[6] + uik[3]*u[7];
      dk[5] += uik[4]*u[4] + uik[5]*u[5] + uik[6]*u[6] + uik[7]*u[7];
      dk[6] += uik[8]*u[4] + uik[9]*u[5] + uik[10]*u[6]+ uik[11]*u[7];
      dk[7] += uik[12]*u[4]+ uik[13]*u[5]+ uik[14]*u[6]+ uik[15]*u[7];

      dk[8] += uik[0]*u[8] + uik[1]*u[9] + uik[2]*u[10] + uik[3]*u[11];
      dk[9] += uik[4]*u[8] + uik[5]*u[9] + uik[6]*u[10] + uik[7]*u[11];
      dk[10]+= uik[8]*u[8] + uik[9]*u[9] + uik[10]*u[10]+ uik[11]*u[11];
      dk[11]+= uik[12]*u[8]+ uik[13]*u[9]+ uik[14]*u[10]+ uik[15]*u[11];

      dk[12]+= uik[0]*u[12] + uik[1]*u[13] + uik[2]*u[14] + uik[3]*u[15];
      dk[13]+= uik[4]*u[12] + uik[5]*u[13] + uik[6]*u[14] + uik[7]*u[15];
      dk[14]+= uik[8]*u[12] + uik[9]*u[13] + uik[10]*u[14]+ uik[11]*u[15];
      dk[15]+= uik[12]*u[12]+ uik[13]*u[13]+ uik[14]*u[14]+ uik[15]*u[15];
      
      /* update -U(i,k) */
      ierr = PetscMemcpy(ba+ili*16,uik,16*sizeof(MatScalar));CHKERRQ(ierr); 

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) {
          /* rtmp += -U(i,k)^T * U_bar(i,j) */
          rtmp_ptr = rtmp + bj[j]*16;
          u = ba + j*16;
          rtmp_ptr[0] += uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2] + uik[3]*u[3];
          rtmp_ptr[1] += uik[4]*u[0] + uik[5]*u[1] + uik[6]*u[2] + uik[7]*u[3];
          rtmp_ptr[2] += uik[8]*u[0] + uik[9]*u[1] + uik[10]*u[2]+ uik[11]*u[3];
          rtmp_ptr[3] += uik[12]*u[0]+ uik[13]*u[1]+ uik[14]*u[2]+ uik[15]*u[3];

          rtmp_ptr[4] += uik[0]*u[4] + uik[1]*u[5] + uik[2]*u[6] + uik[3]*u[7];
          rtmp_ptr[5] += uik[4]*u[4] + uik[5]*u[5] + uik[6]*u[6] + uik[7]*u[7];
          rtmp_ptr[6] += uik[8]*u[4] + uik[9]*u[5] + uik[10]*u[6]+ uik[11]*u[7];
          rtmp_ptr[7] += uik[12]*u[4]+ uik[13]*u[5]+ uik[14]*u[6]+ uik[15]*u[7];

          rtmp_ptr[8] += uik[0]*u[8] + uik[1]*u[9] + uik[2]*u[10] + uik[3]*u[11];
          rtmp_ptr[9] += uik[4]*u[8] + uik[5]*u[9] + uik[6]*u[10] + uik[7]*u[11];
          rtmp_ptr[10]+= uik[8]*u[8] + uik[9]*u[9] + uik[10]*u[10]+ uik[11]*u[11];
          rtmp_ptr[11]+= uik[12]*u[8]+ uik[13]*u[9]+ uik[14]*u[10]+ uik[15]*u[11];

          rtmp_ptr[12]+= uik[0]*u[12] + uik[1]*u[13] + uik[2]*u[14] + uik[3]*u[15];
          rtmp_ptr[13]+= uik[4]*u[12] + uik[5]*u[13] + uik[6]*u[14] + uik[7]*u[15];
          rtmp_ptr[14]+= uik[8]*u[12] + uik[9]*u[13] + uik[10]*u[14]+ uik[11]*u[15];
          rtmp_ptr[15]+= uik[12]*u[12]+ uik[13]*u[13]+ uik[14]*u[14]+ uik[15]*u[15];
        }
      
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;      
    }

    /* save nonzero entries in k-th row of U ... */

    /* invert diagonal block */
    diag = ba+k*16;
    ierr = PetscMemcpy(diag,dk,16*sizeof(MatScalar));CHKERRQ(ierr);
    ierr = Kernel_A_gets_inverse_A_4(diag);CHKERRQ(ierr);
    
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j];           /* block col. index of U */
         u   = ba + j*16;
         rtmp_ptr = rtmp + vj*16;        
         for (k1=0; k1<16; k1++){
           *u++        = *rtmp_ptr; 
           *rtmp_ptr++ = 0.0;
         }
      } 
      
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }    
  } 

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  ierr = PetscFree(dk);CHKERRQ(ierr);
  ierr = PetscFree(uik);CHKERRQ(ierr);
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(perm,&perm_ptr);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*64*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/* Version for when blocks are 3 by 3  */
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_3"
int MatCholeskyFactorNumeric_SeqSBAIJ_3(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 perm = b->row;
  int                *perm_ptr,ierr,i,j,mbs=a->mbs,*bi=b->i,*bj=b->j;
  int                *ai,*aj,*a2anew,k,k1,jmin,jmax,*jl,*il,vj,nexti,ili;
  MatScalar          *ba = b->a,*aa,*ap,*dk,*uik;
  MatScalar          *u,*diag,*rtmp,*rtmp_ptr;

  PetscFunctionBegin;
  /* initialization */
  rtmp  = (MatScalar*)PetscMalloc(9*mbs*sizeof(MatScalar));CHKPTRQ(rtmp);
  ierr = PetscMemzero(rtmp,9*mbs*sizeof(MatScalar));CHKERRQ(ierr); 
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    jl[i] = mbs; il[0] = 0;
  }
  dk  = (MatScalar*)PetscMalloc(9*sizeof(MatScalar));CHKPTRQ(dk);
  uik = (MatScalar*)PetscMalloc(9*sizeof(MatScalar));CHKPTRQ(uik);     
  ierr  = ISGetIndices(perm,&perm_ptr);CHKERRQ(ierr);

  /* check permutation */
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(9*ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,9*ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    a2anew  = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(a2anew); 
    ierr= PetscMemcpy(a2anew,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    for (i=0; i<mbs; i++){
      jmin = ai[i]; jmax = ai[i+1];
      for (j=jmin; j<jmax; j++){
        while (a2anew[j] != j){  
          k = a2anew[j]; a2anew[j] = a2anew[k]; a2anew[k] = k;  
          for (k1=0; k1<9; k1++){
            dk[k1]       = aa[k*9+k1]; 
            aa[k*9+k1] = aa[j*9+k1]; 
            aa[j*9+k1] = dk[k1];   
          }
        }
        /* transform columnoriented blocks that lie in the lower triangle to roworiented blocks */
        if (i > aj[j]){ 
          /* printf("change orientation, row: %d, col: %d\n",i,aj[j]); */
          ap = aa + j*9;                     /* ptr to the beginning of j-th block of aa */
          for (k=0; k<9; k++) dk[k] = ap[k]; /* dk <- j-th block of aa */
          for (k=0; k<3; k++){               /* j-th block of aa <- dk^T */
            for (k1=0; k1<3; k1++) *ap++ = dk[k + 3*k1];         
          }
        }
      }
    }
    ierr = PetscFree(a2anew);CHKERRA(ierr); 
  }
  
  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[perm_ptr[k]]; jmax = ai[perm_ptr[k]+1];
    if (jmin < jmax) {
      ap = aa + jmin*9;
      for (j = jmin; j < jmax; j++){
        vj = perm_ptr[aj[j]];         /* block col. index */  
        rtmp_ptr = rtmp + vj*9;
        for (i=0; i<9; i++) *rtmp_ptr++ = *ap++;        
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    ierr = PetscMemcpy(dk,rtmp+k*9,9*sizeof(MatScalar));CHKERRQ(ierr); 
    i = jl[k]; /* first row to be added to k_th row  */  

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */

      /* uik = -inv(Di)*U_bar(i,k) */
      diag = ba + i*9;
      u    = ba + ili*9;

      uik[0] = -(diag[0]*u[0] + diag[3]*u[1] + diag[6]*u[2]);
      uik[1] = -(diag[1]*u[0] + diag[4]*u[1] + diag[7]*u[2]);
      uik[2] = -(diag[2]*u[0] + diag[5]*u[1] + diag[8]*u[2]);

      uik[3] = -(diag[0]*u[3] + diag[3]*u[4] + diag[6]*u[5]);
      uik[4] = -(diag[1]*u[3] + diag[4]*u[4] + diag[7]*u[5]);
      uik[5] = -(diag[2]*u[3] + diag[5]*u[4] + diag[8]*u[5]);

      uik[6] = -(diag[0]*u[6] + diag[3]*u[7] + diag[6]*u[8]);
      uik[7] = -(diag[1]*u[6] + diag[4]*u[7] + diag[7]*u[8]);
      uik[8] = -(diag[2]*u[6] + diag[5]*u[7] + diag[8]*u[8]);

      /* update D(k) += -U(i,k)^T * U_bar(i,k) */
      dk[0] += uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2];
      dk[1] += uik[3]*u[0] + uik[4]*u[1] + uik[5]*u[2];
      dk[2] += uik[6]*u[0] + uik[7]*u[1] + uik[8]*u[2];

      dk[3] += uik[0]*u[3] + uik[1]*u[4] + uik[2]*u[5];
      dk[4] += uik[3]*u[3] + uik[4]*u[4] + uik[5]*u[5];
      dk[5] += uik[6]*u[3] + uik[7]*u[4] + uik[8]*u[5];

      dk[6] += uik[0]*u[6] + uik[1]*u[7] + uik[2]*u[8];
      dk[7] += uik[3]*u[6] + uik[4]*u[7] + uik[5]*u[8];
      dk[8] += uik[6]*u[6] + uik[7]*u[7] + uik[8]*u[8];

      /* update -U(i,k) */
      ierr = PetscMemcpy(ba+ili*9,uik,9*sizeof(MatScalar));CHKERRQ(ierr); 

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) {
          /* rtmp += -U(i,k)^T * U_bar(i,j) */
          rtmp_ptr = rtmp + bj[j]*9;
          u = ba + j*9;
          rtmp_ptr[0] += uik[0]*u[0] + uik[1]*u[1] + uik[2]*u[2];
          rtmp_ptr[1] += uik[3]*u[0] + uik[4]*u[1] + uik[5]*u[2];
          rtmp_ptr[2] += uik[6]*u[0] + uik[7]*u[1] + uik[8]*u[2];

          rtmp_ptr[3] += uik[0]*u[3] + uik[1]*u[4] + uik[2]*u[5];
          rtmp_ptr[4] += uik[3]*u[3] + uik[4]*u[4] + uik[5]*u[5];
          rtmp_ptr[5] += uik[6]*u[3] + uik[7]*u[4] + uik[8]*u[5];

          rtmp_ptr[6] += uik[0]*u[6] + uik[1]*u[7] + uik[2]*u[8];
          rtmp_ptr[7] += uik[3]*u[6] + uik[4]*u[7] + uik[5]*u[8];
          rtmp_ptr[8] += uik[6]*u[6] + uik[7]*u[7] + uik[8]*u[8];
        }
      
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;      
    }

    /* save nonzero entries in k-th row of U ... */

    /* invert diagonal block */
    diag = ba+k*9;
    ierr = PetscMemcpy(diag,dk,9*sizeof(MatScalar));CHKERRQ(ierr);
    ierr = Kernel_A_gets_inverse_A_3(diag);CHKERRQ(ierr);
    
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j];           /* block col. index of U */
         u   = ba + j*9;
         rtmp_ptr = rtmp + vj*9;        
         for (k1=0; k1<9; k1++){
           *u++        = *rtmp_ptr; 
           *rtmp_ptr++ = 0.0;
         }
      } 
      
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }    
  } 

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  ierr = PetscFree(dk);CHKERRQ(ierr);
  ierr = PetscFree(uik);CHKERRQ(ierr);
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(perm,&perm_ptr);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*27*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 3 by 3 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_3_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_3_NaturalOrdering(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqBAIJ        *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int                ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int                *ajtmpold,*ajtmp,nz,row;
  int                *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar          *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar          p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar          p5,p6,p7,p8,p9,x5,x6,x7,x8,x9;
  MatScalar          *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(9*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+9*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = x[4]  = x[5]  = x[6] = x[7] = x[8] = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 9*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+9*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      x[4]  = v[4];  x[5]  = v[5];  x[6]  = v[6];  x[7]  = v[7];  x[8]  = v[8];
      v    += 9;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 9*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];  p9  = pc[8];
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0) { 
        pv = ba + 9*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];  x9  = pv[8];
        pc[0] = m1 = p1*x1 + p4*x2 + p7*x3;
        pc[1] = m2 = p2*x1 + p5*x2 + p8*x3;
        pc[2] = m3 = p3*x1 + p6*x2 + p9*x3;

        pc[3] = m4 = p1*x4 + p4*x5 + p7*x6;
        pc[4] = m5 = p2*x4 + p5*x5 + p8*x6;
        pc[5] = m6 = p3*x4 + p6*x5 + p9*x6;

        pc[6] = m7 = p1*x7 + p4*x8 + p7*x9;
        pc[7] = m8 = p2*x7 + p5*x8 + p8*x9;
        pc[8] = m9 = p3*x7 + p6*x8 + p9*x9;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 9;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6  = pv[5];   x7 = pv[6];  x8  = pv[7]; x9 = pv[8];
          x    = rtmp + 9*pj[j];
          x[0] -= m1*x1 + m4*x2 + m7*x3;
          x[1] -= m2*x1 + m5*x2 + m8*x3;
          x[2] -= m3*x1 + m6*x2 + m9*x3;
 
          x[3] -= m1*x4 + m4*x5 + m7*x6;
          x[4] -= m2*x4 + m5*x5 + m8*x6;
          x[5] -= m3*x4 + m6*x5 + m9*x6;

          x[6] -= m1*x7 + m4*x8 + m7*x9;
          x[7] -= m2*x7 + m5*x8 + m8*x9;
          x[8] -= m3*x7 + m6*x8 + m9*x9;
          pv   += 9;
        }
        PLogFlops(54*nz+36);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 9*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+9*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; pv[8] = x[8];
      pv   += 9;
    }
    /* invert diagonal block */
    w = ba + 9*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_3(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*27*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
    Numeric U^T*D*U factorization for SBAIJ format. Modified from SNF of YSMP.
    Version for blocks 2 by 2.
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_2"
int MatCholeskyFactorNumeric_SeqSBAIJ_2(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 perm = b->row;
  int                *perm_ptr,ierr,i,j,mbs=a->mbs,*bi=b->i,*bj=b->j;
  int                *ai,*aj,*a2anew,k,k1,jmin,jmax,*jl,*il,vj,nexti,ili;
  MatScalar          *ba = b->a,*aa,*ap,*dk,*uik;
  MatScalar          *u,*diag,*rtmp,*rtmp_ptr;

  PetscFunctionBegin;

  /* initialization */
  /* il and jl record the first nonzero element in each row of the accessing 
     window U(0:k, k:mbs-1).
     jl:    list of rows to be added to uneliminated rows 
            i>= k: jl(i) is the first row to be added to row i
            i<  k: jl(i) is the row following row i in some list of rows
            jl(i) = mbs indicates the end of a list                        
     il(i): points to the first nonzero element in columns k,...,mbs-1 of 
            row i of U */
  rtmp  = (MatScalar*)PetscMalloc(4*mbs*sizeof(MatScalar));CHKPTRQ(rtmp);
  ierr = PetscMemzero(rtmp,4*mbs*sizeof(MatScalar));CHKERRQ(ierr); 
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    jl[i] = mbs; il[0] = 0;
  }
  dk  = (MatScalar*)PetscMalloc(4*sizeof(MatScalar));CHKPTRQ(dk);
  uik = (MatScalar*)PetscMalloc(4*sizeof(MatScalar));CHKPTRQ(uik);     
  ierr  = ISGetIndices(perm,&perm_ptr);CHKERRQ(ierr);

  /* check permutation */
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(4*ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,4*ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    a2anew  = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(a2anew); 
    ierr= PetscMemcpy(a2anew,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    for (i=0; i<mbs; i++){
      jmin = ai[i]; jmax = ai[i+1];
      for (j=jmin; j<jmax; j++){
        while (a2anew[j] != j){  
          k = a2anew[j]; a2anew[j] = a2anew[k]; a2anew[k] = k;  
          for (k1=0; k1<4; k1++){
            dk[k1]       = aa[k*4+k1]; 
            aa[k*4+k1] = aa[j*4+k1]; 
            aa[j*4+k1] = dk[k1];   
          }
        }
        /* transform columnoriented blocks that lie in the lower triangle to roworiented blocks */
        if (i > aj[j]){ 
          /* printf("change orientation, row: %d, col: %d\n",i,aj[j]); */
          ap = aa + j*4;     /* ptr to the beginning of the block */
          dk[1] = ap[1];     /* swap ap[1] and ap[2] */
          ap[1] = ap[2];
          ap[2] = dk[1];
        }
      }
    }
    ierr = PetscFree(a2anew);CHKERRA(ierr); 
  }

  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[perm_ptr[k]]; jmax = ai[perm_ptr[k]+1];
    if (jmin < jmax) {
      ap = aa + jmin*4;
      for (j = jmin; j < jmax; j++){
        vj = perm_ptr[aj[j]];         /* block col. index */  
        rtmp_ptr = rtmp + vj*4;
        for (i=0; i<4; i++) *rtmp_ptr++ = *ap++;        
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    ierr = PetscMemcpy(dk,rtmp+k*4,4*sizeof(MatScalar));CHKERRQ(ierr); 
    i = jl[k]; /* first row to be added to k_th row  */  

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */

      /* uik = -inv(Di)*U_bar(i,k): - ba[ili]*ba[i] */
      diag = ba + i*4;
      u    = ba + ili*4;
      uik[0] = -(diag[0]*u[0] + diag[2]*u[1]);
      uik[1] = -(diag[1]*u[0] + diag[3]*u[1]);
      uik[2] = -(diag[0]*u[2] + diag[2]*u[3]);
      uik[3] = -(diag[1]*u[2] + diag[3]*u[3]);
  
      /* update D(k) += -U(i,k)^T * U_bar(i,k): dk += uik*ba[ili] */
      dk[0] += uik[0]*u[0] + uik[1]*u[1];
      dk[1] += uik[2]*u[0] + uik[3]*u[1];
      dk[2] += uik[0]*u[2] + uik[1]*u[3];
      dk[3] += uik[2]*u[2] + uik[3]*u[3];

      /* update -U(i,k): ba[ili] = uik */
      ierr = PetscMemcpy(ba+ili*4,uik,4*sizeof(MatScalar));CHKERRQ(ierr); 

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) {
          /* rtmp += -U(i,k)^T * U_bar(i,j): rtmp[bj[j]] += uik*ba[j]; */
          rtmp_ptr = rtmp + bj[j]*4;
          u = ba + j*4;
          rtmp_ptr[0] += uik[0]*u[0] + uik[1]*u[1];
          rtmp_ptr[1] += uik[2]*u[0] + uik[3]*u[1];
          rtmp_ptr[2] += uik[0]*u[2] + uik[1]*u[3];
          rtmp_ptr[3] += uik[2]*u[2] + uik[3]*u[3];
        }
      
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;       
    }

    /* save nonzero entries in k-th row of U ... */

    /* invert diagonal block */
    diag = ba+k*4;
    ierr = PetscMemcpy(diag,dk,4*sizeof(MatScalar));CHKERRQ(ierr);
    ierr = Kernel_A_gets_inverse_A_2(diag);CHKERRQ(ierr);
    
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j];           /* block col. index of U */
         u   = ba + j*4;
         rtmp_ptr = rtmp + vj*4;        
         for (k1=0; k1<4; k1++){
           *u++        = *rtmp_ptr; 
           *rtmp_ptr++ = 0.0;
         }
      } 
      
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }    
  } 

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  ierr = PetscFree(dk);CHKERRQ(ierr);
  ierr = PetscFree(uik);CHKERRQ(ierr);
  if (a->permute) {
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }
  ierr = ISRestoreIndices(perm,&perm_ptr);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*8*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 2 by 2 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_2_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_2_NaturalOrdering(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqBAIJ        *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int                ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int                *ajtmpold,*ajtmp,nz,row;
  int                *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar          *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar          p1,p2,p3,p4,m1,m2,m3,m4,x1,x2,x3,x4;
  MatScalar          *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(4*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+4*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 4*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+4*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      v    += 4;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 4*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0) { 
        pv = ba + 4*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        pc[0] = m1 = p1*x1 + p3*x2;
        pc[1] = m2 = p2*x1 + p4*x2;
        pc[2] = m3 = p1*x3 + p3*x4;
        pc[3] = m4 = p2*x3 + p4*x4;
        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 4;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x    = rtmp + 4*pj[j];
          x[0] -= m1*x1 + m3*x2;
          x[1] -= m2*x1 + m4*x2;
          x[2] -= m1*x3 + m3*x4;
          x[3] -= m2*x3 + m4*x4;
          pv   += 4;
        }
        PLogFlops(16*nz+12);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 4*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+4*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv   += 4;
    }
    /* invert diagonal block */
    w = ba + 4*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_2(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  C->preallocated = PETSC_TRUE;
  PLogFlops(1.3333*8*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
    Numeric U^T*D*U factorization for SBAIJ format. Modified from SNF of YSMP.
    Version for blocks are 1 by 1.
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_1"
int MatCholeskyFactorNumeric_SeqSBAIJ_1(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 ip = b->row;
  int                *rip,ierr,i,j,mbs = a->mbs,*bi = b->i,*bj = b->j;
  int                *ai,*aj,*r;
  MatScalar          *rtmp;
  MatScalar          *ba = b->a,*aa,ak;
  MatScalar          dk,uikdi;
  int                k,jmin,jmax,*jl,*il,vj,nexti,ili;

  PetscFunctionBegin;
  ierr  = ISGetIndices(ip,&rip);CHKERRQ(ierr);
  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    r   = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(r); 
    ierr= PetscMemcpy(r,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    jmin = ai[0]; jmax = ai[mbs];
    for (j=jmin; j<jmax; j++){
      while (r[j] != j){  
        k = r[j]; r[j] = r[k]; r[k] = k;     
        ak = aa[k]; aa[k] = aa[j]; aa[j] = ak;         
      }
    }
    ierr = PetscFree(r);CHKERRA(ierr); 
  }
  
  /* initialization */
  /* il and jl record the first nonzero element in each row of the accessing 
     window U(0:k, k:mbs-1).
     jl:    list of rows to be added to uneliminated rows 
            i>= k: jl(i) is the first row to be added to row i
            i<  k: jl(i) is the row following row i in some list of rows
            jl(i) = mbs indicates the end of a list                        
     il(i): points to the first nonzero element in columns k,...,mbs-1 of 
            row i of U */
  rtmp  = (MatScalar*)PetscMalloc(mbs*sizeof(MatScalar));CHKPTRQ(rtmp);
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    rtmp[i] = 0.0; jl[i] = mbs; il[0] = 0;
  }

  /* for each row k */
  for (k = 0; k<mbs; k++){

    /*initialize k-th row with elements nonzero in row perm(k) of A */
    jmin = ai[rip[k]]; jmax = ai[rip[k]+1];
    if (jmin < jmax) {
      for (j = jmin; j < jmax; j++){
        vj = rip[aj[j]];
        /* if (k <= vj)*/ rtmp[vj] = aa[j];
      } 
    } 

    /* modify k-th row by adding in those rows i with U(i,k) != 0 */
    dk = rtmp[k];
    i = jl[k]; /* first row to be added to k_th row  */  
    /* printf(" k=%d, pivot row = %d\n",k,i); */

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */

      /* compute multiplier, update D(k) and U(i,k) */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */
      uikdi = - ba[ili]*ba[i];  
      dk += uikdi*ba[ili];
      ba[ili] = uikdi; /* -U(i,k) */

      /* add multiple of row i to k-th row ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) rtmp[bj[j]] += uikdi*ba[j];         
        /* ... add i to row list for next nonzero entry */
        il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
        j     = bj[jmin];
        jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti; /* printf("                  pivot row i=%d\n",i);  */        
    }

    /* check for zero pivot and save diagoanl element */
    if (dk == 0.0){
      SETERRQ(PETSC_ERR_MAT_LU_ZRPVT,"Zero pivot");
    }else if (PetscRealPart(dk) < 0){
      ierr = PetscPrintf(PETSC_COMM_SELF,"Negative pivot: d[%d] = %g\n",k,dk);
    }                                               

    /* save nonzero entries in k-th row of U ... */
    ba[k] = 1.0/dk;
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         vj = bj[j]; ba[j] = rtmp[vj]; rtmp[vj] = 0.0;
      }       
      /* ... add k to row list for first nonzero entry in k-th row */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }        
  } 
  
  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(ip,&rip);CHKERRQ(ierr);
  C->factor    = FACTOR_CHOLESKY; 
  C->assembled = PETSC_TRUE; 
  C->preallocated = PETSC_TRUE;
  PLogFlops(b->mbs);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactor_SeqSBAIJ"
int MatCholeskyFactor_SeqSBAIJ(Mat A,IS perm,PetscReal f)
{
  int ierr;
  Mat C;

  PetscFunctionBegin;
  ierr = MatCholeskyFactorSymbolic(A,perm,f,&C);CHKERRQ(ierr);
  ierr = MatCholeskyFactorNumeric(A,&C);CHKERRQ(ierr);
  ierr = MatHeaderCopy(A,C);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


