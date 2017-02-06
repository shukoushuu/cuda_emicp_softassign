/*
  Copyright (c) 2010 Toru Tamaki

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation
  files (the "Software"), to deal in the Software without
  restriction, including without limitation the rights to use,
  copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following
  conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.
*/

#include <iostream>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <algorithm>

#include <helper_string.h>



#include <pcl/io/ply_io.h>
#include <pcl/io/vtk_lib_io.h>
#include <pcl/point_types.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/filters/filter_indices.h>

#include <vtkRenderWindow.h>
#include <vtkRendererCollection.h>
#include <vtkCamera.h>



#include "3dregistration.h"

using namespace std;




void
loadFile(const char* fileName,
	 pcl::PointCloud<pcl::PointXYZ>::Ptr cloud
)
{
  pcl::PolygonMesh mesh;
  
  if ( pcl::io::loadPLYFile ( fileName, mesh ) == -1 )
  {
    PCL_ERROR ( "loadFile faild." );
    return;
  }
  else
    pcl::fromPCLPointCloud2<pcl::PointXYZ> ( mesh.cloud, *cloud );
  
  // remove points having values of nan
  std::vector<int> index;
  pcl::removeNaNFromPointCloud ( *cloud, *cloud, index );
}


template <class T>
class isDelete
{
  float _random_sampling_percentage;
public:
  isDelete(float random_sampling_percentage)
  {
    srand((long)time(NULL)); 
    _random_sampling_percentage = random_sampling_percentage / 100.0;
  };
  bool operator() (T& val)
  {
    return (rand()/(double)RAND_MAX >= _random_sampling_percentage);
  }
};

void pointsReduction(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud,
		     float random_sampling_percentage, bool initialize_rand = true){

  isDelete<pcl::PointXYZ> randDel(random_sampling_percentage);
  cloud->points.erase( std::remove_if(cloud->points.begin(), cloud->points.end(), randDel  ), cloud->points.end() );

}


void alignScaleOnce1(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud1,
                    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud2)
{
  Eigen::Vector4f center1, center2;
  Eigen::Matrix3f covariance1, covariance2;
  pcl::computeMeanAndCovarianceMatrix (*cloud1, covariance1, center1);
  pcl::computeMeanAndCovarianceMatrix (*cloud2, covariance2, center2);

  // symmetric scale estimation by Horn 1968
  float s = sqrt( covariance1.trace() / covariance2.trace() );
  
  Eigen::Affine3f Scale;
  Scale.matrix() <<
  s, 0, 0, 0,
  0, s, 0, 0,
  0, 0, s, 0,
  0, 0, 0, 1;
  
  pcl::transformPointCloud ( *cloud2, *cloud2, Scale );

}

void alignScaleOnce(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud1,
                    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud2)
{
  Eigen::Vector4f center1, center2;
  Eigen::Matrix3f covariance1, covariance2;
  pcl::computeMeanAndCovarianceMatrix (*cloud1, covariance1, center1);
  pcl::computeMeanAndCovarianceMatrix (*cloud2, covariance2, center2);

  std::cout << "scale1 = " << covariance1.trace() << std::endl;
  std::cout << "scale2 = " << covariance2.trace() << std::endl;
  
  
  Eigen::Affine3f Scale;
  float s;
  s = 1.0 / sqrt( covariance1.trace() );
  s *= sqrt(0.003); // ad-hoc for numerical issue
  Scale.matrix() <<
  s, 0, 0, 0,
  0, s, 0, 0,
  0, 0, s, 0,
  0, 0, 0, 1;
  pcl::transformPointCloud ( *cloud1, *cloud1, Scale );

  s = 1.0 / sqrt( covariance2.trace() );
  s *= sqrt(0.003); // ad-hoc for numerical issue
  Scale.matrix() <<
  s, 0, 0, 0,
  0, s, 0, 0,
  0, 0, s, 0,
  0, 0, 0, 1;
  pcl::transformPointCloud ( *cloud2, *cloud2, Scale );

  pcl::computeMeanAndCovarianceMatrix (*cloud1, covariance1, center1);
  pcl::computeMeanAndCovarianceMatrix (*cloud2, covariance2, center2);

  std::cout << "scale1 = " << covariance1.trace() << std::endl;
  std::cout << "scale2 = " << covariance2.trace() << std::endl;
}

