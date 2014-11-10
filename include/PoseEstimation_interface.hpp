/* This file implementes the Pose Estimation interface, thus contains definitions,
 * for the interface declarations look in PoseEstimation_interface.h
 */
#ifndef __INTERFACE_HPP_INCLUDED__
#define __INTERFACE_HPP_INCLUDED__

#include <pcl/io/pcd_io.h>
#include <pcl/common/norms.h>
#include <pcl/common/time.h>
#include <pcl/console/print.h>
#include <pcl/surface/mls.h>
#include <pcl/search/kdtree.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/features/vfh.h>
#include <pcl/features/esf.h>
#include <pcl/features/cvfh.h>
#include <pcl/features/our_cvfh.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/registration/icp.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <flann/flann.h>
#include <flann/io/hdf5.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <stdexcept>
#include <pcl/common/centroid.h>
//Definition header
#include "PoseEstimation_interface.h"

#define D2R 0.017453293 //degrees to radians conversion

using namespace pcl::console;
using namespace boost;
using namespace boost::filesystem;
using namespace flann;

/**\brief Compute the MinMax distance between two histograms, used by CVFH and OURCVFH
 * \param[in] a The first histogram
 * \param[in] b The second histogram
 * Returns the computed norm (D), defined as follows:
 * \f[
 *  D = 1 - \frac{1+\sum_i^n{min\lef(a_i,b_i\right)}}{1+\sum_i^n{max\left(a_i,b_i\right)}}
 * \f]
 * where n=308 for CVFH/OURCVFH histograms
 */
float MinMaxDistance (vector<float>& a, vector<float>& b)
{
  if (a.size() != b.size())
  {
    print_error("%*s]\tVectors size mismatch!\n",20,__func__);
    return (-1);
  }
  else
  {
    int size = a.size();
    float num(1.0f), den(1.0f);
    ///Process 11 items with each loop for efficency (since it should be applied to vectors of 308 elements)
    int i=0;
    for (; i<(size-10); i+=11)
    { 
      num += min(a[i],b[i]) + min(a[i+1],b[i+1]) + min(a[i+2],b[i+2]) + min(a[i+3],b[i+3]) + min(a[i+4],b[i+4]) + min(a[i+5],b[i+5]);
      num += min(a[i+6],b[i+6]) + min(a[i+7],b[i+7]) + min(a[i+8],b[i+8]) + min(a[i+9],b[i+9]) + min(a[i+10],b[i+10]);
      den += max(a[i],b[i]) + max(a[i+1],b[i+1]) + max(a[i+2],b[i+2]) + max(a[i+3],b[i+3]) + max(a[i+4],b[i+4]) + max(a[i+5],b[i+5]);
      den += max(a[i+6],b[i+6]) + max(a[i+7],b[i+7]) + max(a[i+8],b[i+8]) + max(a[i+9],b[i+9]) + max(a[i+10],b[i+10]);
    }
    ///process last 0-10 elements (if size!=308)
    while ( i < size)
    {
      num += min(a[i],b[i]);
      den += max(a[i],b[i]);
      ++i;
    }
    return (1 - (num/den));
  }
}

