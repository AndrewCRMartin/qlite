
/******************************************************************************
* Module    :   NNTP Client for the automatic extraction of all news articles
*               from a news group without the use of a news reader.
*
* Author    :   John W. M. Stevens
******************************************************************************/

#include    <stdio.h>
#include    <stdlib.h>
#include    <fcntl.h>
#include    <string.h>
#include    <time.h>

#include    <sys/errno.h>
#include    <sys/param.h>
#include    <sys/file.h>
#include    <sys/ioctl.h>
#include    <sys/socket.h>
#include    <sys/un.h>
#include    <netinet/in.h>
#include    <netdb.h>

#include    "utils.h"
#include    "regexp.h"

#define MAX_BFR     513

#define YES 1
#define NO  0

typedef enum    {
    ST_LINE,
    ST_NORM,
    ST_CR
} STATES;

typedef struct  kill_st {
    char            *NewsGroup;
    REG_EXP_NODE    *PreFix;
    int             NoKillREs;
    REG_EXP_NODE    **KillREs;
    struct  kill_st *Next;
} KILL_ND;

FILE    *ErrFile = stderr;
char    *Version = "CLIENT V1.1.0";
extern int  errno;

static  KILL_ND     *KillList;

/*-----------------------------------------------------------------------------
| Routine   :   MkTmpFl() --- Make a temporary file, and return the name.
|
| Outputs   :   FileName    - Returns the name of the temporary file.
-----------------------------------------------------------------------------*/

FILE    *MkTmpFl(char   *FileName)
{
    register    int     i;
    auto        int     ret;
    auto        char    TmBfr[10];

    /*  Use the time value to create a temporary file name. */
    sprintf(TmBfr, "%08x", time( (time_t *) 0 ));

    /*  Make sure that the file does not already exist. */
    for (i = 0; i < 1000; i++)
    {
        /*  Create the file name.   */
        sprintf(FileName,
                "%03d%.5s.%.3s",
                i,
                TmBfr,
                TmBfr + 5);

        /*  Check to see if the file exists.    */
        if ((ret = access(FileName, 0)) != 0)
        {
            if (errno == ENOENT)
                break;
        }
    }

    /*  Check to see if we failed utterly.  */
    if (i == 1000)
        return( NULL );

    /*  Open the file whose name we just constructed.   */
    return( fopen(FileName, "w") );
}

/*-----------------------------------------------------------------------------
| Routine   :   WrtSrvr() --- Write a buffer to the server.
|
| Inputs    :   SockDesc    - Socket descriptor.
|               Bfr         - Buffer to read into.
|               Len         - Length of message in buffer.
-----------------------------------------------------------------------------*/

void    WrtSrvr(int     SockDesc,
                char    *Bfr,
                int     Len)
{
    auto    int     ret;

    /*  Write buffer to server. */
    if ((ret = write(SockDesc, Bfr, Len)) <= 0)
    {
        fprintf(stderr,
                "%s %d - Error in writing to news server.\n",
                __FILE__,
                __LINE__);
        fprintf(stderr,
                "\tReturn = %d, Error number = %d\n",
                ret,
                errno);
        exit( 1 );
    }
}

/*-----------------------------------------------------------------------------
| Routine   :   RdSrvr() --- Read a buffer from the server.
|
| Inputs    :   SockDesc    - Socket descriptor.
|               Bfr         - Buffer to read into.
|               MaxLen      - Maximum length of buffer.
-----------------------------------------------------------------------------*/

int     RdSrvr(int  SockDesc,
               char *Bfr,
               int  MaxLen)
{
    auto        int     ret;

    /*  Get the buffer full.    */
    if ((ret = read(SockDesc, Bfr, MaxLen - 1)) < 0)
    {
        fprintf(stderr,
                "%s %d - Error in reading from news server.\n",
                __FILE__,
                __LINE__);
        fprintf(stderr,
                "\tReturn = %d, Error number = %d\n",
                ret,
                errno);
        exit( 1 );
    }
    Bfr[ret] = '\0';
    return( ret );
}

/*-----------------------------------------------------------------------------
| Routine   :   GetLine() --- Extract a line from the buffer into the
|               line buffer.
|
| Inputs    :   SockDesc    - Socket descriptor number.
|               Line        - Pointer to line buffer to read into.
-----------------------------------------------------------------------------*/

