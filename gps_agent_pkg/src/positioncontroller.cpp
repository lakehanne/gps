#include "gps_agent_pkg/util.h"
#include "gps_agent_pkg/robotplugin.h"
#include "gps_agent_pkg/camerasensor.h"
#include "gps_agent_pkg/superchickplugin.h"
#include "gps_agent_pkg/positioncontroller.h"


using namespace gps_control;

// Constructor.

// Constructor.
PositionController::PositionController(ros::NodeHandle& n, gps::ActuatorType arm, int size)
    : Controller(n, arm, size)
{
    // Initialize velocity bounds.
    max_velocities_.resize(size);

    // Initialize current angle and position.
    current_angles_.resize(size);
    current_angle_velocities_.resize(size);
    current_pose_.resize(size);

    // Initialize target angle and position.
    target_angles_.resize(size);
    target_pose_.resize(size);

    // Initialize Jacobian temporary storage.
    temp_jacobian_.resize(6,size);

    // Initialize joints temporary storage.
    temp_angles_.resize(size);

    // Set initial mode.
    mode_ = gps::NO_CONTROL;

    // Set initial time.
    last_update_time_ = ros::Time(0.0);

    // Set arm.
    arm_ = arm;

    //
    report_waiting = false;
}

// Destructor.
PositionController::~PositionController()
{
}


// Update the controller (take an action).
void PositionController::update(RobotPlugin *plugin, ros::Time current_time, boost::scoped_ptr<Sample>& sample, Eigen::VectorXd &torques)
{
/*    // Get current joint angles.
    plugin->get_task_space_readings(temp_angles_, arm_);

    // Check dimensionality.
    assert(temp_angles_.rows() == torques.rows());
    assert(temp_angles_.rows() == current_angles_.rows());
*/
    // Estimate joint angle velocities.
    double update_time;// = current_time.toSec() - last_update_time_.toSec();
    /*
    if (!last_update_time_.isZero())
    { // Only compute velocities if we have a previous sample.
        current_angle_velocities_ = (temp_angles_ - current_angles_)/update_time;
    }

    // Store new angles.
    current_angles_ = temp_angles_;

    // Update last update time.
    last_update_time_ = current_time;*/

    // If doing task space control, compute task positions target.
    if (mode_ == gps::TASK_SPACE)
    {
        
        // TODO: implement.

        // Get current end effector position. latest_vicon_pose_
        current_headpose_ = camerasensor->latest_vicon_pose_[1];
        //retrieve the markers just for a check
        fore_  = camerasensor->latest_vicon_markers_[0];
        left_  = camerasensor->latest_vicon_markers_[1];
        right_ = camerasensor->latest_vicon_markers_[2];
        chin_  = camerasensor->latest_vicon_markers_[3];

        //assign current pose for four markers
        temp_pose_ << fore_.x, fore_.y, fore_.z, left_.x, left_.y, left_.z, \
                        right_.x, right_.y, right_.z, chin_.x, chin_.y, chin_.z;
                     
        // Get current Jacobian.

        // estimate twist, i.e. task space velocities
        update_time = current_time.toSec() - last_update_time_.toSec();
        if (!last_update_time_.isZero())
        { // Only compute velocities if we have a previous sample.
            current_pose_velocities_ = (temp_pose_ - current_pose_)/update_time;
        }

        // Store new pose.
        current_pose_ = temp_pose_;  

        // Update last update time.
        last_update_time_ = current_time;

        // TODO: should also try Jacobian pseudoinverse, it may work a little better.
        // Compute desired joint angle offset using Jacobian transpose method.
        target_pose_ = current_pose_ + temp_jacobian_.transpose() * (target_pose_ - current_pose_);
    }

    // If we're doing any kind of control at all, compute torques now.
    if (mode_ != gps::NO_CONTROL)
    {
        // Compute error.
        temp_angles_ = current_angles_ - target_angles_;

        // Add to integral term.
        pd_integral_ += temp_angles_ * update_time;

        // Clamp integral term
        for (int i = 0; i < temp_angles_.rows(); i++){
            if (pd_integral_(i) * pd_gains_i_(i) > i_clamp_(i)) {
                pd_integral_(i) = i_clamp_(i) / pd_gains_i_(i);
            }
            else if (pd_integral_(i) * pd_gains_i_(i) < -i_clamp_(i)) {
                pd_integral_(i) = -i_clamp_(i) / pd_gains_i_(i);
            }
        }

        // Compute torques.
        torques = -((pd_gains_p_.array() * temp_angles_.array()) +
                    (pd_gains_d_.array() * current_angle_velocities_.array()) +
                    (pd_gains_i_.array() * pd_integral_.array())).matrix();
    }
    else
    {
        torques = Eigen::VectorXd::Zero(torques.rows());
    }

}

// Configure the controller.
void PositionController::configure_controller(OptionsMap &options)
{
    // This sets the target position.
    // This sets the mode
    ROS_INFO_STREAM("Received controller configuration");
    // needs to report when finished
    report_waiting = true;
    mode_ = (gps::PositionControlMode) boost::get<int>(options["mode"]);
    if (mode_ != gps::NO_CONTROL)
    {
        Eigen::VectorXd data = boost::get<Eigen::VectorXd>(options["data"]);
        Eigen::MatrixXd pd_gains = boost::get<Eigen::MatrixXd>(options["pd_gains"]);
        for(int i=0; i<pd_gains.rows(); i++)
        {
            pd_gains_p_(i) = pd_gains(i, 0);
            pd_gains_i_(i) = pd_gains(i, 1);
            pd_gains_d_(i) = pd_gains(i, 2);
            i_clamp_(i) = pd_gains(i, 3);
        }
        ROS_INFO_STREAM("data: " << data);
        if(mode_ == gps::JOINT_SPACE)
        {
            target_angles_ = data;
        }
        else if(mode_== gps::TASK_SPACE)
        {
            target_pose_ = data;
        }
        else{
            ROS_ERROR("Unimplemented position control mode!");
        }
    }
}

// Check if controller is finished with its current task.
bool PositionController::is_finished() const
{
    // Check whether we are close enough to the current target.
    if (mode_ == gps::JOINT_SPACE){
        double epspos = 0.185;
        double epsvel = 0.01;
        double error = (current_angles_ - target_angles_).norm();
        double vel = current_angle_velocities_.norm();
        return (error < epspos && vel < epsvel);
    }
    if (mode_ == gps::TASK_SPACE){
        double epspos = 0.185;
        double epsvel = 0.01;
        double error = (current_pose_ - target_pose_).norm();
        double vel = current_pose_velocities_.norm();
        return (error < epspos && vel < epsvel);
    }
    else if (mode_ == gps::NO_CONTROL){
        return true;
    }
}

// Reset the controller -- this is typically called when the controller is turned on.
void PositionController::reset(ros::Time time)
{
    // Clear the integral term.
    pd_integral_.fill(0.0);

    // Clear update time.
    last_update_time_ = ros::Time(0.0);
}

