#
# firewall.py - firewall install data and installation
#
# Bill Nottingham <notting@redhat.com>
# Jeremy Katz <katzj@redhat.com>
#
# Copyright 2004 Red Hat, Inc.
#
# This software may be freely redistributed under the terms of the GNU
# library public license.
#
# You should have received a copy of the GNU Library Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

import os
import iutil
import string
from flags import flags

from rhpl.log import log
from rhpl.translate import _, N_

class Service:
    def __init__ (self, key, name, ports):
        self.key = key
        self.name = name
        self.allowed = 0

        if type(ports) == type(""):
            self.ports = [ ports ]
        else:
            self.ports = ports


    def set_enabled(self, val):
        self.allowed = val

    def get_enabled(self):
        return self.allowed

    def get_name(self):
        return self.name

    def get_ports(self):
        return self.ports

class Firewall:
    def __init__ (self):
	self.enabled = 1
        self.trustdevs = []
	self.portlist = []
        self.services = [ Service("ssh", N_("Remote Login (SSH)"), "22:tcp"),
                          Service("http", N_("Web Server (HTTP, HTTPS)"), "80:tcp"),
                          Service("ftp", N_("File Transfer (FTP)"), "21:tcp"),

                          Service("smtp", N_("Mail Server (SMTP)"), "25:tcp") ]

    def writeKS(self, f):
	f.write("firewall")

        if self.enabled:
	    for arg in self.getArgList():
		f.write(" " + arg)
	else:
	    f.write(" --disabled")

	f.write("\n")

    def getArgList(self):
	args = []

        if self.enabled:
            args.append ("--enabled")
        else:
            args.append("--disabled")
            return args
        
        for service in self.services:
            if service.get_enabled():
                for p in service.get_ports():
                    args = args + [ "--port=%s" %(p,) ]

        for dev in self.trustdevs:
            args = args + [ "--trust=%s" %(dev,) ]

	for port in self.portlist:
	    args = args + [ "--port=%s" %(port,) ]
                
	return args

    def write (self, instPath):
	args = [ "/usr/sbin/lokkit", "--quiet", "--nostart", "-f" ]


        args = args + self.getArgList()

        try:
            if flags.setupFilesystems:
                iutil.execWithRedirect(args[0], args, root = instPath,
                                       stdout = None, stderr = None)
            else:
                log("would have run %s", args)
        except RuntimeError, msg:
            log ("lokkit run failed: %s", msg)
        except OSError, (errno, msg):
            log ("lokkit run failed: %s", msg)
        else:
            f = open(instPath +
                     '/etc/sysconfig/system-config-securitylevel', 'w')
            f.write("# system-config-securitylevel config written out by anaconda\n\n")
            for arg in args[3:]:
                f.write("%s\n" %(arg,))
            f.close()
