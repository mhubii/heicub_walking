#ifndef IO_MODULE_READ_H_
#define IO_MODULE_READ_H_

#include <opencv2/opencv.hpp>
#include <opencv2/core/core_c.h>
#include <opencv2/ximgproc.hpp>

#include <iostream>
#include <yarp/dev/all.h>
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <yarp/eigen/Eigen.h>
#include <ncurses.h>
#include <yaml-cpp/yaml.h>

#include "utils.h"


// ReadJointsToFile implements a simple reader that
// reads joints from a YARP interface.
//
// Implemented by Martin Huber.
class ReadJoints : public yarp::os::RateThread
{
    public:
        
        ReadJoints(int period, const std::string config_file_loc = "../libs/io_module/configs.yaml", 
                   const std::string robot_name = "icubGazeboSim", const std::string out_file_loc = "../libs/io_module/data/states.csv");

        ~ReadJoints();

        // Getters.
        inline const std::string& GetPortName() const { return port_name_; };
        inline const Eigen::VectorXd& GetMinAngles() const { return q_min_; };
        inline const Eigen::VectorXd& GetMaxAngles() const { return q_max_; };

    private:

        // Methods to be implemented for RateThread.
        virtual void run();

        // Write data to file.
        //WriteCsv();

        // Configurations.
        void UnsetDrivers();

        void SetConfigs();

        void SetDrivers();

        void ReadLimits();

        // Robot.
        const std::string robot_name_;

        // Configurations.
        YAML::Node configs_;

        // Parts.
        std::vector<Part> parts_;

        // Extremal angles of the joints.
        Eigen::VectorXd q_min_;
        Eigen::VectorXd q_max_;

        // Drivers.
        std::map<std::string, yarp::dev::PolyDriver*> dd_;
        std::map<std::string, yarp::dev::IEncoders*> enc_;
        std::map<std::string, yarp::dev::IControlLimits*> lim_;

        // Output.
        std::string out_file_loc_;
        yarp::sig::Matrix state_;

        // Port to write joint angles to.
        std::string port_name_;
        yarp::os::BufferedPort<yarp::sig::Matrix> port_;
};


// ReadCameras implements a simple reader that
// reads camera images from a YARP interface.
//
// Implemented by Martin Huber.
class ReadCameras : public yarp::os::RateThread
{
    public:
        
        ReadCameras(int period, const std::string config_file_loc = "../libs/io_module/configs.yaml",
                   const std::string robot_name = "icubGazeboSim", const std::string out_file_loc = "../libs/io_module/data/img",
                   const std::string out_port_name = "/vel/prediction");

        ~ReadCameras();

    private:

        // Methods to be implemented for RateThread.
        virtual void run();

        // Configurations.
        void UnsetDrivers();

        void SetConfigs();

        void SetDrivers();

        // Robot.
        const std::string robot_name_;

        // run() is called every period_ ms.
        int period_;

        // Possible outputs.
        bool show_depth_view_;
        bool save_depth_view_;

        // Configurations.
        YAML::Node configs_;

        // Parts.
        std::vector<Part> parts_;

        // Output.
        std::string out_file_loc_;

        // Drivers.
        std::map<std::string, yarp::dev::PolyDriver*> dd_;
        std::map<std::string, yarp::dev::IFrameGrabberImage*> grab_;

        // Images of the cameras.
        std::map<std::string, yarp::sig::ImageOf<yarp::sig::PixelRgb>> img_;
        std::map<std::string, cv::Mat> img_cv_;

        // Stereo matching and weighted least square filter.
        cv::Ptr<cv::StereoBM> l_matcher_;
        cv::Ptr<cv::StereoMatcher> r_matcher_;
        cv::Ptr<cv::ximgproc::DisparityWLSFilter> wls_;

        // Disparity map.
        cv::Mat l_disp_; 
        cv::Mat r_disp_; 
        cv::Mat wls_disp_;

        // Outgoing information.
        yarp::sig::Vector vel_;

        // Outgoing port.
        yarp::os::BufferedPort<yarp::sig::Vector> port_;
};


// AppReader implements a simple user interface that
// allows the user to connect to an app from which
// it reads commands. The commands are redirected to
// a port via WriteToPort().
//
// Implemented by Martin Huber.
class AppReader : public yarp::os::BufferedPort<yarp::os::Bottle>
{

    public:

        AppReader();

        ~AppReader();

        // Read incomming commands and update the velocity.
        void ReadCommands();

        // Implement BufferedPort methods to communicate with
        // the robots status.
        using yarp::os::BufferedPort<yarp::os::Bottle>::onRead;
        virtual void onRead(yarp::os::Bottle& info);

    private:

        // Port for sending velocities and informations about the status.
        yarp::os::BufferedPort<yarp::os::Bottle> port_vel_app_;
        yarp::os::BufferedPort<yarp::sig::Vector> port_vel_;
        yarp::os::BufferedPort<yarp::os::Bottle> port_status_;

        // Write to port.
        void WriteToPort();

        // Robot status, errors and warnings.
        RobotStatus robot_status_;
        Errors errors_;
        Warnings warnings_;

        // Started user controlled walking.
        bool running_;

        // Velocity.
        yarp::sig::Vector vel_;

        // User interface.
        WINDOW *win_q_, *win_e_, *win_r_;
        WINDOW *win_guide_, *win_robot_status_, *win_err_, *win_vel_;
        WINDOW *win_inv_;

        // Mutex.
        yarp::os::Mutex mutex_;
};


// KeyReader implements a simple user interface that
// supports the user with w, a, s, d controls to write
// to a port via WriteToPort().
//
// Implemented by Martin Huber.
class KeyReader : public yarp::os::BufferedPort<yarp::os::Bottle>
{
    public:

        KeyReader();

        ~KeyReader();

        // Read incomming commands and update the velocity.
        void ReadCommands();

        // Implement BufferedPort methods to communicate with
        // the robots status.
        using yarp::os::BufferedPort<yarp::os::Bottle>::onRead;
        virtual void onRead(yarp::os::Bottle& info);

    private:

        // Port for sending velocities.
        yarp::os::BufferedPort<yarp::sig::Vector> port_;
    
        // Set velocity.
        void SetVelocity(Eigen::Vector3d& acc, double t);

        // Write to port.
        void WriteToPort();

        // Robot status, errors and warnings.
        RobotStatus robot_status_;
        Errors errors_;
        Warnings warnings_;

        // Accelerations.
        Eigen::Vector3d acc_w_, acc_a_, acc_shift_a_, acc_s_, acc_d_, acc_shift_d_;

        // Respective time, accelerated in one direction.
        double t_iter_;

        // Velocity.
        yarp::sig::Vector vel_;

        // User interface.
        WINDOW *win_w_, *win_a_, *win_s_, *win_d_;
        WINDOW *win_q_, *win_e_, *win_guide_, *win_robot_status_, *win_err_, *win_vel_;
        WINDOW *win_inv_;

        // Mutex.
        yarp::os::Mutex mutex_;
};

#endif
