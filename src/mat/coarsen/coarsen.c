
#include <petsc/private/matimpl.h>               /*I "petscmatcoarsen.h" I*/

/* Logging support */
PetscClassId MAT_COARSEN_CLASSID;

PetscFunctionList MatCoarsenList              = NULL;
PetscBool         MatCoarsenRegisterAllCalled = PETSC_FALSE;

/*@C
   MatCoarsenRegister - Adds a new sparse matrix coarsening algorithm to the matrix package.

   Logically Collective

   Input Parameters:
+  sname - name of coarsen (for example MATCOARSENMIS)
-  function - function pointer that creates the coarsen type

   Level: developer

   Sample usage:
.vb
   MatCoarsenRegister("my_agg",MyAggCreate);
.ve

   Then, your aggregator can be chosen with the procedural interface via
$     MatCoarsenSetType(agg,"my_agg")
   or at runtime via the option
$     -mat_coarsen_type my_agg

.seealso: `MatCoarsenRegisterDestroy()`, `MatCoarsenRegisterAll()`
@*/
PetscErrorCode  MatCoarsenRegister(const char sname[],PetscErrorCode (*function)(MatCoarsen))
{
  PetscFunctionBegin;
  PetscCall(MatInitializePackage());
  PetscCall(PetscFunctionListAdd(&MatCoarsenList,sname,function));
  PetscFunctionReturn(0);
}

/*@C
   MatCoarsenGetType - Gets the Coarsen method type and name (as a string)
        from the coarsen context.

   Not collective

   Input Parameter:
.  coarsen - the coarsen context

   Output Parameter:
.  type - coarsener type

   Level: advanced

   Not Collective

.seealso: `MatCoarsenCreate()`, `MatCoarsenType`, `MatCoarsenSetType()`
@*/
PetscErrorCode  MatCoarsenGetType(MatCoarsen coarsen,MatCoarsenType *type)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(coarsen,MAT_COARSEN_CLASSID,1);
  PetscValidPointer(type,2);
  *type = ((PetscObject)coarsen)->type_name;
  PetscFunctionReturn(0);
}

/*@
   MatCoarsenApply - Gets a coarsen for a matrix.

   Collective on MatCoarsen

   Input Parameter:
.   coarsen - the coarsen

   Options Database Keys:
   To specify the coarsen through the options database, use one of
   the following
$    -mat_coarsen_type mis|hem|misk
   To see the coarsen result
$    -mat_coarsen_view

   Level: advanced

   Notes:
    Use MatCoarsenGetData() to access the results of the coarsening

   The user can define additional coarsens; see MatCoarsenRegister().

.seealso: `MatCoarsenRegister()`, `MatCoarsenCreate()`,
          `MatCoarsenDestroy()`, `MatCoarsenSetAdjacency()`
          `MatCoarsenGetData()`
@*/
PetscErrorCode  MatCoarsenApply(MatCoarsen coarser)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(coarser,MAT_COARSEN_CLASSID,1);
  PetscValidPointer(coarser,1);
  PetscCheck(coarser->graph->assembled,PetscObjectComm((PetscObject)coarser),PETSC_ERR_ARG_WRONGSTATE,"Not for unassembled matrix");
  PetscCheck(!coarser->graph->factortype,PetscObjectComm((PetscObject)coarser),PETSC_ERR_ARG_WRONGSTATE,"Not for factored matrix");
  PetscCheck(coarser->ops->apply,PetscObjectComm((PetscObject)coarser),PETSC_ERR_ARG_WRONGSTATE,"Must set type with MatCoarsenSetFromOptions() or MatCoarsenSetType()");
  PetscCall(PetscLogEventBegin(MAT_Coarsen,coarser,0,0,0));
  PetscCall((*coarser->ops->apply)(coarser));
  PetscCall(PetscLogEventEnd(MAT_Coarsen,coarser,0,0,0));
  PetscFunctionReturn(0);
}

/*@
   MatCoarsenSetAdjacency - Sets the adjacency graph (matrix) of the thing to be coarsened.

   Collective on MatCoarsen

   Input Parameters:
+  agg - the coarsen context
-  adj - the adjacency matrix

   Level: advanced

.seealso: `MatCoarsenCreate()`, `MatCoarsenApply()`
@*/
PetscErrorCode  MatCoarsenSetAdjacency(MatCoarsen agg, Mat adj)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(agg,MAT_COARSEN_CLASSID,1);
  PetscValidHeaderSpecific(adj,MAT_CLASSID,2);
  agg->graph = adj;
  PetscFunctionReturn(0);
}

