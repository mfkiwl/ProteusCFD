#include "general.h"
#include "exceptions.h"
#include "move.h"
#include "bc.h"
#include "mesh.h"
#include <vector>
#include <map>

// NOTE: this set of functions are designed for taking a volume mesh that does not have
//       a boundary layer and systematically inflating it one boundary at a time
//       This follows the general methods outlined in "Unstructured Viscous Layer
//       Insertion Using Linear-Elastic Smoothing", by Karman et.al.

void GenerateBoundaryLayers(std::vector<int> boundaryFactagList, std::vector<Real> firstCellThicknesses,
			    std::vector<int> numberOfLayers, BoundaryConditions<Real>* bc, Mesh<Real>* m, Real growthRate);
void InflateBoundary(int boundaryFactag, Real inflationDistance, BoundaryConditions<Real>* bc, Mesh<Real>* m);


// Generates new boundary layers on select surfaces given a volume mesh
// boundaryFactagList - vector of factags to inflate a boundary layer from
// firstCellThicknesses - vector of first cell thickness (one per factag) to use for inflation
// numberOfLayers - vector of layer # (one per factag) to use for inflation
// bc - boundary conditions object pointer
// m - mesh pointer to volume mesh to inflate
// growthRate - geometric growth rate of layers
void GenerateBoundaryLayers(std::vector<int> boundaryFactagList, std::vector<Real> firstCellThicknesses,
			    std::vector<int> numberOfLayers, BoundaryConditions<Real>* bc, Mesh<Real>* m, Real growthRate)
{
  //sanity checking
  if((boundaryFactagList.size() != firstCellThicknesses.size()) || (numberOfLayers.size() != firstCellThicknesses.size())){
    Abort << "GenerateBoundaryLayers: arguments not matching in length";
  }

  // Procedure:
  for(int i = 0; i < boundaryFactagList.size(); ++i){
    int factag = boundaryFactagList[i];

    // check for ideal number of layers (i.e. grid spacing matching)
    Real avgsizing = m->ComputeElementSizingAverageOnFactag(factag);

    std::cout << "MESH UTILITY: Average face sizing on tag " << factag << " is " << avgsizing << std::endl;
    std::cout << "MESH UTILITY: Average edge length is " << sqrt(avgsizing) << std::endl;
    
    int nlayers = numberOfLayers[i];
    int ideallayers = 0;
    Real sizing = firstCellThicknesses[i];
    for(int j = 0; j < 100; ++j){
      sizing = sizing * growthRate;
      if(sizing > sqrt(avgsizing)){
	ideallayers = j - 1;
	break;
      }
    }

    std::cout << "MESH UTILITY: Number of insertion layers required for matching is " << ideallayers << std::endl;
    if(nlayers < 0){
      nlayers = ideallayers;
    }

    //compute thicknesses, we want to insert the big layer first and push on the
    //smaller ones last (closest to wall)
    std::vector<Real> distances(nlayers,0.0);
    Real dist = firstCellThicknesses[i];
    for(int j = 0; j < nlayers; ++j){
      distances[j] = dist;
      // Use geometric growth
      dist = dist * growthRate;
    }

    std::cout << "\nMESH UTILITY: Boundary layers to be generated are" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;
    for(int j = 1; j <= nlayers; ++j){
      std::cout << j << ":\t" << distances[j-1] << std::endl;
    }
    
    for(int j = 1; j <= nlayers; ++j){
      std::cout << "\nMESH UTILITY: Generating layer " << j << " of " << nlayers << std::endl;
      std::cout << "------------------------------------------------------------------------" << std::endl;
      // 1) Displace a single boundary from the list in the list using linear elastic smoothing
      // compute the next layer's distance
      InflateBoundary(factag, distances[nlayers-j], bc, m);
      // continue to next boundary..
    }
  }
  // end when all layers have been inserted
  
}

