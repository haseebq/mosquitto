#!/usr/bin/python

# Test whether a client sends a correct PUBLISH to a topic with QoS 0 and no payload.

# The client should connect to port 1888 with keepalive=60, clean session set,
# and client id publish-qos0-test-np
# The test will send a CONNACK message to the client with rc=0. Upon receiving
# the CONNACK and verifying that rc=0, the client should send a PUBLISH message
# to topic "pub/qos0/no-payload/test" with zero length payload and QoS=0. If
# rc!=0, the client should exit with an error.
# After sending the PUBLISH message, the client should send a DISCONNECT message.

import os
import subprocess
import socket
import sys
import time
from struct import *

rc = 1
keepalive = 60
connect_packet = pack('!BBH6sBBHH20s', 16, 12+2+20,6,"MQIsdp",3,2,keepalive,20,"publish-qos0-test-np")
connack_packet = pack('!BBBB', 32, 2, 0, 0);

publish_packet = pack('!BBH24s', 48, 2+24, 24, "pub/qos0/no-payload/test")

disconnect_packet = pack('!BB', 224, 0)

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
    conn.settimeout(10)
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