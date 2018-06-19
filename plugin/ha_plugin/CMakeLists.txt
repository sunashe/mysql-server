# Copyright (c) 2006, 2015, Oracle and/or its affiliates. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

SET(HA_PLUGIN_SOURCE ha_plugin.cc slave.cc slave.h  master.cc master.h instance.cc instance.h)
LINK_LIBRARIES()
MYSQL_ADD_PLUGIN(ha_plugin ${HA_PLUGIN_SOURCE}
        MODULE_ONLY MODULE_OUTPUT_NAME "ha_plugin")
target_link_libraries(ha_plugin pthread mysqlclient)