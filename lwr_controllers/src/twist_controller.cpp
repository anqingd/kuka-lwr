#include <lwr_controllers/twist_controller.h>
#include <pluginlib/class_list_macros.h>
#include <utils/pseudo_inversion.h>
#include "kdl/chainfksolvervel_recursive.hpp"

namespace lwr_controllers {

  // DEFAULT CONSTRUCTOR
  TwistController::TwistController(){
    // Nothing to do
  }

  // DEFAULT DESTRUCTOR
  TwistController::~TwistController(){
    // Nothing to do
  }

  // INITIALIZING FUNCTION
  bool TwistController::init(hardware_interface::PositionJointInterface *robot,
    ros::NodeHandle &n){
    // Calling the Initializing function of KinematicChainControllerBase
    if(!(KinematicChainControllerBase<hardware_interface::PositionJointInterface>::init(robot, n))){
        ROS_ERROR("Couldn't initilize TwistController.");
        return false;
    }

    // Resetting the solvers
    jnt_to_twist_solver_.reset(new KDL::ChainFkSolverVel_recursive(kdl_chain_));
    jnt_to_jac_solver_.reset(new KDL::ChainJntToJacSolver(kdl_chain_));

    // Resizing vectors and jacobian
    joint_states_.resize(kdl_chain_.getNrOfJoints());
    joint_vel_comm_.resize(kdl_chain_.getNrOfJoints());
    joint_comm_.resize(kdl_chain_.getNrOfJoints());
    jacobian_.resize(kdl_chain_.getNrOfJoints());

    // Initializing the cmd_flag_
    cmd_flag_ = 0;

    // Initializing subscriber to command topic
    sub_command_ = n.subscribe<geometry_msgs::Twist>("command", 1,
      &TwistController::command, this);

    return true;
  }

  // STARTING FUNCTION
  void TwistController::starting(const ros::Time& time){
    // Getting previous time
    last_time_ = time;

    // Setting desired twist to zero
    twist_des_ = KDL::Twist::Zero();

    // Reading the joint states (position and velocity) and setting command
    for(unsigned int i = 0; i < joint_handles_.size(); i++){
      joint_states_.q(i) = joint_handles_[i].getPosition();
      joint_states_.qdot(i) = joint_handles_[i].getVelocity();
      joint_comm_(i) = joint_states_.q(i);
      joint_handles_[i].setCommand(joint_comm_(i));
    }
  }

  // UPDATING FUNCTION
  void TwistController::update(const ros::Time& time,
    const ros::Duration& period){
    // THE CODE INSIDE NEXT IF EXECUTED ONLY IF twist_des_ != 0
    if(cmd_flag_){

      // Getting current time resolution and updating last_time_
      current_time_ = time;
      dt_ = current_time_ - last_time_;
      last_time_ = current_time_;

      // Reading the joint states (position and velocity)
      for(unsigned int i = 0; i < joint_handles_.size(); i++){
        joint_states_.q(i) = joint_handles_[i].getPosition();
        joint_states_.qdot(i) = joint_handles_[i].getVelocity();
      }

      // Setting command to current position (if cmd_flag_ = 0, position hold)
      joint_comm_ = joint_states_.q;

      // Forward computing current ee twist and then error
      jnt_to_twist_solver_->JntToCart(joint_states_, tmp_twist_meas_);
      twist_meas_ = tmp_twist_meas_.deriv();
      error_ = twist_des_ - twist_meas_;

      // Computing the current jacobian
      jnt_to_jac_solver_->JntToJac(joint_states_.q, jacobian_);

      // Computing the pseudo inverse of the jacobian
      pseudo_inverse(jacobian_.data, jacobian_pinv_);

      // Compute the joint velocities using jacobian pseudo inverse
      for(unsigned int i = 0; i < jacobian_pinv_.rows(); i++){
        joint_vel_comm_(i) = 0.0;
        for(unsigned int j = 0; j < jacobian_pinv_.cols(); j++){
          joint_vel_comm_(i) += jacobian_pinv_(i, j) * error_(j);
        }
      }

      // Integrating joint_vel_comm_ to find joint_comm_ (forward Euler)
      for(unsigned int i = 0; i < joint_handles_.size(); i++){
        joint_comm_(i) += joint_vel_comm_(i) * dt_.toSec();
      }

      // Saturating with joint limits
      for(unsigned int i = 0; i < joint_handles_.size(); i++){
        if(joint_comm_(i) < joint_limits_.min(i)){
          joint_comm_(i) = joint_limits_.min(i);
        }
        if(joint_comm_(i) > joint_limits_.max(i)){
          joint_comm_(i) = joint_limits_.max(i);
        }
      }

    }

    // Once we have the torques, setting the effort
    for(unsigned int i = 0; i < joint_handles_.size(); i++){
      joint_handles_[i].setCommand(joint_comm_(i));
    }
  }

  // COMMAND TOPIC CALLBACK FUNCTION
  void TwistController::command(const geometry_msgs::Twist::ConstPtr &msg){
    // Converting and saving msg to twist_des_
    twist_des_.vel(0) = msg->linear.x;
    twist_des_.vel(1) = msg->linear.y;
    twist_des_.vel(2) = msg->linear.z;
    twist_des_.rot(0) = msg->angular.x;
    twist_des_.rot(1) = msg->angular.y;
    twist_des_.rot(2) = msg->angular.z;

    // Setting cmd_flag_ according to the twist
    if(KDL::Equal(twist_des_, KDL::Twist::Zero(), 0.0001)) cmd_flag_ = 0;
    else cmd_flag_ = 1;
  }

}

PLUGINLIB_EXPORT_CLASS(lwr_controllers::TwistController, controller_interface::ControllerBase)
