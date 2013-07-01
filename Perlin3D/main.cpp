/*

  https://github.com/superwills/Ice-OSurface
  version 0.9 (almost working), July 1 2013 4:26p

  Copyright (C) 2013 William Sherif

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  William Sherif
  will.sherif@gmail.com

  This source code includes Stefan Gustavson's Perlin noise code (in Perlin.h)

*/

#ifdef _WIN32
#include <stdlib.h> // MUST BE BEFORE GLUT ON WINDOWS
#include <gl/glut.h>
#else
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#endif
#include "perlin.h"
#include "GLUtil.h"
#include "StdWilUtil.h"
#include "Vectorf.h"
#include "Geometry.h"
#include <vector>
#include <functional>
using namespace std;

// You need a fat epsilon for the tirangle merge. 1e-6f is TOO NARROW
float EPS = 1e-3f;
float w=768.f, h=768.f ;
static float mx, my, sbd=150.f,
  pw=2.59, //0.58 
  isosurface=0.38,
  tol=0.1,
  cubeSize=100.f ;

#define SOLID 0
//int cols=160,rows=160,slabs=160;
//int cols=100,rows=100,slabs=100 ;
int cols=10,rows=10,slabs=10 ;
float worldSize=200;

Vector3f offset( -cols/2.f, -rows/2.f, -slabs/2.f ) ; // offset
Vector3f gridSizer = Vector3f(worldSize) / Vector3f( cols,rows,slabs ) ;

float lineWidth=1.f,ptSize=1.0f;
bool showGradients=0, showLines=0 ;
bool renderPts = 0 ;
bool repeats=0,showTris=1,showCubes=1;
bool voxelGenMode=1;

float speed=0.02f ;
Axis axis ;

struct Voxel
{
  float v ;
  Vector4f d ;
  Vector4f color ;
} ;

#define INDEX(i,j,k) ((i) + (j)*cols + (k)*cols*rows)
// 
struct IsosurfacePunchthru
{
  // the original voxel index that DETECTED the punchthru
  Vector3i voxelIndex ;

  // The actual 3-space point of the punchthru
  // this is computed based on dv and the values at
  // the actual voxels where this punchthru took place.
  //Vector3f pt ;
  int directionIndex ; // the direction of the vector that breaks the isosurface.
  // looks up into IsosurfacePunchthruSet.Directions[ directionIndex ] to get the actual direction.
  
  float t ;    // how far between voxelIndex and the voxel at directionIndex you were at isosurface intn.
  float dv ;   // the actual jump in value across dir.

  IsosurfacePunchthru():t(0.f),dv(0.f)
  {
  }

  IsosurfacePunchthru( Vector3i iVoxelIndex, int iDirectionIndex, float iT, float iDv ) :
    voxelIndex(iVoxelIndex),directionIndex(iDirectionIndex),t(iT),dv(iDv)
  {
  }
} ;

// Every POINT stores the directions for which the isosurface
// punchthru succeeded.
struct IsosurfacePunchthruSet
{
  static vector<Vector3i> Directions ;
  static vector< vector<int> > DirsNeighbours ;
  // Directions[i] has neighbours listed by neighbours[i][0]..neighbours[i][size]

  static void addDirection( const Vector3i& v )
  {
    Directions.push_back( v ) ;
    DirsNeighbours.push_back( vector<int>() ) ;
  }

  static void addNeighbours( int forDirection, int* neighbours, int len )
  {
    if( forDirection >= DirsNeighbours.size() )
    {
      printf( "%d oob DirsNeighbours (%d)\n", forDirection, DirsNeighbours.size() ) ;
      return ;
    }
    for( int i = 0 ; i < len ; i++ )
      DirsNeighbours[forDirection].push_back( neighbours[i] ) ;
  }

  // There are 26 directions over which the isosurface can break.
  // (1 towards each of the 26 points that surround each 3x3 group).
  // If isosurface is drawn differently for going - and going + into it,
  // it will be 2 sided and have different colors.  Or you can just draw
  // the + side, or you can even give it a thickness (provided grid spacing
  // is course enough.)

  // indexing is a 3x3x3 cube LINEAR indexing 
  // in the order z,y,x.
  vector<IsosurfacePunchthru*> directedPunchthrus ; // punchthrus indexed by directions in Directions.

  IsosurfacePunchthruSet()
  {
    directedPunchthrus.resize( Directions.size(),0 ) ;
  }
  ~IsosurfacePunchthruSet()
  {
    for( int i = 0 ; i < directedPunchthrus.size() ; i++ )
      if( directedPunchthrus[i] )
        delete directedPunchthrus[i] ;
  }
} ;

vector<Vector3i> IsosurfacePunchthruSet::Directions ;
vector< vector<int> > IsosurfacePunchthruSet::DirsNeighbours ;

// for now only float.
vector<Voxel> voxels ;
vector<IsosurfacePunchthruSet> punchthru ;
vector<VertexPC> gradients ;
vector<VertexPC> debugLines ;
vector<VertexPNC> verts ;
vector<VertexPC> tris ;

vector<VertexPNC> iVerts ;
vector<int> indices ;


vector<Vector3f> randPts ;

void addDebugLine( const Vector3f& v1, const Vector4f& c1, const Vector3f& v2, const Vector4f& c2 )
{
  debugLines.push_back( VertexPC( v1, c1 ) ) ;
  debugLines.push_back( VertexPC( v2, c2 ) ) ;
}

void genTet( const Vector3f& center )
{
  float s = 5 ;
  /*
    C----G
   /|   /|
  D-A--H E
  |/   |/
  B----F
  */
  Vector3f A( -s, -s, -s ),  B( -s, -s,  s ),  C( -s,  s, -s ),  D( -s,  s,  s ),
           E(  s, -s, -s ),  F(  s, -s,  s ),  G(  s,  s, -s ),  H(  s,  s,  s ) ;
  
  A+=center,  B+=center,  C+=center,  D+=center,
  E+=center,  F+=center,  G+=center,  H+=center ;

  Geometry::addTet( verts, A, D, C, G, Vector4f( 0,0,1,0.5 ) ) ;
  Geometry::addTet( verts, E, G, F, A, Vector4f( 0,1,0,0.5 ) ) ;
  Geometry::addTet( verts, H, D, F, G, Vector4f( 0.76,0.05,0.18,0.5 ) ) ;
  Geometry::addTet( verts, F, D, A, G, Vector4f( 1,1,0,0.5 ) ) ; // MIDDLE TET
  Geometry::addTet( verts, B, D, A, F, Vector4f( 1,0,0,0.5 ) ) ;
  
}

void pt( const Vector3f& p, float size, const Vector4f& color )
{
  //#define RENDER_AS_POINTS 0
  // RENDER AS PTS:
  if( renderPts )
    verts.push_back( VertexPNC( p, Vector3f(0,1,0), color ) ) ;
  else
    Geometry::addCubeFacingOut( verts, p, size*cubeSize, color ) ;
}

float getVoxel( Vector3i index )
{
  index.wrap( cols,rows,slabs ) ;
  int idex = index.index(cols,rows) ;
  return voxels[idex].v ;
}

Vector3f getP( const Vector3i& dex )
{
  return ( offset + dex ) * gridSizer ;
}

// cut point where you get `tol` units away from isosurface.
bool inSurface( float v )
{
  return v < isosurface ; 
    //isNear( v, isosurface, tol ) ;
    //(isosurface - tol) < v && v < (isosurface+tol) ;
}

// so to avoid COMPLETE fill, you DON'T gen a tet for 
// fully embedded tet that is ALL TOO DEEP
bool tooDeep( float v )
{
  return v < isosurface - tol ; 
}

Vector3f getCutPoint( const Vector3i& A, const Vector3i& B )
{
  float vA = getVoxel( A ) ; // REDUNDANT
  float vB = getVoxel( B ) ; // REDUNDANT
  
  float tAB = unlerp( isosurface, vA, vB ) ;
  if( !isBetween( tAB, 0.f, 1.f ) )
  {
    printf( "ERROR: CUT POINT %f OOB\n", tAB ) ;
    return 0 ;
  }
  Vector3f cutAB = Vector3f::lerp( tAB, getP(A), getP(B) ) ;
  return cutAB ;
}

void cutTet1Out( const Vector3i& A, const Vector3i& B, const Vector3i& C, const Vector3i& D )
{
  // C,D is OUT OF SURFACE.
  // Ge t the 3 cut points
  Vector3f cutAD = getCutPoint( A, D ) ;
  Vector3f cutCD = getCutPoint( C, D ) ;
  Vector3f cutBD = getCutPoint( B, D ) ;

  #if SOLID
  //Geometry::triPrism( verts, getP(A)+Vector3f::random(), getP(B)+Vector3f::random(), getP(C)+Vector3f::random(),
  //  cutAD+Vector3f::random(), cutCD+Vector3f::random(), cutBD+Vector3f::random(), Vector4f::random() ) ;
  Geometry::triPrism( verts, getP(A), getP(B), getP(C),   cutAD, cutCD, cutBD, Blue ) ;
  #else
  ////Geometry::addTriWithNormal( verts, getP(A), getP(B), getP(C), Blue ) ;
  Geometry::addTriWithNormal( verts, cutAD, cutCD, cutBD, Blue ) ; // SHOW ONLY THE CUT FACE
  #endif
}

