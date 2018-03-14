#ifndef QUADRATIC_IBM_H
#define QUADRATIC_IBM_H

#include "ImmersedBoundary.h"

namespace qibm
{

    Equation<Vector2D> div(const VectorFiniteVolumeField &phi,
                           VectorFiniteVolumeField &u,
                           const ImmersedBoundary &ib,
                           Scalar theta = 1.);

    Equation<Vector2D> laplacian(Scalar mu,
                                 VectorFiniteVolumeField &u,
                                 const ImmersedBoundary &ib,
                                 Scalar theta = 1.);

    Equation<Vector2D> laplacian(const ScalarFiniteVolumeField &mu,
                                 VectorFiniteVolumeField &u,
                                 const ImmersedBoundary &ib,
                                 Scalar theta = 1.);

    void interpolateFaces(VectorFiniteVolumeField& u, const ImmersedBoundary& ib);
}

#endif