static  int     Size = 0;
static  int     BfrIdx = 0;
static  char    Bfr[MAX_BFR];

void    GetLine(int     SockDesc,
                char    *Line)
{
    register    int     i;
    auto        int     State;

    /*  Read a buffer full. */
    if (BfrIdx >= Size)
    {
        Size = RdSrvr(SockDesc, Bfr, MAX_BFR);
        BfrIdx = 0;
    }

    /*  Get and put characters from buffer to output file.  */
    for (State = ST_NORM, i = 0;
         State != ST_LINE && Size;
        )
    {
        /*  Determine operation based on state. */
        switch ( State )
        {
        case ST_NORM:
            if (Bfr[BfrIdx] == '\n')
            {
                fprintf(stderr,
                        "%s %d : Warning - saw '\\n' before '\\r'.\n",
                        __FILE__,
                        __LINE__);
                Line[i++] = Bfr[BfrIdx];
                State = ST_LINE;
            }
            else if (Bfr[BfrIdx] == '\r')
                State = ST_CR;
            else
                Line[i++] = Bfr[BfrIdx];
            break;
        case ST_CR:
            if (Bfr[BfrIdx] == '\n')
                Line[i++] = Bfr[BfrIdx];
            else if (Bfr[BfrIdx] == '\r')
                break;
            else
            {
                /*  Report warning. */
                fprintf(stderr,
                        "%s %d : Warning - saw '.\\r' but no '\\n'.\n",
                        __FILE__,
                        __LINE__);
            }
            State = ST_LINE;
            break;
        }

        /*  Next character, get new buffer if empty.    */
        if (++BfrIdx >= Size && State != ST_LINE)
        {
            /*  Read a buffer full. */
            Size = RdSrvr(SockDesc, Bfr, MAX_BFR);
            BfrIdx = 0;
        }
    }

    /*  NUL terminate the line and return.  */
    Line[i] = '\0';
}

/*-----------------------------------------------------------------------------
| Routine   :   GetArt() --- Get an article from the news server.
|
| Inputs    :   SockDesc    - Socket descriptor number.
|               OutFp       - Pointer to output file.
-----------------------------------------------------------------------------*/

void    GetArt(int      SockDesc,
               FILE     *OutFp)
{
    auto    char    Line[MAX_BFR];

    /*  Get a line from the server. */
    GetLine(SockDesc, Line);

    /*  Until the last line is seen, get and output lines.  */
    while ( strcmp(Line, ".\n") )
    {
        /*  Output line.    */
        fputs(Line, OutFp);

        /*  Get a line from the server. */
        GetLine(SockDesc, Line);
    }

    /*  Check for underflow.    */
    if (BfrIdx < Size)
    {
        fprintf(stderr,
                "%s %d : Error, did not use complete article body.\n",
                __FILE__,
                __LINE__);
    }

    /*  Put a blank line between articles.  */
    fprintf(OutFp, "\n");
}

/*-----------------------------------------------------------------------------
| Routine   :   RdArt() --- Read an article from the server.
|
| Inputs    :   SockDesc    - Socket descriptor.
|               OutFp       - Pointer to output file.
-----------------------------------------------------------------------------*/

