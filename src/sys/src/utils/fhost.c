/*$Id: fhost.c,v 1.44 2000/04/12 04:21:38 bsmith Exp balay $*/
/*
      Code for manipulating files.
*/
#include "petsc.h"
#include "petscsys.h"
#if defined(PETSC_HAVE_STDLIB_H)
#include <stdlib.h>
#endif
#if !defined(PARCH_win32)
#include <sys/utsname.h>
#endif
#if defined(PARCH_win32)
#include <windows.h>
#include <io.h>
#include <direct.h>
#endif
#if defined (PARCH_win32_gnu)
#include <windows.h>
#endif
#if defined(PETSC_HAVE_SYS_SYSTEMINFO_H)
#include <sys/systeminfo.h>
#endif
#if defined(PETSC_HAVE_UNISTD_H)
#include <unistd.h>
#endif
#include "petscfix.h"

#undef __FUNC__  
#define __FUNC__ /*<a name=""></a>*/"PetscGetHostName"
/*@C
    PetscGetHostName - Returns the name of the host. This attempts to
    return the entire Internet name. It may not return the same name
    as MPI_Get_processor_name().

    Not Collective

    Input Parameter:
.   nlen - length of name

    Output Parameter:
.   name - contains host name.  Must be long enough to hold the name
           This is the fully qualified name, including the domain.

    Level: developer

.keywords: system, get, host, name

.seealso: PetscGetUserName()
@*/
int PetscGetHostName(char name[],int nlen)
{
  char           *domain;
  int            ierr;
  PetscTruth     flag;
#if defined(PETSC_HAVE_UNAME)
  struct utsname utname;
#endif
#if defined(PETSC_HAVE_GETDOMAINNAME)
  PetscTruth     match;
#endif

  PetscFunctionBegin;

#if defined(PARCH_win32) || defined(PARCH_win32_gnu)
  GetComputerName((LPTSTR)name,(LPDWORD)(&nlen));
#elif defined(PETSC_HAVE_UNAME)
  uname(&utname); 
  ierr = PetscStrncpy(name,utname.nodename,nlen);CHKERRQ(ierr);
#elif defined(PETSC_HAVE_GETHOSTNAME)
  gethostname(name,nlen);
#elif defined(PETSC_HAVE_SYSINFO)
  sysinfo(SI_HOSTNAME,name,nlen);
#endif

  /* See if this name includes the domain */
  ierr = PetscStrchr(name,'.',&domain);CHKERRQ(ierr);
  if (!domain) {
    int  l;
    ierr = PetscStrlen(name,&l);CHKERRQ(ierr);
    if (l == nlen) PetscFunctionReturn(0);
    name[l++] = '.';
#if defined(PETSC_HAVE_SYSINFO)
    sysinfo(SI_SRPC_DOMAIN,name+l,nlen-l);
#elif defined(PETSC_HAVE_GETDOMAINNAME)
    getdomainname(name+l,nlen - l);
    /* change domain name if it is an ANL crap one */
    ierr = PetscStrcmp(name+l,"qazwsxedc",&match);CHKERRQ(ierr);
    if (match) {
      ierr = PetscStrncpy(name+l,"mcs.anl.gov",nlen-12);CHKERRQ(ierr);
    }
#endif
    /* 
       Some machines (Linx) default to (none) if not
       configured with a particular domain name.
    */
    ierr = PetscStrncmp(name+l,"(none)",6,&flag);CHKERRQ(ierr);
    if (flag) {
      name[l-1] = 0;
    }
  }
  PetscFunctionReturn(0);
}
