#!/bin/env dls-python
from pkg_resources import require
require("Jinja2")
import os, sys
import csv
import collections
from jinja2 import Environment, FileSystemLoader

ROOT_DIR = os.path.abspath(os.pardir)
OUTPUT_DIR = os.path.join(ROOT_DIR, "build", "config_d")
MODULE_DIR = os.path.join(ROOT_DIR, "modules")
APP_DIR = os.path.join(ROOT_DIR, "apps")

class ConfigGenerator(object):
    def __init__(self):
        self.temp_env = Environment(
            autoescape=False,
            loader=FileSystemLoader(MODULE_DIR),
            trim_blocks=True)

        self.temp_env.globals['newBitBus'] = self.new_bit_bus
        self.temp_env.globals['newBlockNo'] = self.new_block_no
        self.temp_env.globals['newPosBus'] = self.new_pos_bus
        self.temp_env.globals['newBlockReg'] = self.new_block_reg
        self.app_config = collections.OrderedDict()
        self.curr_bitbus = 9
        self.curr_posbus = 0
        self.current_block = 2
        self.block_regs = {}
        self.panda_carrier_config = {"TTLIN": 6,
                                     "TTLOUT": 10,
                                     "LVDSIN": 2,
                                     "LVDSOUT": 2,
                                     "INENC": 4,
                                     "OUTENC": 4}
        self.processing_block = ''

    def render_template(self, template_filename, context={}):
        return self.temp_env.get_template(template_filename).render(context)

    def generate_output_file(self, app_config, out_dir, variables):
        self.start_empty_file(out_dir)
        output = self.render_template(os.path.join("base/", out_dir))
        self.write_output_file(out_dir, output)
        output = self.render_template(os.path.join("panda_carrier/", out_dir))
        self.write_output_file(out_dir, output)
        for config in app_config.keys():
            if config in ["FMC", "SFP"]:
                output = self.render_template(os.path.join(config.lower() + "_loopback", out_dir), variables)
            else:
                output = self.render_template(os.path.join(config.lower(), out_dir), variables)
            self.write_output_file(out_dir, output)

    def generate_description(self, app_config, out_dir):
        self.start_empty_file(out_dir)
        desc_file = os.path.join(MODULE_DIR, "panda_carrier", "description")
        with open(desc_file) as infile:
            self.write_output_file(out_dir, infile.read())
        for config in app_config.keys():
            desc_file = os.path.join(MODULE_DIR, config.lower(), "description")
            if os.path.isfile(desc_file):
                with open(desc_file) as infile:
                    self.write_output_file(out_dir, infile.read())

    def start_empty_file(self, out_dir):
        fname = os.path.join(OUTPUT_DIR, out_dir)
        with open(fname, 'w') as f:
            f.write('')

    def write_output_file(self, out_dir, out_file):
        fname = os.path.join(OUTPUT_DIR, out_dir)
        with open(fname, 'a') as f:
            f.write(out_file)

    def extract_file_info(self, file_name):
        file_info = collections.OrderedDict()
        for line in file(file_name):
            row = line.split()
            try:
                if row and not row[0].startswith("#"):
                    #strip the '_loopback'
                    if "_LOOPBACK" in row[0]:
                        file_info[row[0].split('_LOOPBACK')[0]] = row[1]
                    else:
                        file_info[row[0]] = row[1]
            except:
            #NEED SOME EXTRA CHECKING ON THIS FILE
                print "INVALID ENTRY, LINE", file.line_num,": ", row
        return file_info

    def parse_app_file(self, appfile):
        file = os.path.join(APP_DIR, appfile)
        self.app_config = self.extract_file_info(file)
        #add the carrier_config modules
        self.app_config.update(self.panda_carrier_config)
        self.init_block_regs()
        return self.extract_file_info(file)

    def init_block_regs(self):
        for block in self.app_config.keys():
            self.block_regs[block] = -1

    def parse_meta_file(self, meta_file, block):
        meta_info = collections.OrderedDict()
        try:
            meta_info = self.extract_file_info(meta_file)
        except:
            print "no meta file for: ", block, sys.exc_info()[0]
        return meta_info

    def checkBlockMax(self, app_config):
        for block in app_config.keys():
            if block in ['FMC', 'SFP']:
                meta_file = os.path.join(MODULE_DIR, block.lower() + '_loopback', "meta")
            else:
                meta_file = os.path.join(MODULE_DIR, block.lower(), "meta")
            meta_info = self.parse_meta_file(meta_file, block)
            #check the defined number in the config against the max in the meta
            if int(app_config[block]) > int(meta_info["MAX"]):
                raise ValueError('Max value exceeded for ' + block)

    def new_bit_bus(self):
        bus_values, self.curr_bitbus = self.new_bus(128, 'bit_bus', self.curr_bitbus)
        return bus_values

    def new_pos_bus(self):
        bus_values, self.curr_posbus = self.new_bus(64, 'pos_bus', self.curr_posbus)
        return bus_values

    def new_bus(self, limit, type, current_val):
        block = self.processing_block
        bus_values = []
        for values in range(int(self.app_config[block.upper()])):
            if int(current_val) < limit:
                current_val += 1
                bus_values.append(str(current_val))
            else:
                raise ValueError(
                    'Max '+ type + ' value of ' + str(limit) + ' exceeded')
        return " ".join(bus_values), current_val

    def new_block_no(self, block):
        self.processing_block = block
        self.current_block += 1
        return str(self.current_block)

    def new_block_reg(self, type='', offset=-1):
        block = self.processing_block
        if self.block_regs[block] < offset:
            self.block_regs[block] = offset
        if self.block_regs[block] < 64:
            self.block_regs[block] += 1
        else:
            raise ValueError('Max register value exceeded')
        out = str(self.block_regs[block])
        if 'bit_mux' in type:
            self.block_regs[block] += 1
            out += ' ' + str(self.block_regs[block])
        return out

if __name__ == '__main__':
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    cfg = ConfigGenerator()
    #read in app config file
    app_config = cfg.parse_app_file("myapp")
    variables = {"app_config": app_config}

    #-check that each requested config doesn't exceed the max (from the meta file)
    cfg.checkBlockMax(app_config)

    #combine all relevent descriptions for the output description file
    cfg.generate_description(app_config, "description")

    #combine all relevent config for the output config file
    cfg.generate_output_file(app_config, "config", variables)
        #-make sure to only include the ones that aren't 0
        #-other error checking ?

    #combine all relevent registers for the output registers file
    cfg.generate_output_file(app_config, "registers", variables)
        #-check there are only unique bit numbers ?
        #-other error checking ?