void    RdArt(int       SockDesc,
              long      ArtNo,
              char      *NewsGroup,
              KILL_ND   *KillNd,
              FILE      *OutFp)
{
    register    int     i;
    auto        int     Status;
    auto        char    **SubStrs;
    auto        int     Kill;

    static      char    Line[1025];

    /*  Do we need to do kill checks?   */
    if ( KillNd )
    {
        /*  Get article header.    */
        sprintf(Bfr, "HEAD %ld\r\n", ArtNo);
        WrtSrvr(SockDesc, Bfr, strlen( Bfr ));

        /*  Get status, etc.    */
        GetLine(SockDesc, Line);
        sscanf(Line,
               "%d %ld",
               &Status,
               &ArtNo);

        /*  Execute the command.    */
        switch ( Status )
        {
        case 221:
            break;
        case 220:
        case 222:
        case 223:
            fprintf(stderr,
                    "%s %d : Error, response to ARTICLE = %d.\n",
                    __FILE__,
                    __LINE__,
                    Status);
            return;
        case 412:
            fprintf(stderr,
                    "%s %d : Error, no news group selected.\n",
                    __FILE__,
                    __LINE__);
            return;
        case 420:
            fprintf(stderr,
                    "%s %d : Error, no current article selected.\n",
                    __FILE__,
                    __LINE__);
            return;
        case 423:
            return;
        case 430:
             return;
        }

        /*  Get article header lines and process header.    */
        Kill = NO;
        GetLine(SockDesc, Line);
        while ( strcmp(Line, ".\n") )
        {
            /*  Look for Subject: line. */
            if ( ReMatch(Line, IGN_CASE, KillNd->PreFix, &SubStrs) )
            {
                /*  Now run list of kill RE's and decide whether or not
                *   we have an article to kill. . .
                */
                for (i = 0; i < KillNd->NoKillREs; i++)
                    if ( ReMatch(Line,
                                 IGN_CASE,
                                 KillNd->KillREs[i],
                                 &SubStrs) )
                    {
                        fprintf(stderr,
                                "Killing Article #%ld\n",
                                ArtNo);
                        fprintf(stderr,
                                "%s\n",
                                Line);
                        Kill = YES;
                        break;
                    }
            }

            /*  Get the next line.  */
            GetLine(SockDesc, Line);
        }

        /*  Check for article kill. */
        if (Kill == YES)
            return;
    }

    /*  Get whole article now.    */
    fputc('.', stderr);
    sprintf(Bfr, "ARTICLE %ld\r\n", ArtNo);
    WrtSrvr(SockDesc, Bfr, strlen( Bfr ));

    /*  Get status, etc.    */
    GetLine(SockDesc, Line);
    sscanf(Line,
           "%d %ld",
           &Status,
           &ArtNo);

    /*  Execute the command.    */
    switch ( Status )
    {
    case 220:
        /*  Get all text that follows, write it to
        *   the output file.
        */
        fprintf(OutFp,
                "Article: %ld of %s\n",
                ArtNo,
                NewsGroup);
        break;
    case 221:
    case 222:
    case 223:
        fprintf(stderr,
                "%s %d : Error, response to BODY = %d.\n",
                __FILE__,
                __LINE__,
                Status);
        return;
    case 412:
        fprintf(stderr,
                "%s %d : Error, no news group selected.\n",
                __FILE__,
                __LINE__);
        return;
    case 420:
        fprintf(stderr,
                "%s %d : Error, no current article selected.\n",
                __FILE__,
                __LINE__);
        return;
    case 423:
        return;
    case 430:
        return;
    }

    /*  Get article body lines and write to output file.    */
    GetArt(SockDesc, OutFp);
}

/*-----------------------------------------------------------------------------
| Routine   :   GetPortNo() --- This routine converts a character string
|               to a port number.  It looks up the service by name, and if
|               there is none, then it converts the string to a number with
|               scanf.
|
| Inputs    :   PortName    - Name of port to convert.
|
| Returns   :   Returns the TCP port number for this server.
-----------------------------------------------------------------------------*/

int GetPortNo(char  *PortName)
{
    auto    int     PortNo;
    struct  servent *Service;

    /*  If no port name string, return no port number.  */
    if (PortName == NULL)
        return 0;

    /*  Get server by name and make sure that the protocol
    *   matches as well.
    */
    Service = getservbyname(PortName, "tcp");

    /*  Return either the port number, or 0 for no port number. */
    if (Service != NULL)
        return( Service->s_port );
    else if (sscanf(PortName, "%i", &PortNo) != 1)
        return 0;
    else
        return( htons( PortNo ) );
}

/*-----------------------------------------------------------------------------
| Routine   :   GetHostAddr() --- Convert the host name to an address.
|
| Inputs    :   HostName    - Host name to convert.
|               Addr        - Host address.
|
| Returns   :   Returns 0 for error, 1 for address being returned.
-----------------------------------------------------------------------------*/

int GetHostAddr(char            *HostName,
                struct  in_addr *Addr)
{
    auto        int     len;
    struct      hostent *Host;
    auto        int     count;
    unsigned    int     a1, a2, a3, a4;

