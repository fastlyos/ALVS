#!/usr/bin/env python


#===============================================================================
# imports
#===============================================================================

# system  
import sys
import random
import time

# pythons modules 
# local
sys.path.append("verification/testing")
from test_infra import *

#===============================================================================
# User Area function needed by infrastructure
#===============================================================================

def init_log(args):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	log_file = "state_sync_control.log"
	if 'log_file' in args:
		log_file = args['log_file']
	init_logging(log_file)


def init_ezbox(args,ezbox):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	if args['hard_reset']:
		ezbox.reset_ezbox()
		# init ALVS daemon
	ezbox.connect()
	ezbox.flush_ipvs()
	ezbox.alvs_service_stop()
	ezbox.copy_cp_bin(debug_mode=args['debug'])
	ezbox.copy_dp_bin(debug_mode=args['debug'])
	ezbox.alvs_service_start()
	ezbox.wait_for_cp_app()
	ezbox.wait_for_dp_app()
	ezbox.clean_director()


def check_alvs_state_sync_info(expected_alvs_ss_info, alvs_ss_info):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	error = 0
	if len(alvs_ss_info) != len(expected_alvs_ss_info) :
		print "ERROR, len(alvs_ss_info)  = %d expected = %d\n" % (len(alvs_ss_info), len(expected_alvs_ss_info))
		error = 1
	
	if alvs_ss_info['master_bit'] != expected_alvs_ss_info['master_bit'] :
		print "ERROR, master_bit  = %d expected = %d\n" % (alvs_ss_info['master_bit'], expected_alvs_ss_info['master_bit'])
		error = 1

	if alvs_ss_info['backup_bit'] != expected_alvs_ss_info['backup_bit'] :
		print "ERROR, backup_bit = %d, expected = %d\n" % (alvs_ss_info['backup_bit'], expected_alvs_ss_info['backup_bit'])
		error = 1

	if alvs_ss_info['m_sync_id'] != expected_alvs_ss_info['m_sync_id'] :
		print "ERROR, m_sync_id  = %d expected = %d\n" % (alvs_ss_info['m_sync_id'], expected_alvs_ss_info['m_sync_id'])
		error = 1
		
	if alvs_ss_info['b_sync_id'] != expected_alvs_ss_info['b_sync_id'] :
		print "ERROR, b_sync_id  = %d expected = %d\n" % (alvs_ss_info['b_sync_id'], expected_alvs_ss_info['b_sync_id'])
		error = 1
	
	return error

#===============================================================================
# Tests
#===============================================================================

def start_master(ezbox):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	final_rc = True
	rc = ezbox.start_state_sync_daemon(state = "master")
	if rc == False:
		print "ERROR: Can't start master state sync daemon with default syncid"
		final_rc = False
	
	print "wait 1 second for EZbox to update"
	time.sleep(1)
	
	apps_info = ezbox.get_applications_info()
	alvs_ss_info = apps_info[0]
	
	expected_alvs_ss_info = {'master_bit' : 1,
							 'backup_bit' : 0,
							 'm_sync_id' : 0,
							 'b_sync_id' : 0}
	
	print "expected state sync info:"
	print expected_alvs_ss_info
	
	rc = check_alvs_state_sync_info(expected_alvs_ss_info, alvs_ss_info)	
	if rc:
		print "Error in comparing alvs state sync info"
		final_rc = False
	
	rc = ezbox.stop_state_sync_daemon(state = "master")
	if rc == False:
		print "ERROR: Can't stop master state sync daemon"
		final_rc = False
	
	return final_rc


def start_backup(ezbox):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	final_rc = True
	rc = ezbox.start_state_sync_daemon(state = "backup", syncid = 10)
	if rc == False:
		print "ERROR: Can't start backup state sync daemon with syncid 10"
		final_rc = False
	
	print "wait 1 second for EZbox to update"
	time.sleep(1)
	
	apps_info = ezbox.get_applications_info()
	alvs_ss_info = apps_info[0]
	
	expected_alvs_ss_info = {'master_bit' : 0,
							 'backup_bit' : 1,
							 'm_sync_id' : 0,
							 'b_sync_id' : 10}
	
	print "expected state sync info:"
	print expected_alvs_ss_info
	
	rc = check_alvs_state_sync_info(expected_alvs_ss_info, alvs_ss_info)	
	if rc:
		print "Error in comparing alvs state sync info"
		final_rc = False
	
	rc = ezbox.stop_state_sync_daemon(state = "backup")
	if rc == False:
		print "ERROR: Can't stop backup state sync daemon"
		final_rc = False
	
	return final_rc


