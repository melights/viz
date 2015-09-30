/**

viz - A robotics visualizer specialized for the da Vinci robotic system.
Copyright (C) 2014 Max Allan

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

**/

#include <cinder/Quaternion.h>
#include <cinder/app/App.h>

#include "davinci.hpp"
#include "../include/pose_grabber.hpp"

using namespace viz;

inline void clean_string(std::string &str, const std::vector<char> &to_remove){
  for (auto &ch : to_remove){
    str.erase(std::remove(str.begin(), str.end(), ch), str.end());
  }
}

size_t BasePoseGrabber::grabber_num_id_ = 0;


std::string BasePoseGrabber::WriteSE3ToString(const ci::Matrix44f &mat){

  std::stringstream ss;

  for (int r = 0; r < 4; ++r){
    ss << "| ";
    for (int c = 0; c < 4; ++c){
      ss << mat.at(r, c) << " ";
    }
    ss << "|\n";
  }

  return ss.str();

}


BasePoseGrabber::BasePoseGrabber(const std::string &output_dir) : do_draw_(false) , save_dir_(output_dir) {

  std::stringstream ss;
  ss << "Pose grabber " << grabber_num_id_;
  param_modifier_ = ci::params::InterfaceGl::create(ci::app::getWindow(), ss.str(), ci::app::toPixels(ci::Vec2i(50, 50)));
  param_modifier_->hide();  

  grabber_num_id_++;

}

void BasePoseGrabber::convertFromBouguetPose(const ci::Matrix44f &in_pose, ci::Matrix44f &out_pose){

  out_pose.setToIdentity();

  ci::Vec3f translation = in_pose.getTranslate().xyz();
  translation[1] *= -1;
  translation[2] *= -1;
  out_pose.translate(translation);

  ci::Matrix33f flip;
  flip.setToIdentity();
  flip.at(1, 1) *= -1;
  flip.at(2, 2) *= -1;
  ci::Matrix33f in_gl_coords = flip * in_pose.subMatrix33(0, 0);
  ci::Quatf q(in_gl_coords);
  out_pose.rotate(q.getAxis(), q.getAngle());

  out_pose.invert(); //bouguet poses (from calibration) are grid poses so invert to get camera poses

}

PoseGrabber::PoseGrabber(const ConfigReader &reader, const std::string &output_dir) : BasePoseGrabber(output_dir) {

  self_name_ = "pose-grabber";
  checkSelfName(reader.get_element("name"));

  model_.LoadData(reader.get_element("model-file"));
  
  ifs_.open(reader.get_element("pose-file"));

  if (!ifs_.is_open()){
    throw std::runtime_error("Error, could not open file: " + reader.get_element("pose-file"));
  }

  ifs_.exceptions(std::ifstream::eofbit);

  save_dir_ = output_dir;

  ofs_file_ = save_dir_ + "/" + reader.get_element("output-pose-file");

}

bool PoseGrabber::LoadPose(const bool update_as_new){

  do_draw_ = false; //set to true only if we read a 'good' pose

  //load the new pose (if requested).
  if (update_as_new){
    try{
      std::string line;
      int row = 0;
      while (1)
      {
        std::getline(ifs_, line);
        if (row == 4) break;
        if (line[0] == '#' || line.length() < 1) continue;
        std::stringstream ss(line);
        for (int col = 0; col < 4; ++col){
          float val;
          ss >> val;
          cached_model_pose_.at(row, col) = val;
        }
        row++;
      }

      //update the reference list of old tracks for drawing trajectories
      reference_frame_tracks_.push_back(cached_model_pose_);
      do_draw_ = true;

    }
    catch (std::ofstream::failure e){
      cached_model_pose_.setToIdentity();
      do_draw_ = false;
      return false;
    }
  }

  // update the model with the pose
  std::vector<ci::Matrix44f> ret({ cached_model_pose_ });
  model_.SetTransformSet(ret);

  return true;

}

void PoseGrabber::WritePoseToStream()  {

  if (!ofs_.is_open()) {
    if (!boost::filesystem::exists(save_dir_)) {
      boost::filesystem::create_directory(save_dir_);
    }
    ofs_.open(ofs_file_);
  }

  if (!ofs_.is_open()) throw(std::runtime_error("Error, cannot open file"));

  ofs_ << model_.Body().transform_ << "\n";

  ofs_ << "\n";

}

