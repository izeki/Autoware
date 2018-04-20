#include <iostream>
#include <fstream>
#include <eigen3/Eigen/Geometry>
#include <eigen3/Eigen/LU>
#include "LQR.h"
#include "QPsolver/qpOASES.hpp"
#pragma GCC push_options
#pragma GCC optimize("O2")

using namespace std;
using namespace Eigen;
USING_NAMESPACE_QPOASES

vlr::MPCController* mpc=NULL;

int main(int argc, char** argv) 
{
  ros::init(argc, argv, "controller");  
  try {
    mpc = new vlr::MPCController;
  }
  catch(vlr::Ex<>& e) {
    std::cout << e.what() << std::endl;
  }

  try {
    mpc->run();
  }
  catch(vlr::Ex<>& e) {
    std::cout << e.what() << std::endl;
  }
  delete mpc;
  return 0;
}


namespace vlr {
Eigen::Matrix<double, NUM_STATES, 1> s_close;
Eigen::Matrix<double, NUM_CONTROLS, 1> u_prev2;
Eigen::Matrix<double, NUM_CONTROLS, 1> u_prev1;
double solver_start, solver_end ,t1,t2,t3,t4,t5,t6,t7,t8,t9;
MPCController::MPCController() : nh_(), received_applanix_state_(false),received_trajectory_(false), 
        start_time_flag_(true), flag(0), s_flag(0),mpc_flag(true), plan_flag(0), next_flag(0), mpc_pub_flag(0),
        run_controller_(false) {
p_hertz = 20;
//p_horizon = 20;

p_Q.setZero();
p_R.setZero();
p_R_delta.setZero();
p_vs.set_mkz_params();
changeParams();
des_states_.resize(NUM_STATES, p_horizon + 1);
controls_.resize(NUM_CONTROLS, p_horizon);
controls_.setZero();
//cout<<"controls_(0,0) = "<<controls_(0,0)<<"controls_(1,0) = "<<controls_(1,0)<<endl;

errors_.setZero();
ctl_err_vel_.setZero();
ctl_err_int_.setZero();
ctl_err_.setZero();
vel_err_int_ = 0;
last_dtheta_ = 0;

current_state_sub_ = nh_.subscribe("state_estimate", 5, &MPCController::currentstateHandler, this);
trajectory_sub_ = nh_.subscribe("final_trajectory", 5, &MPCController::trajectoryHandler, this);
steering_sub_ = nh_.subscribe("/vehicle/steering_report", 5, &MPCController::steeringHandler, this);

controller_target_pub_  = nh_.advertise<path_follower_msgs::ControllerTarget>("ControllerTarget", 1);
//steering_pub_ = nh_.advertise<dbw_mkz_msgs::SteeringCmd>("vehicle/steering_cmd",1);
steering_pub_ = nh_.advertise<dbw_mkz_msgs::SteeringCmd>("vehicle/steering_cmd",1);
twist_pub_ = nh_.advertise<geometry_msgs::TwistStamped>("twist_cmd", 1);
s_next_pub_ = nh_.advertise<path_follower_msgs::ApplanixPose>("s_next_", 30);
s_close_pub_ = nh_.advertise<path_follower_msgs::ApplanixPose>("s_close_",30);
des_traj_pub_ =nh_.advertise<path_follower_msgs::traj_plan>("des_traj_",30);
applanix_pub_ = nh_.advertise<path_follower_msgs::ApplanixPose>("Applanix_",30);
Uout_pub_ = nh_.advertise<path_follower_msgs::Uout>("Uout_",50);
time_pub_ = nh_.advertise<path_follower_msgs::MpcTime>("MpcTime_",30);
//err_states_pub_ = nh_.advertise<path_follower::ApplanixPose>("err_states_",10);
}

MPCController::~MPCController() {
}

void MPCController::run() {
  ros::Rate r(p_hertz);
  while (ros::ok()) {
    controlLoop();
    ros::spinOnce();
    r.sleep();
  }
}

// parameter change callback (not invoked)
void MPCController::changeParams() {
  p_q_lon = 1.5;
  p_q_lat = 1.5;
  p_q_theta = 0.01;
  p_q_u = 0.01;
  p_q_v = 0.01;
  p_q_theta_dot = 0.1;

  p_r_udot = 0.01;
  p_r_delta =0.10;
  p_rd_udot =0.01;
  p_rd_delta =0.01;

  p_Q(0, 0) = p_q_lon;
  p_Q(1, 1) = p_q_lat;
  p_Q(2, 2) = p_q_theta;
  p_Q(3, 3) = p_q_u;
  p_Q(4, 4) = p_q_v;
  p_Q(5, 5) = p_q_theta_dot;

  p_R(0, 0) = p_r_udot;
  p_R(1, 1) = p_r_delta;
  p_R_delta(0, 0) = p_rd_udot;
  p_R_delta(1, 1) = p_rd_delta;
}

void MPCController::currentstateHandler(const path_follower_msgs::state_Dynamic current_state){
  applanix_.vel_north  = current_state.vx;
  applanix_.vel_east   = current_state.vy;
  applanix_.smooth_x   = current_state.X;
  applanix_.smooth_y   = current_state.Y;
  applanix_.yaw        = current_state.psi; 
  applanix_.rate_yaw   = current_state.wz;
  received_applanix_state_ = true; 
  if (start_time_flag_ && received_applanix_state_){
    start_time = Time::current();
    start_time_flag_ = false;
  }
}

void MPCController::steeringHandler(const dbw_mkz_msgs::SteeringReport msg){
  steering_current_ = msg.steering_wheel_angle;
}


void MPCController::trajectoryHandler(const path_follower_msgs::Trajectory2D trajectory){
  traj_ = trajectory;
  received_trajectory_ = true;
}


// main controller loop
void MPCController::controlLoop() {
  if (!received_applanix_state_ || !received_trajectory_) {
    return;
  }
  double t_start = Time::current();
  // if we haven't gotten anything for a while, reset
  if (  traj_.header.stamp.toSec() < applanix_.timestamp - 10.0 ) {
    controls_.setZero();
    ctl_err_.setZero();
    ctl_err_vel_.setZero();
    ctl_err_int_.setZero();
    vel_err_int_ = 0;
    last_dtheta_ = 0;
    received_applanix_state_ = false;
    received_trajectory_ = false;
    cout << "No commands in a while, resetting controller..." << endl;
    return;
  }
  double mpc_start;
  double mpc_end;
//compute desired control (delta, velocity)
  getState();
  applanix_.timestamp  = Time::current();
  applanix_pub_.publish(applanix_);
  getDesiredStates();

// shift controls and run MPC
  Eigen::Matrix<double, NUM_STATES, Dynamic> s_pred(NUM_STATES, p_horizon + 1);
  Eigen::Matrix<double, NUM_STATES, 1> s;
  controls_.block(0, 0, NUM_CONTROLS, p_horizon - 1) = controls_.block(0, 1, NUM_CONTROLS, p_horizon - 1);  

// set initial u_prev
  if(mpc_flag == true ) {
    if (mpc_pub_flag > 0){ 
      if(mpc_pub_flag > 1)
        u_prev2 = u_prev1;
      u_prev1 = controls_.col(0);
    }
    else{
      u_prev1.setZero();
      u_prev2.setZero();
    }
    for(int i = 0; i < 1; i++) {
      mpc_start=Time::current();
      mpcLQR(state_, controls_, des_states_, u_prev1,u_prev2, &controls_);
      //mpcLQR(err_states_, controls_, des_states_, u_prev, &controls_);
      //mpcLQR(s_close, controls_, des_states_, u_prev1, u_prev2, &controls_); 
       mpc_end=Time::current(); 
    }
    mpc_pub_flag++; 
    //publish steering cmd 
    double dt = 1.0 / p_hertz;
    double steering_angle = controls_(1, 0) * p_vs.param.steering_ratio;
    //cout<<"steering_angle="<<steering_angle<<endl;
    steering_cmd_.enable = true;
    steering_cmd_.steering_wheel_angle_cmd = steering_angle;
    steering_cmd_.steering_wheel_angle_velocity = (steering_angle - steering_current_)/dt;
    steering_pub_.publish(steering_cmd_);

    // publish velocity cmd to compute throttle 
    twist_.twist.linear.x = des_states_(3);
    twist_pub_.publish(twist_);
  }

  else{
  // publish steering cmd 
    double dt = 1.0 / p_hertz;
    double steering_angle = 0;
    steering_cmd_.enable = true;
    steering_cmd_.steering_wheel_angle_cmd = steering_angle;
    steering_cmd_.steering_wheel_angle_velocity = 0;
    steering_pub_.publish(steering_cmd_);

  // publish velocity cmd to compute throttle 
    twist_.twist.linear.x = state_(3)-dt*2;
    twist_pub_.publish(twist_);
  }

// publish errors
  controller_target_.target_velocity = des_states_(3, 0);
  controller_target_.target_steering_angle = controls_(1, 0) * p_vs.param.steering_ratio;
  controller_target_.cross_track_error = -sin(des_states_(2, 0)) * (state_(0) - des_states_(0, 0)) + cos(des_states_(2, 0)) * (state_(1) - des_states_(1, 0));
  controller_target_.heading_error = (state_(2) - des_states_(2, 0));
  controller_target_.timestamp = Time::current();
  controller_target_pub_.publish(controller_target_);  
  double t_end = Time::current();
 
  time_.t_start = t_start;
  time_.t_end = t_end;
  time_.mpc_start = mpc_start;
  time_.mpc_end = mpc_end;
  time_.solver_start=solver_start;
  time_.solver_end =solver_end;
  time_.t1 =t1;
  time_.t2 =t2;
  time_.t3 =t3;
  time_.t4 =t4;
  time_.t5 =t5;
  time_.t6 =t6;
  time_.t7 =t7;
  time_.t8 =t8;
  time_.t9 =t9;

  time_pub_.publish(time_);

  if((t_end - t_start)> 1/p_hertz){
    printf("WARNING : mpc control frequency < %e Hz\n", p_hertz);
  }

}

//---------------------------------------------------------------------------------
// get current state of the car (really a delta state from desired state)
void MPCController::getState() 
{  
  double cos_th = cos(state_(2)), sin_th = sin(state_(2));
  double imu_to_cg = p_vs.param.b;
  state_(0) = applanix_.smooth_x + cos(applanix_.yaw) * imu_to_cg;
  state_(1) = applanix_.smooth_y + sin(applanix_.yaw) * imu_to_cg;  
  state_(2) = applanix_.yaw;
  state_(5) = applanix_.rate_yaw;
  state_(3) = applanix_.vel_north;
  state_(4) = applanix_.vel_east + state_(5) * imu_to_cg;
}

// get desired states from trajecotry and controls using Newton's method
void MPCController::getDesiredStates() {
  double t = applanix_.timestamp, alpha;
  double ra_to_cg = p_vs.param.b;
  int j = 0;
  // find all desired positions
  // path_follower::TrajectoryPoint2D *p1, *p2;
  double dt = 1.0 / p_hertz;
  for (int i = 0; i < p_horizon + 1; i++) {
    // if the trajectory ends, stop mpc
    if(i==0 && ( traj_.point[(int)traj_.point.size()-1].t + start_time) < t)
      mpc_flag = false;
    while ( (traj_.point[j + 1].t + start_time) < t && j < (int)traj_.point.size() - 2)
       j++;

    path_follower_msgs::TrajectoryPoint2D& p1 = traj_.point[j];       // TODO: Check if copy is better than reference
    path_follower_msgs::TrajectoryPoint2D& p2 = traj_.point[j + 1];
      
    while (p2.theta - p1.theta > M_PI)
      p2.theta -= 2 * M_PI;
    while (p2.theta - p1.theta < -M_PI)
      p2.theta += 2 * M_PI;
    // integrate to create a smoothed trajectory   
    alpha = (t - p1.t -start_time) / (p2.t - p1.t);
    if (alpha > 1) alpha = 1;
    if (alpha < 0) alpha = 0;
    des_states_(0, i) = (1 - alpha) * p1.x + alpha * p2.x;
    des_states_(1, i) = (1 - alpha) * p1.y + alpha * p2.y;
    des_states_(2, i) = (1 - alpha) * p1.theta + alpha * p2.theta;
    des_states_(0, i) += cos(des_states_(2, i)) * ra_to_cg;
    des_states_(1, i) += sin(des_states_(2, i)) * ra_to_cg;
    des_states_(3, i) = (1 - alpha) * p1.v + alpha * p2.v;
    des_states_(4, i) = 0.0;
    des_states_(5, i) =  des_states_(3,i)*((1 - alpha) * p1.kappa + alpha * p2.kappa);
    //des_states_(5, i) =  (1 - alpha) * p1.kappa + alpha * p2.kappa;    //when traj.mat stores 'yaw_rate' instead of 'kappa'
    t += dt;

    //For close_traj mpc : get the initial s_close
    if(flag <1 ){
      s_close = des_states_.col(0);
      flag ++;
    } 

    //For initial position error debug:
    if(i==0){
      err_states_(0,0)= des_states_(0, i) - 7;
      err_states_(1,0)= des_states_(1, i) - 20;
      err_states_(2,0)= des_states_(2, i);
      err_states_(3,0)= des_states_(3, i);
      err_states_(4,0)= des_states_(4, i);
      err_states_(5,0)= des_states_(5, i);
    } 

    des_traj_.x=des_states_(0,i);
    des_traj_.y=des_states_(1,i);
    des_traj_.theta=des_states_(2,i);
    des_traj_.v=des_states_(3,i);
    des_traj_.rate_yaw=des_states_(5,i);
  //if(plan_flag < 20 && mpc_pub_flag >= 1){
    des_traj_pub_.publish(des_traj_); 
  //plan_flag ++;
  //}
  }
  
  // normalize desired angles properly
  while (des_states_(2, 0) - state_(2) > M_PI)
    des_states_(2, 0) -= 2 * M_PI;
  while (des_states_(2, 0) - state_(2) < -M_PI)
    des_states_(2, 0) += 2 * M_PI;
  for (int i = 1; i < p_horizon + 1; i++) {
    while (des_states_(2, i) - des_states_(2, i - 1) > M_PI)
      des_states_(2, i) -= 2 * M_PI;
    while (des_states_(2, i) - des_states_(2, i - 1) < -M_PI)
      des_states_(2, i) += 2 * M_PI;
  }
  errors_ += 0.05 * (Rotation2D<double> (-state_(2)).toRotationMatrix() * (state_.block(0, 0, 2, 1) - des_states_.block(0, 0, 2, 1)) - errors_);
}

// convert array to real_t type
void Array2realt(Eigen::ArrayXd A, real_t *t){
  int k = A.size();
  for(int i=0; i < k; i++){
    t[i] = A[i];
  }
}

// model predictive control using qpOASES for optimization
void MPCController::mpcLQR( const Eigen::Matrix<double, NUM_STATES, 1> &s0, 
                            const Eigen::Matrix<double, NUM_CONTROLS, Dynamic> &u0,
                            const Eigen::Matrix<double, NUM_STATES, Dynamic> &s_star, 
                            const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_prev1, 
                            const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_prev2,
                            Eigen::Matrix<double, NUM_CONTROLS, Dynamic> *u_out) {
  USING_NAMESPACE_QPOASES
//  Eigen::Matrix<double, NUM_EXT_STATES, 1> s_opt, q, ds;
  Eigen::Matrix<double, NUM_STATES, Dynamic> s(NUM_STATES, p_horizon + 1);
  Eigen::Matrix<double, NUM_STATES, NUM_STATES> A[p_horizon];
  Eigen::Matrix<double, NUM_STATES, NUM_CONTROLS> B[p_horizon];
  Eigen::Matrix<double, NUM_EXT_STATES , NUM_EXT_STATES > P, Q, Ae[p_horizon], Qe[p_horizon];
  Eigen::Matrix<double, NUM_EXT_STATES, NUM_CONTROLS> Be[p_horizon];
//  Eigen::Matrix<double, NUM_CONTROLS, 1>  u_opt;             
  Eigen::Matrix<double, NUM_STATES, 1> s_next;
  Eigen::Matrix<double, NUM_CONTROLS, NUM_CONTROLS> R;
  Eigen::Matrix<double, NUM_EXT_STATES * (p_horizon+1), NUM_EXT_STATES> Sx;
//  Eigen::Matrix<double, NUM_EXT_STATES * (p_horizon+1), NUM_CONTROLS * p_horizon> Su;
  Eigen::Matrix<double, NUM_CONTROLS * p_horizon, NUM_CONTROLS * p_horizon> Rbar, H;
//  Eigen::Matrix<double, NUM_CONTROLS * p_horizon, NUM_CONTROLS * p_horizon> H;
  Eigen::Matrix<double, NUM_CONTROLS * p_horizon, 1> g;
  Eigen::Matrix<double, NUM_CONTROLS, 1>lb_unit, ub_unit;
  Eigen::Matrix<double, NUM_EXT_STATES* (p_horizon+1), 1>  Sx_x0;
  Eigen::Matrix<double, NUM_CONTROLS * p_horizon,1> lb, ub;
  Eigen::Matrix<double, NUM_EXT_STATES, 1> x0;
  Eigen::Matrix<double, NUM_EXT_STATES, 1> de[p_horizon];
  Eigen::Matrix<double, NUM_EXT_STATES * p_horizon, 1>De;
  Eigen::Matrix<double, NUM_EXT_STATES* (p_horizon+1), 1>Xref;
//  Eigen::Matrix<double, NUM_EXT_STATES * (p_horizon+1), Dynamic> Sd;
  Eigen::MatrixXd Qbar(NUM_EXT_STATES * (p_horizon+1), NUM_EXT_STATES *(p_horizon+1));
  Eigen::MatrixXd Sd(NUM_EXT_STATES*(p_horizon+1), NUM_EXT_STATES*p_horizon);
  Eigen::MatrixXd Su(NUM_EXT_STATES * (p_horizon+1), NUM_CONTROLS * p_horizon);
//  Eigen::MatrixXd Qbar_Su(NUM_EXT_STATES * (p_horizon+1),  NUM_CONTROLS * p_horizon);
  Eigen::MatrixXd Sutr_Qbar(NUM_EXT_STATES * (p_horizon+1), NUM_EXT_STATES *(p_horizon+1));



//set the initial x0  
  s.col(0) = s0;
  x0.setZero();
  x0.block(0,0,NUM_STATES, 1) = s0;
  x0.block(NUM_STATES,0,NUM_CONTROLS,1) = u_prev1;
  x0.block(NUM_STATES+NUM_CONTROLS,0,NUM_CONTROLS,1) = u_prev2;
//  cout<<"x0="<<endl<<x0<<endl;
  t1= Time::current();
// initialize cost and dynamics matrices
  for (int i = 0; i < p_horizon; i++) {
 //   cout<<"s.col(0)"<<endl<<s.col(i)<<endl;
 //   cout<<"u0.col(i)"<<endl<<u0.col(i)<<endl;
    simulateRK4(s.col(i), u0.col(i), &s_next, &A[i], &B[i]);
    s.col(i + 1) = s_next;
  //  cout<<"s="<<s_next<<endl;
  //  if(mpc_pub_flag <1)
  //   cout<<"A[]="<<endl<<A[i]<<endl;
  }


  // initialize weight matrix
  Q.setZero();
  Q.block(0, 0, NUM_STATES, NUM_STATES) = p_Q;
  R = Rotation2D<double> (s_star(2, p_horizon)).toRotationMatrix();
  Q.block(0, 0, 2, 2) = R.transpose() * p_Q.block(0, 0, 2, 2) * R;
//  cout<<"Qblock"<<endl<< Q.block(0, 0, NUM_STATES, NUM_STATES)<<endl;
  Q.block(NUM_STATES, NUM_STATES, NUM_CONTROLS, NUM_CONTROLS) = p_R_delta;
  Q.block(NUM_STATES + NUM_CONTROLS, NUM_STATES, NUM_CONTROLS, NUM_CONTROLS) = -p_R_delta;
  Q.block(NUM_STATES, NUM_STATES + NUM_CONTROLS, NUM_CONTROLS, NUM_CONTROLS) = -p_R_delta;
  Q.block(NUM_STATES + NUM_CONTROLS, NUM_STATES + NUM_CONTROLS, NUM_CONTROLS, NUM_CONTROLS) = p_R_delta;
  P = Q;
  t2=Time::current();
  for (int i = p_horizon - 1; i >= 0; i--) {
    Qe[i] = Q;
    R = Rotation2D<double> (s_star(2, i)).toRotationMatrix();
    Qe[i].block(0, 0, 2, 2) = R.transpose() * p_Q.block(0, 0, 2, 2) * R;
  // initialize extended dynamics matrices
    Ae[i].setZero();
    Ae[i].block(NUM_STATES + NUM_CONTROLS, NUM_STATES, NUM_CONTROLS, NUM_CONTROLS) = Eigen::Matrix<double, NUM_CONTROLS, NUM_CONTROLS>::Identity();
    Be[i].setZero();
    Be[i].block(NUM_STATES, 0, NUM_CONTROLS, NUM_CONTROLS) = Eigen::Matrix<double, NUM_CONTROLS, NUM_CONTROLS>::Identity();
    Ae[i].block(0, 0, NUM_STATES, NUM_STATES) = A[i];
    Be[i].block(0, 0, NUM_STATES, NUM_CONTROLS) = B[i];
  }

// Set QProblem
  QProblemB example( NUM_CONTROLS * p_horizon);
  Options options;
  example.setOptions( options );
// initialize qpProblem matrices
  Sx.setZero();
  Su.setZero();
  Sd.setZero();  
  Rbar.setZero();
  Qbar.setZero();
  t3=Time::current();
// Define Sx
  Sx.block(0, 0, NUM_EXT_STATES, NUM_EXT_STATES) = Eigen::Matrix<double, NUM_EXT_STATES, NUM_EXT_STATES>::Identity(); 
  for(int i=0; i<p_horizon; i++){
    Sx.block(NUM_EXT_STATES * (i+1), 0, NUM_EXT_STATES, NUM_EXT_STATES) = Ae[i] * Sx.block(NUM_EXT_STATES * i, 0, NUM_EXT_STATES, NUM_EXT_STATES);
  }
  //cout<<"Sx="<<endl<<Sx<<endl;
t4=Time::current();
// Define Su, Sd
  for (int column =0; column < p_horizon; column++){
    Su.block(NUM_EXT_STATES *(column+1), NUM_CONTROLS * column, NUM_EXT_STATES, NUM_CONTROLS) = Be[column];
    Sd.block(NUM_EXT_STATES*(column+1), NUM_EXT_STATES*column, NUM_EXT_STATES,NUM_EXT_STATES) = Eigen::Matrix<double, NUM_EXT_STATES, NUM_EXT_STATES>::Identity();
    for (int row = column +1; row < p_horizon; row++){
      Su.block(NUM_EXT_STATES *(row+1), NUM_CONTROLS * column, NUM_EXT_STATES, NUM_CONTROLS) = Ae[row-1] * Su.block(NUM_EXT_STATES * row, NUM_CONTROLS* column, NUM_EXT_STATES, NUM_CONTROLS);    
      Sd.block(NUM_EXT_STATES * (row+1), NUM_EXT_STATES* column, NUM_EXT_STATES, NUM_EXT_STATES) = Ae[row-1] * Sd.block(NUM_EXT_STATES * row, NUM_EXT_STATES* column, NUM_EXT_STATES, NUM_EXT_STATES);
    }
  }
 //  cout<<"Su="<<endl;
t5=Time::current();
  //Define de[i], De, Sx
  for(int i=0; i<p_horizon;i++){
  //  Sx.block(NUM_EXT_STATES * (i+1), 0, NUM_EXT_STATES, NUM_EXT_STATES) = Ae[i] * Sx.block(NUM_EXT_STATES * i, 0, NUM_EXT_STATES, NUM_EXT_STATES);
    de[i].setZero();
    de[i].block(0,0,NUM_STATES,1) =  s.col(i+1) - A[i] * s.col(i) -B[i] * u0.col(i);
    De.block(NUM_EXT_STATES*i, 0,NUM_EXT_STATES,1) = de[i];
  }


  //Define Xref
  Xref.setZero();
  for(int i=0;i<(p_horizon+1);i++){
      Xref.block(NUM_EXT_STATES*i,0,NUM_STATES,1) = s_star.col(i);}


 // Define Qbar Rbar
  for (int i=0; i< (p_horizon+1); i++){ 
    if (i< p_horizon){
      Rbar.block(NUM_CONTROLS * i, NUM_CONTROLS * i, NUM_CONTROLS, NUM_CONTROLS) = p_R;
      Qbar.block(NUM_EXT_STATES * i, NUM_EXT_STATES * i, NUM_EXT_STATES, NUM_EXT_STATES) = Qe[i];
/*    Xref.block(NUM_EXT_STATES*i,0,NUM_STATES,1) = s_star.col(i);
      Sx.block(NUM_EXT_STATES * (i+1), 0, NUM_EXT_STATES, NUM_EXT_STATES) = Ae[i] * Sx.block(NUM_EXT_STATES * i, 0, NUM_EXT_STATES, NUM_EXT_STATES);
      de[i].setZero();
      de[i].block(0,0,NUM_STATES,1) =  s.col(i+1) - A[i] * s.col(i) -B[i] * u0.col(i);
      De.block(NUM_EXT_STATES*i, 0,NUM_EXT_STATES,1) = de[i];*/
    }
    else {
      Qbar.block(NUM_EXT_STATES * i, NUM_EXT_STATES * i, NUM_EXT_STATES, NUM_EXT_STATES) = P;
//      Xref.block(NUM_EXT_STATES * i,0,NUM_STATES,1) = s_star.col(i);
    }
  }

  t6=Time::current();
  Sutr_Qbar = Su.transpose() * Qbar;
  H = 2 * (Sutr_Qbar * Su + Rbar) ;  
  t7=Time::current();
  g = 2 * Sutr_Qbar *( Sx * x0 + Sd * De - Xref);
  t8 = Time::current();
//  cout<<"H="<<endl<<H<<endl;
//Compute contraints matrices
// constraint on U
  ub_unit <<  20,                                     // u_dot: m/s^2
              p_vs.param.max_wheel_angle;     // wheel_angle: rad
  lb_unit <<  -20,                                    // u_dot: m/s^2
              -p_vs.param.max_wheel_angle;    // wheel_angle: rad

 // Sx_x0 = Sx * x0;
  for(int i=0; i<p_horizon; i++){
    lb.block(NUM_CONTROLS * i, 0, NUM_CONTROLS, 1) = lb_unit;
    ub.block(NUM_CONTROLS * i, 0, NUM_CONTROLS, 1) = ub_unit;
  }
t9=Time::current();
  //* Solve first QP. 
  int_t nWSR = 20000;
  Eigen::MatrixXd Ht = H.transpose();
  ArrayXd aH_  = Map<const ArrayXd>(Ht.data(), Ht.size());
  ArrayXd ag_  = Map<const ArrayXd>(g.data(), g.size()); 
  Eigen::MatrixXd Sut = Su.transpose();
  ArrayXd aSu_ = Map<const ArrayXd>(Sut.data(), Sut.size());

  ArrayXd alb_ = Map<const ArrayXd>(lb.data(), lb.size());
  ArrayXd aub_ = Map<const ArrayXd>(ub.data(), ub.size());

  // array to real_t
  real_t H_[aH_.size()];  
  real_t g_[ag_.size()];
  real_t Su_[aSu_.size()];
  real_t lb_[alb_.size()];
  real_t ub_[aub_.size()];
  Array2realt(aH_, H_);
  Array2realt(ag_, g_);
  Array2realt(aSu_, Su_);
  Array2realt(alb_, lb_);
  Array2realt(aub_, ub_);
//  cout<<"H_="<<H_[0]<<","<<H_[1]<<","<<H_[2]<<","<<H_[3]<<","<<H_[4]<<endl;
//  cout<<"g_ ="<<g_[0]<<","<<g_[1]<<","<<g_[2]<<","<<g_[3]<<","<<g_[4]<<endl;

  solver_start=Time::current();
  example.init( H_, g_, lb_, ub_, nWSR, 0 );
  solver_end = Time::current();
/* Get and print solution of first QP. */ 
  real_t uOpt[p_horizon * NUM_CONTROLS];

  example.getPrimalSolution( uOpt );
 printf( "\nuOpt = [ %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n %e, %e \n];\n", 
      uOpt[0],uOpt[1],uOpt[2],uOpt[3],uOpt[4],uOpt[5],uOpt[6],uOpt[7],uOpt[8],uOpt[9],uOpt[10],uOpt[11],uOpt[12],uOpt[13],uOpt[14],uOpt[15],uOpt[16],uOpt[17],uOpt[18],uOpt[19],uOpt[20],uOpt[21],uOpt[22],uOpt[23],uOpt[24],uOpt[25], uOpt[26],uOpt[27],uOpt[28],uOpt[29],uOpt[30],uOpt[31],uOpt[32],uOpt[33],uOpt[34],uOpt[35], uOpt[36],uOpt[37],uOpt[38],uOpt[39]);
  // simulate forward
 for(int i=0; i< p_horizon;i++){
    Uout_.acceleration = uOpt[NUM_CONTROLS*i];
    Uout_.del = uOpt[NUM_CONTROLS*i+1];
    Uout_pub_.publish(Uout_);
 }


  s_next = s0;
  for (int i = 0; i < p_horizon; i++) {
    s_next_.smooth_x  = s_next(0,0);
    s_next_.smooth_y  = s_next(1,0);
    s_next_.yaw       = s_next(2,0);  
    s_next_.rate_yaw  = s_next(5,0);
    s_next_.vel_north = s_next(3,0);
    s_next_.vel_east  = s_next(4,0);
    s_next_.timestamp = Time::current();
    s_next_pub_.publish(s_next_);

    u_out->col(i) << uOpt[i*2], uOpt[i*2+1];
  //  cout<<"u_out="<<u_out->col(i)<<endl;
    
    simulateRK4(s_next, u_out->col(i), &s_next);
    
    if(i == 0){
      s_close = s_next;
      s_close_.smooth_x  = s_next(0,0);
      s_close_.smooth_y  = s_next(1,0);
      s_close_.yaw       = s_next(2,0);  
      s_close_.vel_north = s_next(3,0);
      s_close_.vel_east  = s_next(4,0);
      s_close_.rate_yaw  = s_next(5,0);
      s_close_.timestamp = Time::current();
      s_close_pub_.publish(s_close_);
    }
  }
}

#define EPSILON 1e-5

// dynamics of the car (bicycle model with velocity/steering input)
void MPCController::dynamics( const Eigen::Matrix<double, NUM_STATES, 1> &s, 
                              const Eigen::Matrix<double, NUM_CONTROLS, 1> &u_, 
                              Eigen::Matrix<double, NUM_STATES, 1> *s_dot, 
                              Eigen::Matrix<double, NUM_STATES, NUM_STATES> *A, 
                              Eigen::Matrix<double, NUM_STATES, NUM_CONTROLS> *B){
  double u = s(3), v = s(4), cos_th = cos(s(2)), sin_th = sin(s(2));
  double th_dot = s(5), u_dot = u_(0);
  double del = u_(1), tan_del = tan(u_(1)), cos_del = cos(u_(1));
  double Fyf, Fyr, Caf = p_vs.param.ftire_stiffness, 
  Car = p_vs.param.rtire_stiffness, m = p_vs.param.mass, 
  a = p_vs.param.a, b = p_vs.param.b, I = p_vs.param.iz;
// compute slip angles and lateral forces
  Fyf = -Caf * (atan2(v + th_dot * a, max(u, dgc::dgc_mph2ms(5))) - del);
  Fyr = -Car * atan2(v - th_dot * b, max(u, dgc::dgc_mph2ms(5)));

// compute derivatives
  (*s_dot)(0) = u * cos_th - v * sin_th;
  (*s_dot)(1) = u * sin_th + v * cos_th;
  (*s_dot)(2) = th_dot;

  (*s_dot)(3) = u_dot;
  (*s_dot)(4) = tan_del * (u_dot - th_dot * v) + (Fyf / cos_del + Fyr) / m - th_dot * u;
  (*s_dot)(5) = m * a / I * tan_del * (u_dot - th_dot * v) + a * Fyf / (I * cos_del) - b * Fyr / I;

// compute Jacobians (numerically) if desired
  if (A != 0) {
    Eigen::Matrix<double, NUM_STATES, 1> s2 = s;
    Eigen::Matrix<double, NUM_STATES, 1> s_dot1, s_dot2;
    for (int i = 0; i < NUM_STATES; i++) {
      s2(i) += EPSILON;
      dynamics(s2, u_, &s_dot1, 0, 0);
      s2(i) -= 2 * EPSILON;
      dynamics(s2, u_, &s_dot2, 0, 0);
      s2(i) += EPSILON;
      A->col(i) = (s_dot1 - s_dot2) / (2 * EPSILON);
    }
  }
  if (B != 0) {
    Eigen::Matrix<double, NUM_CONTROLS, 1> u2 = u_;
    Eigen::Matrix<double, NUM_STATES, 1> s_dot1, s_dot2;
    for (int i = 0; i < NUM_CONTROLS; i++) {
      u2(i) += EPSILON;
      dynamics(s, u2, &s_dot1, 0, 0);
      u2(i) -= 2 * EPSILON;
      dynamics(s, u2, &s_dot2, 0, 0);
      u2(i) += EPSILON;
      B->col(i) = (s_dot1 - s_dot2) / (2 * EPSILON);
    }
  }
}


void MPCController::simulateEuler(const Eigen::Matrix<double, NUM_STATES, 1> &s, 
                                  const Eigen::Matrix<double, NUM_CONTROLS, 1> &u, 
                                  Eigen::Matrix<double, NUM_STATES, 1> *s_next,
                                  Eigen::Matrix<double, NUM_STATES, NUM_STATES> *A, 
                                  Eigen::Matrix<double, NUM_STATES, NUM_CONTROLS> *B) {
  Eigen::Matrix<double, NUM_STATES, 1> s_dot;
  dynamics(s, u, &s_dot, A, B);
  (*s_next) = s + s_dot / p_hertz;

  if (A) {
    (*A) /= p_hertz;
    (*A) += Eigen::Matrix<double, NUM_STATES, NUM_STATES>::Identity();
  }

  if (B) (*B) /= p_hertz;
}


void MPCController::simulateRK4(const Eigen::Matrix<double, NUM_STATES, 1> &s, 
                                const Eigen::Matrix<double, NUM_CONTROLS, 1> &u, 
                                Eigen::Matrix<double, NUM_STATES, 1> *s_next,
                                Eigen::Matrix<double, NUM_STATES, NUM_STATES> *A, 
                                Eigen::Matrix<double, NUM_STATES, NUM_CONTROLS> *B) {
  Eigen::Matrix<double, NUM_STATES, 1> k1, k2, k3, k4;
  double dt = 1 / p_hertz;

  dynamics(s, u, &k1);
  dynamics(s + 0.5 * dt * k1, u, &k2);
  dynamics(s + 0.5 * dt * k2, u, &k3);
  dynamics(s + dt * k3, u, &k4);
  (*s_next) = s + dt * (k1 / 6.0 + k2 / 3.0 + k3 / 3.0 + k4 / 6.0);

// compute Jacobians (numerically) if desired
  if (A != 0) {
    Eigen::Matrix<double, NUM_STATES, 1> s2 = s;
    Eigen::Matrix<double, NUM_STATES, 1> sn1, sn2;
    for (int i = 0; i < NUM_STATES; i++) {
      s2(i) += EPSILON;
      simulateRK4(s2, u, &sn1, 0, 0);
      s2(i) -= 2 * EPSILON;
      simulateRK4(s2, u, &sn2, 0, 0);
      s2(i) += EPSILON;
      A->col(i) = (sn1 - sn2) / (2 * EPSILON);
    }
  }

  if (B != 0) {
    Eigen::Matrix<double, NUM_CONTROLS, 1> u2 = u;
    Eigen::Matrix<double, NUM_STATES, 1> sn1, sn2;
    for (int i = 0; i < NUM_CONTROLS; i++) {
      u2(i) += EPSILON;
      simulateRK4(s, u2, &sn1, 0, 0);
      u2(i) -= 2 * EPSILON;
      simulateRK4(s, u2, &sn2, 0, 0);
      u2(i) += EPSILON;
      B->col(i) = (sn1 - sn2) / (2 * EPSILON);
    }
  }
}


template <class T> void MPCController::getParam(std::string key, T& var) {
   if(!nh_.getParam(key, var)) {
    throw VLRException("Cannot read parameter " + key + std::string("."));
  }
}
} //namespace vlr

#pragma pop_options