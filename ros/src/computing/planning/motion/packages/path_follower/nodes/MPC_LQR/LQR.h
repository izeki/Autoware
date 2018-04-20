#ifndef MPC_LQR_H
#define MPC_LQR_H

#include <eigen3/Eigen/Core>
#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Point.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

#include <math.h>
#include <algorithm>

#include <path_follower_msgs/ApplanixPose.h>
#include <path_follower_msgs/ControllerTarget.h>
#include <path_follower_msgs/Trajectory2D.h>
#include <path_follower_msgs/TrajectoryPoint2D.h>
//#include <path_follower_msgs/TwistStamped.h>
#include <dbw_mkz_msgs/SteeringCmd.h>
//#include <path_follower/SteeringCmd.h>
#include <path_follower_msgs/state_Dynamic.h>
#include <path_follower_msgs/traj_plan.h>
#include <dbw_mkz_msgs/SteeringReport.h>
//#include <path_follower_msgs/SteeringReport.h>
#include <path_follower_msgs/Uout.h>
//#include <path_follower_msgs/Time.h>
#include <path_follower_msgs/MpcTime.h>
#include <path_follower_msgs/SteeringCurrent.h>

#include "vlrException.h"
#include "Vehicle.h"
#include "scaledTime.h"

#define NUM_STATES 6
#define NUM_CONTROLS 2
#define NUM_EXT_STATES (NUM_STATES+2*NUM_CONTROLS)
#define p_horizon 15
#define NUM_EEXT_STATES (NUM_STATES+2*NUM_CONTROLS+1)


namespace vlr {

class MPCController {
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  MPCController();
  ~MPCController();

  void run();
  void controlLoop();
  void changeParams();

private:
  template <class T> void getParam(std::string key, T& var);
  void trajectoryHandler(const path_follower_msgs::Trajectory2D trajectory);
  void currentstateHandler(const path_follower_msgs::state_Dynamic current_state);
  void steeringHandler(const dbw_mkz_msgs::SteeringReport msg);
  void getState();
  void getDesiredStates();
  void getDesiredStates_NearestP();


  void mpc_1(const Eigen::Matrix<double, NUM_STATES, 1> &s0, const Eigen::Matrix<double, NUM_CONTROLS, Eigen::Dynamic> &u0, const Eigen::Matrix<double,
   NUM_STATES, Eigen::Dynamic> &s_star, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_prev1, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_prev2, Eigen::Matrix<double, NUM_CONTROLS, Eigen::Dynamic> *u_out);
  void mpc_2(const Eigen::Matrix<double, NUM_STATES, 1> &s0, const Eigen::Matrix<double, NUM_CONTROLS, Eigen::Dynamic> &u0, const Eigen::Matrix<double,
   NUM_STATES, Eigen::Dynamic> &s_star, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_prev1, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_prev2, Eigen::Matrix<double, NUM_CONTROLS, Eigen::Dynamic> *u_out);
//  void mpcLQR(const Eigen::Matrix<double, NUM_STATES, 1> &s0, const Eigen::Matrix<double, NUM_CONTROLS, Eigen::Dynamic> &u0, const Eigen::Matrix<double,
 //  NUM_STATES, Eigen::Dynamic> &s_star, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_prev, Eigen::Matrix<double, NUM_CONTROLS, Eigen::Dynamic> *u_out);

  void dynamics(const Eigen::Matrix<double, NUM_STATES, 1> &s, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u, Eigen::Matrix<double, NUM_STATES, 1> *s_dot,
      Eigen::Matrix<double, NUM_STATES, NUM_STATES> *A = 0, Eigen::Matrix<double, NUM_STATES, NUM_CONTROLS> *B = 0);

  void simulateEuler(const Eigen::Matrix<double, NUM_STATES, 1> &s, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u,
      Eigen::Matrix<double, NUM_STATES, 1> *s_next, Eigen::Matrix<double, NUM_STATES, NUM_STATES> *A = 0, Eigen::Matrix<double, NUM_STATES, NUM_CONTROLS> *B = 0);
 
