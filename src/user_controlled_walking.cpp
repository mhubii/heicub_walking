#include <yarp/os/all.h>
#include <yarp/os/all.h>
#include <Eigen/Core>
#include <rbdl/rbdl.h>
#include <qpOASES.hpp>

#include "reader.h"
#include "writer.h"
#include "nmpc_generator.h"
#include "mpc_generator.h"
#include "interpolation.h"
#include "kinematics.h"
#include "utils.h"

// Forward declare locations of configuration files.
std::string io_config;
std::string pg_config;
std::string ki_config;

// Forward declare name of the robot.
std::string robot;

// Forward declare WalkingProcessor. This is actually the heart
// of the application. Within it, the pattern is generated,
// and the dynamic filter as well as the inverse kinematics are
// computed.
class WalkingProcessor : public yarp::os::BufferedPort<yarp::sig::Matrix>
{
    public:

        WalkingProcessor(Eigen::VectorXd q_min, Eigen::VectorXd q_max);

        ~WalkingProcessor();

        using yarp::os::BufferedPort<yarp::sig::Matrix>::onRead;
        virtual void onRead(yarp::sig::Matrix& state);

        // Setter.
        inline void SetRobotStatus(RobotStatus stat) { robot_status_ = stat; };

    public: // TEST.. change to private!

        // Building blocks of walking generation.
        NMPCGenerator pg_;
        Interpolation ip_;
        Kinematics ki_;

        // Current state of the robot.
        PatternGeneratorState pg_state_;

        Eigen::VectorXd   q_;
        Eigen::VectorXd  dq_;
        Eigen::VectorXd ddq_;
        Eigen::Vector3d com_pos_;
        Eigen::Vector3d com_vel_;
        Eigen::Vector3d com_acc_;

        // Extremal joint angles of the robot.
        Eigen::VectorXd q_min_;
        Eigen::VectorXd q_max_;

        // User input.
        Eigen::Vector3d vel_;

        // State of the robot on preview horizon.
        Eigen::MatrixXd traj_;
        Eigen::MatrixXd com_traj_;
        Eigen::MatrixXd lf_traj_;
        Eigen::MatrixXd rf_traj_;
        Eigen::MatrixXd q_traj_;

        // External velocity input and joint angle port.
        yarp::os::BufferedPort<yarp::sig::Vector> port_vel_;
        yarp::os::BufferedPort<yarp::sig::Vector> port_q_;

        // Mutex.
        yarp::os::Mutex mutex_;

        // Position initialized.
        RobotStatus robot_status_;
        bool initialized_;

        // Port to communicate the status.
        yarp::os::BufferedPort<yarp::os::Bottle> port_status_;

        // TEST: COM feedback.
        Eigen::MatrixXd com_;
        // TEST END
};