// ABC wound CCW, D is out.
void cutTet2Out( const Vector3i& A, const Vector3i& B, const Vector3i& C, const Vector3i& D )
{
  // D is OUT OF SURFACE.
  // Get the 4 cut points
  Vector3f cutAC = getCutPoint( A, C ) ;
  Vector3f cutAD = getCutPoint( A, D ) ;
  Vector3f cutBC = getCutPoint( B, C ) ;
  Vector3f cutBD = getCutPoint( B, D ) ;

  //Geometry::triPrism( verts, getP(B)+Vector3f::random(), cutBD+Vector3f::random(), cutBC+Vector3f::random(),
  //  getP(A)+Vector3f::random(), cutAC+Vector3f::random(), cutAD+Vector3f::random(), Vector4f::random() ) ;
  
  #if SOLID
  Geometry::triPrism( verts, getP(B), cutBD, cutBC,   getP(A), cutAC, cutAD, Blue ) ;
  #else
  //Geometry::addTriWithNormal( verts, getP(A), getP(B), getP(C), Blue ) ;
  Geometry::addTriWithNormal( verts, cutAC, cutBC, cutBD, Blue ) ;
  Geometry::addTriWithNormal( verts, cutAC, cutBD, cutAD, Blue ) ;
  #endif
}

void cutTet3Out( const Vector3i& A, const Vector3i& B, const Vector3i& C, const Vector3i& D )
{
  // A is in. the rest are out
  Vector3f cutAB = getCutPoint( A, B ) ;
  Vector3f cutAC = getCutPoint( A, C ) ;
  Vector3f cutAD = getCutPoint( A, D ) ;

  #if SOLID
  //Geometry::addTet( verts, getP(A)+Vector3f::random(), cutAB+Vector3f::random(), 
  //  cutAC+Vector3f::random(), cutAD+Vector3f::random(), Vector4f::random() ) ;
  Geometry::addTet( verts, getP(A), cutAB, cutAC, cutAD, Blue ) ;
  #else
  Geometry::addTriWithNormal( verts, cutAB, cutAD, cutAC, Blue ) ;
  #endif
}

void tet( const Vector3i& A, const Vector3i& B, const Vector3i& C, const Vector3i& D )
{
  float vA = getVoxel( A ) ;
  float vB = getVoxel( B ) ;
  float vC = getVoxel( C ) ;
  float vD = getVoxel( D ) ;
  
  if( inSurface( vA ) && inSurface( vB ) && inSurface( vC ) && inSurface( vD ) )
  {
    // REMOVE to just have a shell.
    //if( !tooDeep( vA ) && tooDeep( vB ) && tooDeep( vC ) && tooDeep( vD ) )
    //Geometry::addTet( verts, getP(A), getP(B), getP(C), getP(D), Vector4f( 0,0,1,1 ) ) ;
  }
  else if( inSurface( vA ) && inSurface( vB ) && inSurface( vC ) )
    cutTet1Out( A, B, C, D ) ;
  else if( inSurface( vA ) && inSurface( vD ) && inSurface( vB ) )
    cutTet1Out( A, D, B, C ) ;
  else if( inSurface( vB ) && inSurface( vD ) && inSurface( vC ) )
    cutTet1Out( B, D, C, A ) ;
  else if( inSurface( vA ) && inSurface( vC ) && inSurface( vD ) )
    cutTet1Out( A, C, D, B ) ;

  // 2 in
  else if( inSurface( vA ) && inSurface( vB ) )
    cutTet2Out( A, B, C, D ) ;
  // ASSUMING ORDER DOESN'T MATTER.  IF BACKWARDS TURN AROUND.
  else if( inSurface( vA ) && inSurface( vC ) )
    cutTet2Out( A, C, D, B ) ;
  else if( inSurface( vA ) && inSurface( vD ) )
    cutTet2Out( A, D, B, C ) ;
  else if( inSurface( vB ) && inSurface( vC ) )
    cutTet2Out( B, C, A, D ) ;
  else if( inSurface( vB ) && inSurface( vD ) )
    cutTet2Out( B, D, C, A ) ;
  else if( inSurface( vC ) && inSurface( vD ) )
    cutTet2Out( C, D, A, B ) ;
  
  else if( inSurface( vA ) )
    cutTet3Out( A, B, C, D ) ;
  else if( inSurface( vB ) )
    cutTet3Out( B, A, D, C ) ;
  else if( inSurface( vC ) )
    cutTet3Out( C, A, B, D ) ;
  else if( inSurface( vD ) )
    cutTet3Out( D, B, A, C ) ;
}


// Neighbours are in the order a facing out tri should be wound
// I only need to know these to gen an isosurface.
//    2----6
//   /|   /|
//  3-0--7 4
//  |/   |/
//  1----5

// These are CW faces, LEFT, TOP, RIGHT.
// If you wind a face using the order here, the face will be facing INTO the cube.
int adj[8][3] = {
  { 4,2,1 },{ 0,3,5 },{ 3,0,6 },{ 7,1,2 },
  { 5,6,0 },{ 1,7,4 },{ 2,4,7 },{ 6,5,3 }
} ; // each of the 8 verts has 4 neighbours. always.

void cornerTri( Vector3i* pts, int a, bool rev, Vector4f color )
{
  // assumes cut1, cut2, cut3 specifies a CCW face.

  //!! THERE IS A PROBLEM HERE!!!   when a is a "TOP" tri, its verts are
  // specified 0(left),1(bottom),2(right) which winds a CW face facing the vertx in quesiton.
  //if(a==2||a==3||a==6||a==7)  color=Vector4f(1,1,1,1) ;
  //else  color=Yellow;
  //!! Actually the above lines show that it ISN'T a problem.
  // But I don't know why.
  Vector3f cut1 = getCutPoint( pts[a], pts[ adj[a][0] ] ) ;
  Vector3f cut2 = getCutPoint( pts[a], pts[ adj[a][1] ] ) ;
  Vector3f cut3 = getCutPoint( pts[a], pts[ adj[a][2] ] ) ;

  if( !rev )
    Geometry::addTriWithNormal( verts, cut2,cut1,cut3, color ) ; //So,
    // the default winding is 0,1,2, which is LEFT, UP, RIGHT.
    // If i'm the vertex, then the tri I draw (left,up,right) is CW
    // so its FACING AWAY from me.  But I want the default to have
    // the tri face THE PIONT IN QUESTION.  So its reversed here ;).
  else // REVERSED WINDING ORDER
    Geometry::addTriWithNormal( verts, cut1,cut2,cut3, color ) ;

}

// 2 inversion tricks for making sure the face wound is ccw
void benchReady( Vector3i* pts, int ia, int ib, vector<int>& nia, vector<int>& nib )
{
  // This is the "trick" that makes the benches orient the right way (CCW).
  // if ia is 0, (b is at a's left), or 2 (b is at a's right) then the quad spun below will be the right way.

  // but basically WHEN a's neighbour (b) is on the UP axis,
  // you have to SWAP the default order of the 2 that are NOT
  // b.
  // modulus did this automagically, but I prefer this way.
  
  if( ia == 1 )
    swap(nia[0],nia[1]) ;

  // For the 2nd part, this checking MUST be done with the CONTEXT OF THE POINTS.

  // I will be drawing a quad on the cutpoints.
  // `a` has 2 adjacent pts in nia, and so does b in nib.
  // So I don't get a "butterfly" (twisted quad),
  // If the LAST 2 don't share an edge, make them.
  // This ensures that they are on the same face,
  if( !pts[nia[1]].twoEqual(pts[nib[1]]) )
    swap(nib[0],nib[1]);
}

// nia and nib produced by adjacencyOf2()
void benchTris( Vector3i* pts, int a, int b, int &ia, int &ib, vector<int> &nia, vector<int> &nib, bool rev, const Vector4f& color ) 
{
  benchReady( pts, ia, ib, nia, nib ) ;

  Vector3f cutA1 = getCutPoint( pts[ a ], pts[ adj[ a ][nia[0]] ] ) ;
  Vector3f cutA2 = getCutPoint( pts[ a ], pts[ adj[ a ][nia[1]] ] ) ;
  Vector3f cutB1 = getCutPoint( pts[ b ], pts[ adj[ b ][nib[0]] ] ) ;
  Vector3f cutB2 = getCutPoint( pts[ b ], pts[ adj[ b ][nib[1]] ] ) ;

  if( !rev )
    Geometry::addQuadWithNormal( verts, cutA1,cutB1,cutB2,cutA2, color ) ;  

  else // REVERSED WINDING ORDER
    Geometry::addQuadWithNormal( verts, cutA1,cutA2,cutB2,cutB1, color ) ;

} ;



// tells you if a&b are adjacent to each other.
// returns the INDEX of the adjacency for a and b.
// Does not need/know about the actual pts values (though that is needed actually for
// full determination)
int adjacencyOf2( Vector3i* pts, int a, int b, bool revs )
{
  int ia=-1, ib=-1 ;
  vector<int> nia, nib;

  for( int i = 0 ; i < 3 ; i++ )
  {
    if( adj[a][i] == b )  ia=i; // which index in your adj list is `b`?
    else  nia.push_back( i ) ;  // `i` is an adjacent edge that is NOT `b`
    
    if( adj[b][i] == a )  ib=i;
    else  nib.push_back( i ) ;
  }

  if( ia != -1 )
  {
    benchTris( pts, a,b, ia,ib, nia,nib, revs, Blue ) ;
    return 1;
  }
  else
  {
    // they're not adjacent. 2 edge tris
    cornerTri( pts, a, revs, Blue ) ;
    cornerTri( pts, b, revs, Blue ) ;
    return 0 ;
  }
}

