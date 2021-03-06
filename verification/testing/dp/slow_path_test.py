#!/usr/bin/env python

import sys
sys.path.append("verification/testing")
from test_infra import * 
import random

args = read_test_arg(sys.argv)    

log_file = "scheduling_algorithm_test.log"
if 'log_file' in args:
    log_file = args['log_file']
init_logging(log_file)

# scenarios_to_run = args['scenarios']
scenarios_to_run = [1,2,3]

ezbox = ezbox_host(args['setup_num'])

if args['hard_reset']:
    ezbox.reset_ezbox()

# init ALVS daemon
ezbox.connect()
ezbox.terminate_cp_app()
ezbox.reset_chip()
ezbox.flush_ipvs()
ezbox.copy_cp_bin('bin/alvs_daemon')
ezbox.run_cp()
ezbox.copy_dp_bin('bin/alvs_dp')
ezbox.wait_for_cp_app()
ezbox.run_dp()
time.sleep(5)
    
# each setup can use differen VMs
ip_list = get_setup_list(args['setup_num'])

# each setup can use different the virtual ip 
virtual_ip_address_1 = get_setup_vip(args['setup_num'], 0)
virtual_ip_address_2 = get_setup_vip(args['setup_num'], 1)

# create servers
server1 = real_server(management_ip=ip_list[0]['hostname'], data_ip=ip_list[0]['ip'])
server2 = real_server(management_ip=ip_list[1]['hostname'], data_ip=ip_list[1]['ip'])
server3 = real_server(management_ip=ip_list[2]['hostname'], data_ip=ip_list[2]['ip'])
 
# create services on ezbox
first_service = service(ezbox=ezbox, virtual_ip=virtual_ip_address_1, port='80', schedule_algorithm = 'source_hash')
first_service.add_server(server1, weight='1')
first_service.add_server(server2, weight='1')
first_service.add_server(server3, weight='1')

second_service = service(ezbox=ezbox, virtual_ip=virtual_ip_address_2, port='80', schedule_algorithm = 'source_hash_with_source_port')
second_service.add_server(server1, weight='1')
second_service.add_server(server2, weight='1')
second_service.add_server(server3, weight='1')
 
# create client
client_object = client(management_ip=ip_list[3]['hostname'], data_ip=ip_list[3]['ip'])
     
if 1 in scenarios_to_run:
    print "\nChecking Scenario 1"
    
    # create packets
    packet_sizes = [64,127,129,511,513,1023,1025,1500]
    packet_list_to_send = []
    
    print "Creating Packets"
    for i in range(50):
        # set the packet size
        if i < len(packet_sizes):
            packet_size = packet_sizes[i]
        else:
            packet_size = random.randint(60,1500)
        
        random_source_port = '%02x %02x'%(random.randint(0,256),random.randint(0,256))

        # create packet
        data_packet = tcp_packet(mac_da=ezbox.setup['mac_address'],
                                 mac_sa=client_object.mac_address,
                                 ip_dst=first_service.virtual_ip_hex_display,
                                 ip_src='C0 A8 11 01',#client_object.hex_display_to_ip,
                                 tcp_source_port = random_source_port,
                                 tcp_dst_port = '00 50', # port 80
                                 packet_length=packet_size)
        data_packet.generate_packet()
        
        packet_list_to_send.append(data_packet)
        
    pcap_to_send = create_pcap_file(packets_list=packet_list_to_send, output_pcap_file_name='verification/testing/dp/pcap_files/temp_packet.pcap')
    
    time.sleep(2) 
    server1.capture_packets_from_service(service=first_service)
    server2.capture_packets_from_service(service=first_service)
    server3.capture_packets_from_service(service=first_service)
       
    # send packet
    time.sleep(5)
    print "Send packets"
    client_object.send_packet_to_nps(pcap_to_send)
      
    time.sleep(5)
    
    packets_received_1 = server1.stop_capture()
    packets_received_2 = server2.stop_capture()
    packets_received_3 = server3.stop_capture()
    
    print "Server 1 received %d packets"%packets_received_1
    print "Server 2 received %d packets"%packets_received_2
    print "Server 3 received %d packets"%packets_received_3
    
    if packets_received_1 == 50 and packets_received_2 == 0 and packets_received_3 == 0:
    #     server1.compare_received_packets_to_pcap_file(pcap_file='p1.pcap', pcap_file_on_server='/tmp/server_dump.pcap')
        print "Test Passed"
    else:
        print "Fail, received packets not as expected"
        exit(1)
        
    
        
