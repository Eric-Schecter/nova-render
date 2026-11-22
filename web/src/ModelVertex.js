// Five float4 fields shared by the model and path-tracing shaders.
export const modelVertexFloats = 20
export const modelTriangleFloats = 3 * modelVertexFloats

export const materialSlotLimit = 256
export const shadowBVHByteLimit = 384 * 1024 * 1024