// This is a token pasting macro that performs the
// 3 required swaps (a, ia, and nia).  This means
// the naming convention cannot change.
// People will complain, but using this macro helps avoid typos
// and see what the code is doing
#define SWAP( a,b ) swap(a,b), i##a.swap(i##b), ni##a.swap(ni##b)

// forces them to SHARE the index in question.
// This is done to fix winding order.
// NON is the index NOT to be on
// ONI is the index TO be on.
// You only have to do this when both nia and nib are of SIZE 2,
// and they share a vertex index, but you're not sure what index its on.
void forceShare( int a, int b, vector<int>& nia, vector<int>& nib, int NON, int ONI )
{
  if( nia.size() != 2 || nib.size() != 2 )
  {
    error( "nia=%d, nib=%d, must be 2,2", nia.size(), nib.size() ) ;
    return ;
  }
  // Equality checks the vertex index is the same here.
  if( adj[ a ][ nia[NON] ] == adj[ b ][ nib[NON] ] ) // shared vertex was index NON
    swap( nia[NON], nia[ONI] ), swap( nib[NON], nib[ONI] ) ; // make it index ONI for both.
  else if( adj[ a ][ nia[NON] ] == adj[ b ][ nib[ONI] ] ) // nia was wrong
    swap( nia[NON], nia[ONI] ) ;
  else if( adj[ a ][ nia[ONI] ] == adj[ b ][ nib[NON] ] ) // nib was wrong
    swap( nib[NON], nib[ONI] ) ;
}

// >0 (+ve) means on + side of plane
// =0 means in plane
// <0 (-ve) means on - side of plane (side opposite normal)
int planeSide( Vector3i* pts, int a, int b, int c, const Vector3i& testPoint )
{
  Vector3i AB = pts[b] - pts[a] ;
  Vector3i AC = pts[c] - pts[a] ;
  Vector3i n = AB.cross( AC ) ;
  int dPlane = -n.dot( pts[a] ) ;
  return n.dot( testPoint ) + dPlane ;
}

// I will tell you the adjacency of 3 verts
int adjacencyOf3( Vector3i *pts, int& a, int& b, int& c, bool revs )
{
  vector<int> ia, ib, ic, nia, nib, nic ;

  // there are 3 possibilities:
  // 1) all 3 are off by themselves
  //   RET 0, NO ADJACENCY

  // 2) 2 share an edge, 1 is off by itself.
  //   RET 1, ia[0] and ib[0] have the adjacent tris.  c is the one off by itself.

  // 3) all 3 are adjacent.  this happens iff all 3 are on the same FACE.
  //   RET 2, a is the CENTER, b & c are the sides.
  
  // if `a` has `b` as its `ith` adjacency, save that info
  // So evaluate each of the items in my adjacency list.
  // For a, is the 0th adjacent vertex B or C?  If it is,
  // then save taht info.

  // IA contains adjacencies to a THAT ARE other pts (ie b or c)
  // NIA contains adjacencies that ARE NOT other pts b or c.
  // ia.size() + nia.size() always == 3
  for( int i = 0 ; i < 3 ; i++ )
  {
    if( adj[a][i] == b || adj[a][i] == c )
      ia.push_back( i ) ;   // adj[a][i] IS either b or c.
    else
      nia.push_back( i ) ;  // adj[a][i] is NOT b or c.

    if( adj[b][i] == a || adj[b][i] == c )
      ib.push_back( i ) ;
    else
      nib.push_back( i ) ;

    if( adj[c][i] == a || adj[c][i] == b )
      ic.push_back( i ) ;
    else
      nic.push_back( i ) ;
  }

  // Now evaluate the results
  if( ib.size() == 2 )
  {
    SWAP( a, b ) ;
  }
  else if( ic.size() == 2 )
  {
    SWAP( a, c ) ;
  }
  
  // WIND TRIS.
  if( ia.size() == 2 )
  {
    // they all have adj, ib and ic have 2 adj pts
    // ia is the center.
    // the other 2 must have their _2nd_ point, the shared point.
    // b's adjacent 2nd should be the same as c's adjacent 2nd.
    forceShare( b,c, nib, nic, 0, 1 ) ;
    
    // must use cross product.  too many combinations
    // This forces B to be the CCW neighbour of A (and never C).
    Vector3i AB = pts[b] - pts[a] ;
    Vector3i AC = pts[c] - pts[a] ;
    Vector3i n = AB.cross( AC ) ;
    int dPlane = -n.dot( pts[a] ) ;
    if( n.dot( pts[ adj[a][ nia[0] ] ] ) + dPlane > 0 )
    {
      SWAP( b,c ) ;
    }

    Vector3f cutA  = getCutPoint( pts[ a ], pts[ adj[a][ nia[0] ] ] ) ;
    Vector3f cutB1 = getCutPoint( pts[ b ], pts[ adj[b][ nib[0] ] ] ) ;
    Vector3f cutB2 = getCutPoint( pts[ b ], pts[ adj[b][ nib[1] ] ] ) ;
    Vector3f cutC1 = getCutPoint( pts[ c ], pts[ adj[c][ nic[0] ] ] ) ;
    Vector3f cutC2 = getCutPoint( pts[ c ], pts[ adj[c][ nic[1] ] ] ) ;
      
    if( !revs )
    {
      Geometry::addPentagonWithNormal( verts, cutA, cutB1, cutB2, cutC2, cutC1, Blue ) ;
    }
    else
    {
      Geometry::addPentagonWithNormal( verts, cutA, cutC1, cutC2, cutB2, cutB1, Blue ) ;
    }

    return 2 ;
  }
  
  // 1
  else if( ia.size() == 1 )
  {
    if( !ib.size() )
    {
      SWAP( b,c ) ;
    }
  }
  else if( ib.size() == 1 || ic.size() == 1 )
  {
    if( !ia.size() )
    {
      // a got stuck as the loner. make c the loner
      SWAP( a,c ) ;
    }
  }

  if( ia.size() == 1 )
  {
    // 
    benchTris( pts, a, b, ia[0], ib[0], nia, nib, revs, Blue ) ;
    cornerTri( pts, c, revs, Blue ) ;
  }
  else // ia.size() == 0
  {
    cornerTri( pts, a, revs, Blue ) ;
    cornerTri( pts, b, revs, Blue ) ;
    cornerTri( pts, c, revs, Blue ) ;
  }

  return ia.size() ; // NO ADJACENCY.
}