void PoseGrabber::WritePoseToStream(const ci::Matrix44f &camera_pose)  {

  if (!ofs_.is_open()) {
    if (!boost::filesystem::exists(save_dir_)) {
      boost::filesystem::create_directory(save_dir_);
    }
    ofs_.open(ofs_file_);
  }

  if (!ofs_.is_open()) throw(std::runtime_error("Error, cannot open file"));

  ofs_ << camera_pose.inverted() * model_.Body().transform_ << "\n";

  ofs_ << "\n";

}

BaseDaVinciPoseGrabber::BaseDaVinciPoseGrabber(const ConfigReader &reader, const std::string &output_dir) : BasePoseGrabber(output_dir) {
  
  try{
    model_.LoadData(reader.get_element("model-file"));
  }
  catch (std::runtime_error){
    //no model (e.g. tracking camera)
  }

}

void BaseDaVinciPoseGrabber::convertFromDaVinciPose(const ci::Matrix44f &in_pose, ci::Matrix44f &out_pose){


  out_pose.setToIdentity();

  ci::Matrix44f flip;
  flip.setToIdentity();
  flip.at(1, 1) *= -1;
  flip.at(2, 2) *= -1;

  flip.invert();

  out_pose = in_pose * flip;

}

DHDaVinciPoseGrabber::DHDaVinciPoseGrabber(const ConfigReader &reader, const std::string &output_dir) : BaseDaVinciPoseGrabber(reader, output_dir) {

  self_name_ = "dh-davinci-grabber";
  checkSelfName(reader.get_element("name"));

  if (reader.get_element("joint") == "PSM1")
    target_joint_ = davinci::DaVinciJoint::PSM1;
  else if (reader.get_element("joint") == "PSM2")
    target_joint_ = davinci::DaVinciJoint::PSM2;
  else if (reader.get_element("joint") == "ECM")
    target_joint_ = davinci::DaVinciJoint::ECM;
  else
    throw std::runtime_error("Error, bad joint");

  switch (target_joint_){

  case davinci::DaVinciJoint::PSM1:
    num_base_joints_ = chain_.mSUJ1OriginSUJ1Tip.size();
    num_arm_joints_ = chain_.mPSM1OriginPSM1Tip.size();
    break;
  case davinci::DaVinciJoint::PSM2:
    num_base_joints_ = chain_.mSUJ2OriginSUJ2Tip.size();
    num_arm_joints_ = chain_.mPSM2OriginPSM2Tip.size();
    break;
  case davinci::DaVinciJoint::ECM:
    num_base_joints_ = chain_.mSUJ3OriginSUJ3Tip.size();
    num_arm_joints_ = 4;//chain_.mECM1OriginECM1Tip.size(); 
    break;
  }

  arm_offsets_ = std::vector<double>(num_arm_joints_, 0.0);
  base_offsets_ = std::vector<double>(num_base_joints_, 0.0);
  arm_joints_ = std::vector<double>(num_arm_joints_, 0.0);
  base_joints_ = std::vector<double>(num_base_joints_, 0.0);

  try{
    SetupOffsets(reader.get_element("base-offset"), reader.get_element("arm-offset"));
  }
  catch (std::runtime_error &){
    
  }

  base_ifs_.open(reader.get_element("base-joint-file"));
  if (!base_ifs_.is_open()){
    throw std::runtime_error("Error, could not open file: " + reader.get_element("base-joint-file"));
  }

  base_ifs_.exceptions(std::ifstream::eofbit);

  arm_ifs_.open(reader.get_element("arm-joint-file"));
  if (!arm_ifs_.is_open()){
    throw std::runtime_error("Error, could not open file: " + reader.get_element("arm-joint-file"));
  }

  arm_ifs_.exceptions(std::ifstream::eofbit);

  base_ofs_file_ = output_dir + "/" + reader.get_element("output-base-joint-file");
  arm_ofs_file_ = output_dir + "/" + reader.get_element("output-arm-joint-file");
  try{
    se3_ofs_file_ = output_dir + "/" + reader.get_element("output-se3-file");
  }
  catch (...){
    se3_ofs_file_ = output_dir + "/" + reader.get_element("output-se3"); //stupid old code 
  }
  
}

