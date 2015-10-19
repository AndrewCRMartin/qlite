/*************************************************************************

   Program:    QLite
   File:       qlutil.h
   
   Version:    V1.0
   Date:       04.10.00
   Function:   Header file for all QLite programs
   
   Copyright:  (c) University of Reading / Dr. Andrew C. R. Martin 2000
   Author:     Dr. Andrew C. R. Martin
   Address:    School of Animal and Microbial Sciences,
               The University of Reading,
               Whiteknights,
               P.O. Box 228,
               Reading RG6 6AJ.
               England.
   Phone:      +44 (0)118 987 5123 Extn. 7022
   Fax:        +44 (0)118 931 0180
   EMail:      andrew@bioinf.org.uk
               andrew@stagleys.demon.co.uk
               
**************************************************************************

   This program is not in the public domain.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

   See the file COPYING.DOC for details of what you may and may not
   do with this program.

   In particular, you may not distribute this code without express
   permission from the author; it must be obtained directly from the 
   author.

**************************************************************************

   Description:
   ============

**************************************************************************

   Usage:
   ======

**************************************************************************

   Revision History:
   =================
   V1.0  04.10.00  Original   By: ACRM

*************************************************************************/
/* Includes
*/

/************************************************************************/
/* Defines and macros
*/
#define MAXBUFF         160   /* General purpose buffer                 */
#define MAXCLUSTER      100   /* Max number of clusters (not machines!) */
#define DEFAULT_QLPORT  5468  /* Default port for qlockd                */
#define LOCK_TIMEOUT    30    /* Timeout on trying to get a lock        */

#ifndef SYS_TYPES_H     /* Unix: <sys/types.h>, MS-DOS: <sys\types.h>   */
#ifndef _TYPES_         /* Ditto                                        */
typedef short           BOOL;
typedef long            LONG;
typedef unsigned long   ULONG;
typedef short           SHORT;
typedef unsigned short  USHORT;
typedef unsigned char   UCHAR;
typedef unsigned char   UBYTE;
#endif
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define KILLLEADSPACES(y,x)                                              \
                 do                                                      \
                 {  for((y)=(x); *(y) == ' ' || *(y) == '\t'; (y)++) ; } \
                 while(0)


#define KILLTRAILSPACES(x)                                               \
do {  int _kts_macro_i;                                                  \
      _kts_macro_i = strlen(x) - 1;                                      \
      while(((x)[_kts_macro_i] == ' ' ||                                 \
             (x)[_kts_macro_i] == '\t') &&                               \
            _kts_macro_i>=0)                                             \
         (_kts_macro_i)--;                                               \
      (x)[++(_kts_macro_i)] = '\0';                                      \
   }  while(0)

#define INIT(x,y) do { x=(y *)malloc(sizeof(y));                         \
                    if(x != NULL) x->next = NULL; } while(0)

#define NEXT(x) (x)=(x)->next

#define ALLOCNEXT(x,y) do { (x)->next=(y *)malloc(sizeof(y));            \
                         if((x)->next != NULL) { (x)->next->next=NULL; } \
                         NEXT(x); } while(0)

#define TERMINATE(x) do {  int _terminate_macro_j;                    \
                        for(_terminate_macro_j=0;                     \
                            (x)[_terminate_macro_j];                  \
                            _terminate_macro_j++)                     \
                        {  if((x)[_terminate_macro_j] == '\n')        \
                           {  (x)[_terminate_macro_j] = '\0';         \
                              break;                                  \
                     }  }  }  while(0)


typedef struct _runfile
{
   struct _runfile *next;
   char file[PATH_MAX];
   char node[MAXBUFF];
}  RUNFILE;


/************************************************************************/
/* Globals
*/


/************************************************************************/
/* Prototypes
*/
/* Routines in qlutil.c                                                 */
void CreateFullPath(char *file);
void UpdateSpoolDir(char *spoolDir, int cluster);
int CreateLockFile(char *spoolDir);
void DeleteLockFile(char *spoolDir);
BOOL CheckForSpoolDir(char *spoolDir);
BOOL RootUser(void);
RUNFILE *ReadMachineList(char *spoolDir);
BOOL  DaemonInit(void);
int atoport(char *service, char *proto);
void GetPortAndLockHost(char *spoolDir, int *port, char *lockhost);

/* Routines in qlclient.c                                               */
BOOL InitLocks(char *service, char *host, int port);
int  GetLock(int id, int timeout);
BOOL ReleaseLock(int id);
