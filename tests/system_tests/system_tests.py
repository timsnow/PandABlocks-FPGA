#!/bin/env dls-python

import sys
import os
import socket
import struct
from pkg_resources import require
require("numpy")
require("h5py")
import h5py
import unittest
import xml.etree.ElementTree

# add our python dir
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "python"))

from zebra2.capture import Capture


test_script = os.path.join(os.path.dirname(__file__),  "testseq")

class SystemTest(unittest.TestCase):

    def __init__(self,hostname, cmdport, rcvport, options, reference_hdf):
        name = '{}'.format(' '.join(options))
        setattr(self, name, self.runTest)
        super(SystemTest, self).__init__(name)
        self.options = options
        self.data = []
        self.header_fields = []
        self.hdf5_file_path = os.path.join(os.path.dirname(__file__),'hdf5',
                                           reference_hdf)

        #setup connection
        self.cmdsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.cmdsock.connect((hostname, cmdport))
        self.cmdsock.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
        self.cmdsock.settimeout(1)

        self.rcvsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.rcvsock.connect((hostname, rcvport))
        self.rcvsock.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
        self.rcvsock.settimeout(1)

        #load testscript (testseq)
        self.test_script = open(test_script, 'r')

    def send_set_options(self):
        config_msg = " ".join(self.options) + '\n'
        self.rcvsock.sendall(config_msg)
        data_stream = self.rcvsock.recv(4096)

    def send_test_commands(self):
        #send the test commands
        for line in self.test_script:
            self.cmdsock.sendall(line)
            if line[:1].isdigit() or "TABLE<" in line:
                pass
                # print "TABLE", line
            else:
                # print "SENDING:", line
                data_stream = self.cmdsock.recv(4096)
                # print "RECEIVED: ", data_stream

    def get_data(self):
        data_string = ""
        binary_data_string = ""
        header_string = ""
        while True:
            try:
                data_stream = self.rcvsock.recv(4096)
                # print [data_stream]
            except socket.timeout:
                break
            if data_stream.startswith("END"):
                break
            elif data_stream.startswith("<"):
                header_string += data_stream
            elif data_stream.startswith("BIN"):
                binary_data_string += data_stream
            else:
                data_string += data_stream
        if header_string:
            self.parse_header(header_string)
        if data_string:
            self.data = self.parse_data(data_string.split('\n'))
        elif binary_data_string:
            self.data = self.parse_binary(binary_data_string.strip('BIN ').split("BIN "))

    def parse_data(self, data_stream):
        data = []
        bin_data = []
        for line in data_stream:
            if not line.startswith('OK') and line:
                data.append(line.strip().split(" "))
        return data

    def parse_header(self, header):
        try:
            #get the header
            root = xml.etree.ElementTree.fromstring(header)
            # for data_stream in root.iter('data'):
                # print "data", data_stream.attrib
            for field in root.iter('field'):
                self.header_fields.append(field.attrib)
        except xml.etree.ElementTree.ParseError, e:
            print 'EXCEPTION:', e

    def parse_binary(self, binary_stream):
        binary_data = []
        fmt = self.get_bin_unpack_fmt()
        for line in binary_stream:
            packet_length = struct.unpack('<I', line[0:4])[0]
            binary_data.append(struct.unpack(fmt, line[4:packet_length]))
        return binary_data

    def get_bin_unpack_fmt(self):
        format_chars = {
            'int32': 'i',
            'uint32': 'I',
            'int64': 'q',
            'uint64': 'Q',
            'double': 'd'}
        fmt = '<'
        for field in self.header_fields:
            fmt += format_chars[field['type']]
        return fmt

    def check_data(self):
        #open refrence hdf5 file and check that the data matches
        hdf5_file = h5py.File(self.hdf5_file_path,  "r")
        for item in hdf5_file.attrs.keys():
            print item + ":", hdf5_file.attrs[item]
        counts = hdf5_file['/Scan/data/counts']
        positions = hdf5_file['/Scan/data/positions']
        # print "{}\t{}\t{}".format("\n#", "counts", "positions")
        for i in range(len(counts)):
            self.assertEqual(counts[i], str(self.data[i][0]))
            # print "{}\t{}\t{}".format(i, counts[i], positions[i])
        hdf5_file.close()

    #RENAME
    def runTest(self):
        self.send_set_options()
        self.send_test_commands()
        self.get_data()
        self.check_data()
        #cleanup
        self.test_script.close()
        self.cmdsock.close()
        self.rcvsock.close()

#generate reference HDF5 file
def generateHDF(hostname,cmdport, rcvport, output_dir):
    print "GENERATING REFERENCE HDF5 FILE"
    capture = Capture(hostname, rcvport, output_dir)
    capture.send_test_commands(hostname, cmdport,test_script)
    capture.run()
    print "REFRENCE HDF5 FILE GENERATED OK"
    return capture.hdf_file

def make_suite():
    hdf_name = generateHDF('localhost', 8888, 8889,
                           os.path.join(os.path.dirname(__file__), 'hdf5'))
    suite = unittest.TestSuite()
    options = [["XML"]]
    options.append(["XML", "FRAMED", "SCALED"])
    options.append(["XML", "FRAMED", "UNSCALED"])
    options.append(["XML", "ASCII", "SCALED"])
    for option in options:
        testcase = SystemTest('localhost', 8888, 8889, option, hdf_name)
        suite.addTest(testcase)
    return suite

if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(make_suite())
    sys.exit(not result.wasSuccessful())