int adjacencyOf4( Vector3i* pts, int& a, int& b, int& c, int& d,
  vector<int> &ia, vector<int> &ib, vector<int> &ic, vector<int> &id, 
  vector<int> &nia, vector<int> &nib, vector<int> &nic, vector<int> &nid )
{
  for( int i = 0 ; i < 3 ; i++ )
  {
    if( adj[a][i] == b || adj[a][i] == c || adj[a][i] == d )
      ia.push_back( i ) ;
    else  nia.push_back( i ) ;

    if( adj[b][i] == a || adj[b][i] == c || adj[b][i] == d )
      ib.push_back( i ) ;
    else  nib.push_back( i ) ;

    if( adj[c][i] == a || adj[c][i] == b || adj[c][i] == d )
      ic.push_back( i ) ;
    else  nic.push_back( i ) ;

    if( adj[d][i] == a || adj[d][i] == b || adj[d][i] == c )
      id.push_back( i ) ;
    else  nid.push_back( i ) ;
  }
  
  if( ia.size() == 2 && ib.size() == 2 && ic.size() == 2 && id.size() == 2 )
  {
    // then CASE 5:  all are on same FACE, each with 2 adjacent neighbours.
    
    // Force b,d to be neighbours of a
    if( adj[a][ia[0]]==c || adj[a][ia[1]]==c )
    {
      SWAP(d,c) ; // swapping b & c didn't work (resulted in all criss cross)
      // ia now out of date, but we don't use it below.
    }

    // Make B ccw neighbour 
    Vector3i AB = pts[b] - pts[a] ;
    Vector3i AD = pts[d] - pts[a] ;
    Vector3i n = AB.cross( AD ) ;
    int dPlane = -n.dot( pts[a] ) ;

    if( n.dot( pts[ adj[a][ nia[0] ] ] ) + dPlane > 0 )
    {
      SWAP(b,d);
    }

    Vector3f cutA = getCutPoint( pts[ a ], pts[ adj[a][ nia[0] ] ] ) ;
    Vector3f cutB = getCutPoint( pts[ b ], pts[ adj[b][ nib[0] ] ] ) ;
    Vector3f cutC = getCutPoint( pts[ c ], pts[ adj[c][ nic[0] ] ] ) ;
    Vector3f cutD = getCutPoint( pts[ d ], pts[ adj[d][ nid[0] ] ] ) ;
      
    Geometry::addQuadWithNormal( verts, cutA,cutB,cutC,cutD, Blue ) ;
    return 5 ; // DONE
  }

  // Rearrangement:
  // `a` must be the one with 3.
  else if( ib.size() == 3 )  SWAP( a,b );
  else if( ic.size() == 3 )  SWAP( a,c );
  else if( id.size() == 3 )  SWAP( a,d ) ;

  // 
  if( ia.size() == 3 )
  {
    // CASE 4:
    // one has 3 neighbours.  that means you get a diagonal cut across the cube
    // in the shape of a hexagon.
    
    // put b on edge 0, c on edge 1, d on edge 2, then the winding order will always be correct

    // You don't have to swap.  Just use the INDEX AT EDGE 0,
    //Vector3f cutA = getCutPoint( pts[ adj[a][ ia[0] ] ], pts[ adj[ adj[a][ ia[0] ] ][ nia[0]X ] ] ) ; // not going to work
    // so well b/c I need to know whether to use NIA or NIB.
    Vector4f color=Blue ;

    //if( adj[a][ia[0]]==b && adj[a][ia[1]]==c && adj[a][ia[2]]==d )
    //  color = Blue ;

    //ia[1] is b but b must be at ia[0].
    if( adj[a][ia[1]] == b )
    {
      // b will switch with somebody (whoever's at 0).
      // For my named symbols to work here, I have to check each by name.
      // LEFT=adj[a][ ia[0] ] gets me the pt index for WHOEVER is adj[a] on left of a,
      // BUT I'D ALSO NEED the ni* for LEFT.
      // if nia,nib,nic are abstracted into arrays of arrays, then numeric indexing is possible.

      // who dat at 0 then
      if( adj[a][ia[0]] == c )  SWAP(b,c) ;
      else if( adj[a][ia[0]] == d )  SWAP(b,d) ;
      
      //swap(ia[0],ia[1]) ; // WRONG! XXdo not let ia fall out of date, I will be using it again later.
    }
    else if( adj[a][ia[2]] == b )
    {
      // who dat at 0 then
      if( adj[a][ia[0]] == c )
      {
        SWAP(b,c) ;
        //SWAP(c,d) ;
        color = Blue ;
      }
      else if( adj[a][ia[0]] == d )
      {
        SWAP(b,d) ;
      }
    }

    // 2 possibilities now:
    // 012
    // bcd
    // bdc
    // force c at 1.  c cannot be at 0 b/c b is there already
    if( adj[a][ia[2]] == c )
    {
      // d must be in c's spot @1.
      SWAP(c,d) ;
      //color = Purple ;
    }

    // c's shared vertex with b should be index 0 for both,
    forceShare( b,c, nib,nic, 1,0 ) ; // BC, CB both use 0

    // then let c's shared index with d be index 1 for both.
    forceShare( c,d, nic,nid, 0,1 ) ; // CD, DC both use 1

    // Now they're ordered in the correct order.  BCD is CCW triangle
    // around A, so wind accordingly
    Vector3f cutBC = getCutPoint( pts[b], pts[ adj[b][ nib[0] ] ] ) ;
    Vector3f cutBD = getCutPoint( pts[b], pts[ adj[b][ nib[1] ] ] ) ; // 1 by default (the "other" one)
    Vector3f cutCB = getCutPoint( pts[c], pts[ adj[c][ nic[0] ] ] ) ;
    Vector3f cutCD = getCutPoint( pts[c], pts[ adj[c][ nic[1] ] ] ) ; 
    Vector3f cutDC = getCutPoint( pts[d], pts[ adj[d][ nid[1] ] ] ) ;
    Vector3f cutDB = getCutPoint( pts[d], pts[ adj[d][ nid[0] ] ] ) ; // 0 by default
    
    Geometry::addHexagonWithNormal( verts, cutBC,cutBD,cutDB,cutDC,cutCD,cutCB, color ) ;

    return 4 ;
  }

  // Now check for max size of 2.
  if( ib.size() == 2 )  SWAP( a,b ) ;
  else if( ic.size() == 2 )  SWAP( a,c ) ;
  else if( id.size() == 2 )  SWAP( a,d ) ;
  
  if( ia.size() == 2 )
  {
    // 2 situation.
    // case 3: TWISTY FORK:  2 vertices have __2__ neighbours, but there is no loner.

    // get the 2nd one with 2 neighbours to be b if there is one
    if( ic.size() == 2 )
      SWAP( b,c ) ;
    else if( id.size() == 2 )
      SWAP( b,d ) ;

    if( ib.size() == 2 )
    {
      // this is the most "twisted" case.
      // these are surprisingly common.

      //a,b already have 2 neighbours in ia,ib.
      // let a and b see each other on index 0.
      if( adj[a][ia[0]] != b ) swap(ia[0],ia[1]) ;
      if( adj[b][ib[0]] != a ) swap(ib[0],ib[1]) ;

      // let a see c on its edge 1.
      if( adj[a][ia[1]] != c ) SWAP(c,d);

      // a,d share on 0
      //forceShare( a,d, nia,nid, 1,0 ) ; // can't use this, nia[0] is only size 1.
      // make sure nid[0] is a's free vertex
      if( adj[d][nid[0]] != adj[a][nia[0]] )
        swap( nid[0],nid[1] ) ;

      // make sure c's nic[0] is b's free vertex
      if( adj[c][nic[0]] != adj[b][nib[0]] )
        swap( nic[0],nic[1] ) ;

      // check winding
      bool revs = planeSide( pts, b,d,a, pts[c] )>0 ;

      Vector3f cutAD = getCutPoint( pts[ a ], pts[ adj[a][ nia[0] ] ] ) ;
      Vector3f cutC0 = getCutPoint( pts[ c ], pts[ adj[c][ nic[1] ] ] ) ;
      Vector3f cutCB = getCutPoint( pts[ c ], pts[ adj[c][ nic[0] ] ] ) ;
      Vector3f cutBA = getCutPoint( pts[ b ], pts[ adj[b][ nib[0] ] ] ) ;
      Vector3f cutD0 = getCutPoint( pts[ d ], pts[ adj[d][ nid[1] ] ] ) ;
      Vector3f cutDA = getCutPoint( pts[ d ], pts[ adj[d][ nid[0] ] ] ) ; // could also use adj[a][ nia[0] ]

      if( !revs )
        Geometry::addHexagonWithNormal( verts, cutAD, cutC0, cutCB, cutBA, cutD0, cutDA, Blue ) ;
      else
        Geometry::addHexagonWithNormal( verts, cutAD, cutDA, cutD0, cutBA, cutCB, cutC0, Blue ) ;
      return 3 ;
    }

    // case 2: 1 vertex has 2 neighbours (`a`).  ONE LONER (will be `d`).
    else
    {
      if( ib.size() == 0 ) SWAP( b,d ) ;
      else if( ic.size() == 0 ) SWAP( c,d ) ;

      // render the loner
      cornerTri( pts, d, 0, Blue ) ;

      // 
      forceShare( b,c, nib, nic, 0, 1 ) ;
      
      // pts[adj[a][nia[0]] must be on the - side of the plane.
      //Vector3i AB = pts[b] - pts[a] ;
      //Vector3i AC = pts[c] - pts[a] ;
      //Vector3i n = AB.cross( AC ) ;
      //int dPlane = -n.dot( pts[a] ) ;
      //if( n.dot( pts[ adj[a][ nia[0] ] ] ) + dPlane > 0 )
      if( planeSide( pts, a,b,c, pts[ adj[a][nia[0]] ] ) > 0 )
        SWAP( b,c ) ;

      Vector3f cutA  = getCutPoint( pts[ a ], pts[ adj[a][ nia[0] ] ] ) ;
      Vector3f cutB1 = getCutPoint( pts[ b ], pts[ adj[b][ nib[0] ] ] ) ;
      Vector3f cutB2 = getCutPoint( pts[ b ], pts[ adj[b][ nib[1] ] ] ) ;
      Vector3f cutC1 = getCutPoint( pts[ c ], pts[ adj[c][ nic[0] ] ] ) ;
      Vector3f cutC2 = getCutPoint( pts[ c ], pts[ adj[c][ nic[1] ] ] ) ;
      
      Geometry::addPentagonWithNormal( verts, cutA, cutB1, cutB2, cutC2, cutC1, Blue ) ;
      return 2 ;
    }
  }

  if( ia.size() == 1 && ib.size() == 1 && ic.size() == 1 && id.size() == 1 )
  {
    // 2 benches.
    // RARE.
    // identify which share an edge.
    Vector4f color=Blue ;
    if( adj[a][ia[0]] == b )
    {
      // a--b
      //
      // c--d
      benchTris( pts, a,b, ia[0],ib[0], nia,nib, 0, color ) ;
      benchTris( pts, c,d, ic[0],id[0], nic,nid, 0, color ) ;
    }
    else if( adj[a][ia[0]] == c )
    {
      // a--c
      //
      // b--d
      benchTris( pts, a,c, ia[0],ic[0], nia,nic, 0, color ) ;
      benchTris( pts, b,d, ib[0],id[0], nib,nid, 0, color ) ;
    }
    else if( adj[a][ia[0]] == d )
    {
      // a--d
      //
      // b--c
      benchTris( pts, a,d, ia[0],id[0], nia,nid, 0, color ) ;
      benchTris( pts, b,c, ib[0],ic[0], nib,nic, 0, color ) ;
    }

    return 1 ;
  }

  else
  {
    // RARE.
    // 4 LONERS.  Never revs b/c we used the 4 IN pieces.
    cornerTri( pts, a, 0, Purple ) ;
    cornerTri( pts, b, 0, Purple ) ;
    cornerTri( pts, c, 0, Purple ) ;
    cornerTri( pts, d, 0, Purple ) ;

    return 0 ;
  }
}