def start_master_backup(ezbox):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	final_rc = True
	rc = ezbox.start_state_sync_daemon(state = "master", syncid = 7)
	if rc == False:
		print "ERROR: Can't start master state sync daemon with syncid 7"
		final_rc = False
	rc = ezbox.start_state_sync_daemon(state = "backup", syncid = 17)
	if rc == False:
		print "ERROR: Can't start backup state sync daemon with syncid 17"
		final_rc = False
	
	print "wait 1 second for EZbox to update"
	time.sleep(1)
	
	apps_info = ezbox.get_applications_info()
	alvs_ss_info = apps_info[0]
	
	expected_alvs_ss_info = {'master_bit' : 1,
							 'backup_bit' : 1,
							 'm_sync_id' : 7,
							 'b_sync_id' : 17}
	
	print "expected state sync info:"
	print expected_alvs_ss_info
	
	rc = check_alvs_state_sync_info(expected_alvs_ss_info, alvs_ss_info)	
	if rc:
		print "Error in comparing alvs state sync info"
		final_rc = False
	
	rc = ezbox.stop_state_sync_daemon(state = "master")
	if rc == False:
		print "ERROR: Can't stop master state sync daemon"
		final_rc = False
	rc = ezbox.stop_state_sync_daemon(state = "backup")
	if rc == False:
		print "ERROR: Can't stop backup state sync daemon"
		final_rc = False
	
	return final_rc


def start_stop_master_backup(ezbox):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	final_rc = True
	rc = ezbox.start_state_sync_daemon(state = "master", syncid = 7)
	if rc == False:
		print "ERROR: Can't start master state sync daemon with syncid 7"
		final_rc = False
	rc = ezbox.start_state_sync_daemon(state = "backup", syncid = 17)
	if rc == False:
		print "ERROR: Can't start backup state sync daemon with syncid 17"
		final_rc = False
	
	rc = ezbox.stop_state_sync_daemon(state = "master")
	if rc == False:
		print "ERROR: Can't stop master state sync daemon"
		final_rc = False
	rc = ezbox.stop_state_sync_daemon(state = "backup")
	if rc == False:
		print "ERROR: Can't stop backup state sync daemon"
		final_rc = False
	
	print "wait 1 second for EZbox to update"
	time.sleep(1)
	
	apps_info = ezbox.get_applications_info()
	alvs_ss_info = apps_info[0]
	
	expected_alvs_ss_info = {'master_bit' : 0,
							 'backup_bit' : 0,
							 'm_sync_id' : 0,
							 'b_sync_id' : 0}
	
	print "expected state sync info:"
	print expected_alvs_ss_info
	
	rc = check_alvs_state_sync_info(expected_alvs_ss_info, alvs_ss_info)	
	if rc:
		print "Error in comparing alvs state sync info"
		final_rc = False
	
	return final_rc