    /*  Get a pointer to the host name data.    */
    Host = gethostbyname( HostName );
    if (Host != NULL)
        bcopy(Host->h_addr, Addr, Host->h_length);
    else
    {
        /*  Convert the string representation of the internet
        *   address into bytes.
        */
        count = sscanf(HostName,
                       "%i.%i.%i.%i%n",
                       &a1, &a2, &a3, &a4, &len);
        if (count != 4 || HostName[len] != 0)
            return( 0 );

        /*  Insert the bytes of the internet address into the
        *   return address structure.  I suspect that this is highly
        *   machine specific code.
        */
        Addr->S_un.S_un_b.s_b1 = a1;
        Addr->S_un.S_un_b.s_b2 = a2;
        Addr->S_un.S_un_b.s_b3 = a3;
        Addr->S_un.S_un_b.s_b4 = a4;
    }

    /*  Return no errors occurred.  */
    return( 1 );
}

/*-----------------------------------------------------------------------------
| Routine   :   SetupSocket() --- Set up the socket.
|
| Inputs    :   HostName    - Name of host.
|               PortName    - Name of port.
-----------------------------------------------------------------------------*/

int SetupSocket(char    *HostName,
                char    *PortName)
{
    auto    int         SockDesc;   /*  Socket handle (descriptor).     */
    struct  sockaddr    SockAddr;   /*  Socket address structure.       */
    struct  sockaddr_in *InAddr;    /*  Internet address structure.     */
    struct  in_addr     *AdrPtr;

    /*  Get the address of the server.  */
    InAddr=(struct sockaddr_in *) &SockAddr;
    InAddr->sin_family = AF_INET;
    if (! GetHostAddr(HostName, &InAddr->sin_addr))
    {
        fprintf(stderr,
                "%s %d : Error - Could not convert ",
                __FILE__,
                __LINE__);
        fprintf(stderr,
                "'%s' to a host address.\n",
                HostName);
        exit( 1 );
    }
    AdrPtr = (struct in_addr *) (&InAddr->sin_addr);
    fprintf(stderr,
            "Host '%s', address = %u.%u.%u.%u\n",
            HostName,
            AdrPtr->S_un.S_un_b.s_b1,
            AdrPtr->S_un.S_un_b.s_b2,
            AdrPtr->S_un.S_un_b.s_b3,
            AdrPtr->S_un.S_un_b.s_b4);

    /*  Convert a service name to a port number.    */
    InAddr->sin_port = GetPortNo( PortName );
    if (InAddr->sin_port == 0)
    {
        fprintf(stderr,
                "%s %d : Error - bogus port number '%s'.\n",
                __FILE__,
                __LINE__,
                PortName);
        exit( 1 );
    }
    fprintf(stderr,
            "NNTP port number = %d\n",
            InAddr->sin_port);

    /*  Attempt to get a socket descriptor. */
    SockDesc = socket(AF_INET, SOCK_STREAM, 0);
    if (SockDesc < 0)
    {
        perror("opening stream socket");
        exit( 1 );
    }

    /*  Connect to the server.  */
    if (connect(SockDesc , &SockAddr, sizeof( SockAddr )) < 0)
    {
        /*  Close the old socket descriptor.    */
        perror("connecting");
        fprintf(stderr,
                "%s %d : Error - could not connect to news server.\n",
                __FILE__,
                __LINE__);
        exit( 1 );
    }

    /*  Return the socket descriptor.   */
    return( SockDesc );
}

/*-----------------------------------------------------------------------------
| Routine   :   RdConfig() --- Open and read the configuration file.
|
| Inputs    :   CfgFlNm     - Name of configuration file.
|               SockDesc    - Socket description.
-----------------------------------------------------------------------------*/