void cube( const Vector3i& dex )
{
  //    C----G
  //   /|   /|
  //  D-A--H E
  //  |/   |/
  //  B----F
  Vector3i A=dex+Vector3i(0,0,0), B=dex+Vector3i(0,0,1), C=dex+Vector3i(0,1,0), D=dex+Vector3i(0,1,1),
           E=dex+Vector3i(1,0,0), F=dex+Vector3i(1,0,1), G=dex+Vector3i(1,1,0), H=dex+Vector3i(1,1,1);
  // index: z + 2*y + 4*x (because of binary counting)
  //                  0  1  2  3  4  5  6  7
  Vector3i pts[8] = { A, B, C, D, E, F, G, H } ;
  
  // In the code below, `a`, `b`, `c`, `d` are INDICES of pts in the pts array.
  // `ia` are INDICES into adj[a][ ia[0] ] of the adjacent pts of a
  // THAT ARE ALSO (in or out of the isosurface) along with a.
  // `nia` are INDICES into adj[a][ nia[0] ] of adjacent pts
  // NOT the same isosurface status as `a`.
  
  vector<int> in, out ;
  for( int i = 0 ; i < 8 ; i++ )
    if( inSurface( getVoxel( pts[i] ) ) )
      in.push_back( i ) ; 
    else
      out.push_back( i ) ;

  // now we enumerate those 15 cases.
  // the cases with 2,3,4,5 or 6 in, 
  // have some additional resolution

  // case 0: NOTHING IN SURFACE
  if( !in.size() ) 
    return ;

  // 0 are out
  // case 1: FULLY in surface 
  else if( in.size() == 8 )
  {
    // FILLs
    //Geometry::addCubeFacingOut( verts, getP(A),getP(B),getP(C),getP(D),getP(E),getP(F),getP(G),getP(H), Blue ) ;
  }

  // 1 is out, or 1 is in.
  else if( in.size() == 1 || out.size() == 1 )
  {
    // You get a tri on "halfway" between excluded vert and the excluded verts neighbours.
    // Lookup neighbours.
    // cut from OUTPOINT, to each ADJACENCY OF( outpoint ) (0,1 and 2).
    int a;

    // the isosurface faces OUT.
    // cornerTri winds 0,1,2 (left, UP, right) which means
    // it defaults a CW triangle for adj visitation ordering (0,1,2).
    // If `a` is INSIDE the surface, the surfaces faces AWAY from `a`,
    // so there we reverse the orientation.
    bool revs=0 ;
    if( in.size() == 1 )  a=in[0],revs=1; // you have 1 pt in the isosurface.
    // so revs is 0 because you want the face to face INTO the cube.
    else  a=out[0]; // only 1 vertex OUT.  the cut face
    // faces OUT of rest of the cube.

    cornerTri( pts, a, revs, Blue ) ;
  }

  // If SAME_FACE is false, they don't even share a face at all.
  //#define SAME_FACE(i1,i2) (pts[i1].atLeastOneEqual(pts[i2]))
  //#define SAME_EDGE(i1,i2) (pts[i1].twoEqual(pts[i2]))
  else if( in.size() == 2 || out.size() == 2 )
  {
    // these 2 pts could be adjacent, same face..
    int a,b ;
    bool revs=0;
    if( in.size() == 2 )
      a=in[0],b=in[1],revs=1;
    else // out.size()==2
      a=out[0],b=out[1];
    adjacencyOf2( pts, a, b, revs ) ;
  }

  else if( in.size() == 3 || out.size() == 3 )
  {
    int a,b,c;
    bool revs=0;
    if( in.size()==3 )
    {
      a=in[0],b=in[1],c=in[2],revs=1;
    }
    else //out.size()==3
    {
      a=out[0],b=out[1],c=out[2]; //def render tris face pts. so face out don't reverse.
    }

    adjacencyOf3( pts, a,b,c, revs ) ;
  }
  
  else if( in.size()==4 || out.size()==4 ) // same test really
  {
    // 4 are in and 4 are out.
    int a=out[0],b=out[1],c=out[2],d=out[3]; // render with surfacese
    // facing towards the out points
    vector<int> ia,ib,ic,id, nia,nib,nic,nid ;
    adjacencyOf4( pts, a,b,c,d, ia,ib,ic,id, nia,nib,nic,nid ) ;
  }

  else
  {
    error( "UNHANDLED CASE" ) ;
  }
}


void genVizMarchingTets()
{
  for( int k = 0 ; k < slabs ; k++ )
  {
    for( int j = 0 ; j < rows ; j++ )
    {
      for( int i = 0 ; i < cols ; i++ )
      {
        Vector3i dex( i,j,k ) ;
        Vector3i A=dex+Vector3i(0,0,0), B=dex+Vector3i(0,0,1), C=dex+Vector3i(0,1,0), D=dex+Vector3i(0,1,1),
                 E=dex+Vector3i(1,0,0), F=dex+Vector3i(1,0,1), G=dex+Vector3i(1,1,0), H=dex+Vector3i(1,1,1);
        
        // There are 5 tetrahedra that may be rendered.
        // These are the tetrahedra:
        // A, D, C, G
        // E, G, F, A
        // H, D, F, G
        // F, D, A, G
        // B, D, A, F
        tet( A, D, C, G ) ;
        tet( E, G, F, A ) ;
        tet( H, D, F, G ) ;
        tet( F, D, A, G ) ;
        tet( B, D, A, F ) ;
      }
    }
  }

  
}