/*@
   MatCoarsenSetStrictAggs - Set whether to keep strict (non overlapping) aggregates in the linked list of aggregates for a coarsen context

   Logically Collective on MatCoarsen

   Input Parameters:
+  agg - the coarsen context
-  str - PETSC_TRUE keep strict aggregates, PETSC_FALSE allow overlap
   Level: advanced

.seealso: `MatCoarsenCreate()`
@*/
PetscErrorCode MatCoarsenSetStrictAggs(MatCoarsen agg, PetscBool str)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(agg,MAT_COARSEN_CLASSID,1);
  agg->strict_aggs = str;
  PetscFunctionReturn(0);
}

/*@
   MatCoarsenDestroy - Destroys the coarsen context.

   Collective on MatCoarsen

   Input Parameters:
.  agg - the coarsen context

   Level: advanced

.seealso: `MatCoarsenCreate()`
@*/
PetscErrorCode  MatCoarsenDestroy(MatCoarsen *agg)
{
  PetscFunctionBegin;
  if (!*agg) PetscFunctionReturn(0);
  PetscValidHeaderSpecific((*agg),MAT_COARSEN_CLASSID,1);
  if (--((PetscObject)(*agg))->refct > 0) {*agg = NULL; PetscFunctionReturn(0);}

  if ((*agg)->ops->destroy) {
    PetscCall((*(*agg)->ops->destroy)((*agg)));
  }

  if ((*agg)->agg_lists) {
    PetscCall(PetscCDDestroy((*agg)->agg_lists));
  }

  PetscCall(PetscHeaderDestroy(agg));
  PetscFunctionReturn(0);
}

/*@
   MatCoarsenCreate - Creates a coarsen context.

   Collective

   Input Parameter:
.   comm - MPI communicator

   Output Parameter:
.  newcrs - location to put the context

   Level: advanced

.seealso: `MatCoarsenSetType()`, `MatCoarsenApply()`, `MatCoarsenDestroy()`,
          `MatCoarsenSetAdjacency()`, `MatCoarsenGetData()`

@*/
PetscErrorCode  MatCoarsenCreate(MPI_Comm comm, MatCoarsen *newcrs)
{
  MatCoarsen     agg;

  PetscFunctionBegin;
  *newcrs = NULL;

  PetscCall(MatInitializePackage());
  PetscCall(PetscHeaderCreate(agg, MAT_COARSEN_CLASSID,"MatCoarsen","Matrix/graph coarsen", "MatCoarsen", comm, MatCoarsenDestroy, MatCoarsenView));

  *newcrs = agg;
  PetscFunctionReturn(0);
}

/*@C
   MatCoarsenViewFromOptions - View from Options

   Collective on MatCoarsen

   Input Parameters:
+  A - the coarsen context
.  obj - Optional object
-  name - command line option

   Level: intermediate
.seealso: `MatCoarsen`, `MatCoarsenView`, `PetscObjectViewFromOptions()`, `MatCoarsenCreate()`
@*/
PetscErrorCode  MatCoarsenViewFromOptions(MatCoarsen A,PetscObject obj,const char name[])
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(A,MAT_COARSEN_CLASSID,1);
  PetscCall(PetscObjectViewFromOptions((PetscObject)A,obj,name));
  PetscFunctionReturn(0);
}

/*@C
   MatCoarsenView - Prints the coarsen data structure.

   Collective on MatCoarsen

   Input Parameters:
+  agg - the coarsen context
-  viewer - optional visualization context

   Level: advanced

   Note:
   The available visualization contexts include
+     PETSC_VIEWER_STDOUT_SELF - standard output (default)
-     PETSC_VIEWER_STDOUT_WORLD - synchronized standard
         output where only the first processor opens
         the file.  All other processors send their
         data to the first processor to print.

   The user can open alternative visualization contexts with
.     PetscViewerASCIIOpen() - output to a specified file

.seealso: `PetscViewerASCIIOpen()`
@*/
PetscErrorCode  MatCoarsenView(MatCoarsen agg,PetscViewer viewer)
{
  PetscBool      iascii;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(agg,MAT_COARSEN_CLASSID,1);
  if (!viewer) {
    PetscCall(PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)agg),&viewer));
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,2);
  PetscCheckSameComm(agg,1,viewer,2);

  PetscCall(PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii));
  PetscCall(PetscObjectPrintClassNamePrefixType((PetscObject)agg,viewer));
  if (agg->ops->view) {
    PetscCall(PetscViewerASCIIPushTab(viewer));
    PetscCall((*agg->ops->view)(agg,viewer));
    PetscCall(PetscViewerASCIIPopTab(viewer));
  }
  PetscFunctionReturn(0);
}