// Main application for heicub_walking.
int main(int argc, char *argv[]) {

    // Read user input to obtain locations of the configuration
    // files and the robot's name.
    yarp::os::Property params;
    params.fromCommand(argc, argv);

    if (params.check("io_config")) {
        io_config = params.find("io_config").asString();
    }
    else {
        std::cerr << "Please specify the location of the input output configurations file" << std::endl;
        std::cerr << "--io_config (e.g. ../libs/io_module/configs.yaml)" << std::endl;
        std::exit(1);
    }
    if (params.check("pg_config")) {
        pg_config = params.find("pg_config").asString();
    }
    else {
        std::cerr << "Please specify the location of the pattern generator configurations file" << std::endl;
        std::cerr << "--pg_config (e.g. ../libs/pattern_generator/configs.yaml)" << std::endl;
        std::exit(1);
    }
    if (params.check("ki_config")) {
        ki_config = params.find("ki_config").asString();
    }
    else {
        std::cerr << "Please specify the location of the kinematics configurations file" << std::endl;
        std::cerr << "--ki_config (e.g. ../libs/kinematics/configs.yaml)" << std::endl;
        std::exit(1);
    }
    if (params.check("robot")) {
        robot = params.find("robot").asString();
    }
    else {
        std::cerr << "Please specify name of the robot" << std::endl;
        std::cerr << "--robot (e.g. icubGazeboSim)" << std::endl;
        std::exit(1);
    }

    // Set up the yarp network.
    yarp::os::Network yarp;

    // Reader and writer.
    int period = YAML::LoadFile(pg_config)["command_period"].as<double>()*1e3;

    ReadJoints rj(period, io_config, robot);
    WriteJoints wj(period, io_config, robot);

    // Get the extremal angles for the joints. // TODO add some kind of min max init
    Eigen::VectorXd min = rj.GetMinAngles();
    Eigen::VectorXd max = rj.GetMaxAngles();

    // Process data, read from joints.
    WalkingProcessor pg_port(min, max); 
    pg_port.open("/test/port");

    // Connect reader to external commands (possibly ai thread).
    yarp::os::Network::connect("/vel/command", "/vel");

    // Put reader, processor, and writer together.
    yarp::os::Network::connect(rj.GetPortName(), "/test/port");
    yarp::os::Network::connect("/joint_angles", wj.GetPortName());
    yarp::os::Network::connect("/client_write/robot_status", "/reader/commands");
    yarp::os::Network::connect("/client_write/robot_status", "/walking_processor/commands");
    yarp::os::Network::connect("/walking_processor/commands", "/reader/commands");

    // Start the read and write threads.
    rj.start();
    wj.start();
    
    // Run program for a certain delay.
    yarp::os::Time::delay(120);

    // TEST
    WriteCsv("test.csv", pg_port.ip_.GetTrajectories().transpose());

    // Store com feedback for reference.
    Eigen::MatrixXd comfb = pg_port.ip_.GetTrajectories().rightCols(pg_port.com_.cols());
    comfb.row(0) = pg_port.com_.row(0);
    comfb.row(3) = pg_port.com_.row(1);
    comfb.row(6) = pg_port.com_.row(2);
    WriteCsv("test_com_feedback.csv", comfb.transpose());
    // TEST END

    // Stop reader and writer (on command later).
    pg_port.close();
    rj.stop();
    wj.stop();

    // system("pause");

    return 0;
}


// Implement WalkingProcessor.
WalkingProcessor::WalkingProcessor(Eigen::VectorXd q_min, Eigen::VectorXd q_max)
  : pg_(pg_config),
    ip_(pg_), 
    ki_(ki_config),

      q_(ki_.GetQTraj().rows()),
     dq_(ki_.GetQTraj().rows()),
    ddq_(ki_.GetQTraj().rows()),

    // Extremal joint angles of the robot.
    q_min_(q_min),
    q_max_(q_max),
    
    // State of the robot on preview horizon.
    com_traj_(4, 1),//  ip_.GetTrajectoriesBuffer().cols()),
    lf_traj_(4, 1),// ip_.GetTrajectoriesBuffer().cols()),
    rf_traj_(4, 1),// ip_.GetTrajectoriesBuffer().cols()),
    
    // Position initialized.
    robot_status_(NOT_INITIALIZED),
    initialized_(false) {

    // Pattern generator preparation.
    pg_.SetSecurityMargin(pg_.SecurityMarginX(), 
                          pg_.SecurityMarginY());

    // Set initial values.
    pg_state_ = {pg_.Ckx0(),
                 pg_.Cky0(),
                 pg_.Hcom(),
                 pg_.Fkx0(),
                 pg_.Fky0(),
                 pg_.Fkq0(),
                 pg_.CurrentSupport().foot,
                 pg_.Ckq0()};

    pg_.SetInitialValues(pg_state_);

    // Use callback to call onRead() method.
    useCallback();
    setCallbackLock(&mutex_);

    // Set initial velocity to zero.
    vel_.setZero();

    // Open port for velocity input.
    port_vel_.open("/vel");
    port_q_.open("/joint_angles");
    port_status_.open("/walking_processor/commands");

    // TEST
    ip_.StoreTrajectories(true);
    // TEST END
    // TEST
    // qq = ReadCsv<Eigen::MatrixXd>("/home/martin/Documents/heicub_walking/build/example_nmpc_generator_interpolated_results.csv").transpose();
    // count = 0;
    // TEST END
}


WalkingProcessor::~WalkingProcessor() {

    // Close ports.
    port_vel_.close();
    port_q_.close();
    port_status_.close();
}