void    RdConfig(char   *CfgFlNm,
                 int    SockDesc)
{
    register    long    i;
    auto        FILE    *CfgFp;
    auto        FILE    *NewFp;
    auto        FILE    *OutFp;
    auto        int     ret;

    auto        int     Status;
    auto        long    Number;
    auto        long    LastRd;
    auto        long    Start;
    auto        long    End;
    auto        KILL_ND *KillNd;

    static      char    TmpFlNm[MAX_BFR];
    static      char    OutFlNm[MAX_BFR];
    static      char    Bfr[MAX_BFR];
    static      char    Cmd[MAX_BFR];
    static      char    GroupName[MAX_BFR];

    /*  Open configuration file.    */
    if ((CfgFp = fopen(CfgFlNm, "r")) == NULL)
    {
        fprintf(stderr,
                "%s %d : Error, could not open configuration file.\n",
                __FILE__,
                __LINE__);
        return;
    }

    /*  Open new configuration file.    */
    if ((NewFp = MkTmpFl( TmpFlNm )) == NULL)
    {
        fprintf(stderr,
                "%s %d : Error, could not open new configuration file.\n",
                __FILE__,
                __LINE__);
        fclose( CfgFp );
        return;
    }

    /*  Read the file a line at a time. */
    while (! feof( CfgFp ))
    {
        /*  Get a line. */
        if (fgets(Bfr, MAX_BFR, CfgFp) == NULL)
            break;

        /*  Parse configuration file line.  */
        ret = sscanf(Bfr,
                     "group %s %s %ld",
                     GroupName,
                     OutFlNm,
                     &LastRd);
        if (ret != 3)
        {
            fprintf(stderr,
                    "%s %d : Error - bad configuration file line.\n",
                    __FILE__,
                    __LINE__);
            fprintf(stderr,
                    "\t'%s'\n",
                    Bfr);
        }
        else
        {
            /*  Switch to appropriate group.    */
            sprintf(Cmd, "GROUP %s\r\n", GroupName);
            WrtSrvr(SockDesc, Cmd, strlen( Cmd ));
             RdSrvr(SockDesc, Bfr, MAX_BFR);
            fprintf(stderr, "%s", Bfr);

            /*  Check return status and article number range.   */
            ret = sscanf(Bfr,
                         "%d %ld %ld %ld %s",
                         &Status,
                         &Number,
                         &Start,
                         &End,
                         Cmd);

            /*  Check for non-existant news group.  */
            if (Status == 411)
            {
                fprintf(stderr,
                        "%s %d : Error, no such news group '%s'.\n",
                        __FILE__,
                        __LINE__,
                        GroupName);
            }
            else
            {
                /*  Check for no articles in news group.    */
                if (Number == 0L || Start > End)
                {
                    fprintf(stderr,
                            "%s %d : No news in group '%s'.\n",
                            __FILE__,
                            __LINE__,
                            GroupName);
                }
                else
                {
                    /*  Open output file, get all articles. */
                    if ((OutFp = fopen(OutFlNm, "a")) == NULL)
                    {
                        fprintf(stderr,
                                "%s %d : Error, could not open file ",
                                __FILE__,
                                __LINE__);
                        fprintf(stderr,
                                "'%s' for writing.\n",
                                OutFlNm);
                    }
                    else
                    {
                        /*  Is there a kill file?   */
                        if ( KillList )
                        {
                            /*  Search kill list.   */
                            for (KillNd = KillList;
                                 KillNd;
                                 KillNd = KillNd->Next)
                                if (! strcmp(GroupName, KillNd->NewsGroup))
                                    break;
                        }
                        else
                            KillNd = NULL;

                        /*  Get all articles, write to file.    */
                        if (LastRd <= End)
                        {
                            /*  Get all articles.   */
                            for (i = (LastRd > Start) ? LastRd : Start;
                                 i <= End;
                                  i++)
                                RdArt(SockDesc, i, GroupName, KillNd, OutFp);
                            LastRd = i;
                        }
                        fputc('\n', stderr);

                        /*  Close the output file pointer.  */
                        fclose( OutFp );
                    }
                }
            }
        }

        /*  Write the new value for the configuration file. */
        fprintf(NewFp,
                "group %s %s %ld\n",
                GroupName,
                OutFlNm,
                LastRd);
    }

    /*  Close the configuration files.  */
    fclose( CfgFp );
    fclose( NewFp );

    /*  Delete the old configuration file, and rename the new to
    *   be the same as the old.
    */
    remove( CfgFlNm );
    rename(TmpFlNm, CfgFlNm);
}

/*-----------------------------------------------------------------------------
| Routine   :   AddKillRE() --- Add a kill RE to the list.
|
| Inputs    :   Bfr     - RE source string.
-----------------------------------------------------------------------------*/

void    AddKillRE(KILL_ND   *KillNd,
                  char      *Bfr)
{
    auto    REG_EXP_NODE    **ReExpr;

    /*  Up the number and allocate a new list.  */
    KillNd->NoKillREs++;
    ReExpr = KillNd->KillREs;

    /*  Allocate new list.  */
    KillNd->KillREs = (REG_EXP_NODE **)
    malloc(KillNd->NoKillREs * sizeof(REG_EXP_NODE *));
    if (KillNd->KillREs == NULL)
    {
        fprintf(stderr,
                "%s %d - Out of memory.\n",
                __FILE__,
                __LINE__);
        exit( 1 );
    }

    /*  Copy old list to new and deallocate old list.   */
    if ( ReExpr )
    {
        MemCopy(KillNd->KillREs,
                ReExpr,
                KillNd->NoKillREs * sizeof(REG_EXP_NODE *));
        free( ReExpr );
     }

    /*  Add new kill regular expression to list.    */
    KillNd->KillREs[KillNd->NoKillREs - 1] = ReCompile( Bfr );
}

