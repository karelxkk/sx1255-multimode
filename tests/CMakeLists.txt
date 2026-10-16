add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE sx1255_core)
add_test(NAME config COMMAND test_config)

add_executable(test_session_manager test_session_manager.cpp)
target_link_libraries(test_session_manager PRIVATE sx1255_core)
add_test(NAME session_manager COMMAND test_session_manager)

add_executable(test_dispatcher test_dispatcher.cpp)
target_link_libraries(test_dispatcher PRIVATE sx1255_core)
add_test(NAME dispatcher COMMAND test_dispatcher)

add_executable(test_goertzel test_goertzel.cpp)
target_link_libraries(test_goertzel PRIVATE sx1255_core)
add_test(NAME goertzel COMMAND test_goertzel)

add_executable(test_fm_detect test_fm_detect.cpp)
target_link_libraries(test_fm_detect PRIVATE sx1255_core)
add_test(NAME fm_detect COMMAND test_fm_detect)

add_executable(test_ctcss_tx test_ctcss_tx.cpp)
target_link_libraries(test_ctcss_tx PRIVATE sx1255_core)
add_test(NAME ctcss_tx COMMAND test_ctcss_tx)

add_executable(test_ctcss_rx_tx test_ctcss_rx_tx.cpp)
target_link_libraries(test_ctcss_rx_tx PRIVATE sx1255_core)
add_test(NAME ctcss_rx_tx COMMAND test_ctcss_rx_tx)

add_executable(test_fm_loopback test_fm_loopback.cpp)
target_link_libraries(test_fm_loopback PRIVATE sx1255_core)
add_test(NAME fm_loopback COMMAND test_fm_loopback)

add_executable(test_fm_hang_timer test_fm_hang_timer.cpp)
target_link_libraries(test_fm_hang_timer PRIVATE sx1255_core)
add_test(NAME fm_hang_timer COMMAND test_fm_hang_timer)

add_executable(test_pty_ptt test_pty_ptt.cpp)
target_link_libraries(test_pty_ptt PRIVATE sx1255_core)
add_test(NAME pty_ptt COMMAND test_pty_ptt)
set_tests_properties(pty_ptt PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_resampler test_resampler.cpp)
target_link_libraries(test_resampler PRIVATE sx1255_core)
add_test(NAME resampler COMMAND test_resampler)

add_executable(test_sql_pty test_sql_pty.cpp)
target_link_libraries(test_sql_pty PRIVATE sx1255_core)
add_test(NAME sql_pty COMMAND test_sql_pty)
set_tests_properties(sql_pty PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_m17_units test_m17_units.cpp)
target_link_libraries(test_m17_units PRIVATE sx1255_core)
add_test(NAME m17_units COMMAND test_m17_units)

add_executable(test_m17_loopback test_m17_loopback.cpp)
target_link_libraries(test_m17_loopback PRIVATE sx1255_core)
add_test(NAME m17_loopback COMMAND test_m17_loopback)

add_executable(test_m17_tx_chain test_m17_tx_chain.cpp)
target_link_libraries(test_m17_tx_chain PRIVATE sx1255_core)
add_test(NAME m17_tx_chain COMMAND test_m17_tx_chain)

add_executable(test_m17_network test_m17_network.cpp)
target_link_libraries(test_m17_network PRIVATE sx1255_core)
add_test(NAME m17_network COMMAND test_m17_network)
set_tests_properties(m17_network PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_sx1255_spi test_sx1255_spi.cpp)
target_link_libraries(test_sx1255_spi PRIVATE sx1255_core)
add_test(NAME sx1255_spi COMMAND test_sx1255_spi)

add_executable(test_sx1255_gpio test_sx1255_gpio.cpp)
target_link_libraries(test_sx1255_gpio PRIVATE sx1255_core)
add_test(NAME sx1255_gpio COMMAND test_sx1255_gpio)

add_executable(test_sx1255_buffering test_sx1255_buffering.cpp)
target_link_libraries(test_sx1255_buffering PRIVATE sx1255_core)
add_test(NAME sx1255_buffering COMMAND test_sx1255_buffering)

add_executable(test_dstar_detect test_dstar_detect.cpp)
target_link_libraries(test_dstar_detect PRIVATE sx1255_core)
add_test(NAME dstar_detect COMMAND test_dstar_detect)

add_executable(test_dstar_loopback test_dstar_loopback.cpp)
target_link_libraries(test_dstar_loopback PRIVATE sx1255_core)
add_test(NAME dstar_loopback COMMAND test_dstar_loopback)

add_executable(test_dstar_plugin test_dstar_plugin.cpp)
target_link_libraries(test_dstar_plugin PRIVATE sx1255_core)
add_test(NAME dstar_plugin COMMAND test_dstar_plugin)

add_executable(test_dstar_gateway test_dstar_gateway.cpp)
target_link_libraries(test_dstar_gateway PRIVATE sx1255_core)
add_test(NAME dstar_gateway COMMAND test_dstar_gateway)
set_tests_properties(dstar_gateway PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_dstar_netplugin test_dstar_netplugin.cpp)
target_link_libraries(test_dstar_netplugin PRIVATE sx1255_core)
add_test(NAME dstar_netplugin COMMAND test_dstar_netplugin)
set_tests_properties(dstar_netplugin PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_runtime_stats test_runtime_stats.cpp)
target_link_libraries(test_runtime_stats PRIVATE sx1255_core)
add_test(NAME runtime_stats COMMAND test_runtime_stats)
set_tests_properties(runtime_stats PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_iq_recording test_iq_recording.cpp)
target_link_libraries(test_iq_recording PRIVATE sx1255_core)
add_test(NAME iq_recording COMMAND test_iq_recording)

add_executable(test_iq_replay test_iq_replay.cpp)
target_link_libraries(test_iq_replay PRIVATE sx1255_core)
add_test(NAME iq_replay COMMAND test_iq_replay)

add_executable(test_multimode_replay test_multimode_replay.cpp)
target_link_libraries(test_multimode_replay PRIVATE sx1255_core)
add_test(NAME multimode_replay COMMAND test_multimode_replay)

add_executable(test_fft test_fft.cpp)
target_link_libraries(test_fft PRIVATE sx1255_core)
add_test(NAME fft COMMAND test_fft)

add_executable(test_waterfall test_waterfall.cpp)
target_link_libraries(test_waterfall PRIVATE sx1255_core)
add_test(NAME waterfall COMMAND test_waterfall)

add_executable(test_capture_trigger test_capture_trigger.cpp)
target_link_libraries(test_capture_trigger PRIVATE sx1255_core)
add_test(NAME capture_trigger COMMAND test_capture_trigger)

add_executable(test_dashboard_json test_dashboard_json.cpp)
target_link_libraries(test_dashboard_json PRIVATE sx1255_core)
add_test(NAME dashboard_json COMMAND test_dashboard_json)
set_tests_properties(dashboard_json PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE sx1255_core)
add_test(NAME logger COMMAND test_logger)

add_executable(test_persist_stats test_persist_stats.cpp)
target_link_libraries(test_persist_stats PRIVATE sx1255_core)
add_test(NAME persist_stats COMMAND test_persist_stats)

add_executable(test_health_monitor test_health_monitor.cpp)
target_link_libraries(test_health_monitor PRIVATE sx1255_core)
add_test(NAME health_monitor COMMAND test_health_monitor)

add_executable(test_failure_injection test_failure_injection.cpp)
target_link_libraries(test_failure_injection PRIVATE sx1255_core)
add_test(NAME failure_injection COMMAND test_failure_injection)
