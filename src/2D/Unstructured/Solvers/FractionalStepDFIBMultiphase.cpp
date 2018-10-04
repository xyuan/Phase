#include "Math/Algorithm.h"

#include "FiniteVolume/ImmersedBoundary/DirectForcingImmersedBoundary.h"
#include "FiniteVolume/Multiphase/CelesteImmersedBoundary.h"
#include "FiniteVolume/Discretization/TimeDerivative.h"
#include "FiniteVolume/Discretization/Divergence.h"
#include "FiniteVolume/Discretization/SecondOrderExplicitDivergence.h"
#include "FiniteVolume/Discretization/Laplacian.h"
#include "FiniteVolume/Discretization/Source.h"
#include "FiniteVolume/Discretization/Cicsam.h"

#include "FractionalStepDFIBMultiphase.h"

FractionalStepDirectForcingMultiphase::FractionalStepDirectForcingMultiphase(const Input &input, const std::shared_ptr<const FiniteVolumeGrid2D> &grid)
    :
      FractionalStepDFIB(input, grid),
      gamma_(*addField<Scalar>(input, "gamma", fluid_)),
      rho_(*addField<Scalar>("rho", fluid_)),
      mu_(*addField<Scalar>("mu", fluid_)),
      gammaSrc_(*addField<Scalar>("gammaSrc", fluid_)),
      sg_(*addField<Vector2D>("sg", fluid_)),
      rhoU_(grid_, "rhoU", Vector2D(0., 0.), true, false, fluid_),
      gradGamma_(*std::static_pointer_cast<ScalarGradient>(addField<Vector2D>(std::make_shared<ScalarGradient>(gamma_, fluid_)))),
      gradRho_(*std::static_pointer_cast<ScalarGradient>(addField<Vector2D>(std::make_shared<ScalarGradient>(rho_, fluid_)))),
      fst_(std::make_shared<CelesteImmersedBoundary>(input, grid_, fluid_, ib_)),
      gammaEqn_(input, gamma_, "gammaEqn")
{
    rho1_ = input.caseInput().get<Scalar>("Properties.rho1", FractionalStep::rho_);
    rho2_ = input.caseInput().get<Scalar>("Properties.rho2", FractionalStep::rho_);
    mu1_ = input.caseInput().get<Scalar>("Properties.mu1", FractionalStep::mu_);
    mu2_ = input.caseInput().get<Scalar>("Properties.mu2", FractionalStep::mu_);

    capillaryTimeStep_ = std::numeric_limits<Scalar>::infinity();
    for (const Face &face: grid_->interiorFaces())
    {
        Scalar delta = (face.rCell().centroid() - face.lCell().centroid()).mag();
        capillaryTimeStep_ = std::min(
                    capillaryTimeStep_,
                    std::sqrt(
                        (rho1_ + rho2_) * std::pow(delta, 3) / (4. * M_PI * fst_->sigma())
                        ));
    }

    capillaryTimeStep_ = grid_->comm().min(capillaryTimeStep_);

    addField(fst_->fst());
    addField(fst_->kappa());
    addField(fst_->gammaTilde());
    addField<Vector2D>(fst_->gradGammaTilde());
    addField(fst_->n());
}

void FractionalStepDirectForcingMultiphase::initialize()
{
    FractionalStepDFIB::initialize();

    //- Ensure the computation starts with a valid gamma field
    //- Ensure the computation starts with a valid gamma field
    gradGamma_.compute(*fluid_);
    u_.savePreviousTimeStep(0, 1);
    gamma_.savePreviousTimeStep(0, 1);
    updateProperties(0.);
    updateProperties(0.);
}

