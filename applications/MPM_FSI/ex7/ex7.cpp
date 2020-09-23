
#include "FemusInit.hpp"
#include "MultiLevelProblem.hpp"
#include "VTKWriter.hpp"
#include "TransientSystem.hpp"
#include "NonLinearImplicitSystem.hpp"
#include "Marker.hpp"
#include "Line.hpp"

#include "Fluid.hpp"
#include "Solid.hpp"
#include "Parameter.hpp"

#include "NumericVector.hpp"
#include "adept.h"

using namespace femus;
Line *bulk;
Line *lineI;
void BuildFlag ( MultiLevelSolution & mlSol );
double eps;

#include "../../Nitsche/support/particleInit.hpp"
#include "../../Nitsche/support/sharedFunctions.hpp"
#include "../include/mpmFsi6b.hpp"
using namespace femus;

double SetVariableTimeStep ( const double time ) {
    double dt = 0.005 /*0.008 */ ;
    return dt;
    }



bool SetBoundaryCondition ( const std::vector < double >&x, const char name[], double &value,const int facename, const double t ) {
    bool test = 1;      //dirichlet
    value = 0.;

    const double Ubar = 1.0;    // guess?
    const double L = 0.41;
    const double H = 2.5;

    if ( !strcmp ( name, "DX" ) ) {
        if ( 1 == facename || 2 == facename ) {
            test = 0;
            value = 0.;
            }
        }

    else if ( !strcmp ( name, "DY" ) ) {
        if ( 3 == facename ) { //fluid wall
            test = 0;
            value = 0.;
            }
        }

    else if ( !strcmp ( name, "VX" ) ) {
        if ( 2 == facename ) {  //outflow
            test = 0;
            value = 0.;
            }
        }

    else if ( !strcmp ( name, "VY" ) ) {
        if ( 1 == facename ) {  //inflow
            test = 1;
            if ( t < 2.0 ) {
                value = 1.5 * Ubar * 4.0 / 0.1681 * x[0] * ( L - x[0] ) * 0.5 * ( 1. - cos ( 0.5 * M_PI * t ) );
                }
            else {
                value = 1.5 * Ubar * 4.0 / 0.1681 * x[0] * ( L - x[0] );
                }
            }
        else if ( 2 == facename ) { //outflow
            test = 0;
            value = 0.;
            }
        }

    else if ( !strcmp ( name, "P" ) ) {
        test = 0;
        value = 0;
        }
    else if ( !strcmp ( name, "M" ) ) {
        if ( 1 == facename ) {
            test = 0;
            value = 0;
            }
        }

    return test;

    }


