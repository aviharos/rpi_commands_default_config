export RPI_COMMANDS_CONFIG=$(pwd)              
export $(cat $RPI_COMMANDS_CONFIG/.env | xargs)