void init_RT(float *h_R, float *h_t){
	
  // set to Identity matrix
  h_R[0] = 1.0f;
  h_R[1] = 0.0f;
  h_R[2] = 0.0f;
  h_R[3] = 0.0f;
  h_R[4] = 1.0f;
  h_R[5] = 0.0f;
  h_R[6] = 0.0f;
  h_R[7] = 0.0f;
  h_R[8] = 1.0f;

  h_t[0] = 0.0f;
  h_t[1] = 0.0f;
  h_t[2] = 0.0f;
}




void printRT(const float* R, const float* t){
  printf("R\n");
  for(int r=0; r<9; r++){
    printf("%f ", R[r]);
    if((r+1)%3==0) printf("\n");
  }
  printf("t\n");
  for(int r=0; r<3; r++)
    printf("%f ", t[r]);
  printf("\n");

}

void saveRTtoFile(const float* R, const float* t, const char* filename){

  FILE *fp;
  if((fp = fopen(filename, "w")) != NULL){
    for(int r=0; r<9; r++){
      fprintf(fp, "%f ", R[r]);
      if((r+1)%3==0) fprintf(fp,"\n");
    }
    for(int r=0; r<3; r++)
      fprintf(fp,"%f ", t[r]);
    fprintf(fp,"\n");
  }
  fclose(fp);
}

void loadRTfromFile(float* R, float* t, const char* filename){

  FILE *fp;
  if((fp = fopen(filename, "r")) != NULL){
    if(12 != fscanf(fp,"%f%f%f%f%f%f%f%f%f%f%f%f",
		    &R[0], &R[1], &R[2],
		    &R[3], &R[4], &R[5],
		    &R[6], &R[7], &R[8],
		    &t[0], &t[1], &t[2]
		    )){
      fprintf(stderr, "Fail to read RT from file [%s]\n", filename);
      exit(1);
    }

    }

}



