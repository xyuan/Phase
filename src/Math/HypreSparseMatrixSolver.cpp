#include <numeric>

#include "HypreSparseMatrixSolver.h"

HypreSparseMatrixSolver::HypreSparseMatrixSolver(const Communicator& comm, PreconditionerType preconType)
    :
      comm_(comm),
      preconType_(preconType)
{
    HYPRE_ParCSRBiCGSTABCreate(comm.communicator(), &solver_);

    switch(preconType_)
    {
    case EUCLID:
        HYPRE_EuclidCreate(comm.communicator(), &precon_);
        HYPRE_ParCSRBiCGSTABSetPrecond(solver_,
                                       (HYPRE_PtrToParSolverFcn) HYPRE_EuclidSolve,
                                       (HYPRE_PtrToParSolverFcn) HYPRE_EuclidSetup,
                                       precon_);
        break;
    case BOOMER_AMG:
        HYPRE_BoomerAMGCreate(&precon_);
        HYPRE_ParCSRBiCGSTABSetPrecond(solver_,
                                       (HYPRE_PtrToParSolverFcn) HYPRE_BoomerAMGSolve,
                                       (HYPRE_PtrToParSolverFcn) HYPRE_BoomerAMGSetup,
                                       precon_);
        break;
    }
}

HypreSparseMatrixSolver::~HypreSparseMatrixSolver()
{
    HYPRE_ParCSRBiCGSTABDestroy(solver_);

    switch(preconType_)
    {
    case EUCLID:
        HYPRE_EuclidDestroy(precon_);
        break;
    case BOOMER_AMG:
        HYPRE_BoomerAMGDestroy(precon_);
        break;
    }

    deinitialize();
}

void HypreSparseMatrixSolver::setRank(int rank)
{
    if(rank != iUpper_ - iLower_ + 1) // check if rank has changed
    {
        deinitialize();
        initialize(rank);
    }
}

void HypreSparseMatrixSolver::set(const SparseMatrixSolver::CoefficientList &eqn)
{
    std::vector<int> nCols;
    std::vector<int> rows;
    nCols.reserve(eqn.size());
    rows.reserve(eqn.size());

    Size nEntries = 0;
    int rowNo = iLower_;
    for(const auto& row: eqn)
    {
        nCols.push_back(row.size());
        nEntries += row.size();
        rows.push_back(rowNo++);
    }

    std::vector<int> cols;
    std::vector<Scalar> vals;
    cols.reserve(nEntries);
    vals.reserve(nEntries);

    for(const auto& row: eqn)
        for(const auto& entry: row)
        {
            cols.push_back(entry.first);
            vals.push_back(entry.second);
        }

    HYPRE_IJMatrixInitialize(ijMatrix_);
    HYPRE_IJMatrixSetValues(ijMatrix_, rows.size(), nCols.data(), rows.data(), cols.data(), vals.data());
    HYPRE_IJMatrixAssemble(ijMatrix_);
}

void HypreSparseMatrixSolver::setRhs(const Vector &rhs)
{
    std::vector<int> rows(rhs.size());

    int row = iLower_;
    for(int& val: rows)
        val = row++;

    HYPRE_IJVectorInitialize(b_);
    HYPRE_IJVectorSetValues(b_, rhs.size(), rows.data(), rhs.data());
    HYPRE_IJVectorAssemble(b_);
}

Scalar HypreSparseMatrixSolver::solve()
{
    HYPRE_ParCSRMatrix A;
    HYPRE_ParVector x, b;

    HYPRE_IJVectorInitialize(x_);
    HYPRE_IJVectorAssemble(x_);

    HYPRE_IJMatrixGetObject(ijMatrix_, (void**) &A);
    HYPRE_IJVectorGetObject(b_, (void**) &b);
    HYPRE_IJVectorGetObject(x_, (void**) &x);

    if(nPreconUses_ == maxPreconUses_)
    {
        comm_.printf("Computing preconditioner...\n");
        HYPRE_ParCSRBiCGSTABSetup(solver_, A, b, x);
        nPreconUses_ = 0;
    }
    else
        ++nPreconUses_;

    HYPRE_ParCSRBiCGSTABSolve(solver_, A, b, x);

    HYPRE_ParCSRBiCGSTABGetFinalRelativeResidualNorm(solver_, &toler_);
    HYPRE_ParCSRBiCGSTABGetNumIterations(solver_, &nIters_);

    return toler_;
}

