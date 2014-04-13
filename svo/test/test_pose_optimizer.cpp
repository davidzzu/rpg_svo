// This file is part of SVO - Semi-direct Visual Odometry.
//
// Copyright (C) 2014 Christian Forster <forster at ifi dot uzh dot ch>
// (Robotics and Perception Group, University of Zurich, Switzerland).
//
// SVO is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or any later version.
//
// SVO is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <algorithm>
#include <vikit/pinhole_camera.h>
#include <vikit/math_utils.h>
#include <vikit/sample.h>
#include <vikit/blender_utils.h>
#include <svo/frame.h>
#include <svo/point.h>
#include <svo/feature.h>
#include <svo/config.h>
#include <svo/feature_detection.h>
#include <svo/pose_optimizer.h>
#include "test_utils.h"

namespace {

using namespace svo;
using namespace Eigen;

/// Pose Optimizer Test-Fixture
class PoseOptimizerTest {
 public:
  PoseOptimizerTest()
  {
    cam_ = new vk::PinholeCamera(752, 480, 217.083701215, 217.083701215, 376, 240);

    // load image
    std::string dataset_dir(svo::test_utils::getDatasetDir() + "/flying_room_1_rig_1");
    std::string img_name(dataset_dir+"/img/frame_000001_0.png");
    printf("Loading image '%s'\n", img_name.c_str());
    cv::Mat img(cv::imread(img_name, 0));
    assert(!img.empty());

    // create frame
    frame_.reset(new svo::Frame(cam_, img, 1.0));

    // set pose
    Eigen::Vector3d t_w_ref(2.139359951019287, -1.9544399976730347, 2.8595199584960938  );
    Eigen::Quaterniond q_w_ref(0.0027939670253545046, 0.8382523059844971, 0.5443645715713501, -0.03150530532002449);
    frame_->T_f_w_ = Sophus::SE3(q_w_ref.toRotationMatrix(), t_w_ref).inverse();

    // load ground-truth depth
    vk::blender_utils::loadBlenderDepthmap(dataset_dir + "/depth/frame_000001_0.depth", *cam_, depthmap_);

    // detect features
    feature_detection::Corners corners;
    feature_detection::FastDetector detector(
        cam_->width(), cam_->height(), Config::gridSize(), Config::nPyrLevels());
    detector.detect(frame_->img_pyr_, frame_->fts_, Config::triangMinCornerScore(), &corners);
    size_t n_fts = 0;
    for(feature_detection::Corners::iterator it=corners.begin(); it!=corners.end(); ++it)
    {
      // we must check again if the score is above the threshold because the Corners
      // vector is initialized with dummy corners
      if(it->score > Config::triangMinCornerScore())
      {
        Feature* ftr(new Feature(frame_.get(), Vector2d(it->x, it->y), it->level));
        Point* point(new Point(frame_->f2w(ftr->f*depthmap_.at<float>(it->y, it->x))));
        ftr->point = point;
        ftr->frame = frame_.get();
        frame_->addFeature(ftr);
        ++n_fts;
      }
    }
    printf("Added %zu features to frame.\n", n_fts);
  }

  virtual ~PoseOptimizerTest()
  {
    delete cam_;
  }

  // Individual tests
  void test(const Vector3d& pose_disturbance, double pixel_sigma2);

  // Objects declared here can be used by all tests
  vk::PinholeCamera* cam_;
  FramePtr frame_;
  cv::Mat depthmap_;
};

void PoseOptimizerTest::test(const Vector3d& pose_disturbance, double pixel_sigma2)
{
  printf("Add %f px noise to each observation\n", pixel_sigma2);
  for(Features::iterator it=frame_->fts_.begin(); it!=frame_->fts_.end(); ++it)
  {
    (*it)->px += Vector2d(vk::Sample::gaussian(pixel_sigma2), vk::Sample::gaussian(pixel_sigma2));
    (*it)->f = frame_->c2f((*it)->px);
  }
  frame_->T_f_w_ = frame_->T_f_w_*SE3(Matrix3d::Identity(), pose_disturbance);
  double estimated_scale, error_init, error_final;
  size_t num_obs;
  pose_optimizer::optimizeGaussNewton(
      Config::reprojThresh(), 10, true, frame_,
      estimated_scale, error_init, error_final, num_obs);
}

}  // namespace

int main(int argc, char** argv)
{
  PoseOptimizerTest test;
  test.test(Eigen::Vector3d(0.2, 0.2, 0.2), 1.0);
  return 0;
}
