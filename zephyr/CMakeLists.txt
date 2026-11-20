if(CONFIG_LIBPEER)
  zephyr_library_named(libpeer)

  set(LIBPEER_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

  # Align with upstream libpeer build style: compile core C files.
  file(GLOB LIBPEER_CORE_SRCS ${LIBPEER_DIR}/src/*.c)
  zephyr_library_sources(${LIBPEER_CORE_SRCS})

  zephyr_library_compile_definitions(CONFIG_USE_USRSCTP=0)

  zephyr_library_include_directories(${LIBPEER_DIR}/src)
  zephyr_include_directories(${LIBPEER_DIR}/src)

  if(CONFIG_LIBPEER_SIGNALING)
    include(${LIBPEER_DIR}/third_party/coreHTTP/httpFilePaths.cmake)
    include(${LIBPEER_DIR}/third_party/coreMQTT/mqttFilePaths.cmake)

    zephyr_library_sources(
      ${HTTP_SOURCES}
      ${MQTT_SOURCES}
      ${MQTT_SERIALIZER_SOURCES}
    )
    zephyr_library_compile_definitions(
      HTTP_DO_NOT_USE_CUSTOM_CONFIG
      MQTT_DO_NOT_USE_CUSTOM_CONFIG
    )
    zephyr_library_include_directories(
      ${HTTP_INCLUDE_PUBLIC_DIRS}
      ${MQTT_INCLUDE_PUBLIC_DIRS}
    )
    zephyr_include_directories(
      ${HTTP_INCLUDE_PUBLIC_DIRS}
      ${MQTT_INCLUDE_PUBLIC_DIRS}
    )
    zephyr_library_link_libraries(cjson)
  else()
    # peer_signaling.h and ssl_transport.h also test this, so applications
    # including libpeer headers must see it too.
    zephyr_compile_definitions(DISABLE_PEER_SIGNALING=1)
  endif()

  zephyr_library_link_libraries(srtp)
endif()
