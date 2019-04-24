# Behavioural cloning shell script.

# Location of shell script.
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

# Read the first command line argument.
if ["$1" != "${1#[Yy]}"]; then
  robot="icub"
else
  export YARP_CLOCK=/clock
  robot="icubGazeboSim"
fi

cd $DIR/../build/bin
./behavioural_cloning --io_config ../../libs/io_module/configs.yaml --pg_config ../../libs/pattern_generator/configs.yaml --ki_config ../../libs/kinematics/configs.yaml --robot $robot --out_loc ../../out
