# Copyright (c) 2006, 2014, Oracle and/or its affiliates. All rights reserved.
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


IF(SSL_DEFINES)
 ADD_DEFINITIONS(${SSL_DEFINES})
ENDIF()

INCLUDE_DIRECTORIES(
        ${CMAKE_SOURCE_DIR}/client
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/mysys_ssl
        ${ZLIB_INCLUDE_DIR}
        ${LZ4_INCLUDE_DIR}
        ${SSL_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/libmysql
        ${CMAKE_SOURCE_DIR}/libbinlogevents/include
        ${CMAKE_SOURCE_DIR}/regex
        ${CMAKE_SOURCE_DIR}/strings
        ${EDITLINE_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

SET(SEMISYNC_SLAVE_SOURCES  semisync.cc semisync_slave.cc
  semisync_slave_plugin.cc semisync.h semisync_slave.h )
add_library(semisync_slave_for_virtual_slave SHARED ${SEMISYNC_SLAVE_SOURCES})
target_link_libraries(semisync_slave_for_virtual_slave mysqlclient)

MYSQL_ADD_EXECUTABLE(virtual_slave virtual_slave.cc Config/Config.cc)

ADD_COMPILE_FLAGS(
        virtual_slave.cc
        COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/sql" "-DHAVE_REPLICATION" "-DDISABLE_PSI_MUTEX"
)
TARGET_LINK_LIBRARIES(virtual_slave  binlogevents_static semisync_slave_for_virtual_slave)
