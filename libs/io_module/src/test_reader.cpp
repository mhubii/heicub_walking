#include <stdio.h>
#include <yarp/os/all.h>
#include <Eigen/Dense>
#include <iostream>

#include "reader.h"

int main(int argc, char * argv[]) {
    yarp::os::Network yarp;


    int period = 10;
    ReadJoints rj(1000);
    // ReadCameras rc(100);

<<<<<<< HEAD
    // rj.start();
    rc.start();
    yarp::os::Time::delay(60);
    rc.stop();
    //rj.stop();
    //KeyReader();
=======
    rj.start();
    //rc.start();
    yarp::os::Time::delay(10);
    //rc.stop();
    rj.stop();
>>>>>>> 5fd66e22f3cef2348c5b7fa0c12a6b6a07d04709

    return 0;
}