int main(int argc, char** argv){


  //
  // analyzing command options
  //

  char *pointFileX, *pointFileY;
  int wrongArg = 0;


  // Read filenames of point clouds X and Y.
  // File format is txt or ply. see readPointsFromFile() or readPointsFromPLYFile().
  if (getCmdLineArgumentString(argc, (const char **) argv, "pointFileX", &pointFileX) &&
      getCmdLineArgumentString(argc, (const char **) argv, "pointFileY", &pointFileY)){
    cout << "option: pointFileX= " << pointFileX << endl;
    cout << "option: pointFileY=" << pointFileY << endl;
  } else {
    cerr << "Wrong arguments. see src." << endl;
    cerr << "min ||X - (R*Y+t) || " << endl;
    exit(1);
  }


  int Xsize, Ysize;


  //
  // select algorithm
  //
  int isICP = checkCmdLineFlag(argc, (const char **) argv, "icp");
  int isEMICP = checkCmdLineFlag(argc, (const char **) argv, "emicp");
  int isEMICP_CPU = checkCmdLineFlag(argc, (const char **) argv, "emicpcpu");
  int isSoftassign = checkCmdLineFlag(argc, (const char **) argv, "softassign");

  if (!isICP && !isEMICP && !isEMICP_CPU && !isSoftassign)
    isSoftassign = 1; // default algorithm




  //
  // initialize parameters
  //
  registrationParameters param;

  if(isSoftassign){ // softassign

    if(! (param.JMAX  = getCmdLineArgumentInt(argc, (const char **) argv, "JMAX") ) ) param.JMAX = 100; // default parametersa
    if(! (param.I0    = getCmdLineArgumentInt(argc, (const char **) argv, "I0"  ) ) ) param.I0 = 5;
    if(! (param.I1    = getCmdLineArgumentInt(argc, (const char **) argv, "I1"  ) ) ) param.I1 = 3;
    if(! (param.alpha = getCmdLineArgumentFloat(argc, (const char **) argv, "alpha") ) ) param.alpha = 3.0f;
    if(! (param.T_0   = getCmdLineArgumentFloat(argc, (const char **) argv, "T_0"  ) ) ) param.T_0 = 100.0f;
    if(! (param.TFACTOR  = getCmdLineArgumentFloat(argc, (const char **) argv, "TFACTOR" ) ) ) param.TFACTOR = 0.95f;
    if(! (param.moutlier = getCmdLineArgumentFloat(argc, (const char **) argv, "moutlier") ) ) param.moutlier = (float)(1/sqrtf(param.T_0)*expf(-1.0f));
   


    cout << "softassgin paramters" << endl
	 << "JMAX " << param.JMAX << endl
	 << "I0 " << param.I0 << endl
	 << "I1 " << param.I1 << endl
	 << "alpha " << param.alpha << endl
	 << "T_0 " << param.T_0 << endl
	 << "TFACTOR " << param.TFACTOR << endl
	 << "moutlier " << param.moutlier << endl;
  }

  if(isICP){ // ICP

    if(! (param.maxIteration = getCmdLineArgumentInt(argc, (const char **) argv, "maxIteration")) )
      param.maxIteration = 30;// default parameters

    cout << "ICP paramters" << endl
	 << "maxIteration " << param.maxIteration << endl;
  }

  if(isEMICP || isEMICP_CPU){ // EM-ICP

    
    if(! (param.sigma_p2     = getCmdLineArgumentFloat(argc, (const char **) argv, "sigma_p2") ) )     param.sigma_p2 = 0.01f; // default parameters
    if(! (param.sigma_inf    = getCmdLineArgumentFloat(argc, (const char **) argv, "sigma_inf") ) )    param.sigma_inf = 0.00001f;
    if(! (param.sigma_factor = getCmdLineArgumentFloat(argc, (const char **) argv, "sigma_factor") ) ) param.sigma_factor = 0.9f;
    if(! (param.d_02         = getCmdLineArgumentFloat(argc, (const char **) argv, "d_02") ) )	       param.d_02 = 0.01f;       

    cout << "EM-ICP paramters" << endl
	 << "sigma_p2 " << param.sigma_p2 << endl
	 << "sigma_inf " << param.sigma_inf << endl
	 << "sigma_factor " << param.sigma_factor << endl
	 << "d_02 " << param.d_02 << endl;

  }

  param.noviewer = checkCmdLineFlag(argc, (const char **) argv, "noviewer");
  param.notimer  = checkCmdLineFlag(argc, (const char **) argv, "notimer");
  param.nostop   = checkCmdLineFlag(argc, (const char **) argv, "nostop");


  param.argc = argc;
  param.argv = argv;





  //
  // read points, and initialize
  //
  float *h_X, *h_Y;

  // h_X stores points as the order of
  // [X_x1 X_x2 .... X_x(Xsize-1) X_y1 X_y2 .... X_y(Xsize-1)  X_z1 X_z2 .... X_z(Xsize-1) ],
  // where (X_xi X_yi X_zi) is the i-th point in X.
  //
  // h_Y does as the same way.

  param.cloud_source.reset ( new pcl::PointCloud<pcl::PointXYZ> () );
  param.cloud_target.reset ( new pcl::PointCloud<pcl::PointXYZ> () );
  param.cloud_source_trans.reset ( new pcl::PointCloud<pcl::PointXYZ> () );
  loadFile(pointFileX, param.cloud_target);
  loadFile(pointFileY, param.cloud_source);

  
  
  {
    if(checkCmdLineFlag(argc, (const char **) argv, "alignScaleOnce"))
      alignScaleOnce(param.cloud_target, param.cloud_source);
  }
  
  
  
  float pointsReductionRate;
  if ( (pointsReductionRate = getCmdLineArgumentFloat(argc, (const char **) argv, "pointsReductionRate") ) ) {
    pointsReduction(param.cloud_target, pointsReductionRate);
    pointsReduction(param.cloud_source, pointsReductionRate);
    cout << "number of points X,Y are reduced to "
	 << pointsReductionRate << "% of original." << endl;
  } else {
    if ( (pointsReductionRate = getCmdLineArgumentFloat(argc, (const char **) argv, "pointsReductionRateX") ) ) {
      pointsReduction(param.cloud_target, pointsReductionRate);
      cout << "number of points X are reduced to "
          << pointsReductionRate << "% of original." << endl;
    }
    if ( (pointsReductionRate = getCmdLineArgumentFloat(argc, (const char **) argv, "pointsReductionRateY") ) ) {
      pointsReduction(param.cloud_source, pointsReductionRate);
      cout << "number of points Y are reduced to "
          << pointsReductionRate << "% of original." << endl;
    }
  }
  cout << "Xsize: " << param.cloud_target->size() << endl
       << "Ysize: " << param.cloud_source->size() << endl;





  


  if(!param.noviewer){

    
    param.viewer.reset ( new pcl::visualization::PCLVisualizer ("3D Viewer") );
    param.viewer->setBackgroundColor (0, 0, 0);
    
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> source_color ( param.cloud_source, 0, 255, 0 );
    param.viewer->addPointCloud<pcl::PointXYZ> ( param.cloud_source, source_color, "source");
    param.viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "source");
    
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> target_color ( param.cloud_target, 255, 255, 255 );
    param.viewer->addPointCloud<pcl::PointXYZ> ( param.cloud_target, target_color, "target");
    param.viewer->setPointCloudRenderingProperties ( pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "target" );
    
    param.source_trans_color.reset ( new pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> ( param.cloud_source_trans, 255, 0, 255) );
    param.viewer->addPointCloud<pcl::PointXYZ> ( param.cloud_source_trans, *param.source_trans_color, "source trans" );
    param.viewer->setPointCloudRenderingProperties ( pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "source trans" );
    
    
    // orthographic (parallel) projection; same with pressing key 'o'
    param.viewer->getRenderWindow()->GetRenderers()->GetFirstRenderer()->GetActiveCamera()->SetParallelProjection( 1 );
    
    param.viewer->resetCamera();
    
    if(!param.nostop)
    {
      cout << "Press q to continue." << endl;
      param.viewer->spin();
    }
    
  }



  float* h_R = new float [9]; // rotation matrix
  float* h_t = new float [3]; // translation vector
  init_RT(h_R, h_t); // set R to Identity matrix, t to zero vector



  
  do {
    
    {
      char *loadRTfromFilename;
      if(getCmdLineArgumentString(argc, (const char **) argv, "loadRTfromFile", &loadRTfromFilename))
	loadRTfromFile(h_R, h_t, loadRTfromFilename);
      else
	init_RT(h_R, h_t); // set R to Identity matrix, t to zero vector
    }
    printRT(h_R, h_t);
    
    
    
    
    
    
    clock_t start, end;
    start = clock();
    
    if(isICP)
      icp( param.cloud_target, param.cloud_source, // input
	   h_R, h_t, // return
	   param);
    if(isEMICP)
	emicp( param.cloud_target, param.cloud_source, // input
	       h_R, h_t, // return
	       param);
    if(isEMICP_CPU)
	emicp_cpu( param.cloud_target, param.cloud_source, // input
		   h_R, h_t, // return
	           param);
    if(isSoftassign)
	softassign( param.cloud_target, param.cloud_source, // input
		    h_R, h_t, // return
	            param);
	
	
    end = clock();
    printf("elapsed %f\n", (double)(end - start) / CLOCKS_PER_SEC);

    
    printRT(h_R, h_t);
    
    {
      char *saveRTtoFilename;
      if(getCmdLineArgumentString(argc, (const char **) argv, "saveRTtoFile", &saveRTtoFilename))
	saveRTtoFile(h_R, h_t, saveRTtoFilename);
    }
    
    if ( param.noviewer || param.nostop || param.viewer->wasStopped() )
      break;
    else
      param.viewer->spin();
      
  }
  while( !param.viewer->wasStopped() );
  
  
  
  delete [] h_X;
  delete [] h_Y;
  delete [] h_R;
  delete [] h_t;
  
  

  return 0;
}

