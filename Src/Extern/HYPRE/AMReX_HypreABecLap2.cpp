#include <AMReX_HypreABecLap2.H>
#include <AMReX_HypreABec_F.H>

#include <cmath>

namespace {
    static int ispow2(int i)
    {
        return (i == 1) ? 1 : (((i <= 0) || (i & 1)) ? 0 : ispow2(i / 2));
    }
}

namespace amrex
{

HypreABecLap2::HypreABecLap2 (const BoxArray& grids,
                              const DistributionMapping& dmap,
                              const Geometry& geom_,
                              MPI_Comm comm_)
    : comm(comm_),
      geom(geom_),
      verbose(1),
      hgrid(NULL), stencil(NULL), graph(NULL),
      A(NULL), A0(NULL), b(NULL), x(NULL)
{
    const int ncomp = 1;
    const int ngrow = 0;
    acoefs.define(grids, dmap, ncomp, ngrow);
    acoefs.setVal(0.0);

    for (int i = 0; i < BL_SPACEDIM; ++i) {
        BoxArray edge_boxes(grids);
        edge_boxes.surroundingNodes(i);
        bcoefs[i].define(edge_boxes, dmap, ncomp, ngrow);        
        bcoefs[i].setVal(0.0);
    }

    bd.define(grids, dmap, ncomp, geom);

    int num_procs, myid;
    MPI_Comm_size(comm, &num_procs );
    MPI_Comm_rank(comm, &myid );
 
    HYPRE_SStructGridCreate(comm, BL_SPACEDIM, 1, &hgrid);

    for (int i = 0; i < BL_SPACEDIM; i++) {
        is_periodic[i] = 0;
        if (geom.isPeriodic(i)) {
            is_periodic[i] = geom.period(i);
            AMREX_ASSERT(ispow2(is_periodic[i]));
            AMREX_ASSERT(geom.Domain().smallEnd(i) == 0);
        }
    }
    if (geom.isAnyPeriodic()) {
        HYPRE_SStructGridSetPeriodic(hgrid, 0, is_periodic);
    }

    for (int i = 0; i < grids.size(); i++) {
        if (dmap[i] == myid) {
            const Box& bx = grids[i];
            HYPRE_SStructGridSetExtents(hgrid, 0,
                                        const_cast<int*>(bx.loVect()),
                                        const_cast<int*>(bx.hiVect()));
        }
    }

    // All variables are cell-centered
    HYPRE_SStructVariable vars[1] = {HYPRE_SSTRUCT_VARIABLE_CELL};
    HYPRE_SStructGridSetVariables(hgrid, 0, 1, vars);

    HYPRE_SStructGridAssemble(hgrid);

    // Setup stencils
    static_assert(BL_SPACEDIM > 1, "HypreABecLap2: 1D not supported");
#if (BL_SPACEDIM == 2)
    int offsets[5][2] = {{ 0,  0},
                         {-1,  0},
                         { 1,  0},
                         { 0, -1},
                         { 0,  1}};
#elif (BL_SPACEDIM == 3)
    int offsets[7][3] = {{ 0,  0,  0},
                         {-1,  0,  0},
                         { 1,  0,  0},
                         { 0, -1,  0},
                         { 0,  1,  0},
                         { 0,  0, -1},
                         { 0,  0,  1}};
#endif

    HYPRE_SStructStencilCreate(BL_SPACEDIM, 2 * BL_SPACEDIM + 1, &stencil);

    for (int i = 0; i < 2 * BL_SPACEDIM + 1; i++) {
        HYPRE_SStructStencilSetEntry(stencil, i, offsets[i], 0);
    }

    HYPRE_SStructGraphCreate(comm, hgrid, &graph);
    HYPRE_SStructGraphSetObjectType(graph, HYPRE_PARCSR);

    HYPRE_SStructGraphSetStencil(graph, 0, 0, stencil);

    HYPRE_SStructGraphAssemble(graph);

    HYPRE_SStructMatrixCreate(comm, graph, &A);
    HYPRE_SStructMatrixSetObjectType(A, HYPRE_PARCSR);
    HYPRE_SStructMatrixInitialize(A);

    HYPRE_SStructMatrixCreate(comm, graph, &A0);
    HYPRE_SStructMatrixSetObjectType(A0, HYPRE_PARCSR);
    HYPRE_SStructMatrixInitialize(A0);

    HYPRE_SStructVectorCreate(comm, hgrid, &b);
    HYPRE_SStructVectorSetObjectType(b, HYPRE_PARCSR);
    
    HYPRE_SStructVectorCreate(comm, hgrid, &x);
    HYPRE_SStructVectorSetObjectType(x, HYPRE_PARCSR);

    HYPRE_SStructVectorInitialize(b);
    HYPRE_SStructVectorInitialize(x);
}

HypreABecLap2::~HypreABecLap2 ()
{
    HYPRE_SStructVectorDestroy(b);
    HYPRE_SStructVectorDestroy(x);
    
    HYPRE_SStructMatrixDestroy(A);
    HYPRE_SStructMatrixDestroy(A0);
    
    HYPRE_SStructGraphDestroy(graph);
    HYPRE_SStructStencilDestroy(stencil);
    HYPRE_SStructGridDestroy(hgrid);
}

void
HypreABecLap2::setScalars (Real sa, Real sb)
{
    scalar_a = sa;
    scalar_b = sb;
}

void
HypreABecLap2::setACoeffs (const MultiFab& alpha)
{
    MultiFab::Copy(acoefs, alpha, 0, 0, 1, 0);
}

void
HypreABecLap2::setBCoeffs (const Array<const MultiFab*,BL_SPACEDIM>& beta)
{
    for (int idim=0; idim<BL_SPACEDIM; idim++) {
        MultiFab::Copy(bcoefs[idim], *beta[idim], 0, 0, 1, 0);
    }
}

void
HypreABecLap2::setVerbose (int _verbose)
{
    verbose = _verbose;
}

void
HypreABecLap2::solve (MultiFab& soln, const MultiFab& rhs, Real rel_tol_, Real abs_tol_, 
                      int max_iter_, const BndryData& _bndry)
{
    bd = _bndry;

    loadMatrix();
    finalizeMatrix();

    loadVectors(soln, rhs);
    finalizeVectors();

    setupSolver(rel_tol_, abs_tol_, max_iter_);
    solveDoIt();
    getSolution(soln);
    clearSolver();
}

void
HypreABecLap2::loadMatrix ()
{
    static_assert(BL_SPACEDIM > 1, "HypreABecLap2: 1D not supported");

    const int size = 2 * BL_SPACEDIM + 1;

    int stencil_indices[size];    
    for (int i = 0; i < size; i++) {
        stencil_indices[i] = i;
    }
    
    const int part = 0;
    const Real* dx = geom.CellSize();

    for (MFIter mfi(acoefs); mfi.isValid(); ++mfi)
    {
        int i = mfi.index();
        const Box &reg = mfi.validbox();

        int volume = reg.numPts();
        Real* mat = hypre_CTAlloc(double, size*volume);

        // build matrix interior
        amrex_hpacoef(BL_TO_FORTRAN_BOX(reg),
                      mat,
                      BL_TO_FORTRAN_ANYD(acoefs[mfi]),
                      &scalar_a);
         
        for (int idim = 0; idim < BL_SPACEDIM; idim++) {
            amrex_hpbcoef(BL_TO_FORTRAN_BOX(reg),
                          mat,
                          BL_TO_FORTRAN_ANYD(bcoefs[idim][mfi]),
                          &scalar_b, dx, &idim);
        }

        // add b.c.'s to matrix diagonal, and
        // zero out offdiag values at domain boundaries

        const Vector< Vector<BoundCond> > & bcs_i = bd.bndryConds(i);
        const BndryData::RealTuple      & bcl_i = bd.bndryLocs(i);
        
        for (OrientationIter oit; oit; oit++) {
            Orientation ori = oit();
            int cdir(ori);
            int idim = ori.coordDir();
            const int bctype = bcs_i[cdir][0];
            const Real &bcl  = bcl_i[cdir];
            const Mask &msk  = bd.bndryMasks(ori)[mfi];

            amrex_hpmat(BL_TO_FORTRAN_BOX(reg),
                        mat,
                        BL_TO_FORTRAN_ANYD(bcoefs[idim][mfi]),
                        BL_TO_FORTRAN_ANYD(msk),
                        &scalar_b, dx, &cdir, &bctype, &bcl);
        }

        // initialize matrix
        HYPRE_SStructMatrixSetBoxValues(A, part,
                                        const_cast<int*>(reg.loVect()), 
                                        const_cast<int*>(reg.hiVect()),
                                        0, size, stencil_indices, mat);

        hypre_TFree(mat);
    }
}

void
HypreABecLap2::finalizeMatrix ()
{
    HYPRE_SStructMatrixAssemble(A);   
}

void
HypreABecLap2::loadVectors (MultiFab& soln, const MultiFab& rhs)
{
    const int part = 0;
    const int bho = 0;
    const Real* dx = geom.CellSize();

    FArrayBox fnew;

    soln.setVal(0.0);

    for (MFIter mfi(soln); mfi.isValid(); ++mfi)
    {
        int i = mfi.index();
        const Box &reg = mfi.validbox();

        // initialize soln, since we will reuse the space to set up rhs below:

        FArrayBox *f;
        if (soln.nGrow() == 0) { // need a temporary if soln is the wrong size
            f = &soln[mfi];
        }
        else {
            f = &fnew;
            f->resize(reg);
            f->copy(soln[mfi], 0, 0, 1);
        }
        Real* vec = f->dataPtr(); // sharing space, soln will be overwritten below

        HYPRE_SStructVectorSetBoxValues(x, part,
                                        const_cast<int*>(reg.loVect()),
                                        const_cast<int*>(reg.hiVect()),
                                        0, vec);

        f->copy(rhs[mfi], 0, 0, 1);

        HYPRE_SStructVectorSetBoxValues(b, part,
                                        const_cast<int*>(reg.loVect()),
                                        const_cast<int*>(reg.hiVect()),
                                        0, vec);
    }
}

void
HypreABecLap2::finalizeVectors ()
{
    HYPRE_SStructVectorAssemble(b);
    HYPRE_SStructVectorAssemble(x);
}

void
HypreABecLap2::setupSolver (Real rel_tol_, Real abs_tol_, int max_iter_)
{
    rel_tol = rel_tol_;
    abs_tol = abs_tol_;
    max_iter = max_iter_;

    HYPRE_ParCSRMatrix par_A;
    HYPRE_ParVector par_b;
    HYPRE_ParVector par_x;

    HYPRE_SStructMatrixGetObject(A, (void**) &par_A);
    HYPRE_SStructVectorGetObject(b, (void**) &par_b);
    HYPRE_SStructVectorGetObject(x, (void**) &par_x);

    HYPRE_BoomerAMGCreate(&solver);

    HYPRE_BoomerAMGSetMinIter(solver, 1);
    HYPRE_BoomerAMGSetMaxIter(solver, max_iter);
    HYPRE_BoomerAMGSetTol(solver, rel_tol);

    HYPRE_BoomerAMGSetup(solver, par_A, par_b, par_x);
}

void
HypreABecLap2::clearSolver ()
{
    HYPRE_BoomerAMGDestroy(solver);
}

void
HypreABecLap2::solveDoIt ()
{
    if (abs_tol > 0.0)
    {
        Real bnorm;
        hypre_SStructInnerProd((hypre_SStructVector *) b,
                               (hypre_SStructVector *) b,
                               &bnorm);
        bnorm = std::sqrt(bnorm);

        const BoxArray& grids = acoefs.boxArray();
        Real volume = grids.numPts();
        Real rel_tol_new = bnorm > 0.0 ? abs_tol / bnorm * std::sqrt(volume) : rel_tol;

        if (rel_tol_new > rel_tol) {
            HYPRE_BoomerAMGSetTol(solver, rel_tol_new);
        }
    }

    HYPRE_ParCSRMatrix par_A;
    HYPRE_ParVector par_b;
    HYPRE_ParVector par_x;

    HYPRE_SStructMatrixGetObject(A, (void**) &par_A);
    HYPRE_SStructVectorGetObject(b, (void**) &par_b);
    HYPRE_SStructVectorGetObject(x, (void**) &par_x);

    HYPRE_BoomerAMGSolve(solver, par_A, par_b, par_x);

    HYPRE_SStructVectorGather(x);

    if (verbose >= 2 && ParallelDescriptor::IOProcessor()) {
        int num_iterations;
        Real res;
        HYPRE_BoomerAMGGetNumIterations(solver, &num_iterations);
        HYPRE_BoomerAMGGetFinalRelativeResidualNorm(solver, &res);
        
        int oldprec = std::cout.precision(17);
        std::cout <<"\n" <<  num_iterations
                  << " Hypre Multigrid Iterations, Relative Residual "
                  << res << std::endl;
        std::cout.precision(oldprec);
    }
}

void
HypreABecLap2::getSolution (MultiFab& soln)
{
    const int part = 0;

    FArrayBox fnew;
    for (MFIter mfi(soln); mfi.isValid(); ++mfi)
    {
        const Box &reg = mfi.validbox();

        FArrayBox *f;
        if (soln.nGrow() == 0) { // need a temporary if soln is the wrong size
            f = &soln[mfi];
        }
        else {
            f = &fnew;
            f->resize(reg);
        }

        HYPRE_SStructVectorGetBoxValues(x, part,
                                        const_cast<int*>(reg.loVect()),
                                        const_cast<int*>(reg.hiVect()),
                                        0, f->dataPtr());

        if (soln.nGrow() != 0) {
            soln[mfi].copy(*f, 0, 0, 1);
        }
    }
}

}
