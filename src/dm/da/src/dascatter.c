#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: dascatter.c,v 1.13 1998/04/13 17:58:52 bsmith Exp curfman $";
#endif
 
/*
  Code for manipulating distributed regular arrays in parallel.
*/

#include "src/da/daimpl.h"    /*I   "da.h"   I*/

#undef __FUNC__  
#define __FUNC__ "DAGetScatter"
/*@C
   DAGetScatter - Gets the local-to-global, local-to-global, and 
   local-to-local vector scatter contexts for a distributed array.

   Not Collective, but VecScatter is parallel if DA is parallel

   Input Parameter:
.  da - the distributed array

   Output Parameters:
+  ltog - local-to-global scatter context (may be PETSC_NULL)
.  gtol - global-to-local scatter context (may be PETSC_NULL) 
-  ltol - local-to-local scatter context (may be PETSC_NULL)

   Notes:
   The output contexts are valid only as long as the input da is valid.
   If you delete the da, the scatter contexts will become invalid.

.keywords: distributed array, get, scatter, context, global-to-local,
           local-to-global, local-to-local

.seealso: DAGlobalToLocalBegin(), DAGlobalToLocalEnd(), DALocalToGlobal()
@*/
int DAGetScatter(DA da, VecScatter *ltog,VecScatter *gtol,VecScatter *ltol)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(da,DA_COOKIE);
  if (ltog) *ltog = da->ltog;
  if (gtol) *gtol = da->gtol;
  if (ltol) *ltol = da->ltol;
  PetscFunctionReturn(0);
}
 
