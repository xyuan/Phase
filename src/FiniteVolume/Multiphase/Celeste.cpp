#include "Celeste.h"

Celeste::Celeste(const Input &input,
                 const std::shared_ptr<const FiniteVolumeGrid2D> &grid,
                 const std::weak_ptr<ImmersedBoundary> &ib,
                 const ScalarFiniteVolumeField &rho,
                 const ScalarFiniteVolumeField &mu,
                 const VectorFiniteVolumeField &u)
        :
        SurfaceTensionForce(input, grid, ib)
{
    constructMatrices();
}

void Celeste::computeFaces(const ScalarFiniteVolumeField &gamma, const ScalarGradient &gradGamma)
{
    computeGradGammaTilde(gamma);
    computeInterfaceNormals();
    computeCurvature();

    auto &ft = *this;
    auto &kappa = *kappa_;

    for (const Face &face: grid_->faces())
        ft(face) = sigma_ * kappa(face) * gradGamma(face);
}

void Celeste::compute(const ScalarFiniteVolumeField &gamma, const ScalarGradient &gradGamma)
{
    computeGradGammaTilde(gamma);
    computeInterfaceNormals();
    computeCurvature();

    auto &ft = *this;
    auto &kappa = *kappa_;

    ft.fill(Vector2D(0., 0.));
    for (const Cell &cell: grid_->cellZone("fluid"))
        ft(cell) = sigma_ * kappa(cell) * gradGamma(cell);

    ft.interpolateFaces();
}

void Celeste::constructMatrices()
{
    kappaStencils_.resize(grid_->cells().size());
    gradGammaTildeStencils_.resize(grid_->cells().size());

    for (const Cell &cell: grid_->localActiveCells())
    {
        kappaStencils_[cell.id()] = CelesteStencil(cell, false);
        gradGammaTildeStencils_[cell.id()] = CelesteStencil(cell, true);
    }
}

//- Protected methods

void Celeste::computeGradGammaTilde(const ScalarFiniteVolumeField &gamma)
{
    smoothGammaField(gamma);

    auto &gammaTilde = *gammaTilde_;
    auto &gradGammaTilde = *gradGammaTilde_;

    gradGammaTilde.fill(Vector2D(0., 0.));
    for (const Cell &cell: gradGammaTilde.grid()->cellZone("fluid"))
        gradGammaTilde(cell) = gradGammaTildeStencils_[cell.id()].grad(gammaTilde);
}

void Celeste::computeCurvature()
{
    auto &n = *n_;
    auto &kappa = *kappa_;

    if(!ib_.lock())
    {
        for (const Cell &cell: grid_->cellZone("fluid"))
            kappa(cell) = kappaStencils_[cell.id()].div(n);
    }
    else
    {
        updateStencils(*ib_.lock());
        kappa_->fill(0.);

        auto &n = *n_;
        auto &kappa = *kappa_;
        const auto &gradGammaTilde = *gradGammaTilde_;

        for (const Cell &cell: grid_->cellZone("fluid"))
            if (gradGammaTilde(cell).magSqr() > 0.)
                kappa(cell) = kappaStencils_[cell.id()].kappa(n, *ib_.lock(), *this);

        grid_->sendMessages(kappa);
        kappa.interpolateFaces();

        for (const Face &face: grid_->interiorFaces())
        {
            if (ib_.lock()->ibObj(face.lCell().centroid()))
            {
                kappa(face) = kappa(face.rCell());
            }
            else if (ib_.lock()->ibObj(face.rCell().centroid()))
            {
                kappa(face) = kappa(face.lCell());
            }
        }
    }

    grid_->sendMessages(kappa);
    kappa.interpolateFaces();
}

void Celeste::updateStencils(const ImmersedBoundary &ib)
{
//    auto updateRequired = [&ib](const CelesteStencil &st) {
//        if (st.truncated())
//            return true;
//
//        for (const InteriorLink &nb: st.cell().neighbours())
//            if (ib.ibObj(nb.cell().centroid()))
//                return true;
//
//        for (const CellLink &dg: st.cell().diagonals())
//            if (ib.ibObj(dg.cell().centroid()))
//                return true;
//
//        return false;
//    };

    for (const Cell &cell: grid_->cellZone("fluid"))
    {
        CelesteStencil &st = kappaStencils_[cell.id()];

        //    if (updateRequired(st))
        st.init(ib);
    }
}