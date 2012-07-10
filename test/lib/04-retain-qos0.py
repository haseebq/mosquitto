#!/usr/bin/python

# Test whether a client sends a correct retained PUBLISH to a topic with QoS 0.

import os
import subprocess
import socket
import sys
import time
from struct import *

rc = 1
keepalive = 60
mid = 16
connect_packet = pack('!BBH6sBBHH16s', 16, 12+2+16,6,"MQIsdp",3,2,keepalive,16,"retain-qos0-test")
connack_packet = pack('!BBBB', 32, 2, 0, 0);

publish_packet = pack('!BBH16s16s', 48+1, 2+16+16, 16, "retain/qos0/test", "retained message")

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
            rc = 0
        
    conn.close()
finally:
    client.terminate()
    client.wait()
    sock.close()

exit(rc)