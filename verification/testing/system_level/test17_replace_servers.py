#!/usr/bin/env python
#===============================================================================
# imports
#===============================================================================
# system  
import cmd
from collections import namedtuple
import logging
import os
import sys
import inspect
from multiprocessing import Process

# local
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0,parentdir) 
from e2e_infra import *

#===============================================================================
# Test Globals
#===============================================================================
#general porpuse
g_request_count  = 1000
g_next_vm_index = 0

# got from user
g_setup_num      = None

# Configured acording to test number
g_server_count       = None
g_client_count       = None
g_service_count      = None
g_servers_to_replace = None
g_sched_alg          = None
g_sched_alg_opt      = None


#===============================================================================
# User Area function needed by infrastructure
#===============================================================================

#===============================================================================
# Function: set_user_params
#
# Brief:
#===============================================================================
def user_init(setup_num):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	dict = generic_init(setup_num, g_service_count, g_server_count, g_client_count)
	
	for s in dict['server_list']:
		s.vip = dict['vip_list'][0]
		
	return convert_generic_init_to_user_format(dict)
	
#===============================================================================
# Function: set_user_params
#
# Brief:
#===============================================================================
def client_execution(client, vip):
	client.exec_params += " -i %s -r %d" %(vip, g_request_count)
	client.execute()

#===============================================================================
# Function: set_user_params
#
# Brief:
#===============================================================================
def run_user_test_step(server_list, ezbox, client_list, vip_list):
	# modified global variables
	global g_next_vm_index
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	#===========================================================================
	# init local variab;es 
	#===========================================================================
	vip       = vip_list[0]
	port      = 80
	
	#===========================================================================
	# Set services/servers & prepare server_list to add in the next step 
	#===========================================================================
	new_server_list = []
	
	ezbox.add_service(vip, port, g_sched_alg, g_sched_alg_opt)
	for idx,s in enumerate(server_list):
		if ( idx >= (g_server_count - g_servers_to_replace) ):
			new_server_list.append(s)
		else:
			ezbox.add_server(vip, port, s.ip, port)

	#===========================================================================
	# send requests & replace server while sending requests 
	#===========================================================================
	print "execute requests on client"
	process_list = []
	for client in client_list:
		process_list.append(Process(target=client_execution, args=(client,vip,)))
	for p in process_list:
		p.start()
	
	# wait for requests to start 
	time.sleep(0.5) 
	# remove servers from service (& from list) and add new server to service
	print 'Start user test step 0'
	for idx,new_server in enumerate(new_server_list):
		removed_server = server_list[idx]
		ezbox.delete_server(vip, port, removed_server.ip, port)			
		print "Server %s removed" %removed_server.ip 
		ezbox.add_server(vip, port, new_server.ip, port)
		print "Server %s added" %new_server.ip

	for p in process_list:
		p.join()

	#===========================================================================
	# send requests after modifing servers list
	# - removed servers should not answer
	#===========================================================================
	print 'Start user test step 1'
	process_list = []
	for client in client_list:
		new_log_name = client.logfile_name+'_1'
		client.add_log(new_log_name) 
		process_list.append(Process(target=client_execution, args=(client,vip,)))
	for p in process_list:
		p.start()
	for p in process_list:
		p.join()
		
#===============================================================================
# Function: set_user_params
#
# Brief:
#===============================================================================
def run_user_checker(server_list, ezbox, client_list, vip_list, log_dir):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	server_list_step_2 = []
	for idx in range(g_servers_to_replace, g_server_count):
		server_list_step_2.append(server_list[idx])
			
	expected_dict = {}
	expected_dict[0] = {'client_response_count':g_request_count,
					'client_count'     : len(client_list), 
					'no_404'           : True,
					'expected_servers' : server_list}
	expected_dict[1] = {'client_response_count':g_request_count,
					'client_count'     : len(client_list), 
					'no_404'           : True,
						'check_distribution':(server_list_step_2,vip_list,0.035),
					'expected_servers' : server_list_step_2}
	
	return client_checker(log_dir, expected_dict, 2)

#===============================================================================
# Function: set_user_params
#
# Brief:
#===============================================================================
def set_user_params(setup_num):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	# modified global variables
	global g_setup_num
	global g_server_count
	global g_client_count
	global g_service_count
	global g_servers_to_replace
	global g_sched_alg
	global g_sched_alg_opt
	
	# user configuration
	g_setup_num = setup_num
	
	# static test configuration
	g_server_count       = 15
	g_client_count       = 5
	g_service_count      = 1
	g_servers_to_replace = 5
	g_sched_alg          = "sh"
	g_sched_alg_opt      = "-b sh-port"
	
	# print configuration
	print "setup_num:      " + str(g_setup_num)
	print "service_count:  " + str(g_service_count)
	print "server_count:   " + str(g_server_count)
	print "client_count:   " + str(g_client_count)
	print "request_count:  " + str(g_request_count)
	print "servers_to_add: " + str(g_servers_to_replace)

#===============================================================================
# Function: main function
#
# Brief:
#===============================================================================
def main():
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	config = generic_main()
	
	set_user_params(config['setup_num'])
	
	server_list, ezbox, client_list, vip_list = user_init(config['setup_num'])
	
	init_players(server_list, ezbox, client_list, vip_list, config)

	run_user_test_step(server_list, ezbox, client_list, vip_list)

	log_dir = collect_logs(server_list, ezbox, client_list)

	gen_rc = general_checker(server_list, ezbox, client_list)

	client_rc = run_user_checker(server_list, ezbox, client_list, vip_list, log_dir)

	clean_players(server_list, ezbox, client_list, True, config['stop_ezbox'])

	if client_rc and gen_rc:
		print 'Test passed !!!'
		exit(0)
	else:
		print 'Test failed !!!'
		exit(1)

main()
