#!/usr/bin/python

import sys
sys.path.append("/home/expando-ws/white_space/setup")
from util import run_cmd

if __name__ == '__main__':
	if len(sys.argv) != 3:
		print "Usage: %s num_retrans dirname_log" % sys.argv[0]
		exit(0)

	ACK_TIME_OUT = 60
	MAX_ACK_CNT = 1
	MAX_BATCH_SIZE = 10
	SPEED = 0.0
	min_pkt_cnt = 30  # number of packets in each raw ack

	NUM_RETRANS = int(sys.argv[1])
	dirname = sys.argv[2]

	# Construct log file
	cmd = "mkdir -p %s" % dirname
	run_cmd(cmd)
	filename = "%s/client.dat" % dirname

	if NUM_RETRANS == 0:
		BLOCK_TIME = 100
	elif NUM_RETRANS == 1:
		BLOCK_TIME = 400
	elif NUM_RETRANS == 2:
		BLOCK_TIME = 600
	elif NUM_RETRANS == 3:
		BLOCK_TIME = 650
	elif NUM_RETRANS == 4:
		BLOCK_TIME = 750
	else:
		assert 0

	# Kill iperf
	cmd = "killall iperf"
	run_cmd(cmd)

	print "MAX_BATCH_SIZE: %d" % MAX_BATCH_SIZE
	PER_PKT_LEN = 2 
	TUNNEL_PKT_SIZE = 1448
	MTU = TUNNEL_PKT_SIZE - MAX_BATCH_SIZE * PER_PKT_LEN
	cmd = "ifconfig tun0 mtu %d" % MTU 
	run_cmd(cmd)

	cmd = "./wspace_client_scout -C 128.105.22.249 -i tun0 -T %d -B %d -A %d -p %g -n %d > %s" % \
	(ACK_TIME_OUT, BLOCK_TIME, MAX_ACK_CNT, SPEED, min_pkt_cnt, filename)
	run_cmd(cmd)
