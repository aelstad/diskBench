#--------------------SETS THE FOLLOWING VARIABLES--------------------
#   APR_INCLUDE_PATH
#   APR_CPP_FLAGS
#   APR_C_FLAGS
#   APR_LD_FLAGS
#   APR_LIBS
#   APR_LIB_DIRS
#-------------------------------------------------------------------

find_file(APR_CONFIG apr-1-config HINTS ENV APR_HOME PATH_SUFFIXES bin)
if (APR_CONFIG-NOTFOUND)
   message(FATAL_ERROR "apr-1-config required but not found!")
endif (APR_CONFIG-NOTFOUND)


execute_process(COMMAND "sh" ${APR_CONFIG} --includedir RESULT_VARIABLE apr_ret_var OUTPUT_VARIABLE APR_INCLUDE_PATH)
execute_process(COMMAND "sh" ${APR_CONFIG} --cppflags RESULT_VARIABLE apr_ret_var OUTPUT_VARIABLE APR_CPP_FLAGS)
execute_process(COMMAND "sh" ${APR_CONFIG} --cflags RESULT_VARIABLE apr_ret_var OUTPUT_VARIABLE APR_C_FLAGS)
execute_process(COMMAND "sh" ${APR_CONFIG} --ldflags RESULT_VARIABLE apr_ret_var OUTPUT_VARIABLE APR_LD_FLAGS)
execute_process(COMMAND "sh" ${APR_CONFIG} --link-ld --libs RESULT_VARIABLE apr_ret_var OUTPUT_VARIABLE APR_LIBS)

# Strip whitespace

string(REGEX REPLACE "^[ \t \n]+|[ \t \n]+$" "" APR_INCLUDE_PATH ${APR_INCLUDE_PATH}) 
string(REGEX REPLACE "^[ \t \n]+|[ \t \n]+$" "" APR_CPP_FLAGS ${APR_CPP_FLAGS}) 
string(REGEX REPLACE "^[ \t \n]+|[ \t \n]+$" "" APR_C_FLAGS ${APR_C_FLAGS}) 
string(REGEX REPLACE "^[ \t \n]+|[ \t \n]+$" "" APR_LD_FLAGS ${APR_LD_FLAGS}) 
string(REGEX REPLACE "^[ \t \n]+|[ \t \n]+$" "" APR_LIBS ${APR_LIBS}) 

message(STATUS "APR_INCLUDE_PATH: " ${APR_INCLUDE_PATH})
message(STATUS "APR_CPP_FLAGS: " ${APR_CPP_FLAGS})
message(STATUS "APR_C_FLAGS: " ${APR_C_FLAGS})
message(STATUS "APR_LD_FLAGS: " ${APR_LD_FLAGS})
message(STATUS "APR_LIBS: " ${APR_LIBS})

  
