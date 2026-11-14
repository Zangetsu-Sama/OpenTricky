# From SSX3-recomp (GPL-2.0-or-later): the Vulkan renderer, built for OpenTricky on macOS.
# Vulkan renderer. Standalone: `cmake -S src/vk -B build-vk && ninja -C build-vk`
# builds vkr_selftest. The game links the vkr library target.
cmake_minimum_required(VERSION 3.20)
if(NOT PROJECT_NAME)
    project(vkr C)
endif()

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)

set(VKR_GEN "${CMAKE_CURRENT_BINARY_DIR}/vkr_gen")
file(MAKE_DIRECTORY "${VKR_GEN}")

# vkr_shader(<stage> [<base>]): shaders/<base>.<stage> (default base xbox)
# -> vkr_gen/<name>_spv.h with the array <name>_spv, name = xbox_<stage>, or
# <base>_<stage> for another base.
function(vkr_shader stage)
    set(base "xbox")
    set(name "xbox_${stage}")
    if(ARGC GREATER 1)
        set(base "${ARGV1}")
        set(name "${ARGV1}_${stage}")
    endif()
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${base}.${stage}")
    set(spv "${VKR_GEN}/${name}.spv")
    set(hdr "${VKR_GEN}/${name}_spv.h")
    add_custom_command(
        OUTPUT "${hdr}"
        COMMAND ${GLSLC} --target-env=vulkan1.3 -O -o "${spv}" "${src}"
        COMMAND ${CMAKE_COMMAND} -DIN=${spv} -DOUT=${hdr} -DNAME=${name}_spv
                -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spv.cmake"
        DEPENDS "${src}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_spv.cmake"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_common.glsl"
        VERBATIM)
    set(VKR_HEADERS ${VKR_HEADERS} "${hdr}" PARENT_SCOPE)
endfunction()
# vkr_shader_variant(<stage> <base> <name> <define> [<define>...]): the same
# source built with -D<define> into <name>_spv.
function(vkr_shader_variant stage base name def)
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${base}.${stage}")
    set(spv "${VKR_GEN}/${name}.spv")
    set(hdr "${VKR_GEN}/${name}_spv.h")
    set(defs "")
    foreach(d ${def} ${ARGN})
        list(APPEND defs "-D${d}")
    endforeach()
    add_custom_command(
        OUTPUT "${hdr}"
        COMMAND ${GLSLC} --target-env=vulkan1.3 -O ${defs} -o "${spv}" "${src}"
        COMMAND ${CMAKE_COMMAND} -DIN=${spv} -DOUT=${hdr} -DNAME=${name}_spv
                -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spv.cmake"
        DEPENDS "${src}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_spv.cmake"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_common.glsl"
        VERBATIM)
    set(VKR_HEADERS ${VKR_HEADERS} "${hdr}" PARENT_SCOPE)
endfunction()
vkr_shader(vert)
vkr_shader(frag)
vkr_shader_variant(frag xbox xbox_frag_wdepth W_DEPTH)
# RECOMP_MBLUR rider tags: the draw's tag in the frame's alpha, the real alpha
# as the second blend source
vkr_shader_variant(frag xbox xbox_frag_tags TAGS)
vkr_shader_variant(frag xbox xbox_frag_wdepth_tags W_DEPTH TAGS)
vkr_shader(vert xbox_vsh)
vkr_shader(vert crt)
vkr_shader(frag crt)
vkr_shader(frag scene)
# with MSAA the depth it reads is multisampled
vkr_shader_variant(frag scene scene_frag_ms MS_DEPTH)
# OpenTricky's soft shadows (XBOX_SOFT_SHADOWS): mask, blur h / v, apply
vkr_shader_variant(frag softshadow ss_one_frag PASS_ONE)
vkr_shader_variant(frag softshadow ss_h_frag PASS_H)
vkr_shader_variant(frag softshadow ss_h_ms_frag PASS_H MS_DEPTH)
vkr_shader_variant(frag softshadow ss_v_frag PASS_V)
vkr_shader_variant(frag softshadow ss_v_ms_frag PASS_V MS_DEPTH)
vkr_shader_variant(frag softshadow ss_apply_frag PASS_APPLY)
vkr_shader_variant(frag softshadow ss_apply_ms_frag PASS_APPLY MS_DEPTH)

# SMAA 1x (XBOX_SMAA): smaa.vert/.frag per pass (0 edges, 1 weights, 2
# blending) and preset (0 low .. 3 ultra), with OpenTricky's copy of the
# reference shader (xboxrecomp/src/d3d/smaa/SMAA.hlsl, its GLSL 4 mode).
set(VKR_SMAA_DIR "${XBOXRECOMP_DIR}/src/d3d/smaa")
foreach(pass 0 1 2)
    foreach(preset 0 1 2 3)
        foreach(stage vert frag)
            set(name "smaa_p${pass}_r${preset}_${stage}")
            set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/smaa.${stage}")
            set(spv "${VKR_GEN}/${name}.spv")
            set(hdr "${VKR_GEN}/${name}_spv.h")
            add_custom_command(
                OUTPUT "${hdr}"
                COMMAND ${GLSLC} --target-env=vulkan1.3 -O -DPASS=${pass} -DPRESET=${preset}
                        -I "${VKR_SMAA_DIR}" -o "${spv}" "${src}"
                COMMAND ${CMAKE_COMMAND} -DIN=${spv} -DOUT=${hdr} -DNAME=${name}_spv
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spv.cmake"
                DEPENDS "${src}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_spv.cmake"
                        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/smaa_common.glsl" "${VKR_SMAA_DIR}/SMAA.hlsl"
                VERBATIM)
            list(APPEND VKR_HEADERS "${hdr}")
        endforeach()
    endforeach()
endforeach()

add_library(vkr STATIC vkr.c ${VKR_HEADERS})
# vkr_backend.c is added by the game build (needs the toolkit headers).
target_include_directories(vkr PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${VKR_GEN}" "${CMAKE_CURRENT_SOURCE_DIR}/.." "${XBOXRECOMP_DIR}/src/platform" "${XBOXRECOMP_DIR}/src/platform/posix_include" "${VKR_SMAA_DIR}")
target_link_libraries(vkr PUBLIC Vulkan::Vulkan)