int main ( int argc, char **args ) {

    // init Petsc-MPI communicator
    FemusInit mpinit ( argc, args, MPI_COMM_WORLD );

    MultiLevelMesh mlMsh;
    double scalingFactor = 10000.;
    unsigned numberOfUniformLevels = 5; //for refinement in 3D
    //unsigned numberOfUniformLevels = 1;
    unsigned numberOfSelectiveLevels = 0;

    double Lref = 1.;
    double Uref = 1.;
    double rhos = 7850;
    double rhof = 1000;
    double nu = 0.3;
    double E = 2.e05;
    double muf = 1.0e-3;

    beta = 0.3;
    Gamma = 0.5;


    Parameter par ( Lref, Uref );

    // Generate Solid Object
    Solid solid ( par, E, nu, rhos, "Neo-Hookean" );
    Fluid fluid ( par, muf, rhof, "Newtonian" );

    mlMsh.ReadCoarseMesh ( "../input/turek2D.neu", "fifth", scalingFactor );
    mlMsh.RefineMesh ( numberOfUniformLevels + numberOfSelectiveLevels,
                       numberOfUniformLevels, NULL );

    mlMsh.EraseCoarseLevels ( numberOfUniformLevels - 1 );
    numberOfUniformLevels = 1;

    unsigned dim = mlMsh.GetDimension();

    FEOrder femOrder = FIRST;

    MultiLevelSolution mlSol ( &mlMsh );
    // add variables to mlSol
    mlSol.AddSolution ( "DX", LAGRANGE, femOrder, 2 );
    if ( dim > 1 )
        mlSol.AddSolution ( "DY", LAGRANGE, femOrder, 2 );
    if ( dim > 2 )
        mlSol.AddSolution ( "DZ", LAGRANGE, femOrder, 2 );

    mlSol.AddSolution ( "VX", LAGRANGE, femOrder, 2 );
    if ( dim > 1 )
        mlSol.AddSolution ( "VY", LAGRANGE, femOrder, 2 );
    if ( dim > 2 )
        mlSol.AddSolution ( "VZ", LAGRANGE, femOrder, 2 );

    //mlSol.AddSolution ("P", DISCONTINUOUS_POLYNOMIAL, ZERO, 2);
    //mlSol.AddSolution ("P", DISCONTINUOUS_POLYNOMIAL, FIRST, 2);
    mlSol.AddSolution ( "P", LAGRANGE, FIRST, 2 );

    mlSol.AddSolution ( "eflag", DISCONTINUOUS_POLYNOMIAL, ZERO, 0, false );
    mlSol.AddSolution ( "nflag", LAGRANGE, femOrder, 0, false );

    mlSol.SetIfFSI ( true );

    mlSol.Initialize ( "All" );

    mlSol.AttachSetBoundaryConditionFunction ( SetBoundaryCondition );
    //mlSol.AttachSetBoundaryConditionFunction(SetBoundaryCondition1);

    // ******* Set boundary conditions *******
    mlSol.GenerateBdc ( "DX", "Steady" );
    if ( dim > 1 )
        mlSol.GenerateBdc ( "DY", "Steady" );
    if ( dim > 2 )
        mlSol.GenerateBdc ( "DZ", "Steady" );
    mlSol.GenerateBdc ( "VX", "Time_dependent" );
    if ( dim > 1 )
        mlSol.GenerateBdc ( "VY", "Steady" );
    if ( dim > 2 )
        mlSol.GenerateBdc ( "VZ", "Steady" );
    mlSol.GenerateBdc ( "P", "Steady" );

    MultiLevelProblem ml_prob ( &mlSol );

    ml_prob.parameters.set < Solid > ( "SolidMPM" ) = solid;
    ml_prob.parameters.set < Solid > ( "SolidFEM" ) = solid;
    ml_prob.parameters.set < Fluid > ( "FluidFEM" ) = fluid;

    // ******* Add MPM system to the MultiLevel problem *******
    TransientNonlinearImplicitSystem & system =
        ml_prob.add_system < TransientNonlinearImplicitSystem > ( "MPM_FSI" );
    system.AddSolutionToSystemPDE ( "DX" );
    if ( dim > 1 )
        system.AddSolutionToSystemPDE ( "DY" );
    if ( dim > 2 )
        system.AddSolutionToSystemPDE ( "DZ" );
    system.AddSolutionToSystemPDE ( "VX" );
    if ( dim > 1 )
        system.AddSolutionToSystemPDE ( "VY" );
    if ( dim > 2 )
        system.AddSolutionToSystemPDE ( "VZ" );
    system.AddSolutionToSystemPDE ( "P" );

    system.SetSparsityPatternMinimumSize ( 1000 );
//   if(dim > 1) system.SetSparsityPatternMinimumSize (500, "VY");
//   if(dim > 2) system.SetSparsityPatternMinimumSize (500, "VZ");
//

    // ******* System MPM-FSI Assembly *******
    system.SetAssembleFunction ( AssembleMPMSys );
    //system.SetAssembleFunction (AssembleMPMSysOld);
    //system.SetAssembleFunction(AssembleFEM);
    // ******* set MG-Solver *******
    system.SetMgType ( V_CYCLE );

    system.SetAbsoluteLinearConvergenceTolerance ( 1.0e-10 );
    system.SetMaxNumberOfLinearIterations ( 1 );
    system.SetNonLinearConvergenceTolerance ( 1.e-9 );
    system.SetMaxNumberOfNonLinearIterations ( 2 );

    system.SetNumberPreSmoothingStep ( 1 );
    system.SetNumberPostSmoothingStep ( 1 );

    // ******* Set Preconditioner *******
    system.SetLinearEquationSolverType ( FEMuS_DEFAULT );

    system.init();

    // ******* Set Smoother *******
    system.SetSolverFineGrids ( GMRES );

    system.SetPreconditionerFineGrids ( ILU_PRECOND );

    system.SetTolerances ( 1.e-10, 1.e-15, 1.e+50, 40, 40 );



    // ******* Add MPM system to the MultiLevel problem *******
    NonLinearImplicitSystem & system2 =
        ml_prob.add_system < NonLinearImplicitSystem > ( "DISP" );
    system2.AddSolutionToSystemPDE ( "DX" );
    if ( dim > 1 )
        system2.AddSolutionToSystemPDE ( "DY" );
    if ( dim > 2 )
        system2.AddSolutionToSystemPDE ( "DZ" );

    // ******* System MPM Assembly *******
//  system2.SetAssembleFunction(AssembleSolidDisp);
    //system2.SetAssembleFunction(AssembleFEM);
    // ******* set MG-Solver *******
    system2.SetMgType ( V_CYCLE );


    system2.SetAbsoluteLinearConvergenceTolerance ( 1.0e-10 );
    system2.SetMaxNumberOfLinearIterations ( 1 );
    system2.SetNonLinearConvergenceTolerance ( 1.e-9 );
    system2.SetMaxNumberOfNonLinearIterations ( 1 );

    system2.SetNumberPreSmoothingStep ( 1 );
    system2.SetNumberPostSmoothingStep ( 1 );

    // ******* Set Preconditioner *******
    system2.SetLinearEquationSolverType ( FEMuS_DEFAULT );

    system2.init();

    // ******* Set Smoother *******
    system2.SetSolverFineGrids ( GMRES );

    system2.SetPreconditionerFineGrids ( ILU_PRECOND );

    system2.SetTolerances ( 1.e-10, 1.e-15, 1.e+50, 2, 2 );


    std::vector < std::vector < double >>xp;
    std::vector < double >wp;
    std::vector < double >dist;
    std::vector < double >nSlaves;
    std::vector < MarkerType > markerTypeBulk;


    //turek parameters
    double lbeam = 0.35;
    double hbeam = 0.02;
    double rcircle = 0.05;
    double xcircle = 0.2;
    double ycircle = 0.2;


    //double alphaC = sqrt ( rcircle * rcircle - ( hbeam / 2. ) * ( hbeam / 2. ) );
    double Hs = lbeam + rcircle;                // length of the particle beam, y-direction, delete the section inside the circle later on
    double Ls = hbeam;                          // width of the particle beam, x-direction
    double Lf = 4. * Ls;

    std::vector < double > xcc = { xcircle - 0.5 * hbeam, ycircle};

    double dL = Hs / 60;

    unsigned nbl = 1;       // odd number
    double DB = 0.5 * dL;
    eps = DB;

//   InitRectangleParticle(2, Ls, Hs, Lf, dL, DB, nbl, xcc, markerTypeBulk, xp, wp, dist, nSlaves);
//
//   bulk = new Line(xp, wp, dist, nSlaves, markerTypeBulk, mlSol.GetLevel(numberOfUniformLevels - 1), 2);

    InitRectangleParticle ( 2, Ls, Hs, Lf, dL, DB, nbl, xcc, markerTypeBulk, xp, wp, dist );

    //delete particles inside the circle
    unsigned cnt = 0;
    std::vector < std::vector < double >> XP;
    XP.reserve ( xp.size() );
    std::vector<double> WP;
    WP.reserve ( WP.size() );
    std::vector<double> Dist;
    Dist.reserve ( dist.size() );
    std::vector < MarkerType > markerTypeBulk2;

    for ( unsigned i = 0; i < xp.size(); i++ ) {
        double d = sqrt ( ( xp[i][0] - xcircle ) * ( xp[i][0] - xcircle ) + ( xp[i][1] - ycircle ) * ( xp[i][1] - ycircle ) );
        if ( d > rcircle ) {
            XP.resize ( cnt + 1 );
            XP[cnt] = xp[i];
            WP.resize ( cnt + 1 );
            WP[cnt] = wp[i];
            Dist.resize ( cnt + 1 );
            Dist[cnt] = dist[i];
            markerTypeBulk2.assign ( cnt, VOLUME );

            cnt++;
            //xp.erase(xp.begin() + i); // this is the proper way but did not work.
            }
        }

    std::cout << "Total points  "<< xp.size() <<" cnt = " << xp.size() - cnt << " points deleted" << std::endl;
    for ( unsigned i = 0; i < XP.size(); i++ ) {
        for ( unsigned j = 0; j < XP[0].size(); j++ ) {
            std::cout << XP[i][j] << " " ;
        }
        std::cout << std::endl;
    }




    bulk = new Line ( XP, WP, Dist, markerTypeBulk2, mlSol.GetLevel ( numberOfUniformLevels - 1 ), 2 );


    std::cout << "AAAAAAAAAAAAAAAAAA\n" << std::flush;

    std::vector < std::vector < std::vector < double >>>bulkPoints ( 1 );
    bulk->GetLine ( bulkPoints[0] );
    PrintLine ( DEFAULT_OUTPUTDIR, "bulk", bulkPoints, 0 );


    //interface markers
    //unsigned FI = 1;
    std::vector < std::vector < std::vector < double >>> T;

    InitRectangleInterface ( 2, Ls, Hs, DB, nbl, 1, xcc, markerTypeBulk, xp, T );

    //delete particles inside the circle
    cnt = 0;
    XP.clear();
    XP.reserve ( xp.size() );
    std::vector < std::vector < std::vector < double >>> T2;
    T2.reserve ( T.size() );
    std::vector < MarkerType > markerTypeInt2;

    for ( unsigned i = 0; i < xp.size(); i++ ) {
        double d = sqrt ( ( xp[i][0] - xcircle ) * ( xp[i][0] - xcircle ) + ( xp[i][1] - ycircle ) * ( xp[i][1] - ycircle ) );
        if ( d > rcircle ) {
            XP.resize ( cnt + 1 );
            XP[cnt] = xp[i];
            //xp.erase(xp.begin() + i); // this is the proper way but did not work.
            T2.resize ( cnt + 1 );
            T2[cnt].resize ( 1 );
            T2[cnt][0].resize ( 2 );
            T2[cnt][0][0] = T[i][0][0];
            T2[cnt][0][1] = T[i][0][1];
            //std::cout << T2[cnt][0][0] << " " << T2[cnt][0][1] << std::endl;
            markerTypeInt2.assign ( cnt, INTERFACE );
            cnt++;
            }
        }

//     std::cout << "Total points  "<< xp.size() <<" cnt = " << xp.size() - cnt << " points deleted" << std::endl;
//     for ( unsigned i = 0; i < XP.size(); i++ ) {
//         for ( unsigned j = 0; j < XP[0].size(); j++ ) {
//             std::cout << XP[i][j] << " " ;
//             }
//         std::cout << std::endl;
//         }


    unsigned solType1 = 2;
    lineI = new Line ( XP, T2, markerTypeInt2, mlSol.GetLevel ( numberOfUniformLevels - 1 ), solType1 );

    std::vector < std::vector < std::vector < double >>>lineIPoints ( 1 );
    lineI->GetLine ( lineIPoints[0] );
    PrintLine ( DEFAULT_OUTPUTDIR, "interfaceMarkers", lineIPoints, 0 );
    //END interface markers


    lineI->GetParticlesToGridMaterial ( false ); // breaking here...
    //std::cout << "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB" << std::endl;
    bulk->GetParticlesToGridMaterial ( false );



    BuildFlag ( mlSol );


    //GetParticleWeights(mlSol, bulk);
    //GetInterfaceElementEigenvalues(mlSol, bulk, lineI, eps);

    // ******* Print solution *******
    mlSol.SetWriter ( VTK );

    std::vector < std::string > mov_vars;
    mov_vars.push_back ( "DX" );
    mov_vars.push_back ( "DY" );
    //mov_vars.push_back("DZ");
    mlSol.GetWriter()->SetMovingMesh ( mov_vars );

    std::vector < std::string > print_vars;
    print_vars.push_back ( "All" );

    mlSol.GetWriter()->SetDebugOutput ( true );
    mlSol.GetWriter()->Write ( DEFAULT_OUTPUTDIR, "biquadratic", print_vars, 0 );
    //mlSol.GetWriter()->Write ("./output1", "biquadratic", print_vars, 0);

    //return 1;


    system.AttachGetTimeIntervalFunction ( SetVariableTimeStep );
    unsigned n_timesteps = 200;
    for ( unsigned time_step = 1; time_step <= n_timesteps; time_step++ ) {

        system.CopySolutionToOldSolution();

        mlSol.GetWriter()->Write ( "output1", "biquadratic", print_vars,
                                   time_step );
        bulk->GetLine ( bulkPoints[0] );
        PrintLine ( "output1", "bulk", bulkPoints, time_step );
        lineI->GetLine ( lineIPoints[0] );
        PrintLine ( "output1", "interfaceMarkers", lineIPoints, time_step );

        system.MGsolve();

        mlSol.GetWriter()->Write ( DEFAULT_OUTPUTDIR, "biquadratic",
                                   print_vars, time_step );
        GridToParticlesProjection ( ml_prob, *bulk, *lineI );
        bulk->GetLine ( bulkPoints[0] );
        PrintLine ( DEFAULT_OUTPUTDIR, "bulk", bulkPoints, time_step );
        lineI->GetLine ( lineIPoints[0] );
        PrintLine ( DEFAULT_OUTPUTDIR, "interfaceMarkers", lineIPoints,
                    time_step );

        }

    delete bulk;
    delete lineI;

    return 0;

    }               //end main