/* Class PoseDB Implementation */
bool PoseDB::load(path pathDB)
{
  //load should not be called if flann matrix(es) are already initialized
  if ( exists(pathDB) && is_directory(pathDB) )
  {
    path Pclouds(pathDB.string()+"/Clouds");
    if ( exists(Pclouds) && is_directory(Pclouds) )
    {
      vector<path> pvec;
      copy (directory_iterator(Pclouds), directory_iterator(), back_inserter(pvec));
      sort (pvec.begin(), pvec.end());
      clouds_.resize(pvec.size());
      int i(0);
      for (vector<path>::const_iterator it(pvec.begin()); it != pvec.end(); ++it, ++i)
        pcl::io::loadPCDFile (it->string().c_str(),clouds_[i]);
    }
    if (is_regular_file(pathDB.string()+ "/vfh.h5") && extension(pathDB.string()+ "/vfh.h5") == ".h5")
      flann::load_from_file (vfh_, pathDB.string() + "/vfh.h5", "VFH Histograms");
    else
    {
      print_error("%*s]\tInvalid vfh.h5 file... Try recreating the Database\n",20,__func__);
      return false;
    }
    if (is_regular_file(pathDB.string()+ "/esf.h5") && extension(pathDB.string()+ "/esf.h5") == ".h5")
      flann::load_from_file (esf_, pathDB.string() + "/esf.h5", "ESF Histograms");
    else
    {
      print_error("%*s]\tInvalid esf.h5 file... Try recreating the Database\n",20,__func__);
      return false;
    }
    if (is_regular_file(pathDB.string()+ "/cvfh.h5") && extension(pathDB.string()+ "/cvfh.h5") == ".h5")
      flann::load_from_file (cvfh_, pathDB.string() + "/cvfh.h5", "CVFH Histograms");
    else
    {
      print_error("%*s]\tInvalid cvfh.h5 file... Try recreating the Database\n",20,__func__);
      return false;
    }
    if (is_regular_file(pathDB.string()+ "/ourcvfh.h5") && extension(pathDB.string()+ "/ourcvfh.h5") == ".h5")
      flann::load_from_file (ourcvfh_, pathDB.string() + "/ourcvfh.h5", "OURCVFH Histograms");
    else
    {
      print_error("%*s]\tInvalid ourcvfh.h5 file... Try recreating the Database\n",20,__func__);
      return false;
    }
    if (is_regular_file(pathDB.string()+ "/names.list") && extension(pathDB.string()+ "/names.list") == ".list")
    {
      ifstream file ((pathDB.string()+"/names.list").c_str());
      string line;
      if (file.is_open())
      {
        while (getline (file, line))
        {
          trim(line); //remove white spaces from line
          names_.push_back(line);
        }//end of file
      }
      else
      {
        print_error("%*s]\tCannot open names.list file... Try recreating the Database\n",20,__func__);
        return false;
      }
    }
    else
    {
      print_error("%*s]\tInvalid names.list file... Try recreating the Database\n",20,__func__);
      return false;
    }
    if (is_regular_file(pathDB.string()+ "/cvfh.cluster") && extension(pathDB.string()+ "/cvfh.cluster") == ".cluster")
    {
      ifstream file ((pathDB.string()+"/cvfh.cluster").c_str());
      string line;
      if (file.is_open())
      {
        while (getline (file, line))
        {
          trim(line); //remove white spaces from line
          int c;
          try
          {
            c=stoi(line);
          }
          catch (...)
          {
            print_error("%*s]\tCannot convert string in cvfh.cluster, file is likely corrupted... Try recreating the Database\n",20,__func__);
            return false;
          }
          clusters_cvfh_.push_back(c);
        }//end of file
      }
      else
      {
        print_error("%*s]\tCannot open cvfh.cluster file... Try recreating the Database\n",20,__func__);
        return false;
      }
    }
    else
    {
      print_error("%*s]\tInvalid cvfh.cluster file... Try recreating the Database\n",20,__func__);
      return false;
    }
    if (is_regular_file(pathDB.string()+ "/ourcvfh.cluster") && extension(pathDB.string()+ "/ourcvfh.cluster") == ".cluster")
    {
      ifstream file ((pathDB.string()+"/ourcvfh.cluster").c_str());
      string line;
      if (file.is_open())
      {
        while (getline (file, line))
        {
          trim(line); //remove white spaces from line
          int c;
          try
          {
            c=stoi(line);
          }
          catch (...)
          {
            print_error("%*s]\tCannot convert string in ourcvfh.cluster, file is likely corrupted... Try recreating the Database\n",20,__func__);
            return false;
          }
          clusters_ourcvfh_.push_back(c);
        }//end of file
      }
      else
      {
        print_error("%*s]\tCannot open ourcvfh.cluster file... Try recreating the Database\n",20,__func__);
        return false;
      }
    }
    else
    {
      print_error("%*s]\tInvalid ourcvfh.cluster file... Try recreating the Database\n",20,__func__);
      return false;
    }
  }
  else
  {
    print_error("%*s]\t%s is not a valid database directory, or doesnt exists\n",20,__func__,pathDB.string().c_str());
    return false;
  }
  return true;
}
//////////////////////////////////////
void PoseDB::computeDistanceFromClusters_(PointCloud<VFHSignature308>::Ptr query, int idx, string feature, float& distance)
{
  int clusters_query = query->points.size();
  int clusters;
  distance = 0;
  bool cvfh(false), ourcvfh(false);
  if ( feature.compare("CVFH")==0 )
  {
    cvfh=true;
    clusters = clusters_cvfh_[idx];
  }
  else if ( feature.compare("OURCVFH")==0 )
  {
    ourcvfh=true;
    clusters = clusters_ourcvfh_[idx];
  }
  else
  {
    print_error("%*s]\tFeature must be 'CVFH' or 'OURCVFH'! Exiting...\n",20,__func__);
    return;
  }
  for (int i=0; i< clusters_query; ++i)
  {
    vector<float> tmp_dist;
    vector<float> hist_query;
    for (int n=0; n<308; ++n)
      hist_query.push_back(query->points[i].histogram[n]);
    for (int j=0; j< clusters; ++j)
    {
      vector<float> hist;
      if (cvfh)
      {
        int idxc=0;
        for (int m=0; m<idx; ++m)
          idxc+=clusters_cvfh_[m];
        for (int n=0; n<cvfh_.cols; ++n)
          hist.push_back(cvfh_[idxc+j][n]);
      }
      else if (ourcvfh)
      {
        int idxc=0;
        for (int m=0; m<idx; ++m)
          idxc+=clusters_ourcvfh_[m];
        for (int n=0; n<ourcvfh_.cols; ++n)
          hist.push_back(ourcvfh_[idxc+j][n]);
      }
      tmp_dist.push_back(MinMaxDistance(hist_query, hist));
    }
    distance += *min_element(tmp_dist.begin(), tmp_dist.end());
  }
}
///////////////////////////////////////
void PoseDB::clear()
{
  //vfh_.reset();
  //esf_db_.reset();
  //cvfh_db_.reset();
  //ourcvfh_db_.reset();
  names_.clear();
  clusters_cvfh_.clear();
  clusters_ourcvfh_.clear();
  //idx_vfh_.reset();
  //idx_esf_.reset();
  clouds_.clear();
}
/////////////////////////////////////////
void PoseDB::save(path pathDB)
{
  //TODO
}
/////////////////////////////////////////
void PoseDB::create(path pathClouds, boost::shared_ptr<parameters> params)
{
  ///Parameters correctness are not checked for now... assume they are correct (TODO add checks)
  if (exists(pathClouds) && is_directory(pathClouds))
  {
    this->clear();
    vector<path> pvec;
    copy(directory_iterator(pathClouds), directory_iterator(), back_inserter(pvec));
    sort(pvec.begin(), pvec.end());
    int i(0);
    for (vector<path>::const_iterator it(pvec.begin()); it != pvec.end(); ++it, ++i)
    {
      if (is_regular_file (*it) && it->extension() == ".pcd")
        pcl::io::loadPCDFile(it->string().c_str(), clouds_[i]);
      else
      {
        print_warn("%*s]\tLoaded File (%s) is not a pcd, skipping...\n",20,__func__,it->string().c_str());
        continue;
      }
      vector<string> vst;
      split (vst,it->string(),boost::is_any_of("../\\"), boost::token_compress_on);
      names_.push_back(vst.at(vst.size()-2)); ///filename without extension and path
      PC::Ptr input (clouds_[i].makeShared()); 
      PC::Ptr output (new PC);
      if ((*params)["filtering"] >0)
      {
        StatisticalOutlierRemoval<PointXYZRGBA> filter;
        filter.setMeanK ( (*params)["filterMeanK"] );
        filter.setStddevMulThresh ( (*params)["filterStdDevMulThresh"] );
        filter.setInputCloud(input);
        filter.filter(*output);
        copyPointCloud(*output, *input);
      }
      if ((*params)["upsampling"] >0)
      {
        MovingLeastSquares<PointXYZRGBA, PointXYZRGBA> mls;
        search::KdTree<PointXYZRGBA>::Ptr tree (new search::KdTree<PointXYZRGBA>);
        mls.setInputCloud (input);
        mls.setSearchMethod (tree);
        mls.setUpsamplingMethod (MovingLeastSquares<PointXYZRGBA, PointXYZRGBA>::RANDOM_UNIFORM_DENSITY);
        mls.setComputeNormals (false);
        mls.setPolynomialOrder ( (*params)["mlsPolyOrder"] );
        mls.setPolynomialFit ( (*params)["mlsPolyFit"] );
        mls.setSearchRadius ( (*params)["mlsSearchRadius"] );
        mls.setPointDensity( (*params)["mlsPointDensity"] );
        mls.process (*output); //Process Upsampling
        copyPointCloud(*output, *input);
      }
      if ((*params)["downsampling"]>0)
      {
        VoxelGrid <PointXYZRGBA> vgrid;
        vgrid.setInputCloud (input);
        vgrid.setLeafSize ( (*params)["vgridLeafSize"], (*params)["vgridLeafSize"], (*params)["vgridLeafSize"]); //Downsample to 3mm
        vgrid.setDownsampleAllData (true);
        vgrid.filter (*output); //Process Downsampling
        copyPointCloud(*output, *input);
      }
    //TODO normals and features
    }
  }
}
/////////////////////////////////////////
/* Class Candidate Implementation */
Candidate::Candidate ()
{
  name_ = "UNSET";
  rank_ = -1;
  distance_ = -1;
  rmse_ = -1;
  normalized_distance_=-1;
  transformation_.setIdentity();
}
///////////////////////////////////////////////////////////////////
Candidate::Candidate (string str, PC& cl)
{
  name_ = str;
  cloud_ = cl.makeShared();
  rank_ = -1;
  distance_ = -1;
  rmse_ = -1;
  normalized_distance_=-1;
  transformation_.setIdentity();
}
/////////////////////////////////////////////////////////////////////
Candidate::Candidate (string str, PC::Ptr clp)
{
  name_ = str;
  if (clp)
    cloud_ = clp;
  else
  {
    print_error("%*s]\tShared pointer provided is empty... Aborting Candidate creation\n",20,__func__);
    return;
  }
  rank_ = -1;
  distance_ = -1;
  rmse_ = -1;
  normalized_distance_=-1;
  transformation_.setIdentity();
}
///////////////////////////////////////////////////////////////////////
void Candidate::getRank (int& r) const
{ 
  if (rank_==-1)
    print_warn("%*s]\tCandidate is not part of any list (yet), thus it has no rank, writing -1 ...\n",20,__func__);
  r = rank_; 
}
void Candidate::getDistance (float& d) const
{ 
  if (distance_==-1)
    print_warn("%*s]\tCandidate is not part of any list (yet), thus it has no distance, writing -1 ...\n",20,__func__);
  d = distance_; 
}
void Candidate::getNormalizedDistance (float& d) const 
{ 
  if (normalized_distance_==-1)
    print_warn("%*s]\tCandidate is not part of any list (yet), thus it has no distance, writing -1 ...\n",20,__func__);
  d = normalized_distance_; 
}
void Candidate::getRMSE (float& r) const
{ 
  if (rmse_==-1)
    print_warn("%*s]\tCandidate has not been refined (yet), thus it has no RMSE, writing -1 ...\n",20,__func__);
  r = rmse_; 
}
void Candidate::getTransformation (Eigen::Matrix4f& t) const
{
  if (transformation_.isIdentity())
    print_warn("%*s]\tCandidate has Identity transformation, it probably hasn't been refined (yet)...\n",20,__func__);
  t = transformation_; 
}

