# CMake generated Testfile for 
# Source directory: /home/leo/ads-skynet/DCAS-PolicyEngine
# Build directory: /home/leo/ads-skynet/DCAS-PolicyEngine/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(dcas_policy_tests "/home/leo/ads-skynet/DCAS-PolicyEngine/build/dcas_policy_tests")
set_tests_properties(dcas_policy_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/leo/ads-skynet/DCAS-PolicyEngine/CMakeLists.txt;28;add_test;/home/leo/ads-skynet/DCAS-PolicyEngine/CMakeLists.txt;0;")
subdirs("rt-control-ipc")