  void simulateRK4(const Eigen::Matrix<double, NUM_STATES, 1> &s, const Eigen::Matrix<double, NUM_CONTROLS, 1> &u,
      Eigen::Matrix<double, NUM_STATES, 1> *s_next, Eigen::Matrix<double, NUM_STATES, NUM_STATES> *A = 0, Eigen::Matrix<double, NUM_STATES, NUM_CONTROLS> *B = 0);

  double getdistance(path_follower_msgs::TrajectoryPoint2D Traj,  Eigen::Matrix<double, NUM_STATES, 1> State);
  visualization_msgs::Marker displayWayPoint(geometry_msgs::Point target);  
  void publishTargetWaypointsMarkers();
  

private:
  ros::NodeHandle nh_;
  ros::Subscriber /*currentpose_sub_, currentvelocity_sub_,*/current_state_sub_, trajectory_sub_, steering_sub_;
  ros::Publisher controller_target_pub_, steering_pub_, twist_pub_, s_next_pub_, des_traj_pub_, s_close_pub_, applanix_pub_, Uout_pub_, time_pub_, steering_current_pub_, vis_pub_;

  path_follower_msgs::ApplanixPose applanix_;  
  path_follower_msgs::Trajectory2D traj_;
  dbw_mkz_msgs::SteeringCmd steering_cmd_;
  //path_follower::SteeringCmd steering_cmd_;
  path_follower_msgs::ApplanixPose s_next_, s_close_;
  path_follower_msgs::traj_plan des_traj_;
  path_follower_msgs::ControllerTarget controller_target_;
  path_follower_msgs::Uout Uout_;
  path_follower_msgs::MpcTime time_;
  path_follower_msgs::SteeringCurrent steering_angle_;
  geometry_msgs::TwistStamped twist_;
  visualization_msgs::MarkerArray target_waypoints_marker_array;

  
  bool received_applanix_state_;
  bool received_trajectory_;
  int flag, s_flag, plan_flag, next_flag, mpc_pub_flag, sim_flag;
  bool start_time_flag_, mpc_flag;

  bool run_controller_;

  Eigen::Matrix<double, NUM_STATES, 1> state_, err_states_;



  Eigen::Matrix<double, NUM_CONTROLS, Eigen::Dynamic> controls_;
  Eigen::Matrix<double, NUM_STATES, Eigen::Dynamic> des_states_;
  Eigen::Vector2d errors_;

  Eigen::Matrix<double, NUM_CONTROLS, 1> ctl_err_;
  Eigen::Matrix<double, NUM_CONTROLS, 1> ctl_err_vel_;
  Eigen::Matrix<double, NUM_CONTROLS, 1> ctl_err_int_;

  //MSKenv_t m_mosek_env;

  double vel_err_int_;
  double last_dtheta_;
  double start_time;
  double steering_current_, pre_steering_angle;

  // parameters
  vehicle_state p_vs;
//  int p_horizon;
  double p_hertz;

  Eigen::Matrix<double, NUM_STATES, NUM_STATES> p_Q;
  Eigen::Matrix<double, NUM_STATES+1, NUM_STATES+1> p_Q2;
  Eigen::Matrix<double, NUM_CONTROLS, NUM_CONTROLS> p_R;
  Eigen::Matrix<double, NUM_CONTROLS, NUM_CONTROLS> p_R_delta;
  


  double p_q_lon, p_q_lat, p_q_theta, p_q_u, p_q_v, p_q_theta_dot, p_q_theta_ddot;
  double p_r_udot, p_r_delta, p_rd_udot, p_rd_delta;

  double p_vel_smooth, p_int_decay;

  double p_p_cte, p_d_cte, p_k_yawrate, p_k_aggressive;
  double p_p_lon_err;

  double p_max_vel_int, p_k_cruise_int, p_k_accel, p_k_decel;
};

}// namespace vlr
#endif 
