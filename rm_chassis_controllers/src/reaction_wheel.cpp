//
// Created by qiayuan on 2022/4/17.
//
#include "rm_chassis_controllers/reaction_wheel.h"

#include <unsupported/Eigen/MatrixFunctions>
#include <rm_common/ros_utilities.h>
#include <rm_common/ori_tool.h>
#include <geometry_msgs/Quaternion.h>
#include <pluginlib/class_list_macros.hpp>

namespace rm_chassis_controllers
{
bool ReactionWheelController::init(hardware_interface::RobotHW* robot_hw, ros::NodeHandle& root_nh,
                                   ros::NodeHandle& controller_nh)
{
  if (!MultiInterfaceController::init(robot_hw, root_nh, controller_nh))
    return false;

  imu_handle_ = robot_hw->get<hardware_interface::ImuSensorInterface>()->getHandle(
      getParam(controller_nh, "imu_name", std::string("base_imu")));
  joint_handle_ = robot_hw->get<hardware_interface::EffortJointInterface>()->getHandle(
      getParam(controller_nh, "joint", std::string("reaction_wheel_joint")));

  double m_total, l, g;  //  m_total and l represent the total mass and distance between the pivot point to the
                         //  center of gravity of the whole system respectively.
  if (!controller_nh.getParam("m_total", m_total))
  {
    ROS_ERROR("Params m_total doesn't given (namespace: %s)", controller_nh.getNamespace().c_str());
    return false;
  }
  if (!controller_nh.getParam("l", l))
  {
    ROS_ERROR("Params l doesn't given (namespace: %s)", controller_nh.getNamespace().c_str());
    return false;
  }
  if (!controller_nh.getParam("g", g))
  {
    ROS_ERROR("Params g doesn't given (namespace: %s)", controller_nh.getNamespace().c_str());
    return false;
  }
  if (!controller_nh.getParam("inertia_total", inertia_total_))
  {
    ROS_ERROR("Params inertia_total doesn't given (namespace: %s)", controller_nh.getNamespace().c_str());
    return false;
  }
  if (!controller_nh.getParam("inertia_wheel", inertia_wheel_))
  {
    ROS_ERROR("Params inertia_wheel_ doesn't given (namespace: %s)", controller_nh.getNamespace().c_str());
    return false;
  }

  q_.setZero();
  r_.setZero();
  XmlRpc::XmlRpcValue q, r;
  controller_nh.getParam("q", q);
  controller_nh.getParam("r", r);
  // Check and get Q
  ROS_ASSERT(q.getType() == XmlRpc::XmlRpcValue::TypeArray);
  ROS_ASSERT(q.size() == STATE_DIM);
  for (int i = 0; i < STATE_DIM; ++i)
  {
    ROS_ASSERT(q[i].getType() == XmlRpc::XmlRpcValue::TypeDouble || q[i].getType() == XmlRpc::XmlRpcValue::TypeInt);
    if (q[i].getType() == XmlRpc::XmlRpcValue::TypeDouble)
      q_(i, i) = static_cast<double>(q[i]);
    else if (q[i].getType() == XmlRpc::XmlRpcValue::TypeInt)
      q_(i, i) = static_cast<int>(q[i]);
  }
  // Check and get R
  ROS_ASSERT(r.getType() == XmlRpc::XmlRpcValue::TypeArray);
  ROS_ASSERT(r.size() == CONTROL_DIM);
  for (int i = 0; i < CONTROL_DIM; ++i)
  {
    ROS_ASSERT(r[i].getType() == XmlRpc::XmlRpcValue::TypeDouble || r[i].getType() == XmlRpc::XmlRpcValue::TypeInt);
    if (r[i].getType() == XmlRpc::XmlRpcValue::TypeDouble)
      r_(i, i) = static_cast<double>(r[i]);
    else if (r[i].getType() == XmlRpc::XmlRpcValue::TypeInt)
      r_(i, i) = static_cast<int>(r[i]);
  }

  // Continuous model \dot{x} = A x + B u
  a_ << 0., 1 / inertia_total_, -1 / inertia_total_, m_total * l * g, 0., 0., 0, 0, 0;
  b_ << 0., 0., 1.;

  Lqr<double> lqr(a_, b_, q_, r_);
  if (!lqr.computeK())
  {
    ROS_ERROR("Failed to compute K of LQR.");
    return false;
  }

  k_ = lqr.getK();
  ROS_INFO_STREAM("K of LQR:" << k_);
  return true;
}

void ReactionWheelController::update(const ros::Time& time, const ros::Duration& period)
{
  double pitch_rate = imu_handle_.getAngularVelocity()[1];
  double wheel_rate = joint_handle_.getVelocity();

  // TODO: simplify, add new quatToRPY
  geometry_msgs::Quaternion quat;
  quat.x = imu_handle_.getOrientation()[0];
  quat.y = imu_handle_.getOrientation()[1];
  quat.z = imu_handle_.getOrientation()[2];
  quat.w = imu_handle_.getOrientation()[3];
  double roll{}, pitch{}, yaw{};
  quatToRPY(quat, roll, pitch, yaw);
  Eigen::Matrix<double, STATE_DIM, 1> x;
  x(0) = pitch;
  x(1) = inertia_total_ * pitch_rate + inertia_wheel_ * wheel_rate;
  x(2) = inertia_wheel_ * (pitch_rate + wheel_rate);
  Eigen::Matrix<double, CONTROL_DIM, 1> u;
  u = k_ * (-x);  // regulate to zero: K*(0 - x)
  joint_handle_.setCommand(u(0));
}

}  // namespace rm_chassis_controllers
PLUGINLIB_EXPORT_CLASS(rm_chassis_controllers::ReactionWheelController, controller_interface::ControllerBase)