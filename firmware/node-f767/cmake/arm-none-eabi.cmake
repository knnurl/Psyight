# Cross-compiling for the STM32F767 (Cortex-M7, double-precision FPU) with GNU Arm Embedded.
# The compiler comes from ARM_TOOLCHAIN_DIR, else the one bundled with STM32CubeIDE 1.19.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ARM_TOOLCHAIN_DIR)
    file(GLOB _cubeide_gcc
         "C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin")
    list(SORT _cubeide_gcc)
    list(REVERSE _cubeide_gcc)
    list(GET _cubeide_gcc 0 ARM_TOOLCHAIN_DIR)
endif()
set(ARM_TOOLCHAIN_DIR "${ARM_TOOLCHAIN_DIR}" CACHE PATH "Folder holding arm-none-eabi-gcc")

set(CMAKE_C_COMPILER "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-gcc.exe")
set(CMAKE_ASM_COMPILER "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-gcc.exe")
set(CMAKE_OBJCOPY "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-objcopy.exe" CACHE FILEPATH "")
set(CMAKE_SIZE "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-size.exe" CACHE FILEPATH "")

# Do not try to link a host test program while probing the compiler.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(MCU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${MCU_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs")
