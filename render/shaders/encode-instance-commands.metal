#include <metal_stdlib>
using namespace metal;

struct DrawCommand
{
    uint vertexCount, instanceCount, firstVertex, firstInstance;
};

struct CommandResources
{
    command_buffer commands [[id(0)]];
};

kernel void encodeInstanceCommands(const device DrawCommand* draws [[buffer(0)]],
                                   device CommandResources& resources [[buffer(1)]],
                                   uint index [[thread_position_in_grid]])
{
    const DrawCommand draw = draws[index];
    if (draw.instanceCount != 0)
    {
        render_command command(resources.commands, index);
        command.draw_primitives(primitive_type::triangle, draw.firstVertex,
                                draw.vertexCount, draw.instanceCount, draw.firstInstance);
    }
}