// Implement onRead() method.
void  WalkingProcessor::onRead(yarp::sig::Matrix& state) {

    // Lock callbacks during the computation.
    lockCallback();

    yarp::os::Bottle* bottle = port_status_.read(false);
    if (bottle != YARP_NULLPTR) {

        // Get initial position status.
        robot_status_ = RobotStatus(bottle->pop().asDict()->find("RobotStatus").asInt());
    }

    if (initialized_ && robot_status_ == INITIALIZED) {

        // Read the desired velocity and keep it unchanged if
        // no command arrives.
        yarp::sig::Vector* vel = port_vel_.read(false);
        if (vel != YARP_NULLPTR) {

            // Convert to Eigen.
            vel_ = yarp::eigen::toEigen(*vel);
        }

        // // Use forward kinematics to obtain the com feedback.
        q_ << ki_.GetQTraj().topRows(6).col(0), yarp::eigen::toEigen(state.getCol(0)).bottomRows(15);
        // // // dq_.bottomRows(15) = yarp::eigen::toEigen(state.getCol(1));
        // // // ddq_.bottomRows(15) = yarp::eigen::toEigen(state.getCol(2));

        // ki_.Forward(q_, dq_, ddq_);
        // com_pos_ = ki_.GetComPos();
        // com_vel_ = ki_.GetComVel();
        // com_acc_ = ki_.GetComAcc();

        // // Generate pattern with com feedback.
        pg_state_.com_x(0) = com_pos_(0);//, com_vel_(0), com_acc_(0);
        pg_state_.com_y(0) = com_pos_(1);//, com_vel_(1), com_acc_(1);
        pg_state_.com_z = com_pos_(2);

        // TEST: Store com feedback for reference.
        // com_.conservativeResize(com_.rows(), com_.cols() + 1);
        // com_.rightCols(1) = com_pos_;
        // TEST END

        // pg_state_.com_x(0) = traj_(0, 0);
        // pg_state_.com_y(0) = traj_(3, 0);
        // pg_state_.com_z = traj_(6, 0);

        // pg_.SetInitialValues(pg_state_);

        // Set desired velocity.
        pg_.SetVelocityReference(vel_);

        // // Solve QP.
        // pg_.Solve();
        // pg_.Simulate();

        // TEST
        // Set reference velocities.
        // vel_ << 0.01, 0., 0.06;
        // pg_.SetVelocityReference(vel_);

        // // pg_.SetInitialValues(pg_state_);

        // Solve QP.
        pg_.Solve();
        pg_.Simulate();

        if (ip_.GetCurrentInterval() % ip_.GetIntervals() == 0) {

            // Use forward kinematics to obtain the com feedback.
            q_ << ki_.GetQTraj().topRows(6).col(0), yarp::eigen::toEigen(state.getCol(0)).bottomRows(15);
            // // dq_.bottomRows(15) = yarp::eigen::toEigen(state.getCol(1));
            // // ddq_.bottomRows(15) = yarp::eigen::toEigen(state.getCol(2));

            ki_.Forward(q_, dq_, ddq_);
            com_pos_ = ki_.GetComPos();
            // com_vel_ = ki_.GetComVel();
            // com_acc_ = ki_.GetComAcc();

            // Generate pattern with com feedback.
            pg_state_.com_x(0) = com_pos_(0);//, com_vel_(0), com_acc_(0);
            pg_state_.com_y(0) = com_pos_(1);//, com_vel_(1), com_acc_(1);
            pg_state_.com_z = com_pos_(2);

            pg_state_ = pg_.Update();
            // pg_.SetInitialValues(pg_state_);
        }
        else {

            q_ << ki_.GetQTraj().topRows(6).col(0), yarp::eigen::toEigen(state.getCol(0)).bottomRows(15);

            ki_.Forward(q_, dq_, ddq_);
            com_pos_ = ki_.GetComPos();

            // Generate pattern without feedback.
            pg_state_.com_x(0) = traj_(0, 0);
            pg_state_.com_y(0) = traj_(3, 0);
            pg_state_.com_z = traj_(6, 0);

            // TODO: HOW DID THE FRENCH GUYS IMPLEMENT FEEDBACK ON THE INTERPOLATION AND HOW ON THE PATTERN GENERATOR?
            // // Generate pattern with com feedback.
            // pg_state_.com_x(0) = com_pos_(0);
            // pg_state_.com_y(0) = com_pos_(1);
            // pg_state_.com_z = com_pos_(2);

            pg_.SetInitialValues(pg_state_);
        }

        // // // ip_.InterpolateStep(); // TODO
        // // // traj_ = ip_.GetTrajectoriesBuffer();//.col(0);
        traj_ = ip_.Interpolate();

        // TEST: Store com feedback for reference.
        com_.conservativeResize(3, com_.cols() + 1);
        com_.rightCols(1)(0) = com_pos_(0); // traj_(0, 0);
        com_.rightCols(1)(1) = com_pos_(1); // traj_(3, 0);
        com_.rightCols(1)(2) = com_pos_(2); // traj_(6, 0);
        // TEST END

        // Inverse kinematics.
        com_traj_ << traj_(0, 0),  traj_(3, 0),  traj_(6, 0),  traj_(7, 0);
        lf_traj_  << traj_(13, 0), traj_(14, 0), traj_(15, 0), traj_(16, 0);
        rf_traj_  << traj_(17, 0), traj_(18, 0), traj_(19, 0), traj_(20, 0);

        // ip_.SetInitialValues(lf_traj_, rf_traj_);

        // // TEST -> immediatly feedback the com.
        // q_ << ki_.GetQTraj().col(0); //yarp::eigen::toEigen(state.getCol(0)).bottomRows(15);

        // ki_.Forward(q_, dq_, ddq_);
        // com_pos_ = ki_.GetComPos();
        // com_vel_ = ki_.GetComVel();
        // com_acc_ = ki_.GetComAcc();

        // com_traj_ << com_pos_(0),  com_pos_(1),  com_pos_(2),  0;
        // lf_traj_ << ip_.lf_x_buffer_(0, 0), ip_.lf_y_buffer_(0, 0), 0, 0;
        // rf_traj_ << ip_.rf_x_buffer_(0, 0), ip_.rf_y_buffer_(0, 0), 0, 0;
        // // TEST END

        ki_.Inverse(com_traj_, lf_traj_, rf_traj_);
        q_traj_ = ki_.GetQTraj().bottomRows(15);
        

        // Check for hardware limits.
        bool limits = false;

        limits = limits && (q_traj_.array() < q_min_.array()).any();
        limits = limits && (q_traj_.array() > q_max_.array()).any();

        if (limits) {

            // Communicate hardware limits.
            yarp::os::Bottle& bottle = port_status_.prepare();
            yarp::os::Property& dict = bottle.addDict();

            dict.put("ERROR", HARDWARE_LIMITS);
            port_status_.write();

            std::exit(1);
        }

        // double tempq = q_traj_(12, 0);
        // q_traj_(12, 0) = q_(14, 0);
        // q_traj_(14, 0) = tempq;

        // Initial value embedding by internal states and simulation.
        // pg_state_ = pg_.Update();
        // pg_.SetInitialValues(pg_state_);

        // Write joint angles to output port.
        yarp::sig::Vector data(q_traj_.rows(), q_traj_.cols());
        Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(data.data(), q_traj_.rows(), q_traj_.cols()) = q_traj_;

        port_q_.prepare() = data;
        port_q_.write();
        
        // traj_ = qq.col(count);

        // // Inverse kinematics.
        // com_traj_ << traj_.row(0),  traj_.row(3),  traj_.row(6),  traj_.row(7);
        // lf_traj_  << traj_.row(13), traj_.row(14), traj_.row(15), traj_.row(16);
        // rf_traj_  << traj_.row(17), traj_.row(18), traj_.row(19), traj_.row(20);

        // ki_.Inverse(com_traj_, lf_traj_, rf_traj_);
        // q_traj_ = ki_.GetQTraj().bottomRows(15).col(0); // TODO: there must be a mistake when the data vector gets read out.

        // yarp::sig::Vector data(q_traj_.rows(), q_traj_.cols());
        // Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(data.data(), q_traj_.rows(), q_traj_.cols()) = q_traj_;
        // port_q_.prepare() = data;
        // port_q_.write();

        // COMMENTED BECAUSE OF MPC GENERATOR INSTEAD OF NMPC GENERATOR
        // // Check for correctness of pattern generator.
        // if (pg_.GetStatus() != qpOASES::SUCCESSFUL_RETURN) {

        //     // Communicate quadratic problem status.
        //     yarp::os::Bottle& bottle = port_status_.prepare();
        //     yarp::os::Property& dict = bottle.addDict();

        //     dict.put("ERROR", QP_INFEASIBLE);
        //     port_status_.write();

        //     std::exit(1);
        // }

        // Check for correctness of inverse kinematics including hardware limits.
        if (!ki_.GetStatus()) {

            // Communicate inverse kinematics status.
            yarp::os::Bottle& bottle = port_status_.prepare();
            yarp::os::Property& dict = bottle.addDict();

            dict.put("Warning", IK_DID_NOT_CONVERGE);
            port_status_.write();
        }

        // // Stream the result to the writer port.
        // for (int i = 0; i < ip_.GetTrajectoriesBuffer().cols(); i++) {
            
        //     // Inverse kinematics.
        //     com_traj_ << traj_(0, i),  traj_(3, i),  traj_(6, i),  traj_(7, i);
        //     lf_traj_  << traj_(13, i), traj_(14, i), traj_(15, i), traj_(16, i);
        //     rf_traj_  << traj_(17, i), traj_(18, i), traj_(19, i), traj_(20, i);

        //     ki_.Inverse(com_traj_, lf_traj_, rf_traj_);
        //     q_traj_ = ki_.GetQTraj().bottomRows(15);

            // // Check for hardware limits.
            // bool limits = false;

            // limits = limits && (q_traj_.array() < q_min_.array()).any();
            // limits = limits && (q_traj_.array() > q_max_.array()).any();

            // if (limits) {

            //     // Communicate hardware limits.
            //     yarp::os::Bottle& bottle = port_status_.prepare();
            //     yarp::os::Property& dict = bottle.addDict();

            //     dict.put("ERROR", HARDWARE_LIMITS);
            //     port_status_.write();
            // }

        //     // Write joint angles to output port.
        //     yarp::sig::Vector data(q_traj_.rows(), q_traj_.cols());
        //     Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(data.data(), q_traj_.rows(), q_traj_.cols()) = q_traj_;

        //     port_q_.prepare() = data;
        //     port_q_.write();

        //     yarp::os::Time::delay(0.01);
        // }

        // count++;
        // TEST END

        // // Interpolate results.
        // ip_.Interpolate();
        // traj_ = ip_.GetTrajectoriesBuffer().col(0);

        // // Inverse kinematics.
        // com_traj_ << traj_.row(0),  traj_.row(3),  traj_.row(6),  traj_.row(7);
        // lf_traj_  << traj_.row(13), traj_.row(14), traj_.row(15), traj_.row(16);
        // rf_traj_  << traj_.row(17), traj_.row(18), traj_.row(19), traj_.row(20);

        // ki_.Inverse(com_traj_, lf_traj_, rf_traj_);
        // q_traj_ = ki_.GetQTraj().bottomRows(15);

        // // Write joint angles to output port.
        // yarp::sig::Vector data(q_traj_.rows(), q_traj_.cols());
        // Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(data.data(), q_traj_.rows(), q_traj_.cols()) = q_traj_;

        // port_q_.prepare() = data;
        // port_q_.write();
    }

    else if (!initialized_) {

        // Initialize position with a good guess.
        Eigen::VectorXd q_init(21);
        q_init.setZero();
        
        q_init(2)  = 0.6;
        q_init(6)  = 0.54;
        q_init(9)  = -0.57;
        q_init(10) = -0.23;
        q_init(12) = 0.54;
        q_init(15) = -0.57;
        q_init(16) = -0.23;

        ki_.SetQInit(q_init);

        // Get desired initial state of the robot.
        traj_ = ip_.GetTrajectoriesBuffer().col(0);

        // Initialize inverse kinematics.
        com_traj_ << traj_.row(0),  traj_.row(3),  traj_.row(6),  traj_.row(7);
        lf_traj_  << traj_.row(13), traj_.row(14), traj_.row(15), traj_.row(16);
        rf_traj_  << traj_.row(17), traj_.row(18), traj_.row(19), traj_.row(20);

        ki_.Inverse(com_traj_, lf_traj_, rf_traj_);
        q_traj_ = ki_.GetQTraj().bottomRows(15).col(0);

        // Write joint angles to output port.
        yarp::sig::Vector data(q_traj_.rows(), q_traj_.cols());
        Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(data.data(), q_traj_.rows(), q_traj_.cols()) = q_traj_;

        port_q_.prepare() = data;
        port_q_.write();

        initialized_ = true;
    }

    // Unlock the callback.
    unlockCallback();
}
