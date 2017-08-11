#include "SurfaceTensionForce.h"

SurfaceTensionForce::SurfaceTensionForce(const Input &input,
                                         const ImmersedBoundary &ib,
                                         const ScalarFiniteVolumeField &gamma,
                                         const ScalarFiniteVolumeField &rho,
                                         const ScalarFiniteVolumeField &mu,
                                         const VectorFiniteVolumeField &u,
                                         const ScalarGradient &gradGamma)
        :
        VectorFiniteVolumeField(gamma.gridPtr(), "ft", Vector2D(0., 0.), true, false),
        ib_(ib),
        gamma_(gamma),
        rho_(rho),
        mu_(mu),
        u_(u),
        gradGamma_(gradGamma),
        kappa_(std::make_shared<ScalarFiniteVolumeField>(grid_, "kappa")),
        gammaTilde_(std::make_shared<ScalarFiniteVolumeField>(grid_, "gammaTilde")),
        gradGammaTilde_(std::make_shared<ScalarGradient>(*gammaTilde_)),
        n_(std::make_shared<VectorFiniteVolumeField>(grid_, "n"))
{
    //- Input properties
    sigma_ = input.caseInput().get<Scalar>("Properties.sigma");
    kernelWidth_ = input.caseInput().get<Scalar>("Solver.smoothingKernelRadius");

    //- Determine which patches contact angles will be enforced on
    for (const auto &input: input.boundaryInput().get_child("Boundaries." + gamma.name()))
    {
        if (input.first == "*" || !grid_->hasPatch(input.first))
            continue;

        patchContactAngles_.insert(std::make_pair(
                grid_->patch(input.first).id(),
                input.second.get<Scalar>("contactAngle", 90) * M_PI / 180.
        ));
    }

    //- Determine which IBs contact angles will be enforced on

    if (input.boundaryInput().find("ImmersedBoundaries") == input.boundaryInput().not_found())
        return;

    for (const auto &input: input.boundaryInput().get_child("ImmersedBoundaries"))
    {
        ibContactAngles_.insert(std::make_pair(
                ib_.ibObj(input.first).id(),
                input.second.get<Scalar>(gamma.name() + ".contactAngle", 90) * M_PI / 180.
        ));
    }
}

void SurfaceTensionForce::computeInterfaceNormals()
{
    const VectorFiniteVolumeField &gradGammaTilde = *gradGammaTilde_;
    VectorFiniteVolumeField &n = *n_;

    for(const Cell& cell: grid_->localActiveCells())
        n(cell) = gradGammaTilde(cell).magSqr() >= eps_ * eps_ ? -gradGammaTilde(cell).unitVec() : Vector2D(0., 0.);

    //- Boundary faces set from contact line orientation
    for (const Patch &patch: grid_->patches())
    {
        for (const Face &face: patch)
        {
            Scalar theta = getTheta(patch);

            if (n(face.lCell()) == Vector2D(0., 0.))
                n(face) = Vector2D(0., 0.);
            else
            {
                Vector2D t = face.norm().tangentVec().unitVec();
                n(face) = t.rotate(dot(n(face.lCell()), t) > 0. ? M_PI_2 - theta : 3 * M_PI_2 - theta);
            }
        }
    }
}

Vector2D SurfaceTensionForce::contactLineNormal(const Cell &lCell,
                                                const Cell &rCell,
                                                const ImmersedBoundaryObject &ibObj)
{
    LineSegment2D ln = ibObj.intersectionLine(lCell.centroid(), rCell.centroid());
}

void SurfaceTensionForce::smoothGammaField()
{
    smooth(gamma_, grid().localActiveCells(), kernelWidth_, *gammaTilde_);
    grid_->sendMessages(*gammaTilde_);
    gammaTilde_->setBoundaryFaces();
}