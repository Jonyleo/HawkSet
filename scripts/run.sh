#!/usr/bin/bash

DIR=$( dirname "${BASH_SOURCE[0]}" )
source ${DIR}/env.sh

$TOOL_ROOT/scripts/HawkSet ${@:1} 
