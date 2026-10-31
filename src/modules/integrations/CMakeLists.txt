set(extra_deps
	evolution-mail
	evolution-shell
)
set(sources evolution-integrations.c)
set(extra_defines)
set(extra_cflags)
set(extra_incdirs)
set(extra_ldflags)
add_evolution_module(module-integrations sources extra_deps extra_defines extra_cflags extra_incdirs extra_ldflags)

add_executable(test-integrations test-integrations.c)
target_compile_options(test-integrations PRIVATE ${EVOLUTION_DATA_SERVER_CFLAGS} ${GNOME_PLATFORM_CFLAGS})
target_include_directories(test-integrations PRIVATE
	${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${EVOLUTION_DATA_SERVER_INCLUDE_DIRS} ${GNOME_PLATFORM_INCLUDE_DIRS})
target_link_libraries(test-integrations evolution-shell evolution-util ${EVOLUTION_DATA_SERVER_LDFLAGS} ${GNOME_PLATFORM_LDFLAGS})
add_check_test(test-integrations)
install(TARGETS test-integrations DESTINATION ${LIBEXEC_INSTALL_DIR}/blueevolution/tests)