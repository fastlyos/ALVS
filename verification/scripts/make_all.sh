#!/bin/bash


#######################################################################################
#
#                   Copyright by Mellanox Technologies, 2016
#
#  Project:         ALVS
#
#  Description:     This script runs basic test for ALVS prohjetc like make (release, debug),
#                   coveriry, etc'
#
#  Notes:           runs on each push and nighly build
#
#######################################################################################


script_name=$(basename $0)
script_dir=$(cd $(dirname $0) && pwd)

function error_exit()
{
    echo $@ 1>&2
    exit 1
}


function usage()
{
    cat <<EOF
Usage: $script_name [release | debug | all | EMPTY(both)]
This script runs build on the current working area (release and/or debug)

Examples:
$script_name release

EOF
   exit 1
}


#######################################################################################

function parse_cmd()
{
    # check number of arguments
    
    test $# -eq 0
    if [ $? -eq 0 ]; then
        compile_flag="all"
        return
    fi
    
    test $# -eq 1
    if [ $? -eq 0 ]; then
        if [ "$1" == "release" ] ||  [ "$1" == "debug" ] ||  [ "$1" == "all" ]; then
            compile_flag=$1
        else
            usage
        fi
    else
        usage
    fi
}


#######################################################################################

function start_script()
{
    echo ""
    echo ">>>>>>>>>>>>>>>>>>>>>>>>>>>>"
    echo "> Start running $script_name"
}


#######################################################################################

function compile_git()
{
    echo "Function: $FUNCNAME called"
    rc=0  # no failure

    echo "perform: make clean"
    make clean > $make_clean_file
    rc=$?
    if [ $rc -ne 0 ]; then
        echo 'ERROR: make clean failed. for more details look at $make_clean_file'
        return $rc
    fi

    
    echo "perform: make $make_params"
    make $make_params > $make_log_file
    rc=$?
    if [ $? -ne 0 ]; then
        echo 'ERROR make $make_params failed. for more details look at $make_log_file'
        return $rc
    fi
    
    # return success
    return $rc
}


#######################################################################################

function make_release()
{
    echo "Function: $FUNCNAME called"

    # prepare compilation parameters
    make_params="all"
    make_clean_file=$wa_path"make_clean_release.log"
    make_log_file=$wa_path"make_release.log"

    # make
    compile_git
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "ERROR: $FUNCNAME failed"
        exit_status=1
        return $rc
    fi

    return $rc
}


#######################################################################################

function make_debug()
{
    echo "Function: $FUNCNAME called"

    # set local variable & export
    DEBUG=TRUE
    export DEBUG
    
    # prepare compilation parameters
    make_params="all"
    make_clean_file=$wa_path"make_clean_debug.log"
    make_log_file=$wa_path"make_debug.log"

    # make
    compile_git
    # make
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "ERROR: $FUNCNAME failed"
        exit_status=1
        return $rc
    fi

    # unset local variable
    unset DEBUG

    return $rc
}


#######################################################################################

function exit_script()
{
    echo ""
    echo "< End running $script_name"
    echo "<<<<<<<<<<<<<<<<<<<<<<<<<<<<"

    exit $exit_status
}


#######################################################################################

function create_log_folder()
{
    wa_path="logs/"
    test -d $wa_path
    if [ $? -ne 0 ]; then
        echo "Creatin $wa_path folder"
        mkdir $wa_path
    fi
}

#########################################
#                                       #
#              Main                     #
#                                       #
#########################################
function main()
{
	parse_cmd $@

    start_script
    exit_status=0


    create_log_folder

    echo "compile flag: $compile_flag"

    if [ $compile_flag == "release" ]; then
	    make_release
    elif [ $compile_flag == "debug" ]; then
	    make_debug
    else #"all"
	    make_release
	    make_debug
    fi

    exit_script
}


main $@