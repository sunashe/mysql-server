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
cmake_minimum_required(VERSION 2.8)
project(virtual_slave)

IF(SSL_DEFINES)
 ADD_DEFINITIONS(${SSL_DEFINES})
ENDIF()
## ADD_COMPILE_FLAGS(<source files> COMPILE_FLAGS <flags>)
MACRO(ADD_COMPILE_FLAGS)
 SET(FILES "")
 SET(FLAGS "")
 SET(COMPILE_FLAGS_SEEN)
 FOREACH(ARG ${ARGV})
  IF(ARG STREQUAL "COMPILE_FLAGS")
   SET(COMPILE_FLAGS_SEEN 1)
  ELSEIF(COMPILE_FLAGS_SEEN)
   LIST(APPEND FLAGS ${ARG})
  ELSE()
   LIST(APPEND FILES ${ARG})
  ENDIF()
 ENDFOREACH()
 FOREACH(FILE ${FILES})
  FOREACH(FLAG ${FLAGS})
   GET_SOURCE_FILE_PROPERTY(PROP ${FILE} COMPILE_FLAGS)
   IF(NOT PROP)
    SET(PROP ${FLAG})
   ELSE()
    SET(PROP "${PROP} ${FLAG}")
   ENDIF()
   SET_SOURCE_FILES_PROPERTIES(
           ${FILE} PROPERTIES COMPILE_FLAGS "${PROP}"
   )
  ENDFOREACH()
 ENDFOREACH()
ENDMACRO()

INCLUDE_DIRECTORIES(
        "./include"
        "./src/semisync"

)

INCLUDE_DIRECTORIES(
        "/usr/include"
        "/usr/include/mysql"
)

SET(CMAKE_MACOSX_RPATH 1)

SET(SEMISYNC_SLAVE_SOURCES src/semisync/semisync.cc src/semisync/semisync_slave.cc
        src/semisync/semisync_slave_plugin.cc src/semisync/semisync.h src/semisync/semisync_slave.h)
add_library(semisync_slave_for_virtual_slave SHARED ${SEMISYNC_SLAVE_SOURCES})
target_link_libraries(semisync_slave_for_virtual_slave mysqlclient)

ADD_EXECUTABLE(virtual_slave src/virtual_slave.cc Config/Config.cc)

ADD_COMPILE_FLAGS(
        src/virtual_slave.cc
        COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/sql -DHAVE_REPLICATION -DDISABLE_PSI_MUTEX"
)
TARGET_LINK_LIBRARIES(virtual_slave binlogevents_static semisync_slave_for_virtual_slave)
