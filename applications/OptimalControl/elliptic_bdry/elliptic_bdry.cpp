#include "FemusInit.hpp"
#include "MultiLevelProblem.hpp"
#include "VTKWriter.hpp"
#include "LinearImplicitSystem.hpp"
#include "NumericVector.hpp"

using namespace femus;


int ElementTargetFlag(const std::vector<double> & elem_center) {

 //***** set target domain flag ********************************** 
  int target_flag = 1; //set 0 to 1 to get the entire domain
  
   if ( elem_center[0] < (1./16. + 1./64.)  + 1.e-5  && elem_center[0] > - (1./16. + 1./64.) - 1.e-5  && 
        elem_center[1] < (1./16. + 1./64.)  + 1.e-5  && elem_center[1] > - (1./16. + 1./64.) - 1.e-5 
  ) {
     
     target_flag = 1;
     
  }
  
     return target_flag;

}

// find volume elements that contain a control face element
int ControlDomainFlag(const std::vector<double> & elem_center) {

 //***** set flag ********************************** 

  double mesh_size = 1./32.;
  int control_el_flag = 0;
   if ( elem_center[1] >  0.5 - mesh_size ) { control_el_flag = 1; }

     return control_el_flag;

}


double DesiredTarget() {
 
  return 1.;
}

double InitialValueContReg(const std::vector < double >& x) {
  return ControlDomainFlag(x);
}

double InitialValueTargReg(const std::vector < double >& x) {
  return ElementTargetFlag(x);
}

double InitialValueState(const std::vector < double >& x) {
  return 0.;
}

double InitialValueAdjoint(const std::vector < double >& x) {
  return 0.;
}

double InitialValueControl(const std::vector < double >& x) {
  return 0.;
}

bool SetBoundaryCondition(const std::vector < double >& x, const char name[], double& value, const int faceName, const double time) {

  bool dirichlet = true; //dirichlet
  value = 0;

  if(!strcmp(name,"control")) {
  if (faceName == 3)
    dirichlet = false;
  }
  
  return dirichlet;
}


double ComputeIntegral(MultiLevelProblem& ml_prob);

void AssembleOptSys(MultiLevelProblem& ml_prob);


int main(int argc, char** args) {

  // init Petsc-MPI communicator
  FemusInit mpinit(argc, args, MPI_COMM_WORLD);

  // define multilevel mesh
  MultiLevelMesh mlMsh;
  double scalingFactor = 1.;

  mlMsh.GenerateCoarseBoxMesh(32,32,0,-0.5,0.5,-0.5,0.5,0.,0.,QUAD9,"seventh");
   //1: bottom  //2: right  //3: top  //4: left
  
 /* "seventh" is the order of accuracy that is used in the gauss integration scheme
      probably in the furure it is not going to be an argument of this function   */
  unsigned numberOfUniformLevels = 1;
  unsigned numberOfSelectiveLevels = 0;
  mlMsh.RefineMesh(numberOfUniformLevels , numberOfUniformLevels + numberOfSelectiveLevels, NULL);
  mlMsh.PrintInfo();

  // define the multilevel solution and attach the mlMsh object to it
  MultiLevelSolution mlSol(&mlMsh);

  // add variables to mlSol
  mlSol.AddSolution("state", LAGRANGE, SECOND);
  mlSol.AddSolution("adjoint", LAGRANGE, SECOND);
  mlSol.AddSolution("control", LAGRANGE, SECOND);
  mlSol.AddSolution("TargReg",  DISCONTINOUS_POLYNOMIAL, ZERO); //this variable is not solution of any eqn, it's just a given field
  mlSol.AddSolution("ContReg",  DISCONTINOUS_POLYNOMIAL, ZERO); //this variable is not solution of any eqn, it's just a given field

  
  mlSol.Initialize("All");    // initialize all varaibles to zero

  mlSol.Initialize("state", InitialValueState);
  mlSol.Initialize("adjoint", InitialValueAdjoint);
  mlSol.Initialize("control", InitialValueControl);
  mlSol.Initialize("TargReg", InitialValueTargReg);
  mlSol.Initialize("ContReg", InitialValueContReg);

  // attach the boundary condition function and generate boundary data
  mlSol.AttachSetBoundaryConditionFunction(SetBoundaryCondition);
  mlSol.GenerateBdc("state");
  mlSol.GenerateBdc("adjoint");
  mlSol.GenerateBdc("control");

  // define the multilevel problem attach the mlSol object to it
  MultiLevelProblem mlProb(&mlSol);
  
 // add system  in mlProb as a Linear Implicit System
  LinearImplicitSystem& system = mlProb.add_system < LinearImplicitSystem > ("LiftRestr");
 
  system.AddSolutionToSystemPDE("state");  
  system.AddSolutionToSystemPDE("adjoint");  
  system.AddSolutionToSystemPDE("control");  
  
  // attach the assembling function to system
  system.SetAssembleFunction(AssembleOptSys);

  // initilaize and solve the system
  system.init();
  system.solve();
  
  ComputeIntegral(mlProb);
 
  // print solutions
  std::vector < std::string > variablesToBePrinted;
  variablesToBePrinted.push_back("state");
  variablesToBePrinted.push_back("adjoint");
  variablesToBePrinted.push_back("control");
  variablesToBePrinted.push_back("TargReg");
  variablesToBePrinted.push_back("ContReg");

  VTKWriter vtkIO(&mlSol);
  vtkIO.write(DEFAULT_OUTPUTDIR, "biquadratic", variablesToBePrinted);

//   GMVWriter gmvIO(&mlSol);
//   variablesToBePrinted.push_back("all");
//   gmvIO.SetDebugOutput(false);
//   gmvIO.write(DEFAULT_OUTPUTDIR, "biquadratic", variablesToBePrinted);

  return 0;
}






