#include "ImmersedBoundary.h"
#include "Cicsam.h"
#include "GhostCellImmersedBoundary.h"

ImmersedBoundary::ImmersedBoundary(const FiniteVolumeGrid2D &grid, const Input &input)
    :
      Multiphase(grid, input),
      ibObj_(grid, surfaceTensionForce_),
      cellStatus_(addScalarField("cell_status"))
{
    for(const auto& ibObjects: input.boundaryInput().get_child("ImmersedBoundaries"))
    {
        printf("Initializing immersed boundary object \"%s\".\n", ibObjects.first.c_str());

        for(const auto& child: ibObjects.second)
        {
            if(child.first == "geometry")
            {
                ibObj_.init(
                            Vector2D(child.second.get<std::string>("center")),
                            child.second.get<Scalar>("radius")
                            );
                continue;
            }

            std::string type = child.second.get<std::string>("type");
            ImmersedBoundaryObject::BoundaryType boundaryType;

            if(type == "fixed")
                boundaryType = ImmersedBoundaryObject::FIXED;
            else if(type == "normal_gradient")
                boundaryType = ImmersedBoundaryObject::NORMAL_GRADIENT;
            else if(type == "contact_angle")
                boundaryType = ImmersedBoundaryObject::CONTACT_ANGLE;
            else if(type == "partial_slip")
                boundaryType = ImmersedBoundaryObject::PARTIAL_SLIP;
            else
                throw Exception("ImmersedBoundary", "ImmersedBoundary", "unrecognized boundary type \"" + type + "\".");

            printf("Setting boundary type \"%s\" for field \"%s\".\n", type.c_str(), child.first.c_str());

            ibObj_.addBoundaryType(child.first, boundaryType);

            if(child.first == "p")
                ibObj_.addBoundaryType("pCorr", boundaryType);
        }
    }

    // Only multiphase methods supported at the moment
    interfaceAdvectionMethod_ = CICSAM;
    surfaceTensionForce_ = std::unique_ptr<SurfaceTensionForce>(new ContinuumSurfaceForce(input, gamma, rho));

    ibObj_.constructStencils();
    setCellStatus();
}

Scalar ImmersedBoundary::solve(Scalar timeStep, Scalar prevTimeStep)
{
    computeRho();
    computeMu();

    u.save(timeStep, 1);

    Scalar avgError = 0.;
    for(size_t innerIter = 0; innerIter < nInnerIterations_; ++innerIter)
    {
        avgError += solveUEqn(timeStep, prevTimeStep);

        for(size_t pCorrIter = 0; pCorrIter < nPCorrections_; ++pCorrIter)
        {
            avgError += solvePCorrEqn();
            correctPressure();
            correctVelocity();
        }
    }

    solveGammaEqn(timeStep, prevTimeStep);

    printf("time step = %lf, max Co = %lf\n", timeStep, courantNumber(timeStep));

    return 0.;
}

Scalar ImmersedBoundary::solveUEqn(Scalar timeStep, Scalar prevTimeStep)
{
    ft = surfaceTensionForce_->compute();

    uEqn_ = (fv::ddt(rho, u, timeStep) + fv::div(rho*u, u) + gc::ib(ibObj_, u)
             == fv::laplacian(mu, u) - fv::grad(p) + fv::source(ft));

    uEqn_.relax(momentumOmega_);

    Scalar error = uEqn_.solve();

    rhieChowInterpolation();
    return error;
}

Scalar ImmersedBoundary::solvePCorrEqn()
{
    pCorrEqn_ = (fv::laplacian(d, pCorr) + gc::ib(ibObj_, pCorr) == m);

    Scalar error = pCorrEqn_.solve();

    interpolateFaces(pCorr);

    return error;
}

Scalar ImmersedBoundary::solveGammaEqn(Scalar timeStep, Scalar prevTimeStep)
{
    gamma.save(timeStep, 1);
    interpolateFaces(gamma);
    gammaEqn_ = (fv::ddt(gamma, timeStep) + cicsam::div(u, gamma, timeStep) + gc::ib(ibObj_, gamma) == 0.);

    Scalar error = gammaEqn_.solve();

    if(isnan(error))
        throw Exception("ImmersedBoundary", "solveGammaEqn", "a nan value was detected.");

    return error;
}

//- Protected

void ImmersedBoundary::setCellStatus()
{
    for(const Cell &cell: grid_.inactiveCells())
        cellStatus_[cell.id()] = SOLID;

    for(const Cell &cell: grid_.fluidCells())
        cellStatus_[cell.id()] = FLUID;

    for(const Cell &cell: grid_.cellGroup("ibCells"))
        cellStatus_[cell.id()] = IB;
}