/*-----------------------------------------------------------------------------
| Routine   :   BldKill() --- Build kill list.
|
| Inputs    :   Bfr     - RE source string.
-----------------------------------------------------------------------------*/

void    BldKill(FILE    *KillFp)
{
    auto        KILL_ND     *KillNd;
    auto        char        String[MAX_BFR];

    /*  Assume that the first line is the news group name.  */
    while ( fgets(Bfr, MAX_BFR, KillFp) )
    {
        /*  Allocate a kill list node.  */
        if ((KillNd = (KILL_ND *) calloc(1, sizeof( KILL_ND ))) == NULL)
        {
            fprintf(stderr,
                    "%s %d - Out of memory.\n",
                    __FILE__,
                    __LINE__);
            exit( 1 );
        }

        /*  Initialize the kill list.   */
        if (KillList != NULL)
            KillNd->Next = KillList;
        KillList = KillNd;

        /*  Save the news group name string.    */
        sscanf(Bfr, "%s", String);
        KillNd->NewsGroup = StrDup( String );

        /*  Get begin.  */
        if (fgets(Bfr, MAX_BFR, KillFp) == NULL)
        {
            fprintf(stderr,
                    "%s %d - Error in kill file 'begin'.\n",
                    __FILE__,
                    __LINE__);
            fprintf(stderr,
                    "\tNewsgroup: '%s'\n",
                    String);
            exit( 1 );
        }

        /*  Get and create the prefix RE.   */
        sscanf(Bfr, "begin /%[^/]", String);
        KillNd->PreFix = ReCompile( String );

        /*  Get kill RE's.  */
        while ( fgets(Bfr, MAX_BFR, KillFp) )
        {
            /*  Get the RE. */
            if (sscanf(Bfr, " /%[^/]", String) == 0)
                if (sscanf(Bfr, "%s", String) == 1 &&
                    strcmp(String, "end") == 0)
                    break;
                else
                {
                     /*  Report missing 'end' error. */
                    fprintf(stderr,
                            "%s %d - Error, missing kill file 'end'.\n",
                            __FILE__,
                            __LINE__);
                    exit( 1 );
                }

            /*  Add kill RE to list.    */
            AddKillRE(KillNd, String);
        }
    }
}

void    main(int    argc,
             char   **argv)
{
    auto        int             SockDesc;
    auto        FILE            *KillFp;

    /*  Check the argument count.   */
    if (argc < 3)
    {
        /*  Bad argument count, give synopsis.  */
        fprintf(stderr,
                "Usage : %s <server> <config>\n",
                argv[0]);
        exit( 1 );
    }
    else if (argc == 4)
    {
        /*  Open kill file. */
        if ((KillFp = fopen(argv[3], "r")) == NULL)
        {
            fprintf(stderr,
                    "%s %d : Error, could not open kill file.\n",
                    __FILE__,
                    __LINE__);
            exit( 1 );
        }

        /*  Build kill list.    */
        BldKill( KillFp );

        /*  Close kill file.    */
        fclose( KillFp );
    }

    /*  Attempt to set up the socket.   */
    SockDesc = SetupSocket(argv[1], "nntp");

    /*  Read the sign on message and print it.  */
    fprintf(stderr, "%s\n", Version);
    RdSrvr(SockDesc, Bfr, MAX_BFR);
    fprintf(stderr, "%s", Bfr);

    /*  Read and execute the commands out of the configuration file.    */
    RdConfig(argv[2], SockDesc);

    /*  Quit server.    */
    sprintf(Bfr, "QUIT\r\n");
    WrtSrvr(SockDesc, Bfr, strlen( Bfr ));
    RdSrvr(SockDesc, Bfr, MAX_BFR);
    fprintf(stderr, "%s", Bfr);

    /*  Close the socket descriptor and exit with no errors.    */
    close( SockDesc );
    exit( 0 );
}