void AssembleOptSys(MultiLevelProblem& ml_prob) {
  //  ml_prob is the global object from/to where get/set all the data

  //  level is the level of the PDE system to be assembled
  //  levelMax is the Maximum level of the MultiLevelProblem
  //  assembleMatrix is a flag that tells if only the residual or also the matrix should be assembled

  //  extract pointers to the several objects that we are going to use

  LinearImplicitSystem* mlPdeSys  = &ml_prob.get_system<LinearImplicitSystem> ("LiftRestr");   // pointer to the linear implicit system named "LiftRestr"
  const unsigned level = mlPdeSys->GetLevelToAssemble();
  const unsigned levelMax = mlPdeSys->GetLevelMax();
  const bool assembleMatrix = mlPdeSys->GetAssembleMatrix();

  Mesh*                    msh = ml_prob._ml_msh->GetLevel(level);    // pointer to the mesh (level) object
  elem*                     el = msh->el;  // pointer to the elem object in msh (level)

  MultiLevelSolution*    mlSol = ml_prob._ml_sol;  // pointer to the multilevel solution object
  Solution*                sol = ml_prob._ml_sol->GetSolutionLevel(level);    // pointer to the solution (level) object

  LinearEquationSolver* pdeSys = mlPdeSys->_LinSolver[level]; // pointer to the equation (level) object
  SparseMatrix*             KK = pdeSys->_KK;  // pointer to the global stifness matrix object in pdeSys (level)
  NumericVector*           RES = pdeSys->_RES; // pointer to the global residual vector object in pdeSys (level)

  const unsigned  dim = msh->GetDimension(); // get the domain dimension of the problem
  unsigned dim2 = (3 * (dim - 1) + !(dim - 1));        // dim2 is the number of second order partial derivatives (1,3,6 depending on the dimension)
  const unsigned maxSize = static_cast< unsigned >(ceil(pow(3, dim)));          // conservative: based on line3, quad9, hex27

  unsigned    iproc = msh->processor_id(); // get the process_id (for parallel computation)

   //*************************** 
  unsigned xType = 2; // get the finite element type for "x", it is always 2 (LAGRANGE QUADRATIC)
  vector < vector < double > > x(dim);
  vector < vector < double> >  x_bdry(dim);
  for (unsigned i = 0; i < dim; i++) {
         x[i].reserve(maxSize);
	 x_bdry[i].reserve(maxSize);
  }

 //*************************** 

 //*************************** 
  double weight = 0.; // gauss point weight
  double weight_bdry = 0.; // gauss point weight on the boundary


 //******** state ******************* 
 //*************************** 
  vector <double> phi_u;  // local test function
  vector <double> phi_u_x; // local test function first order partial derivatives
  vector <double> phi_u_xx; // local test function second order partial derivatives

  phi_u.reserve(maxSize);
  phi_u_x.reserve(maxSize * dim);
  phi_u_xx.reserve(maxSize * dim2);
  
  unsigned solIndex_u    = mlSol->GetIndex("state");    // get the position of "state" in the ml_sol object
  unsigned solType_u     = mlSol->GetSolutionType(solIndex_u);    // get the finite element type for "state"
  unsigned solPdeIndex_u = mlPdeSys->GetSolPdeIndex("state");    // get the position of "state" in the pdeSys object

  vector < double >  sol_u;     sol_u.reserve(maxSize);
  vector< int > l2GMap_u;    l2GMap_u.reserve(maxSize);
 //*************************** 
 //*************************** 

  
  

  
 //************ ThomAdj *************** 
 //*************************** 
  vector <double> phi_adj;  // local test function
  vector <double> phi_adj_x; // local test function first order partial derivatives
  vector <double> phi_adj_xx; // local test function second order partial derivatives

  phi_adj.reserve(maxSize);
  phi_adj_x.reserve(maxSize * dim);
  phi_adj_xx.reserve(maxSize * dim2);
 
  
  unsigned solIndex_adj       = mlSol->GetIndex("adjoint");    // get the position of "state" in the ml_sol object
  unsigned solType_adj     = mlSol->GetSolutionType(solIndex_adj);    // get the finite element type for "state"
  unsigned solPdeIndex_adj = mlPdeSys->GetSolPdeIndex("adjoint");    // get the position of "state" in the pdeSys object

  vector < double >  sol_adj;   sol_adj.reserve(maxSize);
  vector < int > l2GMap_adj; l2GMap_adj.reserve(maxSize);
  //*************************** 
 //*************************** 

  
 //************ Tcont *************** 
 //*************************** 
  vector <double> phi_ctrl;  // local test function
  vector <double> phi_x_Tcont; // local test function first order partial derivatives
  vector <double> phi_xx_Tcont; // local test function second order partial derivatives
  vector <double> phi_ctrl_bdry;  
  vector <double> phi_x_Tcont_bdry; 

  phi_ctrl.reserve(maxSize);
  phi_x_Tcont.reserve(maxSize * dim);
  phi_xx_Tcont.reserve(maxSize * dim2);

  phi_ctrl_bdry.reserve(maxSize);
  phi_x_Tcont_bdry.reserve(maxSize * dim);
  
  unsigned solIndexTcont;
  solIndexTcont = mlSol->GetIndex("control");
  unsigned solTypeTcont = mlSol->GetSolutionType(solIndexTcont);

  unsigned solPdeIndexTcont;
  solPdeIndexTcont = mlPdeSys->GetSolPdeIndex("control");

  vector < double >  solTcont; // local solution
  solTcont.reserve(maxSize);
 vector< int > l2GMap_Tcont;
  l2GMap_Tcont.reserve(maxSize);
  //*************************** 
 //*************************** 
  

 //*************************** 
  //********* WHOLE SET OF VARIABLES ****************** 
  const int solType_max = 2;  //biquadratic

  const int n_vars = 3;
 
  vector< int > l2GMap_AllVars; // local to global mapping
  l2GMap_AllVars.reserve(n_vars*maxSize);
  
  vector< double > Res; // local redidual vector
  Res.reserve(n_vars*maxSize);

  vector < double > Jac;
  Jac.reserve( n_vars*maxSize * n_vars*maxSize);
 //*************************** 

  
 //********** DATA ***************** 
  double T_des = DesiredTarget();
  double alpha = 1;
  double beta  = 1.e-3;
  double gamma = 1.e-3;
  double penalty_strong = 10e+14;
 //*************************** 
  
  
  if (assembleMatrix)  KK->zero();

    
  // element loop: each process loops only on the elements that owns
  for (int iel = msh->IS_Mts2Gmt_elem_offset[iproc]; iel < msh->IS_Mts2Gmt_elem_offset[iproc + 1]; iel++) {

    unsigned kel = msh->IS_Mts2Gmt_elem[iel]; // mapping between paralell dof and mesh dof
    short unsigned kelGeom = el->GetElementType(kel);    // element geometry type

 //********* GEOMETRY ****************** 
    unsigned nDofx = el->GetElementDofNumber(kel, xType);    // number of coordinate element dofs
    for (int i = 0; i < dim; i++)  x[i].resize(nDofx);
    // local storage of coordinates
    for (unsigned i = 0; i < nDofx; i++) {
      unsigned iNode = el->GetMeshDof(kel, i, xType);    // local to global coordinates node
      unsigned xDof  = msh->GetMetisDof(iNode, xType);    // global to global mapping between coordinates node and coordinate dof

      for (unsigned jdim = 0; jdim < dim; jdim++) {
        x[jdim][i] = (*msh->_coordinate->_Sol[jdim])(xDof);      // global extraction and local storage for the element coordinates
      }
    }

   // elem average point 
    vector < double > elem_center(dim);   
    for (unsigned j = 0; j < dim; j++) {  elem_center[j] = 0.;  }
  for (unsigned j = 0; j < dim; j++) {  
      for (unsigned i = 0; i < nDofx; i++) {
         elem_center[j] += x[j][i];
       }
    }
    
   for (unsigned j = 0; j < dim; j++) { elem_center[j] = elem_center[j]/nDofx; }
  //*************************************** 
  
  //***** set target domain flag ********************************** 
   int target_flag = 0;
   target_flag = ElementTargetFlag(elem_center);
  //*************************************** 
   
  //***** set control flag ********************************** 
  int control_el_flag = 0;
        control_el_flag = ControlDomainFlag(elem_center);
//   std::vector<int> control_node_flag(nDofx,0);
//   if (control_el_flag == 0) std::fill(control_node_flag.begin(), control_node_flag.end(), 0);
  //*************************************** 
    
 //*********** Thom **************************** 
    unsigned nDofThom     = el->GetElementDofNumber(kel, solType_u);    // number of solution element dofs
    sol_u    .resize(nDofThom);
    l2GMap_u.resize(nDofThom);
   // local storage of global mapping and solution
    for (unsigned i = 0; i < sol_u.size(); i++) {
      unsigned iNode = el->GetMeshDof(kel, i, solType_u);    // local to global solution node
      unsigned solDofThom = msh->GetMetisDof(iNode, solType_u);    // global to global mapping between solution node and solution dof
      sol_u[i] = (*sol->_Sol[solIndex_u])(solDofThom);      // global extraction and local storage for the solution
      l2GMap_u[i] = pdeSys->GetKKDof(solIndex_u, solPdeIndex_u, iNode);    // global to global mapping between solution node and pdeSys dof
    }
 //*********** Thom **************************** 


 //*********** ThomAdj **************************** 
    unsigned nDofThomAdj  = el->GetElementDofNumber(kel, solType_adj);    // number of solution element dofs
    sol_adj    .resize(nDofThomAdj);
    l2GMap_adj.resize(nDofThomAdj);
    for (unsigned i = 0; i < sol_adj.size(); i++) {
      unsigned iNode = el->GetMeshDof(kel, i, solType_adj);    // local to global solution node
      unsigned solDofThomAdj = msh->GetMetisDof(iNode, solType_adj);    // global to global mapping between solution node and solution dof
      sol_adj[i] = (*sol->_Sol[solIndex_adj])(solDofThomAdj);      // global extraction and local storage for the solution
      l2GMap_adj[i] = pdeSys->GetKKDof(solIndex_adj, solPdeIndex_adj, iNode);    // global to global mapping between solution node and pdeSys dof
    } 
 //*********** ThomAdj **************************** 

 //*********** Tcont **************************** 
    unsigned nDofTcont  = el->GetElementDofNumber(kel, solTypeTcont);    // number of solution element dofs
    solTcont    .resize(nDofTcont);
    l2GMap_Tcont.resize(nDofTcont);
    for (unsigned i = 0; i < solTcont.size(); i++) {
      unsigned iNode = el->GetMeshDof(kel, i, solTypeTcont);    // local to global solution node
      unsigned solDofTcont = msh->GetMetisDof(iNode, solTypeTcont);    // global to global mapping between solution node and solution dof
      solTcont[i] = (*sol->_Sol[solIndexTcont])(solDofTcont);      // global extraction and local storage for the solution
      l2GMap_Tcont[i] = pdeSys->GetKKDof(solIndexTcont, solPdeIndexTcont, iNode);    // global to global mapping between solution node and pdeSys dof
    } 
 //*********** Tcont **************************** 
 
 //********** ALL VARS ***************** 
    unsigned nDof_AllVars = nDofThom + nDofThomAdj + nDofTcont; 
    int nDof_max    =  nDofThom;   // AAAAAAAAAAAAAAAAAAAAAAAAAAA TODO COMPUTE MAXIMUM maximum number of element dofs for one scalar variable
    
    if(nDofThomAdj > nDof_max) 
    {
      nDof_max = nDofThomAdj;
      }
    
    if(nDofTcont > nDof_max)
    {
      nDof_max = nDofTcont;
    }
    
    
    Res.resize(nDof_AllVars);
    std::fill(Res.begin(), Res.end(), 0.);

    Jac.resize(nDof_AllVars * nDof_AllVars);
    std::fill(Jac.begin(), Jac.end(), 0.);
    
    l2GMap_AllVars.resize(0);
    l2GMap_AllVars.insert(l2GMap_AllVars.end(),l2GMap_u.begin(),l2GMap_u.end());
    l2GMap_AllVars.insert(l2GMap_AllVars.end(),l2GMap_adj.begin(),l2GMap_adj.end());
    l2GMap_AllVars.insert(l2GMap_AllVars.end(),l2GMap_Tcont.begin(),l2GMap_Tcont.end());
 //*************************** 

    
 //===========================   

	// Perform face loop over elements that contain some control face
	if (control_el_flag == 1) {
	  
	  double tau=0.;
	  vector<double> normal(dim,0);
	       
	  // loop on faces of the current element

	  for(unsigned jface=0; jface < el->GetElementFaceNumber(kel); jface++) {
            std::vector < double > xx(3,0.);
	    // look for boundary faces
	    if(el->GetFaceElementIndex(kel,jface) < 0) {
	      unsigned int face = -( msh->el->GetFaceElementIndex(kel,jface)+1);
	      
// 	      if( !ml_sol->_SetBoundaryConditionFunction(xx,"U",tau,face,0.) && tau!=0.){
	      if(  face == 3) { //control face
		
		unsigned nve = msh->el->GetElementFaceDofNumber(kel,jface,solTypeTcont);
		const unsigned felt = msh->el->GetElementFaceType(kel, jface);  		  		  
		for(unsigned i=0; i<nve; i++) {
		  unsigned inode=msh->el->GetFaceVertexIndex(kel,jface,i)-1u;
		  unsigned inode_Metis=msh->GetMetisDof(inode,xType);
		  unsigned int ilocal = msh->el->GetLocalFaceVertexIndex(kel, jface, i);
		  for(unsigned idim=0; idim<dim; idim++) {
		      x_bdry[idim][i]=(*msh->_coordinate->_Sol[idim])(inode_Metis);
		  }
		}
		for(unsigned igs=0; igs < msh->_finiteElement[felt][solTypeTcont]->GetGaussPointNumber(); igs++) {
		  msh->_finiteElement[felt][solTypeTcont]->JacobianSur(x_bdry,igs,weight_bdry,phi_ctrl_bdry,phi_x_Tcont_bdry,normal);
		  //phi1 =msh->_finiteElement[felt][SolType2]->GetPhi(igs);
		  // *** phi_i loop ***
		  for(unsigned i=0; i<nve; i++) {
		    unsigned int ilocal = msh->el->GetLocalFaceVertexIndex(kel, jface, i);
		    
 // // 			Rhs[indexVAR[dim+idim]][ilocal]   += phi[]normal[idim];

// 		    for(unsigned j=0; j<nve; j++) {
//    
// 		    Jac[  i*(nDofThom + nDofThomAdj + nDofTcont)   + (0+j)  ] += weight_bdry* (alpha*phi_ctrl_bdry[i]*phi_ctrl_bdry[j]);
// 		    
// 		    double grad_bdry = 0.;
// 		      for (unsigned d = 0; d < dim; d++) {  double grad_bdry += phi_x_Tcont_bdry[i+d*nve] * phi_x_Tcont_bdry[j+d*nve];    }
// 		    Jac[  i*     j*  ] += weight_bdry* beta * grad_bdry;
// 		   }
				  
		  }
		}
	      }
	      
	    }
	  }    
	  
	} //end if
	
	else { //here we set the diagonal to 1 and the rhs to 0
	  
	  
	}
    
 //===========================   

    if (level == levelMax || !el->GetRefinedElementIndex(kel)) {      // do not care about this if now (it is used for the AMR)
   
      // *** Gauss point loop ***
      for (unsigned ig = 0; ig < msh->_finiteElement[kelGeom][solType_max]->GetGaussPointNumber(); ig++) {
	
        // *** get gauss point weight, test function and test function partial derivatives ***
        //  ==== Thom 
	msh->_finiteElement[kelGeom][solType_u]   ->Jacobian(x, ig, weight, phi_u, phi_u_x, phi_u_xx);
        //  ==== ThomAdj 
        msh->_finiteElement[kelGeom][solType_adj]->Jacobian(x, ig, weight, phi_adj, phi_adj_x, phi_adj_xx);
          
	
       //FILLING WITH THE EQUATIONS ===========
	// *** phi_i loop ***
        for (unsigned i = 0; i < nDof_max; i++) {

          double srcTerm = 10.;
	  
          // FIRST ROW
	  if (i < nDofThom)    Res[0                      + i] += weight * (0.) ;
          // SECOND ROW
          if (i < nDofThomAdj) Res[nDofThom               + i] += weight * ( alpha * target_flag * T_des * phi_adj[i] );
  
          // THIRD ROW
//          if ( control_el_flag == 1)  {
//            if (i < nDofTcont)   Res[nDofThom + nDofThomAdj + i] += weight * ( 0.);
// 	      }
// 	 else if ( control_el_flag == 0)  {  
           if (i < nDofTcont)   {
// 	     Res[nDofThom + nDofThomAdj + i] += weight * 0.* phi_ctrl[i]; //weak enforcement
	     Res[nDofThom + nDofThomAdj + i] += penalty_strong * 0.; //strong enforcement 
	  }
// 	}
	      
	      
	      
          if (assembleMatrix) {
	    
            // *** phi_j loop ***
            for (unsigned j = 0; j < nDof_max; j++) {
              double laplace_mat_Thom = 0.;
              double laplace_mat_ThomAdj = 0.;
              double laplace_mat_Tcont = 0.;
              double laplace_mat_ThomVSTcont = 0.;
              double laplace_mat_ThomAdjVSTcont = 0.;

              for (unsigned kdim = 0; kdim < dim; kdim++) {
              if ( i < nDofThom && j < nDofThom )         laplace_mat_Thom           += (phi_u_x   [i * dim + kdim] * phi_u_x   [j * dim + kdim]);
              if ( i < nDofThomAdj && j < nDofThomAdj )   laplace_mat_ThomAdj        += (phi_adj_x[i * dim + kdim] * phi_adj_x[j * dim + kdim]);
              if ( i < nDofTcont   && j < nDofTcont   )   laplace_mat_Tcont          += (phi_x_Tcont  [i * dim + kdim] * phi_x_Tcont  [j * dim + kdim]);
              if ( i < nDofThom    && j < nDofTcont )     laplace_mat_ThomVSTcont    += (phi_u_x   [i * dim + kdim] * phi_x_Tcont  [j * dim + kdim]);
              if ( i < nDofTcont   && j < nDofThomAdj )   laplace_mat_ThomAdjVSTcont += (phi_adj_x[i * dim + kdim] * phi_x_Tcont  [j * dim + kdim]);
		
	      }

              //first row ==================
              //DIAG BLOCK Thom
	      if ( i < nDofThom && j < nDofThom )       Jac[    0    * (nDofThom + nDofThomAdj + nDofTcont)    +
                                                                   i    * (nDofThom + nDofThomAdj + nDofTcont) +
		                                                (0 + j)                                           ]  += weight * laplace_mat_Thom;
              // BLOCK Thom - Tcont
              /*if ( i < nDofThom    && j < nDofTcont )   Jac[    0     * (nDofThom + nDofThomAdj + nDofTcont)   +
                                                                   i    * (nDofThom + nDofThomAdj + nDofTcont) +
		                                                (nDofThom + nDofThomAdj + j)                      ]  += weight * laplace_mat_ThomVSTcont;     */ 
	      
              //second row ==================
              //DIAG BLOCK ThomAdj
              if ( i < nDofThomAdj && j < nDofThomAdj ) Jac[ (nDofThom + 0)           * (nDofThom + nDofThomAdj + nDofTcont) +
		                                                   i    * (nDofThom + nDofThomAdj + nDofTcont) +
								(nDofThom + j)                                    ]  += weight * laplace_mat_ThomAdj;
	      
              // BLOCK ThomAdj - Thom	      
              if ( i < nDofThomAdj && j < nDofThom )   Jac[    (nDofThom + 0)           * (nDofThom + nDofThomAdj + nDofTcont)  +
                                                                   i    * (nDofThom + nDofThomAdj + nDofTcont) +
		                                                (0 + j)                      ]                       += weight * 1. * target_flag *  phi_adj[i] * phi_u[j];   
	      
              // BLOCK ThomAdj - Tcont	      
//               if ( i < nDofThomAdj && j < nDofTcont )   Jac[    (nDofThom + 0)           * (nDofThom + nDofThomAdj + nDofTcont)  +
//                                                                    i    * (nDofThom + nDofThomAdj + nDofTcont) +
// 		                                                (nDofThom + nDofThomAdj + j)                      ]  += weight * alpha * target_flag  *  phi_adj[i] * phi_ctrl[j]; 

              //third row ==================
             //DIAG BLOCK Tcont
      
// // // 	      else if ( control_el_flag == 0)  {  
		
              //BLOCK Tcont - Tcont

		//  weak enforcement
// 	      if ( i < nDofTcont   && j < nDofTcont   ) {
// 		
// 		Jac[ (nDofThom + nDofThomAdj) * (nDofThom + nDofThomAdj + nDofTcont) +
// 		                                                   i    * (nDofThom + nDofThomAdj + nDofTcont)               +
// 								(nDofThom  + nDofThomAdj + j)                     ] 
// 								+= weight * phi_ctrl[j] * phi_ctrl[i]; 
// 	      }
		
		//  strong enforcement
		if ( i < nDofTcont   && j < nDofTcont &&  i==j ) {
		Jac[ (nDofThom + nDofThomAdj) * (nDofThom + nDofThomAdj + nDofTcont) +
		                                                   i    * (nDofThom + nDofThomAdj + nDofTcont)               +
								(nDofThom  + nDofThomAdj + j)                     ] += penalty_strong;
		}
	      
// // // 	   }
	      
	      
	      
            } // end phi_j loop
          } // endif assemble_matrix

        } // end phi_i loop
        
      } // end gauss point loop
    } // endif single element not refined or fine grid loop

    //--------------------------------------------------------------------------------------------------------
    // Add the local Matrix/Vector into the global Matrix/Vector

    //copy the value of the adept::adoube aRes in double Res and store
    RES->add_vector_blocked(Res, l2GMap_AllVars);

    if (assembleMatrix) {
      //store K in the global matrix KK
      KK->add_matrix_blocked(Jac, l2GMap_AllVars, l2GMap_AllVars);
    }
  } //end element loop for each process

  RES->close();

  if (assembleMatrix) KK->close();

  // ***************** END ASSEMBLY *******************

  return;
}