void genVizMarchingCubes()
{
  for( int k = 0 ; k < slabs ; k++ )
  {
    for( int j = 0 ; j < rows ; j++ )
    {
      for( int i = 0 ; i < cols ; i++ )
      {
        Vector3i dex( i,j,k ) ;
        cube(dex);
      }
    }
  }

  // now smooth the normals.
  iVerts.clear() ;
  indices.clear() ;

  // first I merge into an index buffer.
  for( int i = 0 ; i < verts.size() ; i++ )
  {
    int jindex = -1 ;
    for( int j = 0 ; j < iVerts.size() && jindex==-1 ; j++ )
      if( iVerts[j].pos.isNear( verts[i].pos, EPS ) )
        jindex = j ; // verts[i] already exists at jindex
    
    if( jindex==-1 )
    {
      iVerts.push_back( verts[i] ) ; // another vertex
      indices.push_back( iVerts.size() - 1 ) ;  // 
    }
    else
    {
      iVerts[jindex].normal += verts[i].normal ;
      indices.push_back( jindex ) ; // there is another index.
    }
  }
  for( int i = 0 ; i < iVerts.size() ; i++ )
    iVerts[i].normal.normalize() ;

  // parallel array of NEIGHBOUR structure, tells you:
  // WHAT EDGE YOU'RE ON (if you are on an edge)
  // and WHO YOUR NEIGHBOUR IS
  // Neighbour is actually a bad name for this structure
  // more like Isopoint or SamePoint -- the LAST PT on the mesh
  // is exactly the same as the 1st pt, only translated +worldSize in x,y, or z
  struct Neighbour
  {
    enum NEdge{ NOTHING, NX, PX, NY, PY, NZ, PZ } ;
    int edgeSide ;  //which edgeside is the point on?
    int axisIndex ; //0 for x, 1 for y, 2 for z
    int neighbour ; // WHO IS MY NEIGHBOUR?  he is always on the opposite edgeside.

    Neighbour(){
      edgeSide = NOTHING ;
      neighbour=-1; // no neighbour
      axisIndex=-1;
    }

    inline int otherAxis1(){ return (axisIndex+1)%3; }
    inline int otherAxis2(){ return (axisIndex+2)%3; }
  } ;

  // offset to use on THE POTENTIAL NEIGHBOURS to bring them to my side.
  // my neighbours satisfy NEIGHBOURPOS+NeighbourOffsets[MYEDGESIDE] = MYPOS.
  Vector3f NeighbourOffsets[] = { Vector3f( 0,0,0 ),
    Vector3f( -worldSize,0,0 ), Vector3f( worldSize,0,0 ), 
    Vector3f( 0,-worldSize,0 ), Vector3f( 0,worldSize,0 ), 
    Vector3f( 0,0,-worldSize ), Vector3f( 0,0,worldSize )  } ;

  vector<Neighbour> iVertsNeighbours ;
  iVertsNeighbours.resize( iVerts.size() ) ;
  vector<int> pxEdges,pyEdges,pzEdges,nxEdges,nyEdges,nzEdges ;

  for( int i = 0 ; i < iVerts.size() ; i++ )
  {
    // This pt is ON THE NX WALL.
    if( isNear( iVerts[i].pos.x, -worldSize/2, EPS ) ) {
      iVertsNeighbours[i].edgeSide = Neighbour::NX ;
      iVertsNeighbours[i].axisIndex = 0 ;
      nxEdges.push_back( i ) ;
      //iVerts[i].color=Red*0.5;
    }
    else if( isNear( iVerts[i].pos.x, worldSize/2, EPS ) ) {
      iVertsNeighbours[i].edgeSide = Neighbour::PX ;
      iVertsNeighbours[i].axisIndex = 0 ;
      pxEdges.push_back( i ) ;
      //iVerts[i].color=Red;
    }

    if( isNear( iVerts[i].pos.y, -worldSize/2, EPS ) ) {
      iVertsNeighbours[i].edgeSide = Neighbour::NY ;
      iVertsNeighbours[i].axisIndex = 1 ;
      nyEdges.push_back( i ) ;
      //iVerts[i].color=Green*.5;
    }
    else if( isNear( iVerts[i].pos.y, worldSize/2, EPS ) ) {
      iVertsNeighbours[i].edgeSide = Neighbour::PY ;
      iVertsNeighbours[i].axisIndex = 1 ;
      pyEdges.push_back( i ) ;
      //iVerts[i].color=Green;
    }

    if( isNear( iVerts[i].pos.z, -worldSize/2, EPS ) ) {
      iVertsNeighbours[i].edgeSide = Neighbour::NZ ;
      iVertsNeighbours[i].axisIndex = 2 ;
      nzEdges.push_back( i ) ;
      //iVerts[i].color=Purple*.5;
    }
    else if( isNear( iVerts[i].pos.z, worldSize/2, EPS ) ) {
      iVertsNeighbours[i].edgeSide = Neighbour::PZ ;
      iVertsNeighbours[i].axisIndex = 2 ;
      pzEdges.push_back( i ) ;
      //iVerts[i].color=Purple;
    }
  }

  // So now I know what side you're on.  Now get your neighbour.
  for( int i = 0 ; i < iVerts.size() ; i++ )
  {
    // which collection to search?
    vector<int>* nCollection = &pxEdges ;
    switch( iVertsNeighbours[i].edgeSide )
    {
      // Look in PX for my neighbour.
      case Neighbour::NX:  nCollection = &pxEdges ;  break ;
      case Neighbour::PX:  nCollection = &nxEdges ;  break ;
      case Neighbour::NY:  nCollection = &pyEdges ;  break ;
      case Neighbour::PY:  nCollection = &nyEdges ;  break ;
      case Neighbour::NZ:  nCollection = &pzEdges ;  break ;
      case Neighbour::PZ:  nCollection = &nzEdges ;  break ;
    }

    // which value?
    for( int j = 0 ; j < nCollection->size() ; j++ )
      if( iVerts[i].pos.isNear( iVerts[ (*nCollection)[j] ].pos + NeighbourOffsets[ iVertsNeighbours[i].edgeSide ], EPS ) )
        iVertsNeighbours[i].neighbour = (*nCollection)[j];
  }
  
  // show neighbours
  //for( int i = 0 ; i < iVertsNeighbours.size() ; i++ )
  //{
  //  if( iVertsNeighbours[i].neighbour != -1 )
  //  {
  //    printf( "Vertex %d is neighboured to %d\n", i, iVertsNeighbours[i].neighbour ) ;
  //    iVerts[i].color=iVerts[iVertsNeighbours[i].neighbour].color=Vector4f::random();
  //  }
  //}
  
  // Now we can downsample the mesh.
  // You can only merge EDGES.
  // attempt to reduce small triangles to degeneracy (actually sharing all 3 pts)
  for( int i = 0 ; i < indices.size() ; i+=3 )
  {
    // 3 edges per tri (group of 3 verts)
    for( int eNo=0 ; eNo < 3 ; eNo++ )
    {
      // [0,1], [1,2], [2,0]
      int i1 = indices[i + eNo] ;
      int i2 = indices[i + (eNo+1)%3] ;

      Vector3f edge = iVerts[i1].pos-iVerts[i2].pos ;
      
      // tolerance is the max size edge to collapse
      if( edge.len() < tol )
      {
        // CHANGE first vertex's value to new avg value
        Vector3f newPt = ( iVerts[i1].pos + iVerts[i2].pos ) / 2 ;
        Vector3f newNormal = ( iVerts[i1].normal + iVerts[i2].normal ).normalize() ;

        // if a collapsed edge has a neighbour,
        // then you must also collapse the neighbour
        // and lock it against its wall
        
        Neighbour &n1=iVertsNeighbours[i1], &n2=iVertsNeighbours[i2];
        if( n1.neighbour != -1 && n2.neighbour != -1 )
        {
          // you are merging an edge that sits on the same endwall
          // The whole EDGE is on a neighbour border.  that means
          // you have to merge BOTH verts at the other side as well.
          // (BUT, this also means that those 2 verts will merge on their own too).
          //iVerts[n1.neighbour].color = iVerts[n2.neighbour].color = Orange;
        }
        
        if( n1.neighbour != -1 )
        {
          // newPt needs to be NAILED to the axis that i1 was original against.
          // If I am on the NX wall, then my x value MUST be nailed at -worldSize/2 IN X.
          //printf( "i1's original axisIndex val %f, making it %f\n",
          //  newPt.elts[ n1.axisIndex ],
          //  NeighbourOffsets[n1.edgeSide].elts[ n1.axisIndex ]/2 ) ;
          newPt.elts[ n1.axisIndex ] = NeighbourOffsets[n1.edgeSide].elts[ n1.axisIndex ]/2 ;
          //iVerts[n1.neighbour].color = Green ;
        }
        if( n2.neighbour != -1 )
        {
          // if n2 enforces this constraint, so let it.
          newPt.elts[ n2.axisIndex ] = NeighbourOffsets[n2.edgeSide].elts[ n2.axisIndex ]/2 ;
          //iVerts[n2.neighbour].color = Red ;
        }

        if( n1.neighbour != -1 )
        {
          // What i'm doing here is forcing the NEIGHBOR at the other side
          // to take the same value as newPt.  This has to be done to prevent
          // breaks in the mesh at the seams.
          iVerts[n1.neighbour].pos = newPt - NeighbourOffsets[n1.edgeSide];
          iVerts[n1.neighbour].normal = newNormal ;
          //iVerts[n1.neighbour].color = Red ;
        }
        if( n2.neighbour != -1 )
        {
          iVerts[n2.neighbour].pos = newPt - NeighbourOffsets[n2.edgeSide];
          iVerts[n2.neighbour].normal = newNormal ;
          //iVerts[n2.neighbour].color = Red ;
        }
        
        iVerts[i1].pos = newPt ;
        iVerts[i1].normal = newNormal ;

        // EVERYBODY that used i2 now uses i1
        for( int j = 0 ; j < indices.size() ; j++ )  if( indices[j] == i2 )  indices[j]=i1 ;
      }
    }
  }

  // You need to REBUILD the mesh now becaue the deleted vertices
  // that are never references STILL TAKE UP SPACES.  deleting them
  // shifts the array, so changes ALL the indices after the deleted elt.
  vector<VertexPNC> rebuiltiVerts ;
  vector<int> rebuiltIndices ;
  
  // if its not referenced it gets left out of the rebuild
  for( int i = 0 ; i < indices.size() ; i++ )
  {
    int jindex = -1 ;

    // Look for a vertex near iVerts[indices[i]]
    for( int j = 0 ; j < rebuiltiVerts.size() && jindex==-1 ; j++ )
      if( rebuiltiVerts[j].pos.isNear( iVerts[ indices[i] ].pos ) )
        jindex = j ; // iVerts[ indices[i] ] already exists at jindex
    
    if( jindex==-1 )
    {
      rebuiltiVerts.push_back( iVerts[ indices[i] ] ) ; // another vertex
      rebuiltIndices.push_back( rebuiltiVerts.size() - 1 ) ;  // 
    }
    else
    {
      //rebuiltiVerts[jindex].normal += iVerts[ indices[i] ].normal ;
      rebuiltIndices.push_back( jindex ) ; // there is another index just into this same vertex
    }
  }

  iVerts.swap( rebuiltiVerts ) ;
  indices.swap( rebuiltIndices ) ;

  // SNAP/force equal the edges
  for( int i = 0 ; i < iVertsNeighbours.size() ; i++ )
  {
  }

  printf( "%d vertices converted to %d vertices and %d indices\n", verts.size(), iVerts.size(), indices.size() ) ;
  
}

void genVizPunchthru()
{
  punchthru.clear() ;
  punchthru.resize( voxels.size() ) ;

  for( int k = 0 ; k < slabs ; k++ )
  {
    for( int j = 0 ; j < rows ; j++ )
    {
      for( int i = 0 ; i < cols ; i++ )
      {
        Vector3i dex( i,j,k ) ;
        int idex = dex.index(cols,rows) ;
        float val = voxels[ idex ].v ;
        
        // Measure the isosurface breaks in 26 directions.
        //punchthru[dex.index(cols,rows)]. ;
        for( int dirIndex = 0 ; dirIndex < IsosurfacePunchthruSet::Directions.size() ; dirIndex++ )
        {
          const Vector3i& dir = IsosurfacePunchthruSet::Directions[dirIndex] ; // GET A DIRECTION. ONE DIRECTION.
          Vector3i adjCell = dex + dir ;    // GET THE IJK INDEX OF THE ADJACENT CELL IN THIS DIRECTION
          adjCell.wrap( cols,rows,slabs ) ; // MAKE SURE IS IN BOUNDS. WRAP AT BORDERS
          float adjVal = voxels[ adjCell.index(cols,rows) ].v ;

          float t = unlerp( isosurface, val, adjVal ) ;
          if( isBetween( t, 0.f, 1.f ) )
          {
            // BROKE THE SURFACE
            float diff = adjVal - val ; //+ if value INCREASES towards adjVal.
            // this is the amount you need to "add" to val to GET adjVal.
            //- if value GOING DOWN
            // like a type of derivative
            punchthru[idex].directedPunchthrus[dirIndex] = new IsosurfacePunchthru( dex, dirIndex, t, diff ) ;
            
            Vector3f voxelCenter = (offset + dex)*gridSizer ;
            Vector3f p2 = (offset + dex + dir)*gridSizer ;
            Vector3f p = Vector3f::lerp( t, voxelCenter, p2 ) ;
            pt( p, 0.25, Vector4f(0.8) ) ;
          }
        }
      }
    }
  }
}

void genVizFromVoxelData()
{
  // Generate the visualization
  verts.clear() ;
  gradients.clear() ;
  debugLines.clear() ;
  tris.clear() ;

  // GENERATE THE VISUALIZATION AS POINTS
  //genVizPunchthru() ;
  
  // ISOSURFACE GENERATION!
  //genVizMarchingTets() ; // not as good
  genVizMarchingCubes() ;
  
}