void HypreSparseMatrixSolver::mapSolution(ScalarFiniteVolumeField &field)
{
    std::vector<int> inds(field.grid.localActiveCells().size());
    std::vector<Scalar> vals(field.grid.localActiveCells().size());

    std::transform(field.grid.localActiveCells().begin(), field.grid.localActiveCells().end(),
                   inds.begin(), [](const Cell& cell)->int{ return cell.globalIndex(0); });

    HYPRE_IJVectorGetValues(x_, inds.size(), inds.data(), vals.data());

    auto begin = vals.begin();
    for(const Cell& cell: field.grid.localActiveCells())
        field(cell) = *(begin++);
}

void HypreSparseMatrixSolver::mapSolution(VectorFiniteVolumeField &field)
{
    std::vector<int> inds(2*field.grid.localActiveCells().size());
    std::vector<Scalar> vals(2*field.grid.localActiveCells().size());

    Size nActiveCells = field.grid.localActiveCells().size();

    std::transform(field.grid.localActiveCells().begin(), field.grid.localActiveCells().end(),
                   inds.begin(), [](const Cell& cell)->int{ return cell.globalIndex(1); });

    std::transform(field.grid.localActiveCells().begin(), field.grid.localActiveCells().end(),
                   inds.begin() + nActiveCells,
                   [nActiveCells](const Cell& cell)->int{ return cell.globalIndex(2); });

    HYPRE_IJVectorGetValues(x_, inds.size(), inds.data(), vals.data());

    auto begin = vals.begin();
    for(const Cell& cell: field.grid.localActiveCells())
    {
        field(cell).x = *begin;
        field(cell).y = *(begin + nActiveCells);
        ++begin;
    }
}

void HypreSparseMatrixSolver::setMaxIters(int maxIters)
{
    HYPRE_ParCSRBiCGSTABSetMaxIter(solver_, maxIters);
}

void HypreSparseMatrixSolver::setToler(Scalar toler)
{
    HYPRE_ParCSRBiCGSTABSetTol(solver_, toler);
}

void HypreSparseMatrixSolver::setDropToler(Scalar toler)
{
    switch(preconType_)
    {
    case EUCLID:
        HYPRE_EuclidSetSparseA(precon_, toler);
        break;
    case BOOMER_AMG:
        HYPRE_BoomerAMGSetEuSparseA(precon_, toler);
        break;
    }
}

void HypreSparseMatrixSolver::setFillFactor(int fill)
{
    switch(preconType_)
    {
    case EUCLID:
        HYPRE_EuclidSetLevel(precon_, fill);
        break;
    }
}

void HypreSparseMatrixSolver::printStatus(const std::string &msg) const
{

    comm_.printf("%s iterations = %d, error = %lf.\n", msg.c_str(), nIters(), error());
}

void HypreSparseMatrixSolver::initialize(int rank)
{
    localSizes_ = comm_.allGather(rank);

    iLower_ = 0;
    for(int proc = 0; proc < comm_.rank(); ++proc)
        iLower_ += localSizes_[proc];

    iUpper_ = iLower_ + localSizes_[comm_.rank()] - 1;

    globalInds_.resize(localSizes_[comm_.rank()]);
    std::iota(globalInds_.begin(), globalInds_.end(), iLower_);

    HYPRE_IJMatrixCreate(comm_.communicator(), iLower_, iUpper_, iLower_, iUpper_, &ijMatrix_);
    HYPRE_IJMatrixSetObjectType(ijMatrix_, HYPRE_PARCSR);

    HYPRE_IJVectorCreate(comm_.communicator(), iLower_, iUpper_, &b_);
    HYPRE_IJVectorSetObjectType(b_, HYPRE_PARCSR);

    HYPRE_IJVectorCreate(comm_.communicator(), iLower_, iUpper_, &x_);
    HYPRE_IJVectorSetObjectType(x_, HYPRE_PARCSR);

    initialized_ = true;
}

void HypreSparseMatrixSolver::deinitialize()
{
    if(initialized_)
    {
        HYPRE_IJMatrixDestroy(ijMatrix_);
        HYPRE_IJVectorDestroy(b_);
        HYPRE_IJVectorDestroy(x_);
        initialized_ = false;
    }
}
