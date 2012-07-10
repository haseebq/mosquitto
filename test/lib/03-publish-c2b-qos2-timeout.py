#!/usr/bin/python

# Test whether a client sends a correct PUBLISH to a topic with QoS 1 and responds to a delay.

# The client should connect to port 1888 with keepalive=60, clean session set,
# and client id publish-qos2-test
# The test will send a CONNACK message to the client with rc=0. Upon receiving
# the CONNACK the client should verify that rc==0. If not, it should exit with
# return code=1.
# On a successful CONNACK, the client should send a PUBLISH message with topic
# "pub/qos2/test", payload "message" and QoS=2.
# The test will not respond to the first PUBLISH message, so the client must
# resend the PUBLISH message with dup=1. Note that to keep test durations low, a
# message retry timeout of less than 5 seconds is required for this test.
# On receiving the second PUBLISH message, the test will send the correct
# PUBREC response. On receiving the correct PUBREC response, the client should
# send a PUBREL message.
# The test will not respond to the first PUBREL message, so the client must
# resend the PUBREL message with dup=1. On receiving the second PUBREL message,
# the test will send the correct PUBCOMP response. On receiving the correct
# PUBCOMP response, the client should send a DISCONNECT message.

import os
import subprocess
import socket
import sys
import time
from struct import *

rc = 1
keepalive = 60
connect_packet = pack('!BBH6sBBHH17s', 16, 12+2+17,6,"MQIsdp",3,2,keepalive,17,"publish-qos2-test")
connack_packet = pack('!BBBB', 32, 2, 0, 0);

disconnect_packet = pack('!BB', 224, 0)

mid = 1
publish_packet = pack('!BBH13sH7s', 48+4, 2+13+2+7, 13, "pub/qos2/test", mid, "message")
publish_dup_packet = pack('!BBH13sH7s', 48+8+4, 2+13+2+7, 13, "pub/qos2/test", mid, "message")
pubrec_packet = pack('!BBH', 80, 2, mid)
pubrel_packet = pack('!BBH', 96+2, 2, mid)
pubrel_dup_packet = pack('!BBH', 96+8+2, 2, mid)
pubcomp_packet = pack('!BBH', 112, 2, mid)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.settimeout(10)
sock.bind(('', 1888))
sock.listen(5)

client_args = sys.argv[1:]
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = '../../lib:../../lib/cpp'
try:
    pp = env['PYTHONPATH']
except KeyError:
    pp = ''
env['PYTHONPATH'] = '../../lib/python:'+pp
client = subprocess.Popen(client_args, env=env)

try:
    (conn, address) = sock.accept()
    conn.settimeout(5)
    connect_recvd = conn.recv(256)

    if connect_recvd != connect_packet:
        print("FAIL: Received incorrect connect.")
        print("Received: "+connect_recvd+" length="+str(len(connect_recvd)))
        print("Expected: "+connect_packet+" length="+str(len(connect_packet)))
    else:
        conn.send(connack_packet)
        publish_recvd = conn.recv(256)

        if publish_recvd != publish_packet:
            print("FAIL: Received incorrect publish.")
            print("Received: "+publish_recvd+" length="+str(len(publish_recvd)))
            print("Expected: "+publish_packet+" length="+str(len(publish_packet)))
        else:
            # Delay for > 3 seconds (message retry time)
            publish_recvd = conn.recv(256)

            if publish_recvd != publish_dup_packet:
                print("FAIL: Received incorrect publish.")
                print("Received: "+publish_recvd+" length="+str(len(publish_recvd)))
                print("Expected: "+publish_dup_packet+" length="+str(len(publish_dup_packet)))
            else:
                conn.send(pubrec_packet)
                pubrel_recvd = conn.recv(256)
                
                if pubrel_recvd != pubrel_packet:
                    print("FAIL: Received incorrect pubrel.")
                    (cmd, rl, mid_recvd) = unpack('!BBH', pubrel_recvd)
                    print("Received: "+str(cmd)+", " + str(rl)+", " + str(mid_recvd))
                    print("Expected: 98, 2, " + str(mid))
                else:
                    pubrel_recvd = conn.recv(256)
                
                    if pubrel_recvd != pubrel_dup_packet:
                        print("FAIL: Received incorrect pubrel.")
                        (cmd, rl, mid_recvd) = unpack('!BBH', pubrel_recvd)
                        print("Received: "+str(cmd)+", " + str(rl)+", " + str(mid_recvd))
                        print("Expected: 116, 2, " + str(mid))
                    else:
                        conn.send(pubcomp_packet)
                        disconnect_recvd = conn.recv(256)

                        if disconnect_recvd != disconnect_packet:
                            print("FAIL: Received incorrect disconnect.")
                            (cmd, rl) = unpack('!BB', disconnect_recvd)
                            print("Received: "+str(cmd)+", " + str(rl))
                            print("Expected: 224, 0")
                        else:
                            rc = 0

    conn.close()
finally:
    client.terminate()
    client.wait()
    sock.close()

exit(rc)