void genVoxelData()
{
  voxels.resize( cols*rows*slabs ) ;
  
  Vector4f d1, d2 ;
  for( int i = 0 ; i < cols ; i++ )
  {
    for( int j = 0 ; j < rows ; j++ )
    {
      for( int k = 0 ; k < slabs ; k++ )
      {
        int dex = i + j*cols + k*cols*rows ;
        float fx=(float)i/cols, fy=(float)j/rows, fz=(float)k/slabs ;
        
        //voxels[ dex ].v = Perlin::sdnoise( fx*f1, fy*f1, fz*f1, pw, &d1.x, &d1.y, &d1.z, &d1.w ) ;
        //voxels[ dex ].v += Perlin::sdnoise( fx*f2, fy*f2, fz*f2, pw, &d2.x, &d2.y, &d2.z, &d2.w ) ;
        //voxels[ dex ].d = d1 + d2 ;

        voxels[ dex ].v = Perlin::pnoise( fx, fy, fz, pw, 1,1,1, 8 ) ;

        for( int i = 2 ; i <= 4 ; i *= 2 )
          voxels[ dex ].v += Perlin::pnoise( fx*i, fy*i, fz*i, pw, i,i,i, 8 ) ;

        //voxels[ dex ].v = Perlin::noise( fx*f1, fy*f1, fz*f1, pw ) -
        //                  fabsf( Perlin::noise( fx*f2, fy*f2, fz*f2, 10*pw ) ) ; //randFloat() ;
        //voxels[ dex ].v = Perlin::noise( sin(fx), cos(fy), sin(fz), w ) ; //randFloat() ;
      }
    }
  }
}

void genPointsRandomly()
{
  verts.clear() ;
  randPts.clear() ;

  for( int i = 0 ; i < cols*rows*slabs ; i++ )
  {
    Vector3f p = Vector3f::random() ;
    float noise = Perlin::pnoise( p.x,p.y,p.z, pw, 1,1,1, 8 ) ;
    if( isNear( noise, isosurface, 0.01 ) )
      randPts.push_back( (p-.5)*worldSize ) ;
  }

  for( int i = 0 ; i < randPts.size() ; i++ )
  {
    pt( randPts[i], 0.1, Blue ) ; /// also viz here
  }

}

void initDirections()
{
  #if 0
  for( int k = -1 ; k <= 1 ; k++ )
  for( int j = -1 ; j <= 1 ; j++ )
  for( int i = -1 ; i <= 1 ; i++ )
    IsosurfacePunchthruSet::Directions.push_back( Vector3i( i, j, k ) ) ;
  #else
  IsosurfacePunchthruSet::addDirection( Vector3i( 1, 0, 0 ) ) ;  //0
  IsosurfacePunchthruSet::addDirection( Vector3i( 0, 1, 0 ) ) ;  //1
  IsosurfacePunchthruSet::addDirection( Vector3i( 0, 0, 1 ) ) ;  //2
  
  IsosurfacePunchthruSet::addDirection( Vector3i( -1,  0,  0 ) ) ; //3
  IsosurfacePunchthruSet::addDirection( Vector3i(  0, -1,  0 ) ) ; //4
  IsosurfacePunchthruSet::addDirection( Vector3i(  0,  0, -1 ) ) ; //5

  // UPPER NEIGHBOR, LEFT NEIGHBOUR,  LEFT NEIGHBOUR, LOWER NEIGHBOUR
  int px[4] = { 1, 2,  2, 4 } ;
  IsosurfacePunchthruSet::addNeighbours( 0, px, 4 ) ;
  
  // y has NO NEIGHBOURS listed.
  
  int pz[4] = { 1, 3,  3, 4 } ;
  IsosurfacePunchthruSet::addNeighbours( 2, pz, 4 ) ;

  int nx[4] = { 1, 5,  5, 4 } ;
  IsosurfacePunchthruSet::addNeighbours( 3, nx, 4 ) ;

  int nz[4] = { 1, 0,  0, 4 } ;
  IsosurfacePunchthruSet::addNeighbours( 5, nz, 4 ) ;

  //IsosurfacePunchthruSet::DirsNeighbours.push_back( 
  #endif
  for( int i = 0 ; i < IsosurfacePunchthruSet::Directions.size() ; i++ )
  {
    Vector3i p = IsosurfacePunchthruSet::Directions[i] + 1 ; // ADD ONE WHEN GETTING INDEX
    printf( "(%d,%d,%d) index %d\n", p.x, p.y, p.z, p.index( 3, 3 ) ) ;
  }
}

void regen()
{
  offset = Vector3f( -cols/2.f, -rows/2.f, -slabs/2.f ) ;
  gridSizer = Vector3f(worldSize) / Vector3f( cols,rows,slabs ) ;

  if( voxelGenMode )
  {
    genVoxelData() ;    
    genVizFromVoxelData() ;
  }
  else
    genPointsRandomly() ;
}

void init() // Called before main loop to set up the program
{
  initDirections() ;
  regen() ;
  
  axis.pos = Vector3f( 0, 0, worldSize ) ;
  glClearColor( 0.1, 0.1, 0.1, 0.1 ) ;
  glEnable( GL_COLOR_MATERIAL ) ;

  //glPointSize(16.f);
  //for( int i = 0 ; i < 3*1000 ; i++ )
  //  verts.push_back( VertexPC( Vector3f::random(-1,1), Vector4f::random() ) ) ;
  
  glEnable( GL_BLEND ) ;
  glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA ) ;
}

void keys()
{
  #define IS_KEYDOWN( c ) (GetAsyncKeyState( c ) & 0x8000)
  if( IS_KEYDOWN( 'W' ) )
  {
    axis.pos += axis.forward * speed ;
  }
  
  if( IS_KEYDOWN( 'S' ) )
  {
    axis.pos -= axis.forward * speed ;
  }

  if( IS_KEYDOWN( 'A' ) )
  {
    axis.pos -= axis.right * speed ;
  }

  if( IS_KEYDOWN( 'D' ) )
  {
    axis.pos += axis.right * speed ;
  }

  if( IS_KEYDOWN( 'Q' ) )
  {
    axis.roll( -0.01f ) ;
  }

  if( IS_KEYDOWN( 'E' ) )
  {
    axis.roll( 0.01f ) ;
  }

}