Scalar FractionalStepDirectForcingMultiphase::solve(Scalar timeStep)
{
    //- Perform field extension
    //solveExtEqns();

    grid_->comm().printf("Updating IB positions and cell categories...\n");
    ib_->updateIbPositions(timeStep);
    ib_->updateCells();

    grid_->comm().printf("Solving gamma equation...\n");
    solveGammaEqn(timeStep);

    grid_->comm().printf("Updating physical properties...\n");
    updateProperties(timeStep);

    grid_->comm().printf("Solving momentum and pressure equations...\n");
    solveUEqn(timeStep);
    solvePEqn(timeStep);
    correctVelocity(timeStep);

    grid_->comm().printf("Max divergence error = %.4e\n", grid_->comm().max(maxDivergenceError()));
    grid_->comm().printf("Max CFL number = %.4lf\n", maxCourantNumber(timeStep));

    grid_->comm().printf("Computing IB forces...\n");
    fst_->appyFluidForces(rho_, mu_, u_, p_, gamma_, g_, *ib_);
    ib_->applyCollisionForce(true);

    return 0;
}

Scalar FractionalStepDirectForcingMultiphase::solveGammaEqn(Scalar timeStep)
{
    auto beta = cicsam::faceInterpolationWeights(u_, gamma_, gradGamma_, timeStep);

    //- Predictor
    gamma_.savePreviousTimeStep(timeStep, 1);
    gammaEqn_ = (fv::ddt(gamma_, timeStep) + cicsam::div(u_, gamma_, beta, 0.) == 0.);

    Scalar error = gammaEqn_.solve();
    gamma_.sendMessages();

    //- Corrector
//    gamma_.savePreviousIteration();
//    fst_->computeContactLineExtension(gamma_);

//    for(const Cell &c: *fluid_)
//        gammaSrc_(c) = (gamma_(c) - gamma_.prevIteration()(c)) / timeStep;

//    gammaEqn_ == cicsam::div(u_, gamma_, beta, 0.5) - cicsam::div(u_, gamma_, beta, 0.) + src::src(gammaSrc_);

//    gammaEqn_.solve();
//    gamma_.sendMessages();
//    gamma_.interpolateFaces();

    //- Compute exact fluxes used for gamma advection
    rhoU_.savePreviousTimeStep(timeStep, 2);
    cicsam::computeMomentumFlux(rho1_, rho2_, u_, gamma_, beta, rhoU_.oldField(0));
    cicsam::computeMomentumFlux(rho1_, rho2_, u_, gamma_.oldField(0), beta, rhoU_.oldField(1));

    //- Update the gradient
    gradGamma_.compute(*fluid_);
    gradGamma_.sendMessages();

    return error;
}

Scalar FractionalStepDirectForcingMultiphase::solveUEqn(Scalar timeStep)
{
    u_.savePreviousTimeStep(timeStep, 1);
    const auto &fst = *fst_->fst();

    gradP_.faceToCell(rho_, rho_.oldField(0), *fluid_);

    //- Explicit predictor
    uEqn_ = (fv::ddt(rho_, u_, timeStep) + fv::div2e(rhoU_, u_, 0.5)
             == fv::laplacian(mu_, u_, 0.) + src::src(fst + sg_ - gradP_));

    Scalar error = uEqn_.solve();
    u_.sendMessages();

    //- Compute forcing term
//    fbEqn_ = ib_->computeForcingTerm(rho_, u_, timeStep, fb_);
//    fbEqn_.solve();
//    grid_->sendMessages(fb_);

    //- Semi-implicit corrector
    uEqn_ == fv::laplacian(mu_, u_, 0.5) - fv::laplacian(mu_, u_, 0.) + ib_->velocityBcs(rho_, u_, u_, timeStep);
    error = uEqn_.solve();

    for(const Cell& c: *fluid_)
        u_(c) += timeStep / rho_(c) * gradP_(c);

    u_.sendMessages();

    for (const Face &f: grid_->interiorFaces())
    {
        Scalar g = f.volumeWeight();
        const Cell &l = f.lCell();
        const Cell &r = f.rCell();

        if(ib_->ibObj(f.lCell().centroid()) || ib_->ibObj(f.rCell().centroid()))
            u_(f) = g * u_(l) + (1. - g) * u_(r);
        else
            u_(f) = g * (u_(l) - timeStep / rho_(l) * (fst(l) + sg_(l)))
                    + (1. - g) * (u_(r) - timeStep / rho_(r) * (fst(r) + sg_(r)))
                    + timeStep / rho_(f) * (fst(f) + sg_(f));
    }

    for (const FaceGroup &patch: grid_->patches())
        switch (u_.boundaryType(patch))
        {
        case VectorFiniteVolumeField::FIXED:
            break;
        case VectorFiniteVolumeField::NORMAL_GRADIENT:
            for (const Face &f: patch)
            {
                const Cell &l = f.lCell();
                u_(f) = u_(l) - timeStep / rho_(l) * (fst(l) + sg_(l))
                        + timeStep / rho_(f) * (fst(f) + sg_(f));
            }
            break;
        case VectorFiniteVolumeField::SYMMETRY:
            for (const Face &f: patch)
                u_(f) = u_(f.lCell()) - dot(u_(f.lCell()), f.norm()) * f.norm() / f.norm().magSqr();
            break;
        }

    return error;
}

