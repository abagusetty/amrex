//BL_COPYRIGHT_NOTICE

//
// $Id: tVisMF.cpp,v 1.13 1997-11-18 18:51:53 lijewski Exp $
//

#include <stdlib.h>

#include <VisMF.H>

//
// This is IAMRAll/preload.cpp -- too bad this couldn't be in a library.
//
#ifdef BL_USE_BSP

#ifdef BL_USE_NEW_HFILES
#include <cstring>
#else
#include <string.h>
#endif

extern int BSP_DO_STAT;
extern int BSP_DO_CGPROF;
extern int BSP_DO_PROF;
extern int BSP_NBUFFERS;
extern int BSP_BUFFER_SIZE;
extern int BSP_SLOTSIZE_USECS;
extern int BSP_BUFFER_STALLS;
extern int BSP_THROTTLE_PROCS;
extern int BSP_COMM_FIFO_SIZE;
extern int BSP_OPT_CONTENTION_LEVEL;
extern int BSP_OPT_FCOMBINE_PUTS;
extern int BSP_OPT_FCOMBINE_PUTS_MAX;
extern int BSP_OPT_FCOMBINE_PUTS_MIN;
extern int BSP_OPT_BSMP_BUFFER_SIZE;
extern char *BSP_COMPILE_FLAGS;
extern char *BSP_ARCH;
extern char *BSP_INCLUDE_DIR;
extern int BSP_CHECK_SYNCS;
extern char *BSP_EXEC_FILE;
extern char BSP_LIBRARY_TYPE;
extern int  BSP_OPT_FLIBRARY_LEVEL;

extern "C" void _bsp_preload_init ();

//
// Set BSP_INCLUDE_DIR from the environment else take precompiled default.
//

static
void
get_bsp_include_dir ()
{
    const char* dir = getenv("BSP_INCLUDE_DIR");

    if (dir == 0 || *dir == 0)
    {
        if (BSP_INCLUDE_DIR == 0)
            bsp_abort("BSP_INCLUDE_DIR must be set");
    }
    else
    {
        if (!(BSP_INCLUDE_DIR == 0))
            free(BSP_INCLUDE_DIR);

        if ((BSP_INCLUDE_DIR = (char*) malloc(strlen(dir) + 1)) == 0)
            bsp_abort("malloc() failed");

        strcpy(BSP_INCLUDE_DIR, dir);
    }

    printf("Using BSP_INCLUDE_DIR=%s\n", BSP_INCLUDE_DIR);

    fflush(stdout);
}

//
// This function is written by BSP when configuring BSP.
// BSP expects to be able to call this function on startup.
//

void
_bsp_preload_init ()
{
    BSP_DO_CGPROF             = 0;
    BSP_DO_PROF               = 0;
    BSP_DO_STAT               = 0;
    BSP_NBUFFERS              = 2;
    BSP_BUFFER_SIZE           = 10240;
    BSP_SLOTSIZE_USECS        = 0;
    BSP_THROTTLE_PROCS        = 0;
    BSP_COMM_FIFO_SIZE        = 100;
    BSP_BUFFER_STALLS         = 2;
    BSP_OPT_CONTENTION_LEVEL  = 1;
    BSP_OPT_FCOMBINE_PUTS     = 20480;
    BSP_OPT_FCOMBINE_PUTS_MAX = 102400;
    BSP_OPT_FCOMBINE_PUTS_MIN = 5120;
    BSP_OPT_BSMP_BUFFER_SIZE  = -1;
    BSP_CHECK_SYNCS           = 1;
    BSP_LIBRARY_TYPE          = 'O';
    BSP_OPT_FLIBRARY_LEVEL    = 2;
 
    BSP_COMPILE_FLAGS  = (char*) malloc(1+strlen(" -O3 -flibrary-level 2 -fcombine-puts-buffer 20480,102400,5120 -fcontention-resolve 1"));
    BSP_ARCH=(char*) malloc(1+strlen("OSF1"));
    BSP_INCLUDE_DIR=(char*) malloc(1+strlen("/usr/people/vince/Parallel/BSP/BSP1.1.2/include/"));
    BSP_EXEC_FILE= (char*)malloc(1+strlen("hedgehog"));

    if (BSP_COMPILE_FLAGS==NULL || BSP_ARCH==NULL || 
        BSP_INCLUDE_DIR==NULL   || BSP_EXEC_FILE==NULL)
        bsp_abort("{bsp_start}: unable to malloc for compile flags");

    BSP_COMPILE_FLAGS=strcpy(BSP_COMPILE_FLAGS, " -O3 -flibrary-level 2 -fcombine-puts-buffer 20480,102400,5120 -fcontention-resolve 1");
    BSP_ARCH         =strcpy(BSP_ARCH,"OSF1");
    BSP_INCLUDE_DIR  =strcpy(BSP_INCLUDE_DIR,"/usr/people/vince/Parallel/BSP/BSP1.1.2/include/");
    BSP_EXEC_FILE    =strcpy(BSP_EXEC_FILE,"hedgehog");
    //
    // This call is not part of the original BSP code.
    // This allows us to override where BSP_INCLUDE_DIR is found.
    //
    get_bsp_include_dir();
}