/*@C
   MatCoarsenSetType - Sets the type of aggregator to use

   Collective on MatCoarsen

   Input Parameters:
+  coarser - the coarsen context.
-  type - a known coarsening method

   Options Database Command:
$  -mat_coarsen_type  <type>
$      Use -help for a list of available methods
$      (for instance, misk)

   Level: advanced

.seealso: `MatCoarsenCreate()`, `MatCoarsenApply()`, `MatCoarsenType`, `MatCoarsenGetType()`

@*/
PetscErrorCode  MatCoarsenSetType(MatCoarsen coarser, MatCoarsenType type)
{
  PetscBool      match;
  PetscErrorCode (*r)(MatCoarsen);

  PetscFunctionBegin;
  PetscValidHeaderSpecific(coarser,MAT_COARSEN_CLASSID,1);
  PetscValidCharPointer(type,2);

  PetscCall(PetscObjectTypeCompare((PetscObject)coarser,type,&match));
  if (match) PetscFunctionReturn(0);

  if (coarser->ops->destroy) {
    PetscCall((*coarser->ops->destroy)(coarser));
    coarser->ops->destroy = NULL;
  }
  PetscCall(PetscMemzero(coarser->ops,sizeof(struct _MatCoarsenOps)));

  PetscCall(PetscFunctionListFind(MatCoarsenList,type,&r));
  PetscCheck(r,PetscObjectComm((PetscObject)coarser),PETSC_ERR_ARG_UNKNOWN_TYPE,"Unknown coarsen type %s",type);
  PetscCall((*r)(coarser));

  PetscCall(PetscFree(((PetscObject)coarser)->type_name));
  PetscCall(PetscStrallocpy(type,&((PetscObject)coarser)->type_name));
  PetscFunctionReturn(0);
}

/*@C
   MatCoarsenSetGreedyOrdering - Sets the ordering of the vertices to use with a greedy coarsening method

   Logically Collective on Coarsen

   Input Parameters:
+  coarser - the coarsen context
-  perm - vertex ordering of (greedy) algorithm

   Level: advanced

   Notes:
      The IS weights is freed by PETSc, so user has given this to us

.seealso: `MatCoarsenCreate()`, `MatCoarsenSetType()`
@*/
PetscErrorCode MatCoarsenSetGreedyOrdering(MatCoarsen coarser, const IS perm)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(coarser,MAT_COARSEN_CLASSID,1);
  coarser->perm = perm;
  PetscFunctionReturn(0);
}

/*@C
   MatCoarsenGetData - Gets the weights for vertices for a coarsen.

   Logically Collective on Coarsen

   Input Parameter:
.  coarser - the coarsen context

   Output Parameter:
.  llist - linked list of aggregates

   Level: advanced

.seealso: `MatCoarsenCreate()`, `MatCoarsenSetType()`
@*/
PetscErrorCode MatCoarsenGetData(MatCoarsen coarser, PetscCoarsenData **llist)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(coarser,MAT_COARSEN_CLASSID,1);
  PetscCheck(coarser->agg_lists,PetscObjectComm((PetscObject)coarser),PETSC_ERR_ARG_WRONGSTATE,"No linked list - generate it or call ApplyCoarsen");
  *llist             = coarser->agg_lists;
  coarser->agg_lists = NULL; /* giving up ownership */
  PetscFunctionReturn(0);
}

/*@
   MatCoarsenSetFromOptions - Sets various coarsen options from the
        options database.

   Collective on MatCoarsen

   Input Parameter:
.  coarser - the coarsen context.

   Options Database Command:
$  -mat_coarsen_type  <type>
$      Use -help for a list of available methods
$      (for instance, mis)

   Level: advanced

@*/
PetscErrorCode MatCoarsenSetFromOptions(MatCoarsen coarser)
{
  PetscBool      flag;
  char           type[256];
  const char     *def;

  PetscFunctionBegin;
  PetscObjectOptionsBegin((PetscObject)coarser);
  if (!((PetscObject)coarser)->type_name) {
    def = MATCOARSENMISK;
  } else {
    def = ((PetscObject)coarser)->type_name;
  }

  PetscCall(PetscOptionsFList("-mat_coarsen_type","Type of aggregator","MatCoarsenSetType",MatCoarsenList,def,type,256,&flag));
  if (flag) PetscCall(MatCoarsenSetType(coarser,type));
  /*
   Set the type if it was never set.
   */
  if (!((PetscObject)coarser)->type_name) {
    PetscCall(MatCoarsenSetType(coarser,def));
  }

  if (coarser->ops->setfromoptions) PetscCall((*coarser->ops->setfromoptions)(PetscOptionsObject,coarser));
  PetscOptionsEnd();

  PetscFunctionReturn(0);
}