void DHDaVinciPoseGrabber::SetupOffsets(const std::string &base_offsets, const std::string &arm_offsets){

  std::stringstream ss;
  
  param_modifier_->addText("", "label=`Edit the set up joints`");

  ss << base_offsets;
  for (size_t i = 0; i < base_offsets_.size(); ++i){
    ss >> base_offsets_[i];
    std::stringstream ss;
    ss << "SU Joint " << i;
    param_modifier_->addParam(ss.str(), &(base_offsets_[i]), "min=-10 max=10 step= 0.0001 keyIncr=z keyDecr=Z");
  }

  param_modifier_->addSeparator();
  param_modifier_->addText("", "label=`Edit the arm joints`");

  ss.clear();
  ss << arm_offsets;
  for (size_t i = 0; i < arm_offsets_.size(); ++i){
    ss >> arm_offsets_[i];
    std::stringstream ss;
    ss << "Joint " << i;
    if (i < 3)
      param_modifier_->addParam(ss.str(), &(arm_offsets_[i]), "min=-10 max=10 step= 0.0001 keyIncr=z keyDecr=Z");
    else
      param_modifier_->addParam(ss.str(), &(arm_offsets_[i]), "min=-10 max=10 step= 0.001 keyIncr=z keyDecr=Z");
  }

}

ci::Matrix44f DHDaVinciPoseGrabber::GetPose(){

  if (target_joint_ == davinci::ECM){

    viz::davinci::ECMData ecm;

    for (std::size_t i = 0; i < base_joints_.size(); ++i){
      ecm.sj_joint_angles[i] = base_joints_[i] + base_offsets_[i];
    }

    for (std::size_t i = 0; i < arm_joints_.size(); ++i){
      ecm.jnt_pos[i] = arm_joints_[i] + arm_offsets_[i];
    }

    buildKinematicChainECM1(chain_, ecm, model_.Shaft().transform_);

    return model_.Shaft().transform_;

  }

  else if (target_joint_ == davinci::PSM1 || target_joint_ == davinci::PSM2){

    viz::davinci::PSMData psm;

    for (std::size_t i = 0; i < base_joints_.size(); ++i){
      psm.sj_joint_angles[i] = base_joints_[i] + base_offsets_[i];
    }

    for (std::size_t i = 0; i < arm_joints_.size(); ++i){
      psm.jnt_pos[i] = arm_joints_[i] + arm_offsets_[i];
    }

    if (target_joint_ == davinci::PSM1)
      buildKinematicChainPSM1(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);
    else if (target_joint_ == davinci::PSM2)
      buildKinematicChainPSM2(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);

    //ci::app::console() << "Current setup joint values: ";
    //for (std::size_t i = 0; i < base_joints_.size(); ++i){
    //  ci::app::console() << psm.sj_joint_angles[i] << " ";
    //}
    //ci::app::console() << std::endl

    //ci::app::console() << "Current joint values: ";
    //for (std::size_t i = 0; i < arm_joints_.size(); ++i){
    //  ci::app::console() << psm.jnt_pos[i] << " ";
    //}
    //ci::app::console() << std::endl;

    return model_.Shaft().transform_;

  }
  else{

    throw std::runtime_error("Error, bad joint type");

  }
}

bool DHDaVinciPoseGrabber::LoadPose(const bool update_as_new){

  if (update_as_new){
    if (!ReadDHFromFiles(base_joints_, arm_joints_))
      return false;
  }

  //don't care about the return.
  GetPose();

  // update the list of previous poses for plotting trajectories.
  if (update_as_new){
    reference_frame_tracks_.push_back(model_.Shaft().transform_);
  }

  return true;

}

bool DHDaVinciPoseGrabber::ReadDHFromFiles(std::vector<double> &psm_base_joints, std::vector<double> &psm_arm_joints){

  assert(num_arm_joints_ == psm_arm_joints.size());
  assert(num_base_joints_ == psm_base_joints.size());

  try{
    for (int i = 0; i < num_arm_joints_; ++i){
      double x;
      arm_ifs_ >> x;
      psm_arm_joints[i] = x;
    }

    for (int i = 0; i < num_base_joints_; ++i){
      double x;
      base_ifs_ >> x;
      psm_base_joints[i] = x;
    }

  }
  catch (std::ifstream::failure){
    do_draw_ = false;
    return false;
  }

  return true;

}