#endif /*BL_USE_BSP*/

static int nProcs = 1;

static int nBoxs  = 10;

static char* the_prog_name;

static aString PerFab("PerFab");

static aString PerCPU("PerCPU");

//
// How defaults to PerCPU.
//
static aString How(PerCPU);

static
void
usage ()
{
    std::cout << "usage: "
              << the_prog_name
              << " [-how PerFab|PerCPU]"
              << " [-nprocs N]"
              << " [-nboxs N]"
              << std::endl;
    exit(1);
}

static
void
parse_args (char**& argv)
{
    while (*++argv && **argv == '-')
    {
        if (strcmp(*argv, "-nprocs") ==  0)
        {
            if (*++argv)
            {
                nProcs = atoi(*argv);

                if (nProcs <= 0)
                {
                    std::cout << "nprocs must be positive" << std::endl;
                    usage();
                }
            }
            else
            {
                std::cout << "No argument to -nprocs supplied.\n";
                usage();
            }
        }
        else if (strcmp(*argv, "-nboxs") ==  0)
        {
            if (*++argv)
            {
                nBoxs = atoi(*argv);

                if (nBoxs <= 0)
                {
                    std::cout << "nboxs must be positive" << std::endl;
                    usage();
                }
            }
            else
            {
                std::cout << "No argument to -nprocs supplied.\n";
                usage();
            }
        }
        else if (strcmp(*argv, "-how") ==  0)
        {
            if (*++argv)
            {
                How = *argv;

                if (!(How == PerCPU || How == PerFab))
                {
                    std::cout << "Invalid value for -how argument\n";
                    usage();
                }
            }
            else
            {
                std::cout << "No argument to -how supplied.\n";
                usage();
            }
        }
        else
        {
            std::cout << "Exiting, unknown option: " << *argv << std::endl;
            usage();
        }
    }
}

static
void
Write_N_Read (const MultiFab& mf,
              const aString&  mf_name,
              VisMF::How      how)
{
    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "Writing the MultiFab to disk ...\n";
    }

    double start, end;

    ParallelDescriptor::Synchronize();

    if (ParallelDescriptor::IOProcessor())
    {
        start = Utility::wsecond();
    }

    switch (how)
    {
    case VisMF::OneFilePerCPU:
        VisMF::Write(mf, mf_name, VisMF::OneFilePerCPU); break;
    case VisMF::OneFilePerFab:
        VisMF::Write(mf, mf_name, VisMF::OneFilePerFab); break;
    default:
        BoxLib::Error("Bad case in switch");
    }

    ParallelDescriptor::Synchronize();

    if (ParallelDescriptor::IOProcessor())
    {
        end = Utility::wsecond();

        std::cout << "\nWallclock time for MF write: " << (end-start) << '\n';

        std::cout << "Reading the MultiFab from disk ...\n";
    }

    VisMF vmf(mf_name);

    assert(vmf.length() == mf.boxArray().length());

    for (ConstMultiFabIterator mfi(mf); mfi.isValid(); ++mfi)
    {
        const FArrayBox& fab = vmf[mfi.index()];

        std::cout << "\tCPU #"
                  << ParallelDescriptor::MyProc()
                  << " read FAB #"
                  << mfi.index()
                  << '\n';
    }

    ParallelDescriptor::Synchronize();

    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "Building new MultiFab from disk version ....\n\n";
    }

    MultiFab new_mf;
    
    VisMF::Read(new_mf, mf_name);
}

int
main (int, char** argv)
{
    the_prog_name = argv[0];

    parse_args(argv);

    StartParallel(nProcs);

    BoxArray ba(nBoxs);

    ba.set(0, Box(IntVect(D_DECL(0,0,0)), IntVect(D_DECL(2,2,2))));

    for (int i = 1; i < nBoxs; i++)
    {
        ba.set(i,grow(ba[i-1],2));
    }

    MultiFab mf(ba, 2, 1);

    for (ConstMultiFabIterator mfi(mf); mfi.isValid(); ++mfi)
    {
        mf[mfi.index()].setVal(mfi.index()+1);
    }
    //
    // Set cells in ghost region to zero.
    //
    mf.setBndry(0);

    static const aString mf_name = "Spam-n-Eggs";

    Write_N_Read (mf,
                  mf_name,
                  (How==PerCPU) ? VisMF::OneFilePerCPU : VisMF::OneFilePerFab);

    EndParallel();
}