void BuildFlag ( MultiLevelSolution & mlSol ) {


    unsigned level = mlSol._mlMesh->GetNumberOfLevels() - 1;

    Solution *sol = mlSol.GetSolutionLevel ( level );
    Mesh *msh = mlSol._mlMesh->GetLevel ( level );
    unsigned iproc = msh->processor_id();

    unsigned eflagIndex = mlSol.GetIndex ( "eflag" );
    unsigned nflagIndex = mlSol.GetIndex ( "nflag" );

    unsigned nflagType = mlSol.GetSolutionType ( nflagIndex );

    std::vector < Marker * >particle3 = bulk->GetParticles();
    std::vector < unsigned >markerOffset3 = bulk->GetMarkerOffset();
    unsigned imarker3 = markerOffset3[iproc];

    sol->_Sol[eflagIndex]->zero();
    sol->_Sol[nflagIndex]->zero();

    for ( int iel = msh->_elementOffset[iproc];
            iel < msh->_elementOffset[iproc + 1]; iel++ ) {

        bool inside = false;
        bool outside = false;
        bool interface = false;

        while ( imarker3 < markerOffset3[iproc + 1]
                && iel > particle3[imarker3]->GetMarkerElement() ) {
            imarker3++;
            }
        while ( imarker3 < markerOffset3[iproc + 1]
                && iel == particle3[imarker3]->GetMarkerElement() ) {
            double dg1 = particle3[imarker3]->GetMarkerDistance();
            if ( dg1 < -1.0e-10 ) {
                outside = true;
                if ( inside ) {
                    interface = true;
                    break;
                    }
                }
            else if ( dg1 > 1.0e-10 ) {
                inside = true;
                if ( outside ) {
                    interface = true;
                    break;
                    }
                }
            else {
                interface = true;
                break;
                }
            imarker3++;
            }
        if ( interface ) {
            sol->_Sol[eflagIndex]->set ( iel, 1. );
            unsigned nDofu = msh->GetElementDofNumber ( iel, nflagType );   // number of solution element dofs
            for ( unsigned i = 0; i < nDofu; i++ ) {
                unsigned iDof = msh->GetSolutionDof ( i, iel, nflagType );
                sol->_Sol[nflagIndex]->set ( iDof, 1. );
                }
            }
        else if ( inside ) {
            sol->_Sol[eflagIndex]->set ( iel, 2. );
            }
        }
    sol->_Sol[eflagIndex]->close();
    sol->_Sol[nflagIndex]->close();
    }