void DHDaVinciPoseGrabber::WritePoseToStream()  {

  if (!se3_ofs_.is_open()) {
    if (!boost::filesystem::exists(save_dir_)) {
      boost::filesystem::create_directory(save_dir_);
    }
    se3_ofs_.open(se3_ofs_file_);
    arm_ofs_.open(arm_ofs_file_);
    base_ofs_.open(base_ofs_file_);
  }

  if (!se3_ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));
  if (!arm_ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));
  if (!base_ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));

  se3_ofs_ << WriteSE3ToString(model_.Shaft().transform_) << "\n";
  for (size_t i = 4; i < arm_joints_.size(); ++i){
    se3_ofs_ << arm_joints_[i] + arm_offsets_[i] << "\n";
  }
  se3_ofs_ << std::endl;

  for (size_t i = 0; i < arm_joints_.size(); ++i){
    arm_ofs_ << arm_joints_[i] + arm_offsets_[i] << " ";
  }
  arm_ofs_ << std::endl;

  for (size_t i = 0; i < base_joints_.size(); ++i){
    base_ofs_ << base_joints_[i] + base_offsets_[i] << " ";
  }
  base_ofs_ << std::endl;

}

void DHDaVinciPoseGrabber::WritePoseToStream(const ci::Matrix44f &camera_pose)  {

  if (!se3_ofs_.is_open()) {
    if (!boost::filesystem::exists(save_dir_)) {
      boost::filesystem::create_directory(save_dir_);
    }
    se3_ofs_.open(se3_ofs_file_);
    arm_ofs_.open(arm_ofs_file_);
    base_ofs_.open(base_ofs_file_);
  }

  if (!se3_ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));
  if (!arm_ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));
  if (!base_ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));

  se3_ofs_ << WriteSE3ToString(camera_pose.inverted() * model_.Shaft().transform_) << "\n";
  for (size_t i = 4; i < arm_joints_.size(); ++i){
    se3_ofs_ << arm_joints_[i] + arm_offsets_[i] << "\n";
  }
  se3_ofs_ << std::endl;

  for (size_t i = 0; i < arm_joints_.size(); ++i){
    arm_ofs_ << arm_joints_[i] + arm_offsets_[i] << " ";
  }
  arm_ofs_ << std::endl;

  for (size_t i = 0; i < base_joints_.size(); ++i){
    base_ofs_ << base_joints_[i] + base_offsets_[i] << " ";
  }
  base_ofs_ << std::endl;

}

SE3DaVinciPoseGrabber::SE3DaVinciPoseGrabber(const ConfigReader &reader, const std::string &output_dir) : BaseDaVinciPoseGrabber(reader, output_dir) {

  self_name_ = "se3-davinci-grabber";
  checkSelfName(reader.get_element("name"));

  if (reader.get_element("joint") == "PSM1")
    target_joint_ = davinci::DaVinciJoint::PSM1;
  else if (reader.get_element("joint") == "PSM2")
    target_joint_ = davinci::DaVinciJoint::PSM2;
  else if (reader.get_element("joint") == "ECM")
    target_joint_ = davinci::DaVinciJoint::ECM;
  else
    throw std::runtime_error("Error, bad joint");

  ifs_.open(reader.get_element("pose-file"));
  ofs_file_ = output_dir + "/" + reader.get_element("output-pose-file");

  num_wrist_joints_ = 3; //should this load from config file?

  wrist_dh_params_ = std::vector<double>(num_wrist_joints_, 0.0);

}

bool SE3DaVinciPoseGrabber::LoadPose(const bool update_as_new){
  
  do_draw_ = false; //set to true only if we read a 'good' pose

  assert(num_wrist_joints_ == wrist_dh_params_.size());

  if (update_as_new){

    try{
      std::string line;
      int row = 0;
      //remember - also set psmatend rotation angle for tip to +- val rather than +- 0.5*val. aslo skipping frist 59 frames.
      ci::Vec3f translation;
      ci::Vec4f rotation;
      ci::Vec4f articulation;
      for (int i = 0; i < 3; ++i){
        ifs_ >> translation[i];
      }
      for (int i = 0; i < 4; ++i){
        ifs_ >> rotation[i];
      }
      for (int i = 0; i < 4; ++i){
        ifs_ >> articulation[i];
      }
      for (int i = 0; i < 3; ++i){
        wrist_dh_params_[i] = articulation[i];
      }
      ci::Quatf q(rotation[0], rotation[1], rotation[2], rotation[3]);
      ci::Matrix44f rot = q;
      for (int r = 0; r < 3; ++r){
        for (int c = 0; c < 3; ++c){
          shaft_pose_.at(r, c) = rot.at(r, c);
        }
      }

      shaft_pose_.setTranslate(translation);
      do_draw_ = true;

      // update the list of previous poses for plotting trajectories.
      reference_frame_tracks_.push_back(shaft_pose_);

    }
    catch (std::ifstream::failure e){
      shaft_pose_.setToIdentity();
      do_draw_ = false;
      return false;
    }

  }
  //if (do_draw_ == false) return false;

  model_.Shaft().transform_ = shaft_pose_;

  viz::davinci::PSMData psm;
  for (size_t i = 0; i < num_wrist_joints_; ++i){
    psm.jnt_pos[i] = wrist_dh_params_[i];
  }

  if (target_joint_ == davinci::PSM1)
    buildKinematicChainAtEndPSM1(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);
  else if (target_joint_ == davinci::PSM2)
    buildKinematicChainAtEndPSM2(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);

  return true;

}

