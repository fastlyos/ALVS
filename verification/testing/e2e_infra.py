#!/usr/bin/env python
#===============================================================================
# imports
#===============================================================================
# system  
import cmd
from collections import namedtuple
import logging
from optparse import OptionParser
import os
import sys
from time import gmtime, strftime
from os import listdir
from os.path import isfile, join

from common_infra import *
from server_infra import *
from client_infra import *
from test_infra import *

	
#===============================================================================
# init functions
#===============================================================================

#------------------------------------------------------------------------------ 
def init_players(server_list, ezbox, client_list, vip_list):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	# init HTTP servers 
	for s in server_list:
		print "init server: " + s.str()
		s.init_server(s.ip)

	# init ALVS daemon
	ezbox.connect()
	ezbox.terminate_cp_app()
	ezbox.reset_chip()
	ezbox.flush_ipvs()
	ezbox.copy_binaries('bin/alvs_daemon','bin/alvs_dp')
	ezbox.run_cp()
	ezbox.run_dp()
	ezbox.wait_for_cp_app()
	ezbox.config_vips(vip_list)

	# init client
	for c in client_list:
		print "init client: " + c.str()
		c.init_client()
	
	
#===============================================================================
# clean functions
#===============================================================================
#------------------------------------------------------------------------------ 
def clean_players(server_list, ezbox, client_list):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	# init HTTP servers 
	for s in server_list:
		print "clean server: " + s.str()
		s.clean_server()


	# init client
	for c in client_list:
		print "clean client: " + c.str()
		c.clean_client()

	ezbox.clean()

#===============================================================================
# Run functions
#===============================================================================
def run_test(server_list, client_list):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	
	# Execute HTTP servers scripts 
	for s in server_list:
		print "running server: " + s.str()
		s.execute()

	# Excecure client
	for c in client_list:
		print "running client: " + c.str()
		c.execute()
	
	

def collect_logs(server_list, ezbox, client_list):
	print "FUNCTION " + sys._getframe().f_code.co_name + " called"
	current_time = strftime("%Y-%m-%d_%H-%M-%S", gmtime())
 	dir_name = 'test_logs_%s' %current_time
 	cmd = "mkdir -p %s" %dir_name
	os.system(cmd)
	for c in client_list:
		c.get_log(dir_name)

	return dir_name

def client_checker(log_dir, expected={}, step_count = 1):
	if len(expected) == 0:
		return True
	
	file_list = [log_dir+'/'+f for f in listdir(log_dir) if isfile(join(log_dir, f))]
	all_responses = dict((i,{}) for i in range(step_count))
	for filename in file_list:
		step = 0
		if filename[-1] == '1':
			step = 1
		if filename[-1] == '2':
			step = 2
		client_ip = filename[filename.find('client_')+7 : filename.find('.log')]
		client_responses = {}
		logfile=open(filename, 'r')
		for line in logfile:
			if len(line) > 2 and line[0] != '#':
				split_line = line.split(':')
				key = split_line[1].strip()
				client_responses[key] = client_responses.get(key, 0) + 1
		all_responses[step][client_ip] = client_responses	 
	
	for step, responses in all_responses.items():
		if step_count > 1:
			expected_dict = expected[step]
			print 'step %d :'%step
		else:
			expected_dict = expected
		if 'client_count' in expected_dict:
			if len(responses) != expected_dict['client_count']:
				print 'ERROR: wrong number of logs. log count = %d, client count = %d' %(len(responses),expected_dict['client_count'])
				return False
			
		#expected_servers
		for client_ip,client_responses in responses.items():
			print 'testing client %s ...' %client_ip
			total = 0
			for ip, count in client_responses.items():
				print 'response count from server %s = %d' %(ip,count)
				total += count

			if 'no_404' in expected_dict:
				if expected_dict['no_404'] == True:
					if '404 ERROR' in client_responses:
						print 'ERROR: client received 404 response. count = %d' % client_responses['404 ERROR']
						return False
			if 'server_count_per_client' in expected_dict:
				if len(client_responses) != expected_dict['server_count_per_client']:
					print 'ERROR: client received responses from different number of expected servers. expected = %d , received = %d' %(expected_dict['server_count_per_client'], len(client_responses))
					return False
			if 'client_response_count' in expected_dict:
				if total != expected_dict['client_response_count']:
					print 'ERROR: client received wrong number of responses. expected = %d , received = %d' %(expected_dict['client_response_count'], total)
					return False
			if 'expected_servers' in expected_dict:
				expected_servers = expected_dict['expected_servers'][client_ip]
				for ip, count in client_responses.items():
					if ip not in expected_servers:
						print 'ERROR: client received response from unexpected server. server ip = %s , list of expected servers: %s' %(ip, expected_servers)
						return False
	return True		

