set(ANIMAL_TOOL_SHADERS "")
if(ANIMAL_DEV_TOOLS)
    list(APPEND ANIMAL_TOOL_SHADERS imgui.vert imgui.frag)
endif()

animal_add_shaders(animal_shaders
    SOURCES
        ${ANIMAL_TOOL_SHADERS}
        sprite.vert
        sprite.frag
        sprite_lit.frag
        light.vert
        light.frag
        composite.vert
        composite.frag
        light_downsample.frag
        fluid_advect.frag
        fluid_splat.frag
        fluid_curl.frag
        fluid_vorticity.frag
        fluid_divergence.frag
        fluid_pressure.frag
        fluid_gradient.frag
        post_bright.frag
        post_blur.frag
        post_grade.frag
        water.frag
        gpu_particles_update.frag
        gpu_particles.vert
        gpu_particles_lit.frag
        gpu_particles_glow.frag)