void SE3DaVinciPoseGrabber::WritePoseToStream() {

  if (!ofs_.is_open()) {
    if (!boost::filesystem::exists(save_dir_)) {
      boost::filesystem::create_directory(save_dir_);
    }
    ofs_.open(ofs_file_);
  }

  if (!ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));

  ofs_ << WriteSE3ToString(model_.Shaft().transform_) << "\n";
  
  for (size_t i = 0; i < wrist_dh_params_.size(); ++i){
    ofs_ << wrist_dh_params_[i] << "\n";
  }
  ofs_ << "\n";
}

void SE3DaVinciPoseGrabber::WritePoseToStream(const ci::Matrix44f &camera_pose)  {

  if (!ofs_.is_open()) {
    if (!boost::filesystem::exists(save_dir_)) {
      boost::filesystem::create_directory(save_dir_);
    }
    ofs_.open(ofs_file_);
  }

  if (!ofs_.is_open()) throw(std::runtime_error("Error, could not open file"));

  ofs_ << WriteSE3ToString(camera_pose.inverted() * model_.Shaft().transform_) << "\n";
  for (size_t i = 0; i < wrist_dh_params_.size(); ++i){
    ofs_ << wrist_dh_params_[i] << "\n";
  }
  ofs_ << "\n";
  
}

void DHDaVinciPoseGrabber::GetModelPose(ci::Matrix44f &head, ci::Matrix44f &clasper_left, ci::Matrix44f &clasper_right){
	
	if (target_joint_ == davinci::PSM1 || target_joint_ == davinci::PSM2){

		viz::davinci::PSMData psm;

    for (std::size_t i = 0; i < base_joints_.size(); ++i){
			psm.sj_joint_angles[i] = base_joints_[i] + base_offsets_[i];
		}

		for (std::size_t i = 0; i < arm_joints_.size(); ++i){
			psm.jnt_pos[i] = arm_joints_[i] + arm_offsets_[i];
		}

		if (target_joint_ == davinci::PSM1)
			buildKinematicChainPSM1(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);
		else if (target_joint_ == davinci::PSM2)
			buildKinematicChainPSM2(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);

		head = model_.Head().transform_;
		clasper_left = model_.Clasper1().transform_;
		clasper_right = model_.Clasper2().transform_;

	}
	else{

		throw std::runtime_error("Error, bad joint type");

	}

}


void SE3DaVinciPoseGrabber::GetModelPose(ci::Matrix44f &head, ci::Matrix44f &clasper_left, ci::Matrix44f &clasper_right){

  if (target_joint_ == davinci::PSM1 || target_joint_ == davinci::PSM2){
  
    model_.Shaft().transform_ = shaft_pose_;

    viz::davinci::PSMData psm;
    for (size_t i = 0; i < num_wrist_joints_; ++i){
      psm.jnt_pos[i] = wrist_dh_params_[i];
    }


    if (target_joint_ == davinci::PSM1)
      buildKinematicChainPSM1(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);
    else if (target_joint_ == davinci::PSM2)
      buildKinematicChainPSM2(chain_, psm, model_.Shaft().transform_, model_.Head().transform_, model_.Clasper1().transform_, model_.Clasper2().transform_);

    head = model_.Head().transform_;
    clasper_left = model_.Clasper1().transform_;
    clasper_right = model_.Clasper2().transform_;

  }
  else{

    throw std::runtime_error("Error, bad joint type");

  }

}



void SE3DaVinciPoseGrabber::DrawBody(){

  model_.DrawBody();

}


void SE3DaVinciPoseGrabber::DrawHead(){

  model_.DrawHead();
  model_.DrawLeftClasper();
  model_.DrawRightClasper();

}

void DHDaVinciPoseGrabber::DrawBody(){

	model_.DrawBody();

}


void DHDaVinciPoseGrabber::DrawHead(){

	model_.DrawHead();
  model_.DrawLeftClasper();
  model_.DrawRightClasper();

}