// Insert a layer of prism/hexes using the surface mesh as a connecting region
// boundaryFactag - factag of boundary we'd like to inflate
// inflationDistance - distance in mesh coordinates (dimensional/non-dimensional) to inflate
// bc - boundary condition object pointer
// m - volume mesh to insert a layer into on boundary
void InflateBoundary(int boundaryFactag, Real inflationDistance, BoundaryConditions<Real>* bc, Mesh<Real>* m)
{
  Int iter = 10;
  Real* dx = new Real[m->GetNumNodes()*3];

  Int* pts;
  Int npts = m->FindPointsWithFactag(&pts, boundaryFactag);

  std::cout << "MESH UTILITY: Inflating " << npts << " boundary nodes by " << inflationDistance << std::endl;
   
  if(npts == 0){
    std::cout << "WARNING: factag " << boundaryFactag << " does not seem to have points associated" << std::endl;
    return;
  }
  

  // save off the current location of these points
  Real* oldxyz = new Real[npts*3];
  m->GetCoordsForPoints(oldxyz, pts, npts);

  // check to see if any of these points are connected on multiple factags
  // If a point is connected to multiple factags that are being extruded it's distance
  // should be split between those two surfaces, if it is connected to another that is
  // not being extruded then the adjacent boundary should be treated as a symmetry surface
  // (that is) the movement normal is in the plane with the symmetry wall
  // we are going to allow symmetry plane nodes to move
  std::vector<Int> symmBoundaryList = GetBoundariesOnBCType(m, bc, Proteus_Symmetry);

  // compute the normals at each point using surrounding geometry
  // and then compute the total displacement at that node
  for(Int i = 0; i < npts; ++i){
    int ptid = pts[i];
    std::vector<Real> normal;
    m->GetNodeNeighborhoodNormal(ptid, symmBoundaryList, normal);
    dx[ptid*3 + 0] = normal[0]*inflationDistance;
    dx[ptid*3 + 1] = normal[1]*inflationDistance;
    dx[ptid*3 + 2] = normal[2]*inflationDistance;
  }

  // find the surface elements which are on the appropriate boundary
  std::vector<Int> elementIds;
  m->FindSurfaceElementsWithFactag(elementIds, boundaryFactag);

  std::cout << "MESH UTILITY: " << elementIds.size() << " surface elements ready for extrusion " << std::endl;
  
  MoveMeshLinearElastic(m, bc, dx, iter);

  // append the old nodes back and use them to reset the boundary elements and create an
  // interstitial layer of volume elements
  Int oldnnode = m->GetNumNodes();
  m->AppendNodes(npts, oldxyz);
  // map goes from oldNodeId --> adjacent inserted node
  std::map<int,int> nodemap;
  for(Int jpt = 0; jpt < npts; ++jpt){
    Int nodeid = pts[jpt];
    //new nodes are appended at the end of the mesh
    nodemap[nodeid] = jpt+oldnnode;
  }

  std::cout << "MESH UTILITY: pre-extrusion mesh has " << m->elementList.size() << " elements" << std::endl;
  std::cout << "MESH UTILITY: pre-extrusion mesh has " << oldnnode << " nodes" << std::endl;

  // loop over all the elements on that factag
  for(Int i = 0; i < elementIds.size(); ++i){
    Int ielem = elementIds[i];
    Element<Real>* elemsurf = m->elementList[ielem];
    Int etypesurf = elemsurf->GetType();
    Int* oldsurfnodes;
    Int enodes = elemsurf->GetNnodes();
    elemsurf->GetNodes(&oldsurfnodes);

    // --- get nodes on surface elements from the new nodes list
    Int newsurfnodes[4];
    for(Int j = 0; j < enodes; ++j){
      // access the map like
      newsurfnodes[j] = nodemap.at(oldsurfnodes[j]);
    }

    // we now have, for each surface element, the old node numbers which are
    // now pushed into the volume field and the new node numbers which define the extrusion
    // at the surface stitch a new volume element from that information
    Element<Real>* tempe = NULL;
    // allocate maximum needed space for new node list for volume element
    Int newvolnodes[8];
    // --- create new volume element to stitch the layer
    // TRI -> PRISM : QUAD -> HEX keywords
    switch(etypesurf) {
    case TRI :
      tempe = new Prism<Real>;
      for(Int k = 0; k < 3; ++k){
	newvolnodes[k] = oldsurfnodes[k];
	newvolnodes[k+3] = newsurfnodes[k];
      }
      break;
    case QUAD :
      tempe = new Hexahedron<Real>;
      for(Int k = 0; k < 4; ++k){
	newvolnodes[k] = oldsurfnodes[k];
	newvolnodes[k+4] = newsurfnodes[k];
      }
      break;
    default:
      std::cerr << "Type not inflatable in InflateBoundary()" << std::endl;
    }
    tempe->Init(newvolnodes);
    tempe->SetFactag(-1);
    m->elementList.push_back(tempe);
    
    // set the old surface element to map to its new nodes on the boundary
    elemsurf->Init(newsurfnodes);
  }

  std::cout << "MESH UTILITY: extruded mesh has " << m->elementList.size() << " elements" << std::endl;
  std::cout << "MESH UTILITY: extruded mesh has " << m->GetNumNodes() << " nodes" << std::endl;

  m->UpdateElementCounts();
  delete [] pts;
  delete [] oldxyz;

  // generate the maps required to take the next layer insertion
  m->BuildMaps();
  m->CalcMetrics();
  
  std::cout << "LAYER INFLATION SUCCESSFUL ON FACTAG: " << boundaryFactag << std::endl;
}