/* Class PoseEstimation Implementation */
PoseEstimation::PoseEstimation ()
{
  vp_supplied_ = query_set_ = candidates_found_ = refinement_done_ = false;
  feature_count_ = 4;
  params_["verbosity"]=1;
  params_["useVFH"]=params_["useESF"]=params_["useCVFH"]=params_["useOURCVFH"]=1;
  params_["progBisection"]=params_["downsampling"]=1;
  params_["vgridLeafSize"]=0.003f;
  params_["upsampling"]=params_["filtering"]=0;
  params_["kNeighbors"]=20;
  params_["maxIterations"]=200;
  params_["progItera"]=5;
  params_["progFraction"]=0.5f;
  params_["rmseThreshold"]=0.003f;
  params_["mlsPolyOrder"]=2;
  params_["mlsPointDensity"]=250;
  params_["mlsPolyFit"]=1;
  params_["mlsSearchRadius"]=0.03f;
  params_["filterMeanK"]=50;
  params_["filterStdDevMulThresh"]=3;
  params_["neRadiusSearch"]=0.015;
  params_["useSOasViewpoint"]=1;
  params_["computeViewpointFromName"]=0;
  params_["cvfhEPSAngThresh"]=7.5;
  params_["cvfhCurvThresh"]=0.025;
  params_["cvfhClustTol"]=0.01;
  params_["cvfhMinPoints"]=50;
  params_["ourcvfhEPSAngThresh"]=7.5;
  params_["ourcvfhCurvThresh"]=0.025;
  params_["ourcvfhClustTol"]=0.01;
  params_["ourcvfhMinPoints"]=50;
  params_["ourcvfhAxisRatio"]=0.95;
  params_["ourcvfhMinAxisValue"]=0.01;
  params_["ourcvfhRefineClusters"]=1;
}
////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::setParam(string key, float value)
{
  int size = params_.size();
  if (value < 0)
  {
    if (params_["verbosity"]>0)
      print_warn("%*s]\tParameter '%s' has a negative value (%g), ignoring...\n", 20,__func__, key.c_str(), value);
    return;
  }
  params_[key]=value;
  //Check if key was a valid one, since the class has fixed number of parameters, 
  //if one was mispelled, now we have one more 
  if (params_.size() != size)
  {
    if (params_["verbosity"]>0)
      print_warn("%*s]\tInvalid key parameter '%s', ignoring...\n", 20,__func__, key.c_str());
    params_.erase(key);
  }
  else if (params_["verbosity"]>1)
    print_info("%*s]\tSetting parameter: %s=%g\n",20,__func__,key.c_str(),value);
  //Recheck how many features we want
  int count(0);
  if (params_["useVFH"]>=1)
    count++;
  if (params_["useESF"]>=1)
    count++;
  if (params_["useCVFH"]>=1)
    count++;
  if (params_["useOURCVFH"]>=1)
    count++;
  feature_count_ = count;
  if (feature_count_ <=0 && params_["verbosity"]>0)
    print_warn("%*s]\tYou disabled all features, pose estimation will not initialize query...\n",20,__func__);
}
/////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::setParam_ (string key, string& value)
{
  float f;
  try
  {
    f = stof(value);
  }
  catch (const std::invalid_argument& ia)
  {
    if (params_["verbosity"] > 0)
      print_warn("%*s]\tInvalid %s=%s : %s \n",20,__func__, key.c_str(), value.c_str(), ia.what());
  }
  catch (const std::out_of_range& oor)
  {
    if (params_["verbosity"] > 0)
      print_warn("%*s]\tInvalid %s=%s : %s \n", 20,__func__, key.c_str(), value.c_str(), oor.what());
  }
  setParam(key, f); 
}
//////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::initParams(path config_file)
{ 
  if ( exists(config_file) && is_regular_file(config_file))   
  {
    if (extension(config_file)==".conf") 
    {
      ifstream file (config_file.string().c_str());
      string line;
      if (file.is_open())
      {
        while (getline (file, line))
        {
          trim(line); //remove white spaces from line
          if (line.empty())
          {
            //do nothing empty line ...
          }
          else if (line.compare(0,1,"%") == 0 )
          {
            //do nothing comment line ...
          }
          else
          {
            vector<string> vst;
            //split the line to get a key and a token
            split(vst, line, boost::is_any_of("="), boost::token_compress_on);
            if (vst.size()!=2)
            {
              if (params_["verbosity"]>0)
                print_warn("%*s]\tInvalid configuration line (%s), ignoring... Must be [Token]=[Value]\n", 20,__func__, line.c_str());
              continue;
            }
            setParam_(vst.at(0), vst.at(1));
          }
        }//end of config file
      }
      else
        print_error("%*s]\tCannot open config file! (%s)\n", 20,__func__, config_file.string().c_str());
    }  
    else
      print_error("%*s]\tConfig file provided (%s) has no valid extension! (must be .conf)\n", 20,__func__, config_file.string().c_str());
  }
  else
    print_error("%*s]\tPath to Config File is not valid ! (%s)\n", 20,__func__, config_file.string().c_str());
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::filtering_()
{
  StopWatch timer;
  if (params_["verbosity"] >1)
  {
    print_info("%*s]\tSetting Statistical Outlier Filter to preprocess query cloud...\n",20,__func__);
    print_info("%*s]\tSetting mean K to %g\n",20,__func__, params_["filterMeanK"]);
    print_info("%*s]\tSetting Standard Deviation multiplier to %g\n",20,__func__, params_["filterStdDevMulThresh"]);
    timer.reset();
  }
  PC::Ptr filtered (new PC);
  StatisticalOutlierRemoval<PointXYZRGBA> fil;
  fil.setMeanK (params_["filterMeanK"]);
  fil.setStddevMulThresh (params_["filterStdDevMulThresh"]);
  fil.setInputCloud(query_cloud_);
  fil.filter(*filtered);
  if (query_cloud_processed_)
    copyPointCloud(*filtered, *query_cloud_processed_);
  else
    query_cloud_processed_ = filtered;
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tTotal time elapsed during filter: ",20,__func__);
    print_value("%g", timer.getTime());
    print_info(" ms\n");
  }
}
//////////////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::upsampling_()
{
  StopWatch timer;
  if (params_["verbosity"] >1)
  {
    print_info("%*s]\tSetting MLS with Random Uniform Density to preprocess query cloud...\n",20,__func__);
    print_info("%*s]\tSetting polynomial order to %g\n",20,__func__, params_["mlsPolyOrder"]);
    string t = params_["mlsPolyFit"] ? "true" : "false";
    print_info("%*s]\tSetting polynomial fit to %s\n",20,__func__, t.c_str());
    print_info("%*s]\tSetting desired point density to %g\n",20,__func__, params_["mlsPointDensity"]);
    print_info("%*s]\tSetting search radius to %g\n",20,__func__, params_["mlsSearchRadius"]);
    timer.reset();
  }
  PC::Ptr upsampled (new PC);
  search::KdTree<PointXYZRGBA>::Ptr tree (new search::KdTree<PointXYZRGBA>);
  MovingLeastSquares<PointXYZRGBA, PointXYZRGBA> mls;
  if (query_cloud_processed_)
    mls.setInputCloud(query_cloud_processed_);
  else
    mls.setInputCloud(query_cloud_);
  mls.setSearchMethod(tree);
  mls.setUpsamplingMethod (MovingLeastSquares<PointXYZRGBA, PointXYZRGBA>::RANDOM_UNIFORM_DENSITY);
  mls.setComputeNormals (false);
  mls.setPolynomialOrder(params_["mlsPolyOrder"]);
  mls.setPolynomialFit(params_["mlsPolyFit"]);
  mls.setSearchRadius(params_["mlsSearchRadius"]);
  mls.setPointDensity(params_["mlsPointDensity"]);
  mls.process(*upsampled);
  if (query_cloud_processed_)
    copyPointCloud(*upsampled, *query_cloud_processed_);
  else
    query_cloud_processed_ = upsampled;
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tTotal time elapsed during upsampling: ",20,__func__);
    print_value("%g", timer.getTime());
    print_info(" ms\n");
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::downsampling_()
{
  StopWatch timer;
  if (params_["verbosity"] >1)
  {
    print_info("%*s]\tSetting Voxel Grid to preprocess query cloud...\n",20,__func__);
    print_info("%*s]\tSetting Leaf Size to %g\n",20,__func__, params_["vgridLeafSize"]);
    timer.reset();
  }
  PC::Ptr downsampled (new PC);
  VoxelGrid<PointXYZRGBA> vg;
  if (query_cloud_processed_)
    vg.setInputCloud(query_cloud_processed_);
  else
    vg.setInputCloud(query_cloud_);
  vg.setLeafSize (params_["vgridLeafSize"], params_["vgridLeafSize"], params_["vgridLeafSize"]);
  vg.setDownsampleAllData (true);
  vg.filter(*downsampled);
  if (query_cloud_processed_)
    copyPointCloud(*downsampled, *query_cloud_processed_);
  else
    query_cloud_processed_ = downsampled;
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tTotal time elapsed during downsampling: ",20,__func__);
    print_value("%g", timer.getTime());
    print_info(" ms\n");
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::setQueryViewpoint(float x, float y, float z)
{
  vpx_ = x;
  vpy_ = y;
  vpz_ = z;
  vp_supplied_ = true;
}
/////////////////////////////////////////////////////////////////////////////////////////////////
bool PoseEstimation::computeNormals_()
{
  StopWatch timer;
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tSetting normal estimation to calculate query normals...\n",20,__func__);
    print_info("%*s]\tSetting a neighborhood radius of %g\n",20,__func__, params_["neRadiusSearch"]);
    timer.reset();
  }
  NormalEstimationOMP<PointXYZRGBA, Normal> ne;
  search::KdTree<PointXYZRGBA>::Ptr tree (new search::KdTree<PointXYZRGBA>);
  ne.setSearchMethod(tree);
  ne.setRadiusSearch(params_["neRadiusSearch"]);
  ne.setNumberOfThreads(0); //use pcl autoallocation
  ne.setInputCloud(query_cloud_processed_);
  if (vp_supplied_)
  {
    //A Viewpoint was already supplied by setQueryViewpoint, so we use it
    ne.setViewPoint (vpx_, vpy_, vpz_);
    if (params_["verbosity"] >1)
      print_info("%*s]\tUsing supplied viewpoint: %g, %g, %g\n",20,__func__, vpx_, vpy_, vpz_);
  }
  else if (params_["computeViewpointFromName"])
  {
    //Try to compute viewpoint from query name
    try
    {
      //assume correct naming convection (name_lat_long)
      //If something goes wrong an exception is catched
      vector<string> vst;
      split(vst, query_name_, boost::is_any_of("_"), boost::token_compress_on);
      float vx,vy,vz;
      int lat,lon;
      lat = stoi(vst.at(1));
      lon = stoi(vst.at(2));
      //assume radius of one meter and reference frame in object center
      vx = cos(lat*D2R)*sin(lon*D2R);
      vy = sin(lat*D2R);
      vz = cos(lat*D2R)*cos(lon*D2R);
      setQueryViewpoint(vx,vy,vz);
      if (vp_supplied_)
      {
        if (params_["verbosity"]>1)
          print_info("%*s]\tUsing calculated viewpoint: %g, %g, %g\n",20,__func__, vpx_, vpy_, vpz_);
        ne.setViewPoint(vpx_, vpy_, vpz_);
      }
    }
    catch (...)
    {
      //something went wrong
      print_error("%*s]\tCannot compute Viewpoint from query name... Check naming convention and object reference frame!\n",20,__func__);
      return false;
    }
  }
  else if (params_["useSOasViewpoint"])
  {
    //Use Viewpoint stored in sensor_origin_ of query cloud
    //However we want to save it in class designated spots so it can be used again by 
    //other features
    float vx,vy,vz;
    vx = query_cloud_->sensor_origin_[0];
    vy = query_cloud_->sensor_origin_[1];
    vz = query_cloud_->sensor_origin_[2];
    setQueryViewpoint(vx,vy,vz);
    if (vp_supplied_)
    {
      ne.setViewPoint (vpx_, vpy_, vpz_);
      if (params_["verbosity"]>1)
        print_info("%*s]\tUsing viewpoint from sensor_origin_: %g, %g, %g\n",20,__func__, vpx_, vpy_, vpz_);
    }
    else
    {
      print_error("%*s]\tCannot set viewpoint from sensor_origin_...\n",20,__func__);
      return false;
    }
  }
  else
  {
    print_error("%*s]\tNo Viewpoint supplied!! Cannot continue!!\n",20,__func__);
    print_error("%*s]\tEither use setQueryViewpoint or set 'useSOasViewpoint'/'computeViewpointFromName'\n",20,__func__);
    return false;
  }
  ne.compute(normals_);
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tTotal time elapsed during normal estimation: ",20,__func__);
    print_value("%g", timer.getTime());
    print_info(" ms\n");
  }
  return true;
}
//////////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::computeVFH_()
{
  if(!vp_supplied_)
  {
    //Should never happen, viewpoint was set by normal estimation
    print_error("%*s]\tCannot estimate VFH of query, viewpoint was not set...\n",20,__func__);
    return;
  }
  else
  {
    StopWatch timer;
    if (params_["verbosity"]>1)
    {
      print_info("%*s]\tEstimating VFH feature of query...\n",20,__func__);
      timer.reset();
    }
    VFHEstimation<PointXYZRGBA, Normal, VFHSignature308> vfhE;
    search::KdTree<PointXYZRGBA>::Ptr tree (new search::KdTree<PointXYZRGBA>);
    vfhE.setSearchMethod(tree);
    vfhE.setInputCloud (query_cloud_processed_);
    vfhE.setViewPoint (vpx_, vpy_, vpz_);
    vfhE.setInputNormals (normals_.makeShared());
    vfhE.compute (vfh_);
    if (params_["verbosity"]>1)
    {
      print_info("%*s]\tTotal time elapsed during VFH estimation: ",20,__func__);
      print_value("%g", timer.getTime());
      print_info(" ms\n");
    }
  }
}
////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::computeESF_()
{
  StopWatch timer;
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tEstimating ESF feature of query...\n",20,__func__);
    timer.reset();
  }
  ESFEstimation<PointXYZRGBA, ESFSignature640> esfE;
  search::KdTree<PointXYZRGBA>::Ptr tree (new search::KdTree<PointXYZRGBA>);
  esfE.setSearchMethod(tree);
  esfE.setInputCloud (query_cloud_processed_);
  esfE.compute (esf_);
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tTotal time elapsed during ESF estimation: ",20,__func__);
    print_value("%g", timer.getTime());
    print_info(" ms\n");
  }
}
//////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::computeCVFH_()
{
  if(!vp_supplied_)
  {
    //Should never happen, viewpoint was set by normal estimation
    print_error("%*s]\tCannot estimate CVFH of query, viewpoint was not set...\n",20,__func__);
    return;
  }
  else
  {
    StopWatch timer;
    if (params_["verbosity"]>1)
    {
      print_info("%*s]\tEstimating CVFH feature of query...\n",20,__func__);
      print_info("%*s]\tUsing Angle Threshold of %g degress for normal deviation\n",20,__func__,params_["cvfhEPSAngThresh"]); 
      print_info("%*s]\tUsing Curvature Threshold of %g\n",20,__func__,params_["cvfhCurvThresh"]); 
      print_info("%*s]\tUsing Cluster Tolerance of %g\n",20,__func__,params_["cvfhClustTol"]); 
      print_info("%*s]\tConsidering a minimum of %g points for a cluster\n",20,__func__,params_["cvfhMinPoints"]); 
      timer.reset();
    }
    CVFHEstimation<PointXYZRGBA, Normal, VFHSignature308> cvfhE;
    search::KdTree<PointXYZRGBA>::Ptr tree (new search::KdTree<PointXYZRGBA>);
    cvfhE.setSearchMethod(tree);
    cvfhE.setInputCloud (query_cloud_processed_);
    cvfhE.setViewPoint (vpx_, vpy_, vpz_);
    cvfhE.setInputNormals (normals_.makeShared());
    cvfhE.setEPSAngleThreshold(params_["cvfhEPSAngThresh"]*D2R); //angle needs to be supplied in radians
    cvfhE.setCurvatureThreshold(params_["cvfhCurvThresh"]);
    cvfhE.setClusterTolerance(params_["cvfhClustTol"]);
    cvfhE.setMinPoints(params_["cvfhMinPoints"]);
    cvfhE.setNormalizeBins(false);
    cvfhE.compute (cvfh_);
    if (params_["verbosity"]>1)
    {
      print_info("%*s]\tTotal of %d clusters were found on query\n",20,__func__, cvfh_.points.size());
      print_info("%*s]\tTotal time elapsed during CVFH estimation: ",20,__func__);
      print_value("%g", timer.getTime());
      print_info(" ms\n");
    }
  }
}
//////////////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::computeOURCVFH_()
{
  if(!vp_supplied_)
  {
    //Should never happen, viewpoint was set by normal estimation
    print_error("%*s]\tCannot estimate OURCVFH of query, viewpoint was not set...\n",20,__func__);
    return;
  }
  else
  {
    StopWatch timer;
    if (params_["verbosity"]>1)
    {
      print_info("%*s]\tEstimating OURCVFH feature of query...\n",20,__func__);
      print_info("%*s]\tUsing Angle Threshold of %g degress for normal deviation\n",20,__func__,params_["ourcvfhEPSAngThresh"]); 
      print_info("%*s]\tUsing Curvature Threshold of %g\n",20,__func__,params_["ourcvfhCurvThresh"]); 
      print_info("%*s]\tUsing Cluster Tolerance of %g\n",20,__func__,params_["ourcvfhClustTol"]); 
      print_info("%*s]\tConsidering a minimum of %g points for a cluster\n",20,__func__,params_["ourcvfhMinPoints"]); 
      print_info("%*s]\tUsing Axis Ratio of %g and Min Axis Value of %g during SGURF disambiguation\n",20,__func__,params_["ourcvfhAxisRatio"],params_["ourcvfhMinAxisValue"]); 
      print_info("%*s]\tUsing Refinement Factor of %g for clusters\n",20,__func__,params_["ourcvfhRefineClusters"]); 
      timer.reset();
    }
    OURCVFHEstimation<PointXYZ, Normal, VFHSignature308> ourcvfhE;
    search::KdTree<PointXYZ>::Ptr tree (new search::KdTree<PointXYZ>);
    PointCloud<PointXYZ>::Ptr cloud (new PointCloud<PointXYZ>);
    copyPointCloud(*query_cloud_processed_, *cloud);
    ourcvfhE.setSearchMethod(tree);
    ourcvfhE.setInputCloud (cloud);
    ourcvfhE.setViewPoint (vpx_, vpy_, vpz_);
    ourcvfhE.setInputNormals (normals_.makeShared());
    ourcvfhE.setEPSAngleThreshold(params_["ourcvfhEPSAngThresh"]*D2R); //angle needs to be supplied in radians
    ourcvfhE.setCurvatureThreshold(params_["ourcvfhCurvThresh"]);
    ourcvfhE.setClusterTolerance(params_["ourcvfhClustTol"]);
    ourcvfhE.setMinPoints(params_["ourcvfhMinPoints"]);
    ourcvfhE.setAxisRatio(params_["ourcvfhAxisRatio"]);
    ourcvfhE.setMinAxisValue(params_["ourcvfhMinAxisValue"]);
    ourcvfhE.setRefineClusters(params_["ourcvfhRefineClusters"]);
    ourcvfhE.compute (ourcvfh_);
    if (params_["verbosity"]>1)
    {
      print_info("%*s]\tTotal of %d clusters were found on query\n",20,__func__, ourcvfh_.points.size());
      print_info("%*s]\tTotal time elapsed during OURCVFH estimation: ",20,__func__);
      print_value("%g", timer.getTime());
      print_info(" ms\n");
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////
bool PoseEstimation::initQuery_()
{
  StopWatch timer;
  if (params_["verbosity"]>1)
    timer.reset();
  if (feature_count_ <=0)
  {
    print_error("%*s]\tCannot initialize query, zero features chosen to estimate\n",20,__func__);
    return false;
  }
  //Check if a filter is needed
  if (params_["filtering"] >= 1)
    filtering_();
  //Check if upsampling is needed
  if (params_["upsampling"] >= 1)
    upsampling_();
  //Check if downsampling is needed
  if (params_["downsampling"] >= 1)
    downsampling_();
  if (! query_cloud_processed_ )
    query_cloud_processed_ = query_cloud_;

  //Check if we need ESF descriptor
  if (params_["useESF"] >= 1)
    computeESF_();
  //Check if we need Normals
  if (params_["useVFH"] >=1 || params_["useCVFH"] >=1 || params_["useOURCVFH"] >=1)
  {
    if (computeNormals_())
    {
      //And consequently other descriptors
      if (params_["useVFH"] >=1)
        computeVFH_();
      if (params_["useCVFH"] >=1)
        computeCVFH_();
      if (params_["useOURCVFH"] >=1)
        computeOURCVFH_();
    }
    else
      return false;
  }
  if (params_["verbosity"]>1)
  {
    print_info("%*s]\tQuery succesfully set and initialized:\n",20,__func__);
    print_info("%*s]\t",20,__func__);
    print_value("%s", query_name_.c_str());
    print_info(" with ");
    print_value("%d", query_cloud_processed_->points.size());
    print_info(" points\n");
    print_info("%*s]\tTotal time elapsed to initialize a query: ",20,__func__);
    print_value("%g", timer.getTime());
    print_info(" ms\n");
  }
  return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::setQuery(string str, PC& cl)
{
  query_name_ = str;
  if (query_cloud_)
    copyPointCloud(cl, *query_cloud_);
  else
    query_cloud_ = cl.makeShared();
  if (initQuery_())
    query_set_ = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::setQuery(string str, PC::Ptr clp)
{
  query_name_ = str;
  if (query_cloud_)
    copyPointCloud(*clp, *query_cloud_);
  else
    query_cloud_ = clp;
  if (initQuery_())
    query_set_ = true;
}
/////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::printParams()
{
  for (auto &x : params_)
    cout<< x.first.c_str() <<"="<< x.second<<endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::setDatabase(path dbPath)
{
  database_.clear();
  db_set_=false;
  if (database_.load(dbPath))
  {
    db_set_ = true;
    if (params_["verbosity"]>1)
    {
      print_info("%*s]\tDatabase loaded and set from location %s\n",20,__func__,dbPath.string().c_str());
      print_info("%*s]\tTotal number of poses found: ",20,__func__);
      print_value("%d\n",database_.names_.size());
    }
  }
}
//////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::generateLists()
{
  int k = params_["kNeighbor"];
  if (!db_set_)
  {
    print_error("%*s]\tDatabase is not set, set it with setDatabase first! Exiting...\n",20,__func__);
    return;
  }
  if (!query_set_)
  {
    print_error("%*s]\tQuery is not set, set it with setQuery first! Exiting...\n",20,__func__);
    return;
  }
  if (params_["useVFH"]>=1)
  {
    try
    {
      VFH_list_.clear();
      flann::Matrix<float> vfh_query (new float[1*308],1,308);
      for (size_t j=0; j < 308; ++j)
        vfh_query[0][j]= vfh_.points[0].histogram[j];
      flann::Matrix<int> match_id (new int[1*k],1,k);
      flann::Matrix<float> match_dist (new float[1*k],1,k);
      /**Unfortunately FLANN has to initialize the index the moment you use it, Index doesn't have 
       * a default empty constructor, so you can't put it inside PoseDB class. Instead we load it here
       * with its constructor.
       */
      if (is_regular_file(database_.dbPath.string()+ "/vfh.idx") && extension(database_.dbPath.string()+ "/vfh.idx") == ".idx")
      {
        flann::Index vfh_idx (database.vfh_, SavedIndexParams(database_.dbPath.string()+"/vfh.idx"));
        vfh_idx.buildIndex();
        cout<<"vfh knnsearch BEFORE"<<endl;
        vfh_idx.knnSearch (vfh_query, match_id, match_dist, k, SearchParams(256));
        cout<<"vfh knnsearch AFTER"<<endl;
        for (size_t i=0; i<k; ++i)
        {
          string name= database_.names_[match_id[0][i]];
          Candidate c(name,database_.clouds_[match_id[0][i]]);
          c.rank_=i+1;
          c.distance_=match_dist[0][i];
          c.normalized_distance_=(match_dist[0][i] - match_dist[0][0])/(match_dist[0][k-1] - match_dist[0][0]);
          VFH_list_.push_back(c);
        }
      }
      else
      {
        print_error("%*s]\tInvalid vfh.idx file... Try recreating the Database\n",20,__func__);
        return false;
      }
    }
    catch (...)
    {
      print_error("%*s]\tError Computing VFH list\n",20,__func__);
      return;
    }
  }
  if (params_["useESF"]>=1)
  {
    try
    {
      ESF_list_.clear();
      flann::Matrix<float> esf_query (new float[1*640],1,640);
      for (size_t j=0; j < 640; ++j)
        esf_query[0][j]= esf_.points[0].histogram[j];
      flann::Matrix<int> match_id (new int[1*k],1,k);
      flann::Matrix<float> match_dist (new float[1*k],1,k);
      /**Unfortunately FLANN has to initialize the index the moment you use it, Index doesn't have 
       * a default empty constructor, so you can't put it inside PoseDB class. Instead we load it here
       * with its constructor.
       */
      if (is_regular_file(database_.dbPath.string()+ "/esf.idx") && extension(database_.dbPath.string()+ "/esf.idx") == ".idx")
    {
      indexESF idx (esf_, SavedIndexParams(pathDB.string()+"/esf.idx"));
      idx_esf_ = make_shared<indexESF>(idx);
      idx_esf_->buildIndex();
    }
    else
    {
      print_error("%*s]\tInvalid esf.idx file... Try recreating the Database\n",20,__func__);
      return false;
    }
      database_.idx_esf_->knnSearch (esf_query, match_id, match_dist, k, SearchParams(256) );
      for (size_t i=0; i<k; ++i)
      {
        string name= database_.names_[match_id[0][i]];
        Candidate c(name,database_.clouds_[match_id[0][i]]);
        c.rank_=i+1;
        c.distance_=match_dist[0][i];
        c.normalized_distance_=(match_dist[0][i] - match_dist[0][0])/(match_dist[0][k-1] - match_dist[0][0]);
        ESF_list_.push_back(c);
      }
    }
    catch (...)
    {
      print_error("%*s]\tError Computing ESF list\n",20,__func__);
      return;
    }
  }
  if (params_["useCVFH"]>=1)
  {
    try
    {
      CVFH_list_.clear();
      for (int i=0; i<database_.names_.size(); ++i)
      {
        cout<<"i-for "<<i<<endl; //
        Candidate c;
        c.name_ = database_.names_[i];
        database_.computeDistanceFromClusters_(cvfh_.makeShared() ,i ,"CVFH", c.distance_);
        CVFH_list_.push_back(c);
      }
      if ( CVFH_list_.size() >=k )
      {
        sort(CVFH_list_.begin(), CVFH_list_.end(),
            [](Candidate const &a, Candidate const &b)
            {
            float d,e;
            a.getDistance(d);
            b.getDistance(e);
            return (d < e);
            });
        CVFH_list_.resize(k);
        cout<<"resize done"<<endl;
      }
      else
      {
        print_error("%*s]\tNot enough candidates to select in CVFH list (kNeighbor is too big)...\n",20,__func__);
        return;
      }
      for (int i=0; i<k; ++i)
      {
        cout<<"normalizing "<<i<<endl;
        CVFH_list_[i].rank_ = i+1;
        CVFH_list_[i].setCloud_(database_.clouds_[i]);
        CVFH_list_[i].normalized_distance_=(CVFH_list_[i].distance_ - CVFH_list_[0].distance_)/(CVFH_list_[k-1].distance_ - CVFH_list_[0].distance_);
      }
    }
    catch (...)
    {
      print_error("%*s]\tError computing CVFH list\n",20,__func__);
      return;
    }
  }
  if (params_["useOURCVFH"]>=1)
  {
    try
    {
      OURCVFH_list_.clear();
      for (int i=0; i<database_.names_.size(); ++i)
      {
        Candidate c;
        c.name_ = database_.names_[i];
        database_.computeDistanceFromClusters_(ourcvfh_.makeShared() ,i ,"OURCVFH", c.distance_);
        OURCVFH_list_.push_back(c);
      }
      if ( OURCVFH_list_.size() >=k )
      {
        sort(OURCVFH_list_.begin(), OURCVFH_list_.end(),
            [](Candidate const &a, Candidate const &b)
            {
            float d,e;
            a.getDistance(d);
            b.getDistance(e);
            return (d < e);
            });
        OURCVFH_list_.resize(k);
      }
      else
      {
        print_error("%*s]\tNot enough candidates to select in OURCVFH list (kNeighbor is too big)...\n",20,__func__);
        return;
      }
      for (int i=0; i<k; ++i)
      {
        OURCVFH_list_[i].rank_ = i+1;
        OURCVFH_list_[i].setCloud_(database_.clouds_[i]);
        OURCVFH_list_[i].normalized_distance_=(OURCVFH_list_[i].distance_ - OURCVFH_list_[0].distance_)/(OURCVFH_list_[k-1].distance_ - OURCVFH_list_[0].distance_);
      }
    }
    catch (...)
    {
      print_error("%*s]\tError computing OURCVFH list\n",20,__func__);
      return;
    }
  }
}
//////////////////////////////////////////////////////////////////////////////////////////
void PoseEstimation::generateLists(path dbPath)
{
  if (!query_set_)
  {
    print_error("%*s]\tQuery is not set, set it with setQuery first! Exiting...\n",20,__func__);
    return;
  }
  if (db_set_ && params_["verbosity"]>0)
  {
    print_warn("%*s]\tDatabase was already set, but a new path was specified, loading the new database...\n",20,__func__);
    print_warn("%*s\tUse generateLists() without arguments if you want to keep using the previously loaded database\n",20,__func__);
  }
  setDatabase(dbPath);
  generateLists();
}
#endif