def start_stop_restart_master_backup(ezbox):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	final_rc = True
	rc = ezbox.start_state_sync_daemon(state = "master")
	if rc == False:
		print "ERROR: Can't start master state sync daemon with default syncid"
		final_rc = False
	rc = ezbox.start_state_sync_daemon(state = "backup")
	if rc == False:
		print "ERROR: Can't start backup state sync daemon with default syncid"
		final_rc = False
	
	print "wait 1 second for EZbox to update"
	time.sleep(1)
	
	apps_info = ezbox.get_applications_info()
	alvs_ss_info = apps_info[0]
	
	expected_alvs_ss_info = {'master_bit' : 1,
							 'backup_bit' : 1,
							 'm_sync_id' : 0,
							 'b_sync_id' : 0}
	print "expected state sync info:"
	print expected_alvs_ss_info
	
	rc = check_alvs_state_sync_info(expected_alvs_ss_info, alvs_ss_info)	
	if rc:
		print "Error in comparing alvs state sync info"
		final_rc = False
	
	rc = ezbox.stop_state_sync_daemon(state = "master")
	if rc == False:
		print "ERROR: Can't stop master state sync daemon"
		final_rc = False
	rc = ezbox.stop_state_sync_daemon(state = "backup")
	if rc == False:
		print "ERROR: Can't stop backup state sync daemon"
		final_rc = False
	
	rc = ezbox.start_state_sync_daemon(state = "master", syncid = 5)
	if rc == False:
		print "ERROR: Can't start master state sync daemon with syncid 5"
		final_rc = False
	rc = ezbox.start_state_sync_daemon(state = "backup", syncid = 3)
	if rc == False:
		print "ERROR: Can't start backup state sync daemon with syncid 3"
		final_rc = False
	
	print "wait 1 second for EZbox to update"
	time.sleep(1)
	
	apps_info = ezbox.get_applications_info()
	alvs_ss_info = apps_info[0]
	
	expected_alvs_ss_info = {'master_bit' : 1,
							 'backup_bit' : 1,
							 'm_sync_id' : 5,
							 'b_sync_id' : 3}
	
	print "expected state sync info:"
	print expected_alvs_ss_info
	
	rc = check_alvs_state_sync_info(expected_alvs_ss_info, alvs_ss_info)	
	if rc:
		print "Error in comparing alvs state sync info"
		final_rc = False
	
	rc = ezbox.stop_state_sync_daemon(state = "master")
	if rc == False:
		print "ERROR: Can't stop master state sync daemon"
		final_rc = False
	rc = ezbox.stop_state_sync_daemon(state = "backup")
	if rc == False:
		print "ERROR: Can't stop backup state sync daemon"
		final_rc = False
	
	return final_rc


def start_master_with_illegal_mcast_ifn(ezbox):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	rc = ezbox.start_state_sync_daemon(state = "master", syncid = 5, mcast_if = "eth1")
	if rc == True:
		print "ERROR: Can't start master state sync daemon with syncid 5 and mcast-interface eth1"
		return False
	return True


#===============================================================================
# main function
#===============================================================================

def main():
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	args = read_test_arg(sys.argv)	

	init_log(args)
	
	ezbox = ezbox_host(args['setup_num'])
	
	init_ezbox(args,ezbox)
	
	failed_tests = 0
	rc = 0
	
	rc = start_master(ezbox)
	if rc:
		print 'start_master passed !!!\n'
	else:
		print 'start_master failed !!!\n'
		failed_tests += 1
	
	rc = start_backup(ezbox)
	if rc:
		print 'start_backup passed !!!\n'
	else:
		print 'start_backup failed !!!\n'
		failed_tests += 1
	
	rc = start_master_backup(ezbox)
	if rc:
		print 'start_master_backup passed !!!\n'
	else:
		print 'start_master_backup failed !!!\n'
		failed_tests += 1
	
	rc = start_stop_master_backup(ezbox)
	if rc:
		print 'start_stop_master_backup passed !!!\n'
	else:
		print 'start_stop_master_backup failed !!!\n'
		failed_tests += 1
	
	rc = start_stop_restart_master_backup(ezbox)
	if rc:
		print 'start_stop_restart_master_backup passed !!!\n'
	else:
		print 'start_stop_restart_master_backup failed !!!\n'
		failed_tests += 1
	
	rc = start_master_with_illegal_mcast_ifn(ezbox)
	if rc:
		print 'start_master_with_illegal_mcast_ifn passed !!!\n'
	else:
		print 'start_master_with_illegal_mcast_ifn failed !!!\n'
		failed_tests += 1
	
	print "Cleaning EZbox..."
	ezbox.clean(use_director=False, stop_service=True)
	
	if failed_tests == 0:
		print 'ALL Tests were passed !!!'
		exit(0)
	else:
		print 'Number of failed tests: %d' %failed_tests
		exit(1)

main()

