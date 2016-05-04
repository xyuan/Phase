#include "Poisson.h"

Poisson::Poisson(const FiniteVolumeGrid2D &grid, const Input &input)
    :
      Solver(grid, input),
      phi(addScalarField(input, "phi")),
      gamma(addScalarField("gamma")),
      phiEqn_(phi, "phi")
{
    gamma.fill(input.caseInput().get<Scalar>("Properties.gamma", 1.));
}

Scalar Poisson::solve(Scalar timeStep, Scalar prevTimeStep)
{
    phiEqn_ = (fv::laplacian(gamma, phi) == 0.);
    phiEqn_.solve();
    return phiEqn_.error();
}