void draw()
{
  keys() ;

  //pw += 0.01f ;
  //genVoxels() ;
  glEnable( GL_DEPTH_TEST ) ;
  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT ) ;

  #if SOLID
  glEnable( GL_CULL_FACE ) ;
  glCullFace( GL_BACK ) ;
  #endif

  glViewport( 0, 0, w, h ) ;
  glMatrixMode( GL_PROJECTION ) ;
  glLoadIdentity();
  //glOrtho( -5, 5, -5, 5, 5, -5 ) ;
  gluPerspective( 45.0, 1.0, 0.5, 1000.0 ) ;
  
  glMatrixMode(GL_MODELVIEW);
  
  glLoadIdentity();
  Matrix4f mat = axis.getViewingMatrix4f() ;
  glMultMatrixf( &mat.m00 ) ;

  //gluLookAt( 0, 0, sbd,   0, 0, 0,   0, 1, 0 ) ;
  //glRotatef( my, 1, 0, 0 ) ;
  //glRotatef( mx, 0, 1, 0 ) ;
  
  glLineWidth( lineWidth ) ;
  drawAxisLines() ;

  glEnable( GL_LIGHTING ) ;

  glEnable( GL_LIGHT0 ) ;
  //glEnable( GL_LIGHT1 ) ;
  //glEnable( GL_LIGHT2 ) ;
  //glEnable( GL_LIGHT3 ) ;

  Vector4f lightPos0, lightPos1, lightPos2, lightPos3 ;
  static float ld = worldSize ;
  lightPos0 = Vector4f(  ld,  ld/2,  ld, 1 ) ;
  lightPos1 = Vector4f(  ld,  ld,  ld, 1 ) ;
  lightPos2 = Vector4f(   0,  ld,   0, 1 ) ;
  lightPos3 = Vector4f( -ld,   0,   0, 1 ) ;
  
  glLightfv( GL_LIGHT0, GL_POSITION, &lightPos0.x ) ;
  glLightfv( GL_LIGHT1, GL_POSITION, &lightPos1.x ) ;
  glLightfv( GL_LIGHT2, GL_POSITION, &lightPos2.x ) ;
  glLightfv( GL_LIGHT3, GL_POSITION, &lightPos3.x ) ;
  
  float white[4] = {1,1,1,1};
  glLightfv( GL_LIGHT0, GL_SPECULAR, white ) ;
  
  glLightfv( GL_LIGHT1, GL_DIFFUSE, white ) ;
  glLightfv( GL_LIGHT2, GL_DIFFUSE, white ) ;
  glLightfv( GL_LIGHT3, GL_DIFFUSE, white ) ;

  glEnableClientState( GL_VERTEX_ARRAY ) ;  CHECK_GL ;
  glEnableClientState( GL_NORMAL_ARRAY ) ;  CHECK_GL ;
  glEnableClientState( GL_COLOR_ARRAY ) ;  CHECK_GL ;
  
  Vector4f spec(1,1,1,25) ;
  glMaterialfv( GL_FRONT_AND_BACK, GL_SPECULAR, &spec.x ) ;
  glMaterialf( GL_FRONT_AND_BACK, GL_SHININESS, spec.w ) ;
  
  if( verts.size() && showCubes )
  {
    int renderMode = renderPts ? GL_POINTS : GL_TRIANGLES ;
    int ptsToRender = verts.size() ;
    if( renderMode == GL_TRIANGLES )
      ptsToRender = ptsToRender - ptsToRender%3 ;// make sure is mult 3
    //glDepthMask( 0 ) ;

    if( !indices.size() )
    {
      // vertex arrays with no index buffer
      glVertexPointer( 3, GL_FLOAT, sizeof( VertexPNC ), &verts[0].pos ) ;
      glNormalPointer( GL_FLOAT, sizeof( VertexPNC ), &verts[0].normal ) ;
      glColorPointer( 4, GL_FLOAT, sizeof( VertexPNC ), &verts[0].color ) ;

      if( repeats )
      {
        for( int i = -1 ; i <= 1 ; i++ )
        {
          for( int j = -1 ; j <= 1 ; j++ )
          {
            glPushMatrix();
            glTranslatef( i*worldSize, j*worldSize,0 ) ;
            glDrawArrays( renderMode, 0, (int)verts.size() ) ;
            glPopMatrix();
          }
        }
      }
      else
        glDrawArrays( renderMode, 0, (int)verts.size() ) ;
    }
    else
    {
      // Use the index buffer
      glVertexPointer( 3, GL_FLOAT, sizeof( VertexPNC ), &iVerts[0].pos ) ;
      glNormalPointer( GL_FLOAT, sizeof( VertexPNC ), &iVerts[0].normal ) ;
      glColorPointer( 4, GL_FLOAT, sizeof( VertexPNC ), &iVerts[0].color ) ;

      if( repeats )
      {
        for( int i = -1 ; i <= 1 ; i++ )
        {
          for( int j = -1 ; j <= 1 ; j++ )
          {
            glPushMatrix();
            glTranslatef( i*worldSize, j*worldSize,0 ) ;
            glDrawElements( renderMode, indices.size(), GL_UNSIGNED_INT, &indices[0] ) ;
            glPopMatrix();
          }
        }
      }
      else
      {
        // draw it once
        glDrawElements( renderMode, indices.size(), GL_UNSIGNED_INT, &indices[0] ) ;
      }
    }
    //glDepthMask( 1 ) ;
  }
  
  glDisableClientState( GL_NORMAL_ARRAY ) ;
  
  if( tris.size() && showTris )
  {
    glVertexPointer( 3, GL_FLOAT, sizeof( VertexPC ), &tris[0].pos ) ;
    glColorPointer( 4, GL_FLOAT, sizeof( VertexPC ), &tris[0].color ) ;
    glDrawArrays( GL_TRIANGLES, 0, (int)tris.size() ) ;
  }

  glDisable( GL_LIGHTING ) ;

  if( showGradients )
  {
    glVertexPointer( 3, GL_FLOAT, sizeof( VertexPC ), &gradients[0].pos ) ;
    glColorPointer( 4, GL_FLOAT, sizeof( VertexPC ), &gradients[0].color ) ;
    glDrawArrays( GL_LINES, 0, (int)gradients.size() ) ;
  }
  if( debugLines.size() )
  {
    glVertexPointer( 3, GL_FLOAT, sizeof( VertexPC ), &debugLines[0].pos ) ;
    glColorPointer( 4, GL_FLOAT, sizeof( VertexPC ), &debugLines[0].color ) ;
    glDrawArrays( GL_LINES, 0, (int)debugLines.size() ) ;
  }
  glDisableClientState( GL_VERTEX_ARRAY ) ;
  glDisableClientState( GL_COLOR_ARRAY ) ;
  


  
  
  
  //TEXT
  glMatrixMode( GL_MODELVIEW ) ;
  glLoadIdentity() ;
  glMatrixMode( GL_PROJECTION ) ;
  glLoadIdentity();
  glDisable( GL_DEPTH_TEST ) ;
  
  glRasterPos2f( -0.95, 0.8 ) ;
  //glRasterPos2i(100, 120);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  char buf[300];
  sprintf( buf, "tris=%d gridSize=%d eps=%.6f w=%.2f isosurface=%.2f tol=%.3f speed=%.2f worldSize=%.2f",
    renderPts?verts.size():(verts.size()/( 3 )), cols,
    EPS, pw, isosurface, tol, speed, worldSize ) ;
  const char * p = buf ;
  do glutBitmapCharacter( GLUT_BITMAP_HELVETICA_18, *p ); while( *(++p) ) ;
  
  glutSwapBuffers();
}

// Called every time a window is resized to resize the projection matrix
void resize( int newWidth, int newHeight )
{
  w = newWidth ;
  h = newHeight ;
}

static int lastX=0, lastY=0 ;
static int mmode=0;
void mouseMotion( int x, int y )
{
  int dx = x-lastX, dy=y-lastY ;
  
  // LMB
  if( mmode == GLUT_LEFT_BUTTON )
  {
    mx += dx, my += dy ;
    axis.yaw( 0.01f*dx ) ;
    axis.pitch( 0.01f*dy ) ;
  }
  else if( mmode == GLUT_RIGHT_BUTTON )
  {
    // dolly
    sbd +=0.1*(-dx+dy) ;
    clamp( sbd, 10, 1000 ) ;
  }
  
  lastX=x,lastY=y;
}

void mouse( int button, int state, int x, int y )
{
  lastX = x ;
  lastY = y ;
  
  mmode=button; // 0 for LMB, 2 for RMB
  //printf( "%d %d %d %d\n", button,state,x,y ) ;
}

void keyboard( unsigned char key, int x, int y )
{
  float diff = 0.01f ;
  
  switch( key )
  {
  case '2':
    {
    int pMode[2];
    glGetIntegerv( GL_POLYGON_MODE, pMode ) ;
    if( pMode[0] == GL_FILL )  glPolygonMode( GL_FRONT_AND_BACK, GL_LINE ) ;
    else  glPolygonMode( GL_FRONT_AND_BACK, GL_FILL ) ;
    }
    break ;

  case '3':
    renderPts = !renderPts ;
    if( voxelGenMode )
      genVizFromVoxelData() ;
    break ;
    
  case '4':
    EPS += 1e-4f ;
    regen() ;
    break;
  case '$':
    EPS -= 1e-4f ;
    regen() ;
    break ;
  case '=':
    pw+=diff;
    regen();
    break;
  
  case '+':
    pw+=10*diff;
    regen() ;
    break;
  
  case '-':
    pw-=diff;
    regen();
    break; 
  
  case '_':
    pw-=10*diff;
    regen();
    break;

  case 'i':
    isosurface += 0.01f ;
    regen();
    break ;

  case 'I':
    isosurface -= 0.01f ;
    regen();
    break ;

  case 'c':
    debugLines.clear() ;
    break ;

  case 'g':
    speed += 0.01f;
    break ;
  case 'G':
    speed -= 0.01f;
    break; 
  case 'h':
    showGradients = !showGradients ;
    break ;

  case 'j':
    worldSize++ ;
    regen() ;
    break;

  case 'J':
    worldSize --;
    regen() ;
    break; 

  // refine the grid
  case 'k':
    cols += 1 ;
    rows += 1 ;
    slabs += 1 ;
    regen() ;
    break ;

  case 'K':
    cols -= 1 ;
    rows -= 1 ;
    slabs -= 1 ;
    regen() ;
    break ;

  case 'l':
    lineWidth++;
    clamp( lineWidth, 1.f, 16.f ) ;
    break; 
  case 'L':
    lineWidth--;
    clamp( lineWidth, 1.f, 16.f ) ;
    break;
    
  case 'n':
    tol += 0.1 ;
    regen() ;
    break ;
  case 'N':
    tol -= 0.1 ;
    regen() ;
    break ;
  case 'm':
    for( int i = 0 ;  i < verts.size() ; i++ )
      addDebugLine( verts[i].pos, Black, verts[i].pos+verts[i].normal*1, verts[i].color ) ;
    break; 
  case 'M':
    for( int i = 0 ;  i < iVerts.size() ; i++ )
      addDebugLine( iVerts[i].pos, Black, iVerts[i].pos+iVerts[i].normal*1, iVerts[i].color ) ;
    break; 
  case 'p':
    if( renderPts )
    {
      ptSize++;
      ::clamp( ptSize, 1, 16 ) ;
      glPointSize( ptSize ) ;
    }
    else
    {
      cubeSize += 1 ;
      if( voxelGenMode )
        genVizFromVoxelData() ;
    } 
    break ;
  case 'P':
    if( renderPts )
    {
      ptSize--;
      ::clamp( ptSize, 1, 16 ) ;
      glPointSize( ptSize ) ;
    }
    else
    {
      cubeSize -= 1 ;
      if( voxelGenMode )
        genVizFromVoxelData() ;
    }
    break ;
  case 'r': repeats = !repeats ; break ;

  case 't': showTris = !showTris ; break ;
  case 'T': showCubes = !showCubes ; break ;

  case 27:
    exit(0);
    break;
    
  default:
    break;
  }
}

int main( int argc, char **argv )
{
  glutInit( &argc, argv ) ; // Initializes glut

  glutInitDisplayMode( GLUT_DOUBLE | GLUT_DEPTH | GLUT_RGBA ) ;
  glutInitWindowSize( w, h ) ;
  glutInitWindowPosition( 0, 0 ) ;
  glutCreateWindow( "Perlin" ) ;
  glutReshapeFunc( resize ) ;
  glutDisplayFunc( draw ) ;
  glutIdleFunc( draw ) ;
  
  glutMotionFunc( mouseMotion ) ;
  glutMouseFunc( mouse ) ;
  
  glutKeyboardFunc( keyboard ) ;

  init();

  glutMainLoop();
  return 0;
}











