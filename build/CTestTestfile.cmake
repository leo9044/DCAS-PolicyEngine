# CMake generated Testfile for 
# Source directory: /home/siwoo/ads-skynet/DCAS-PolicyEngine
# Build directory: /home/siwoo/ads-skynet/DCAS-PolicyEngine/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(dcas_policy_tests "/home/siwoo/ads-skynet/DCAS-PolicyEngine/build/dcas_policy_tests")
set_tests_properties(dcas_policy_tests PROPERTIES  _BACKTRACE_TRIPLES "/home/siwoo/ads-skynet/DCAS-PolicyEngine/CMakeLists.txt;31;add_test;/home/siwoo/ads-skynet/DCAS-PolicyEngine/CMakeLists.txt;0;")
subdirs("rt-control-ipc")
