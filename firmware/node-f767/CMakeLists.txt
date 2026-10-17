# Psyight pixel node firmware for the Nucleo-F767ZI (SPEC section 12).
# Configure from firmware/node-f767 after fetch_deps.ps1:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DNODE_STAGE=3
#   cmake --build build
cmake_minimum_required(VERSION 3.25)
project(psyight_node VERSION 0.1.0 LANGUAGES C ASM)

set(NODE_STAGE 3 CACHE STRING "Bring-up stage (SPEC 12.4): 1 LEDs only, 2 network only, 3 the node")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(NOT EXISTS ${TP}/stm32f7xx_hal_driver/Src/stm32f7xx_hal.c)
    message(FATAL_ERROR "Run fetch_deps.ps1 first: ${TP} is missing the ST sources")
endif()
set(HAL ${TP}/stm32f7xx_hal_driver/Src)
set(LWIP ${TP}/stm32_mw_lwip/src)

set(HAL_SOURCES
    ${HAL}/stm32f7xx_hal.c ${HAL}/stm32f7xx_hal_cortex.c ${HAL}/stm32f7xx_hal_dma.c
    ${HAL}/stm32f7xx_hal_eth.c ${HAL}/stm32f7xx_hal_exti.c ${HAL}/stm32f7xx_hal_flash.c
    ${HAL}/stm32f7xx_hal_flash_ex.c ${HAL}/stm32f7xx_hal_gpio.c ${HAL}/stm32f7xx_hal_iwdg.c
    ${HAL}/stm32f7xx_hal_pwr.c ${HAL}/stm32f7xx_hal_pwr_ex.c ${HAL}/stm32f7xx_hal_rcc.c
    ${HAL}/stm32f7xx_hal_rcc_ex.c ${HAL}/stm32f7xx_hal_uart.c ${HAL}/stm32f7xx_hal_uart_ex.c)
file(GLOB LWIP_SOURCES ${LWIP}/core/*.c ${LWIP}/core/ipv4/*.c)
list(APPEND LWIP_SOURCES ${LWIP}/netif/ethernet.c)

add_executable(node
    src/main.c
    src/board/board.c src/board/config_store.c src/board/stm32f7xx_it.c src/board/syscalls.c
    src/core/node_config.c src/core/node_core.c src/core/protocol.c
    src/net/ethernetif.c src/net/net.c
    src/ws2812/ws2812.c
    ${TP}/cmsis_device_f7/Source/Templates/system_stm32f7xx.c
    ${TP}/cmsis_device_f7/Source/Templates/gcc/startup_stm32f767xx.s
    ${TP}/stm32-lan8742/lan8742.c
    ${HAL_SOURCES} ${LWIP_SOURCES})
set_target_properties(node PROPERTIES SUFFIX .elf OUTPUT_NAME psyight_node_f${NODE_STAGE})
target_compile_definitions(node PRIVATE STM32F767xx USE_HAL_DRIVER NODE_STAGE=${NODE_STAGE})
target_include_directories(node PRIVATE
    src src/board src/core src/net src/ws2812
    ${TP}/cmsis_core/CMSIS/Core/Include
    ${TP}/cmsis_device_f7/Include
    ${TP}/stm32f7xx_hal_driver/Inc
    ${TP}/stm32-lan8742
    ${LWIP}/include
    ${TP}/stm32_mw_lwip/system)
# Our code: strict. Vendor code: as shipped.
set_source_files_properties(src/main.c src/board/board.c src/board/config_store.c
    src/board/stm32f7xx_it.c src/board/syscalls.c src/core/node_config.c src/core/node_core.c
    src/core/protocol.c src/net/ethernetif.c src/net/net.c src/ws2812/ws2812.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wno-unused-parameter")
target_link_options(node PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F767ZITX_FLASH.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/psyight_node_f${NODE_STAGE}.map
    -Wl,--print-memory-usage
    -Wl,--no-warn-rwx-segments) # No MMU: RAM being executable means nothing here.
target_link_libraries(node PRIVATE m)

add_custom_command(TARGET node POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:node> $<TARGET_FILE_DIR:node>/psyight_node_f${NODE_STAGE}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:node> $<TARGET_FILE_DIR:node>/psyight_node_f${NODE_STAGE}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:node>
    VERBATIM)
