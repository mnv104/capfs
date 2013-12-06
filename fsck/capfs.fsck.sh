#!/bin/bash
#
# Copyright (C) 2004
#  - Murali Vilayannur

# Invoke this script on the MGR node with
# 'capfs.fsck.sh -m <mgr meta directory> -i <iod configuration file>'
# The mgr meta-data directory is the base directory name in which
# meta-data for the PVFS file system is kept.
# The iod configuration file is a file whose format is described below
# Each line of the iod configuration file consists of the IOD host name
# followed by the base directory in which the data files are stored on the IOD
# for this PVFS file system.
# example
# host1 /opt/capfs-data
# host2 /capfs-data ...
# and so on. Blank lines are not allowed! Comments begin with a # and 
# are treated as single line comments!
# This script launches the actual fsck program as an MPI application.
# Hence it is important to have mpirun in your path before running it.
# Another point to be noted is that it would be in the best interests to run
# script when the file system is not mounted on any client node and the MGR
# and the IOD daemons are not running on any nodes. This would eliminate any 
# random errors and/or further file system corruption.

E_OPTERR=65
E_INVAL=66
E_FATAL=67
E_PERM=68
E_NON_ROOT_USER=70
ROOTUSER=root
NOARGS=0

usage()
{
	echo "Usage: `basename $0`
-m <mgr meta-data directory>
-f <iod configuration file>)
-s {simulate actions alone}
-p {Don't prompt before executing actions}"
 	exit $E_OPTERR;
}

welcome()
{
	echo;
	echo "Launching `basename $0`:"
	echo "The front-end script that launches the capfs.fsck"
	echo "as an MPI application to try and recover potential"
	echo "PVFS-Version 1 file system inconsistencies. Please";
	echo "unmount the file system and stop the MGR and IOD daemons";
	echo "before proceeding!"
	echo ;
	echo "Press a key to continue";
	read <& 1;
}

welcome;

# Script must be run as root
username=`id -nu`
if [ "$username" != "$ROOTUSER" ]
then
	echo "Warning! Must be root to run \"`basename $0`\".";
	exit  $E_NON_ROOT_USER;
fi

CURRENT_DIR=`pwd`;
# Make sure that the current directory is writable
if [ ! -w "$CURRENT_DIR" ]
then
	echo "\"$CURRENT_DIR\" is not writable!";
	exit $E_PERM;
fi

if [ $# -eq "$NOARGS" ]  # No Command line arguments
then
	echo "No command-line parameters!";
	usage;
fi

SIMULATE=0;
PROMPT=1;
while getopts "m:f:sp" Option
do
	case $Option in
		m) MGR_META_DIR=$OPTARG;;
		f) IOD_CONF_FILE=$OPTARG;;
		s) SIMULATE=1;;
		p) PROMPT=0;;
		*) usage;;	
	esac
done

# Make sure that the meta-data directory is always specified! 
if [ ! -n "$MGR_META_DIR" ]
then
	echo "Did not specify the MGR meta-data directory!";
	usage;
fi

if [ ! -n "$IOD_CONF_FILE" ]
then
	echo "Did not specify IOD Config file name!";
	usage;
fi

#Make sure that mgr-meta pathname is actually a directory!
if [ ! -d "$MGR_META_DIR" ]
then
	echo "\"$MGR_META_DIR\" is not a valid existing directory!";
	echo "Make sure this script runs on the MGR node of the PVFS file system!";
	exit $E_INVAL;
fi

# try to parse the iod conf file
if [ ! -f "$IOD_CONF_FILE" ]
then
	echo "\"$IOD_CONF_FILE\" is not a valid existing file!";
	exit $E_INVAL;
else 
	# Paranoid checks for cat and gawk!
	if [ ! -x "/bin/cat" ]
	then
		echo "Could not find an executable \"cat\" in /bin!";
		exit $E_FATAL;
	fi

	if [ ! -x "/usr/bin/gawk" ]
	then
		echo "Could not find an executable \"gawk\" in /usr/bin!";
		exit $E_FATAL;
	fi

	echo $HOSTNAME > "machinefile";
	# count is the number of hosts written to the machine file
	count=`cat $IOD_CONF_FILE | gawk -f ./capfs.fsck.awk`;
	if [ "$count" -le "0" ]
	then
		echo "Parse error in \"$IOD_CONF_FILE\"";
		exit $E_FATAL;
	fi
	count=$[$count+1];
fi
# Make sure that mpirun is available in your path!
# Now that we have done most of the work, we spawn the MPI process here
if [ "$SIMULATE" -eq "1" ]; then
	if [ "$PROMPT" -eq "1" ]; then
		echo "Launching:: mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE} -s"
		mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE} -s
	else
		echo "Launching:: mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE} -s -p"
		mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE} -s -p
	fi
else
	if [ "$PROMPT" -eq "1" ]; then
		echo "Launching:: mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE}"
		mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE}
	else
		echo "Launching:: mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE} -p"
		mpirun -np ${count} -machinefile machinefile ./capfs.fsck -m ${MGR_META_DIR} -f ${IOD_CONF_FILE} -p
	fi
fi