Scalar FractionalStepDirectForcingMultiphase::solvePEqn(Scalar timeStep)
{
    pEqn_ = (fv::laplacian(timeStep / rho_, p_) == src::div(u_));

    Scalar error = pEqn_.solve();
    p_.sendMessages();
    p_.setBoundaryFaces();

    gradP_.computeFaces();
    gradP_.faceToCell(rho_, rho_, *fluid_);
    gradP_.sendMessages();

    return error;
}

void FractionalStepDirectForcingMultiphase::updateProperties(Scalar timeStep)
{
    //- Update density
    rho_.savePreviousTimeStep(timeStep, 1);

    rho_.computeCells([this](const Cell &c) {
        return rho1_ + clamp(gamma_(c), 0., 1.) * (rho2_ - rho1_);
    });

    rho_.computeFaces([this](const Face &f) {
        return rho1_ + clamp(gamma_(f), 0., 1.) * (rho2_ - rho1_);
    });

    //- Update the gravitational source term
    gradRho_.computeFaces();

    for (const Face &face: grid_->faces())
        sg_(face) = dot(g_, -face.centroid()) * gradRho_(face);

    sg_.faceToCell(rho_, rho_, *fluid_);
    sg_.sendMessages();

    //- Update viscosity from kinematic viscosity
    mu_.savePreviousTimeStep(timeStep, 1);

    mu_.computeCells([this](const Cell &c) {
        return rho_(c) / (rho1_ / mu1_ + clamp(gamma_(c), 0., 1.) * (rho2_ / mu2_ - rho1_ / mu1_));
    });

    mu_.computeFaces([this](const Face &f) {
        return rho_(f) / (rho1_ / mu1_ + clamp(gamma_(f), 0., 1.) * (rho2_ / mu2_ - rho1_ / mu1_));
    });

    //- Update the surface tension
    fst_->computeFaceInterfaceForces(gamma_, gradGamma_);
    fst_->fst()->faceToCell(rho_, rho_, *fluid_);
    fst_->fst()->fill(Vector2D(0., 0.), ib_->solidCells());

    //- Must be communicated for proper momentum interpolation
    fst_->fst()->sendMessages();

    //for(const Cell &c: *fluid_)
    //    std::cout << (*fst_->gradGammaTilde())(c) << " " << (*fst_->n())(c) << " " << (*fst_->kappa())(c) << " " << (*fst_->fst())(c) << "\n";
}

void FractionalStepDirectForcingMultiphase::correctVelocity(Scalar timeStep)
{
    for (const Cell &cell: *fluid_)
        u_(cell) -= timeStep / rho_(cell) * gradP_(cell);

    u_.sendMessages();

    for (const Face &face: grid_->faces())
        u_(face) -= timeStep / rho_(face) * gradP_(face);
}
