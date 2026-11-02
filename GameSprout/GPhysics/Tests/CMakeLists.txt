add_executable(GPhysicsChecks GPhysicsChecks.cpp)
target_link_libraries(GPhysicsChecks PRIVATE GPhysics::GPhysics)
add_test(NAME GPhysics.MaterialsAndWorld COMMAND GPhysicsChecks)
set_tests_properties(GPhysics.MaterialsAndWorld PROPERTIES TIMEOUT 30)
