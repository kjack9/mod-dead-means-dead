#!/usr/bin/env bash

DEAD_MEANS_DEAD_PATH_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/" && pwd )"

source $DEAD_MEANS_DEAD_PATH_ROOT"/conf/conf.sh.dist"

if [ -f $DEAD_MEANS_DEAD_PATH_ROOT"/conf/conf.sh" ]; then
    source $DEAD_MEANS_DEAD_PATH_ROOT"/conf/conf.sh"
fi