double ComputeIntegral(MultiLevelProblem& ml_prob)    {
  
  
  LinearImplicitSystem* mlPdeSys  = &ml_prob.get_system<LinearImplicitSystem> ("LiftRestr");   // pointer to the linear implicit system named "LiftRestr"
  const unsigned level = mlPdeSys->GetLevelToAssemble();
  const unsigned levelMax = mlPdeSys->GetLevelMax();

  Mesh*                    msh = ml_prob._ml_msh->GetLevel(level);    // pointer to the mesh (level) object
  elem*                     el = msh->el;  // pointer to the elem object in msh (level)

  MultiLevelSolution*    mlSol = ml_prob._ml_sol;  // pointer to the multilevel solution object
  Solution*                sol = ml_prob._ml_sol->GetSolutionLevel(level);    // pointer to the solution (level) object

  LinearEquationSolver* pdeSys = mlPdeSys->_LinSolver[level]; // pointer to the equation (level) object

  const unsigned  dim = msh->GetDimension(); // get the domain dimension of the problem
  unsigned dim2 = (3 * (dim - 1) + !(dim - 1));        // dim2 is the number of second order partial derivatives (1,3,6 depending on the dimension)
  const unsigned maxSize = static_cast< unsigned >(ceil(pow(3, dim)));          // conservative: based on line3, quad9, hex27

  unsigned    iproc = msh->processor_id(); // get the process_id (for parallel computation)

   //*************************** 
  vector < vector < double > > x(dim);    // local coordinates
  unsigned xType = 2; // get the finite element type for "x", it is always 2 (LAGRANGE QUADRATIC)
  for (unsigned i = 0; i < dim; i++) {
    x[i].reserve(maxSize);
  }
 //*************************** 

 //*************************** 
  double weight; // gauss point weight
  

 //******** Thom ******************* 
 //*************************** 
  vector <double> phi_u;  // local test function
  vector <double> phi_u_x; // local test function first order partial derivatives
  vector <double> phi_u_xx; // local test function second order partial derivatives

  phi_u.reserve(maxSize);
  phi_u_x.reserve(maxSize * dim);
  phi_u_xx.reserve(maxSize * dim2);
  
 
  unsigned solIndex_u;
  solIndex_u = mlSol->GetIndex("state");    // get the position of "state" in the ml_sol object
  unsigned solType_u = mlSol->GetSolutionType(solIndex_u);    // get the finite element type for "state"

  vector < double >  sol_u; // local solution
  sol_u.reserve(maxSize);
  
  double Thom_gss = 0.;
 //*************************** 
 //*************************** 

  
 //************ ThomAdj *************** 
 //*************************** 
  vector <double> phi_Tdes;  // local test function
  vector <double> phi_x_Tdes; // local test function first order partial derivatives
  vector <double> phi_xx_Tdes; // local test function second order partial derivatives

    phi_Tdes.reserve(maxSize);
    phi_x_Tdes.reserve(maxSize * dim);
    phi_xx_Tdes.reserve(maxSize * dim2);
 
  
//  unsigned solIndexTdes;
//   solIndexTdes = mlSol->GetIndex("Tdes");    // get the position of "state" in the ml_sol object
//   unsigned solTypeTdes = mlSol->GetSolutionType(solIndexTdes);    // get the finite element type for "state"

  vector < double >  solTdes; // local solution
  solTdes.reserve(maxSize);
  vector< int > l2GMap_Tdes;
  l2GMap_Tdes.reserve(maxSize);
  double Tdes_gss = 0.;
  //*************************** 
 //*************************** 

  
 //************ Tcont *************** 
 //*************************** 
  vector <double> phi_ctrl;  // local test function
  vector <double> phi_x_Tcont; // local test function first order partial derivatives
  vector <double> phi_xx_Tcont; // local test function second order partial derivatives

  phi_ctrl.reserve(maxSize);
  phi_x_Tcont.reserve(maxSize * dim);
  phi_xx_Tcont.reserve(maxSize * dim2);
  
  unsigned solIndexTcont;
  solIndexTcont = mlSol->GetIndex("control");
  unsigned solTypeTcont = mlSol->GetSolutionType(solIndexTcont);

  vector < double >  solTcont; // local solution
  solTcont.reserve(maxSize);
  vector< int > l2GMap_Tcont;
  l2GMap_Tcont.reserve(maxSize);
  
  double Tcont_gss = 0.;
  //*************************** 
 //*************************** 
  

 //*************************** 
  //********* WHOLE SET OF VARIABLES ****************** 
  const int solType_max = 2;  //biquadratic

  const int n_vars = 3;
 
  vector< int > l2GMap_AllVars; // local to global mapping
  l2GMap_AllVars.reserve(n_vars*maxSize);
  
  vector< double > Res; // local redidual vector
  Res.reserve(n_vars*maxSize);

  vector < double > Jac;
  Jac.reserve( n_vars*maxSize * n_vars*maxSize);
 //*************************** 

  
 //********** DATA ***************** 
  double T_des = DesiredTarget();
  //*************************** 
  
  double integral = 0.;

    
  // element loop: each process loops only on the elements that owns
  for (int iel = msh->IS_Mts2Gmt_elem_offset[iproc]; iel < msh->IS_Mts2Gmt_elem_offset[iproc + 1]; iel++) {

    unsigned kel = msh->IS_Mts2Gmt_elem[iel]; // mapping between paralell dof and mesh dof
    short unsigned kelGeom = el->GetElementType(kel);    // element geometry type

 //********* GEOMETRY ****************** 
    unsigned nDofx = el->GetElementDofNumber(kel, xType);    // number of coordinate element dofs
    for (int i = 0; i < dim; i++)  x[i].resize(nDofx);
    // local storage of coordinates
    for (unsigned i = 0; i < nDofx; i++) {
      unsigned iNode = el->GetMeshDof(kel, i, xType);    // local to global coordinates node
      unsigned xDof  = msh->GetMetisDof(iNode, xType);    // global to global mapping between coordinates node and coordinate dof

      for (unsigned jdim = 0; jdim < dim; jdim++) {
        x[jdim][i] = (*msh->_coordinate->_Sol[jdim])(xDof);      // global extraction and local storage for the element coordinates
      }
    }

   // elem average point 
    vector < double > elem_center(dim);   
    for (unsigned j = 0; j < dim; j++) {  elem_center[j] = 0.;  }
  for (unsigned j = 0; j < dim; j++) {  
      for (unsigned i = 0; i < nDofx; i++) {
         elem_center[j] += x[j][i];
       }
    }
    
   for (unsigned j = 0; j < dim; j++) { elem_center[j] = elem_center[j]/nDofx; }
  //*************************************** 
  
  //***** set target domain flag ********************************** 
   int target_flag = 0;
   target_flag = ElementTargetFlag(elem_center);
  //*************************************** 

   
 //*********** Thom **************************** 
    unsigned nDofThom     = el->GetElementDofNumber(kel, solType_u);    // number of solution element dofs
    sol_u    .resize(nDofThom);
   // local storage of global mapping and solution
    for (unsigned i = 0; i < sol_u.size(); i++) {
      unsigned iNode = el->GetMeshDof(kel, i, solType_u);    // local to global solution node
      unsigned solDofThom = msh->GetMetisDof(iNode, solType_u);    // global to global mapping between solution node and solution dof
      sol_u[i] = (*sol->_Sol[solIndex_u])(solDofThom);      // global extraction and local storage for the solution
    }
 //*********** Thom **************************** 


 //*********** Tcont **************************** 
    unsigned nDofTcont  = el->GetElementDofNumber(kel, solTypeTcont);    // number of solution element dofs
    solTcont    .resize(nDofTcont);
    for (unsigned i = 0; i < solTcont.size(); i++) {
      unsigned iNode = el->GetMeshDof(kel, i, solTypeTcont);    // local to global solution node
      unsigned solDofTcont = msh->GetMetisDof(iNode, solTypeTcont);    // global to global mapping between solution node and solution dof
      solTcont[i] = (*sol->_Sol[solIndexTcont])(solDofTcont);      // global extraction and local storage for the solution
    } 
 //*********** Tcont **************************** 
 
 
 //*********** Tdes **************************** 
    unsigned nDofTdes  = el->GetElementDofNumber(kel, solType_u);    // number of solution element dofs
    solTdes    .resize(nDofTdes);
    for (unsigned i = 0; i < solTdes.size(); i++) {
      solTdes[i] = T_des;  //dof value
    } 
 //*********** Tdes **************************** 

 
 //********** ALL VARS ***************** 
    int nDof_max    =  nDofThom;   // AAAAAAAAAAAAAAAAAAAAAAAAAAA TODO COMPUTE MAXIMUM maximum number of element dofs for one scalar variable
    
    if(nDofTdes > nDof_max) 
    {
      nDof_max = nDofTdes;
      }
    
    if(nDofTcont > nDof_max)
    {
      nDof_max = nDofTcont;
    }
    
  //*************************** 


    if (level == levelMax || !el->GetRefinedElementIndex(kel)) {      // do not care about this if now (it is used for the AMR)
   
      // *** Gauss point loop ***
      for (unsigned ig = 0; ig < msh->_finiteElement[kelGeom][solType_max]->GetGaussPointNumber(); ig++) {
	
        // *** get gauss point weight, test function and test function partial derivatives ***
        //  ==== Thom 
	msh->_finiteElement[kelGeom][solType_u]   ->Jacobian(x, ig, weight, phi_u, phi_u_x, phi_u_xx);
        //  ==== ThomAdj 
        msh->_finiteElement[kelGeom][solType_u/*solTypeTdes*/]->Jacobian(x, ig, weight, phi_Tdes, phi_x_Tdes, phi_xx_Tdes);
        //  ==== ThomCont 
        msh->_finiteElement[kelGeom][solTypeTcont]  ->Jacobian(x, ig, weight, phi_ctrl, phi_x_Tcont, phi_xx_Tcont);

	Thom_gss = 0.;  for (unsigned i = 0; i < nDofThom; i++) Thom_gss += sol_u[i] * phi_u[i];		
	Tcont_gss = 0.; for (unsigned i = 0; i < nDofTcont; i++) Tcont_gss += solTcont[i] * phi_ctrl[i];  
	Tdes_gss  = 0.; for (unsigned i = 0; i < nDofTdes; i++)  Tdes_gss  += solTdes[i]  * phi_Tdes[i];  

               integral += target_flag * weight * (Thom_gss +  Tcont_gss - Tdes_gss) * (Thom_gss +  Tcont_gss - Tdes_gss);
	  
      } // end gauss point loop
    } // endif single element not refined or fine grid loop
  } //end element loop

  std::cout << "The value of the integral is " << std::setw(11) << std::setprecision(10) << integral << std::endl;
  
return integral;
  
}
  
  