###########################################################################################################################
# this scenario check the scheduling algorithm of source hash, ip source is changing, (service is on source port disable) #
###########################################################################################################################        
if 2 in scenarios_to_run:
    print "\nChecking Scenario 2"
    
    # create packets
    ip_source_address = '192.168.18.1'
    packet_sizes = [60,127,129,511,513,1023,1025,1500]
    packet_list_to_send = []
    
    print "Creating Packets"
    for i in range(50):
        # set the packet size
        if i < len(packet_sizes):
            packet_size = packet_sizes[i]
        else:
            packet_size = random.randint(60,1500)
        
        ip_source_address = add2ip(ip_source_address,1)
        
        # create packet
        data_packet = tcp_packet(mac_da=ezbox.setup['mac_address'],
                                 mac_sa=client_object.mac_address,
                                 ip_dst=first_service.virtual_ip_hex_display,
                                 ip_src=ip_source_address,
                                 tcp_source_port = '00 00',
                                 tcp_dst_port = '00 50', # port 80
                                 packet_length=packet_size)
        data_packet.generate_packet()
        
        packet_list_to_send.append(data_packet)
        
    pcap_to_send = create_pcap_file(packets_list=packet_list_to_send, output_pcap_file_name='verification/testing/dp/pcap_files/temp_packet.pcap')
     
    server1.capture_packets_from_service(service=first_service)
    server2.capture_packets_from_service(service=first_service)
    server3.capture_packets_from_service(service=first_service)
       
    # send packet
    client_object.send_packet_to_nps(pcap_to_send)
      
    time.sleep(10)
    
    packets_received_1 = server1.stop_capture()
    packets_received_2 = server2.stop_capture()
    packets_received_3 = server3.stop_capture()
    
    print "Server 1 received %d packets"%packets_received_1
    print "Server 2 received %d packets"%packets_received_2
    print "Server 3 received %d packets"%packets_received_3
    
    if packets_received_1 == 17 and packets_received_2 == 20 and packets_received_3 == 13:
    #     server1.compare_received_packets_to_pcap_file(pcap_file='p1.pcap', pcap_file_on_server='/tmp/server_dump.pcap')
        print "Test Passed"
    else:
        print "Fail, received packets not as expected"
        exit(1)
        
        
######################################################################################################################
########                       this scenario check the scheduling algorithm of source hash,                 ##########
########                  ip source and source port is changing, (service is on source port enable)         ##########
######################################################################################################################        
if 3 in scenarios_to_run:
    print "\nChecking Scenario 3"
    
    # create packets
    ip_source_address = '192.168.18.1'
    packet_sizes = [60,127,129,511,513,1023,1025,1500]
    packet_list_to_send = []
    
    print "Creating Packets"
    for i in range(50):
        # set the packet size
        if i < len(packet_sizes):
            packet_size = packet_sizes[i]
        else:
            packet_size = random.randint(60,1500)
        
        tcp_source_port = "%02x %02x"%(i,i)
        
        # create packet
        data_packet = tcp_packet(mac_da=ezbox.setup['mac_address'],
                                 mac_sa=client_object.mac_address,
                                 ip_dst=first_service.virtual_ip_hex_display,
                                 ip_src=client_object.hex_display_to_ip,
                                 tcp_source_port = tcp_source_port,
                                 tcp_dst_port = '00 50', # port 80
                                 packet_length=packet_size)
        data_packet.generate_packet()
        
        packet_list_to_send.append(data_packet)
        
    pcap_to_send = create_pcap_file(packets_list=packet_list_to_send, output_pcap_file_name='verification/testing/dp/pcap_files/temp_packet.pcap')
     
    server1.capture_packets_from_service(service=first_service)
    server2.capture_packets_from_service(service=first_service)
    server3.capture_packets_from_service(service=first_service)
       
    # send packet
    client_object.send_packet_to_nps(pcap_to_send)
      
    time.sleep(10)
    
    packets_received_1 = server1.stop_capture()
    packets_received_2 = server2.stop_capture()
    packets_received_3 = server3.stop_capture()
    
    print "Server 1 received %d packets"%packets_received_1
    print "Server 2 received %d packets"%packets_received_2
    print "Server 3 received %d packets"%packets_received_3
    
    if packets_received_1 == 30 and packets_received_2 == 37 and packets_received_3 == 33:
    #     server1.compare_received_packets_to_pcap_file(pcap_file='p1.pcap', pcap_file_on_server='/tmp/server_dump.pcap')
        print "Test Passed"
    else:
        print "Fail, received packets not as expected"
        exit(1)       
        
        
        
# checkers

# server1.compare_received_packets_to_pcap_file(pcap_file='p1.pcap', pcap_file_on_server='/tmp/server_dump.pcap')


# tear down
# server1.close()
# server2.close()
# client.close()
# ezbox.close()
