#!/usr/bin/python

# Test whether a retained PUBLISH to a topic with QoS 0 is sent with
# retain=false to an already subscribed client.

import subprocess
import socket
import time
from struct import *

rc = 1
keepalive = 60
mid = 16
connect_packet = pack('!BBH6sBBHH22s', 16, 12+2+22,6,"MQIsdp",3,2,keepalive,22,"retain-qos0-fresh-test")
connack_packet = pack('!BBBB', 32, 2, 0, 0);

publish_packet = pack('!BBH16s16s', 48+1, 2+16+16, 16, "retain/qos0/test", "retained message")
publish_fresh_packet = pack('!BBH16s16s', 48, 2+16+16, 16, "retain/qos0/test", "retained message")
subscribe_packet = pack('!BBHH16sB', 130, 2+2+16+1, mid, 16, "retain/qos0/test", 0)
suback_packet = pack('!BBHB', 144, 2+1, mid, 0)

broker = subprocess.Popen(['../../src/mosquitto', '-p', '1888'], stderr=subprocess.PIPE)

try:
    time.sleep(0.5)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", 1888))
    sock.send(connect_packet)
    connack_recvd = sock.recv(256)

    if connack_recvd != connack_packet:
        print("FAIL: Connect failed.")
    else:
        sock.send(subscribe_packet)

        suback_recvd = sock.recv(256)

        if suback_recvd != suback_packet:
            (cmd, rl, mid_recvd, qos) = unpack('!BBHB', suback_recvd)
            print("FAIL: Expected 144,3,"+str(mid)+",0 got " + str(cmd) + "," + str(rl) + "," + str(mid_recvd) + "," + str(qos))
        else:
            sock.send(publish_packet)
            publish_recvd = sock.recv(256)

            if publish_recvd != publish_fresh_packet:
                print("FAIL: Received incorrect publish.")
                print("Received: "+publish_recvd+" length="+str(len(publish_recvd)))
                print("Expected: "+publish_fresh_packet+" length="+str(len(publish_fresh_packet)))
            else:
                rc = 0
    sock.close()
finally:
    broker.terminate()
    broker.wait()

exit(rc)
